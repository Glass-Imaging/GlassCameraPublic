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

import Foundation
import Photos
import UIKit

class PhotoCaptureProcessor: NSObject {
    private(set) var captureData: Data?
    private(set) var requestedPhotoSettings: AVCapturePhotoSettings

    private(set) var rawFileURL: URL?
    private(set) var procesedImageURL: URL?
    private(set) var compressedData: Data?
    private(set) var metadata: NSDictionary?

    private let willCapturePhotoAnimation: () -> Void
    private let completionHandler: (PhotoCaptureProcessor) -> Void
    private let photoProcessingHandler: (Bool) -> Void
    private var maxPhotoProcessingTime: CMTime?

    private let rawProcessor = RawProcessor()

    init(with requestedPhotoSettings: AVCapturePhotoSettings,
         willCapturePhotoAnimation: @escaping () -> Void,
         completionHandler: @escaping (PhotoCaptureProcessor) -> Void,
         photoProcessingHandler: @escaping (Bool) -> Void) {
        self.requestedPhotoSettings = requestedPhotoSettings
        self.willCapturePhotoAnimation = willCapturePhotoAnimation
        self.completionHandler = completionHandler
        self.photoProcessingHandler = photoProcessingHandler
    }
}

extension CGImagePropertyOrientation {
    init(_ uiOrientation: UIImage.Orientation) {
        switch uiOrientation {
            case .up: self = .up
            case .upMirrored: self = .upMirrored
            case .down: self = .down
            case .downMirrored: self = .downMirrored
            case .left: self = .left
            case .leftMirrored: self = .leftMirrored
            case .right: self = .right
            case .rightMirrored: self = .rightMirrored
        @unknown default:
            fatalError()
        }
    }
}

extension UIImage {
    var cgImageOrientation: CGImagePropertyOrientation { .init(imageOrientation) }
}

extension UIImage {
    var heic: Data? { heic() }
    func heic(compressionQuality: CGFloat = 1) -> Data? {
        guard
            let mutableData = CFDataCreateMutable(nil, 0),
            let destination = CGImageDestinationCreateWithData(mutableData, "public.heic" as CFString, 1, nil),
            let cgImage = cgImage
        else { return nil }
        CGImageDestinationAddImage(destination, cgImage, [kCGImageDestinationLossyCompressionQuality: compressionQuality, kCGImagePropertyOrientation: cgImageOrientation.rawValue] as [CFString : Any] as CFDictionary)
        guard CGImageDestinationFinalize(destination) else { return nil }
        return mutableData as Data
    }
}

extension CGImage {
    func heic(withMetadata metadata:NSDictionary?) -> Data? {
        guard let mutableData = CFDataCreateMutable(nil, 0),
            let destination = CGImageDestinationCreateWithData(mutableData, "public.heic" as CFString, 1, nil) else { return nil }
        CGImageDestinationAddImage(destination, self, metadata)
        guard CGImageDestinationFinalize(destination) else { return nil }
        return mutableData as Data
    }
}

func removeFile(at url:URL) {
    do {
        let fileManager = FileManager()
        try fileManager.removeItem(at: url)
    } catch {
        print("can't remove processed image...")
    }
}

extension PhotoCaptureProcessor: AVCapturePhotoCaptureDelegate {
    // This extension adopts all of the AVCapturePhotoCaptureDelegate protocol methods.

    func photoOutput(_ output: AVCapturePhotoOutput, willBeginCaptureFor resolvedSettings: AVCaptureResolvedPhotoSettings) {
        maxPhotoProcessingTime = resolvedSettings.photoProcessingTimeRange.start + resolvedSettings.photoProcessingTimeRange.duration
    }

    func photoOutput(_ output: AVCapturePhotoOutput, willCapturePhotoFor resolvedSettings: AVCaptureResolvedPhotoSettings) {
        DispatchQueue.main.async {
            self.willCapturePhotoAnimation()
        }

        guard let maxPhotoProcessingTime = maxPhotoProcessingTime else {
            return
        }

        // Show a spinner if processing time exceeds one second.
        let oneSecond = CMTime(seconds: 2, preferredTimescale: 1)
        if maxPhotoProcessingTime > oneSecond {
            DispatchQueue.main.async {
                self.photoProcessingHandler(true)
            }
        }
    }

    func photoOutput(_ output: AVCapturePhotoOutput, didFinishProcessingPhoto photo: AVCapturePhoto, error: Error?) {
        DispatchQueue.main.async {
            self.photoProcessingHandler(false)
        }

        if let error = error {
            print("Error capturing photo: \(error)")
        } else {
            captureData = photo.fileDataRepresentation()
        }

        if photo.isRawPhoto {
            print("got raw data")
            // Generate a unique URL to write the RAW file.
            rawFileURL = makeUniqueDNGFileURL()
            do {
                // Write the RAW data to a DNG file.
                try captureData!.write(to: rawFileURL!)

                // Convert DNG file to PNG.
                let procesedImagePath = rawProcessor.convertDngFile(rawFileURL!.path())

                // Convert PNG to HEIC.
                let image = UIImage(contentsOfFile: procesedImagePath)
                var isHeicSupported: Bool {
                    (CGImageDestinationCopyTypeIdentifiers() as! [String]).contains("public.heic")
                }
                if isHeicSupported, let heicData = image!.heic(compressionQuality: 0.75) {
                    let heic_name = procesedImagePath.replacingOccurrences(of: ".png", with: ".heic")
                    let heic_url = URL(fileURLWithPath: heic_name)
                    try heicData.write(to: heic_url)
                    procesedImageURL = heic_url
                }

                // Remove DNG file.
                removeFile(at: rawFileURL!)
                // Remove PNG file.
                removeFile(at: URL(fileURLWithPath: procesedImagePath))
            } catch {
                fatalError("Couldn't write DNG file to the URL.")
            }
        } else {
            print("got compressed data")

            // Store compressed bitmap data.
            compressedData = captureData

            // Save metadata for reprocessed image.
            metadata = photo.metadata as NSDictionary
        }
    }

    private func makeUniqueDNGFileURL() -> URL {
        let tempDir = FileManager.default.temporaryDirectory
        let fileName = ProcessInfo.processInfo.globallyUniqueString
        return tempDir.appendingPathComponent(fileName).appendingPathExtension("dng")
    }

    // MARK: Saves capture to photo library
    func saveToPhotoLibrary(_ captureData: Data) {
        // TODO: this code needs cleanup!
        PHPhotoLibrary.requestAuthorization { status in
            if status == .authorized {
                PHPhotoLibrary.shared().performChanges({
                    // Add the compressed (HEIF) data as the main resource for the Photos asset.
                    let creationRequest = PHAssetCreationRequest.forAsset()

                    if let compressedData = self.compressedData {
                        creationRequest.addResource(with: .photo, data: compressedData, options: nil)
                    }
                }, completionHandler: { _, error in
                    if let error = error {
                        print("Error occurred while saving photo to photo library: \(error)")
                    }

                    DispatchQueue.main.async {
                        self.completionHandler(self)
                    }
                })
            } else {
                DispatchQueue.main.async {
                    self.completionHandler(self)
                }
            }
        }
        // Add the RAW (DNG) file as second file.
        if let procesedImageURL = self.procesedImageURL {
            PHPhotoLibrary.requestAuthorization { status in
                if status == .authorized {
                    PHPhotoLibrary.shared().performChanges({
                        // Add the compressed (HEIF) data as the main resource for the Photos asset.
                        let creationRequest = PHAssetCreationRequest.forAsset()

                        if let cgImageSource = CGImageSourceCreateWithURL(procesedImageURL as CFURL, nil) {
                            if let cgImage = CGImageSourceCreateImageAtIndex(cgImageSource, 0, nil) {
                                if let heicData = cgImage.heic(withMetadata: self.metadata) {
                                    creationRequest.addResource(with: .photo, data: heicData, options: nil)
                                }
                            }
                        }
                    }, completionHandler: { _, error in
                        if let error = error {
                            print("Error occurred while saving photo to photo library: \(error)")
                        }

                        DispatchQueue.main.async {
                            self.completionHandler(self)
                        }

                        // Remove HEIC processed file.
                        removeFile(at: procesedImageURL)
                    })
                } else {
                    DispatchQueue.main.async {
                        self.completionHandler(self)
                    }

                    // Remove HEIC processed file.
                    removeFile(at: procesedImageURL)
                }
            }
        }
    }

    func photoOutput(_ output: AVCapturePhotoOutput, didFinishCaptureFor resolvedSettings: AVCaptureResolvedPhotoSettings, error: Error?) {
        if let error = error {
            print("Error capturing photo: \(error)")
            DispatchQueue.main.async {
                self.completionHandler(self)
            }
        } else {
            guard let data = captureData else {
                DispatchQueue.main.async {
                    self.completionHandler(self)
                }
                return
            }
            self.saveToPhotoLibrary(data)
        }
    }
}
