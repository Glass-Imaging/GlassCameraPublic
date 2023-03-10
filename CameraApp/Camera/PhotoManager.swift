// Copyright (c) 2021-2022 Glass Imaging Inc.
// Author: Fabio Riccardi <fabio@glass-imaging.com>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

import CoreImage
import Combine
import UniformTypeIdentifiers
import Photos
import UIKit

class PhotoManager {
    private static let ciContext = CIContext(options: nil)

    private static var publisher: AnyCancellable?

    static func capturePhoto() {
        publisher = FrameManager.frameManager.$imageBuffer.first().sink { imageBuffer in
            guard imageBuffer != nil else {
                debugPrint("[W] PhotoManager.capturePhoto: empty imageBuffer")
                return
            }
            
            let inputImage = CIImage(cvPixelBuffer: imageBuffer!)
            guard let cgImage = ciContext.createCGImage(inputImage, from: inputImage.extent) else {
                debugPrint("[W] PhotoManager.capturePhoto: CGImage is nil")
                return
            }

            // Save image to the Photo Library
            PHPhotoLibrary.shared().performChanges {
                _ = PHAssetChangeRequest.creationRequestForAsset(from: UIImage(cgImage: cgImage))
            } completionHandler: { (success, error) in
                if (success) {
                    debugPrint("[I] Successfully Added image to Photo Library")
                } else {
                    debugPrint("[W] Failed to Add image to Photo Library", error.debugDescription)
                }
            }
        }
        
    }
    
    static func saveToFile(image: CGImage, filename: String) {
        let cfdata: CFMutableData = CFDataCreateMutable(nil, 0)
        if let destination = CGImageDestinationCreateWithData(cfdata, String(describing: UTType.png) as CFString, 1, nil) {
            CGImageDestinationAddImage(destination, image, nil)
            if CGImageDestinationFinalize(destination) {
                debugPrint("[I] PhotoManager.save: saved image at \(destination)")
                do {
                    try (cfdata as Data).write(to: self.asURL(filename)!)
                    debugPrint("[I] PhotoManager.save: Saved image")
                } catch {
                    debugPrint("[E] PhotoManager.save: Failed to save image \(error)")
                }
            }
        }
        debugPrint("[I] PhotoManager.save: func completed")
    }
    
    static func asURL(_ filename: String) -> URL? {
        guard let documentsDirectory = FileManager.default.urls(
            for: .documentDirectory,
            in: .userDomainMask).first else {
            return nil
        }
        
        let url = documentsDirectory.appendingPathComponent(filename)
        debugPrint(".asURL:", url)
        return url
    }
}
