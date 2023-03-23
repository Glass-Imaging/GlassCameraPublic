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

class PhotoCaptureProcessor: NSObject {
    private(set) var captureData: Data?
    private(set) var requestedPhotoSettings: AVCapturePhotoSettings

    private(set) var rawFileURL: URL?
    private(set) var procesedImageURL: URL?
    private(set) var compressedData: Data?

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
            // Generate a unique URL to write the RAW file.
            rawFileURL = makeUniqueDNGFileURL()
            do {
                // Write the RAW data to a DNG file.
                try captureData!.write(to: rawFileURL!)

                // Convert DNG file to PNG.
                let procesedImagePath = rawProcessor.convertDngFile(rawFileURL!.path())
                procesedImageURL = URL(fileURLWithPath: procesedImagePath)

                // Remove DNG file.
                let fileManager = FileManager()
                try fileManager.removeItem(at: rawFileURL!)
            } catch {
                fatalError("Couldn't write DNG file to the URL.")
            }
        } else {
            // Store compressed bitmap data.
            compressedData = captureData
        }
    }

    private func makeUniqueDNGFileURL() -> URL {
        let tempDir = FileManager.default.temporaryDirectory
        let fileName = ProcessInfo.processInfo.globallyUniqueString
        return tempDir.appendingPathComponent(fileName).appendingPathExtension("dng")
    }

    // MARK: Saves capture to photo library
    func saveToPhotoLibrary(_ captureData: Data) {
        PHPhotoLibrary.requestAuthorization { status in
            if status == .authorized {
                PHPhotoLibrary.shared().performChanges({
                    // Add the compressed (HEIF) data as the main resource for the Photos asset.
                    let creationRequest = PHAssetCreationRequest.forAsset()

                    if let compressedData = self.compressedData {
                        creationRequest.addResource(with: .photo, data: compressedData, options: nil)
                    }

                    // Add the RAW (DNG) file as an altenate resource.
                    if let procesedImageURL = self.procesedImageURL {
                        let options = PHAssetResourceCreationOptions()
                        options.shouldMoveFile = true
                        creationRequest.addResource(with: .alternatePhoto, fileURL: procesedImageURL, options: options)
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
