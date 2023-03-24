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

class AtomicCounter {
    private var value: Int
    private let lock = NSLock()

    init(_ value: Int) {
        self.value = value
    }

    var wrappedValue: Int {
        get {
            lock.lock()
            defer { lock.unlock() }

            return value
        }
        set {
            lock.lock()
            defer { lock.unlock() }

            value = newValue
        }
    }

    static prefix func ++(x:AtomicCounter) -> Int {
        x.lock.lock()
        defer { x.lock.unlock() }

        x.value += 1
        return x.value
    }

    static prefix func --(x:AtomicCounter) -> Int {
        x.lock.lock()
        defer { x.lock.unlock() }

        x.value -= 1
        return x.value
    }
}

class PhotoCaptureProcessor: NSObject {
    private(set) var requestedPhotoSettings: AVCapturePhotoSettings

    private(set) var rawImage: URL?
    private(set) var procesedImage: Data?
    private(set) var capturedImage: Data?
    private(set) var imageMetadata: NSDictionary?

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
            let destination = CGImageDestinationCreateWithData(mutableData, AVFileType.heic as CFString, 1, nil),
            let cgImage = cgImage
        else {
            return nil
        }

        let options = [
            kCGImageDestinationLossyCompressionQuality: compressionQuality,
            kCGImagePropertyOrientation: cgImageOrientation.rawValue
        ] as [CFString : Any] as CFDictionary

        CGImageDestinationAddImage(destination, cgImage, options)
        guard CGImageDestinationFinalize(destination) else {
            return nil
        }
        return mutableData as Data
    }
}

extension CGImage {
    func heic(withMetadata metadata:NSDictionary?) -> Data? {
        guard let mutableData = CFDataCreateMutable(nil, 0),
            let destination = CGImageDestinationCreateWithData(mutableData, AVFileType.heic as CFString, 1, nil) else { return nil }

        CGImageDestinationAddImage(destination, self, metadata)
        guard CGImageDestinationFinalize(destination) else {
            return nil
        }
        return mutableData as Data
    }
}

func removeFile(at url:URL) {
    do {
        let fileManager = FileManager()
        try fileManager.removeItem(at: url)
    } catch {
        print("can't remove item: ", url)
    }
}

func encodeImageToHeif(_ image: CIImage, compressionQuality: CGFloat, use10BitRepresentation: Bool = false ) -> Data? {
    return autoreleasepool(invoking: { () -> Data? in
        let color = CGColorSpace(name: CGColorSpace.sRGB)  // TODO: use displayP3
        let context = CIContext()

        if use10BitRepresentation {
            do {
                return try context.heif10Representation(of: image,
                                                        colorSpace: color!,
                                                        options: [kCGImageDestinationLossyCompressionQuality
                                                                  as CIImageRepresentationOption : compressionQuality] )
            } catch {
                return nil
            }
        } else {
            return context.heifRepresentation(of: image,
                                              format: CIFormat.RGBA8,
                                              colorSpace: color!,
                                              options: [kCGImageDestinationLossyCompressionQuality
                                                        as CIImageRepresentationOption : compressionQuality] )
        }
    })
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
        }

        if photo.isRawPhoto {
            // Generate a unique URL to write the RAW file.
            rawImage = makeUniqueDNGFileURL()

            if let rawImage = rawImage {
                do {
                    // Write the RAW data to a DNG file.
                    if let captureData = photo.fileDataRepresentation() {
                        try captureData.write(to: rawImage)

                        // Convert DNG file to PNG.
                        let pngImagePath = rawProcessor.convertDngFile(rawImage.path())

                        // TODO: add some error checking here...

                        // Convert PNG to HEIC.
                        let png_image = UIImage(contentsOfFile: pngImagePath)

                        print("PNG image bit depth: ", png_image!.cgImage!.bitsPerComponent)

                        // procesedImage = png_image!.heic(compressionQuality: 0.9)
                        procesedImage = encodeImageToHeif(CIImage(image: png_image!)!, compressionQuality: 0.9, use10BitRepresentation: true)

                        // Remove PNG file.
                        removeFile(at: URL(fileURLWithPath: pngImagePath))
                    }
                } catch {
                    fatalError("Couldn't write DNG file to the URL.")
                }
            }
        } else {
            // Store compressed bitmap data.
            capturedImage = photo.fileDataRepresentation()

            // Save metadata for reprocessed image.
            imageMetadata = photo.metadata as NSDictionary
        }
    }

    private func makeUniqueDNGFileURL() -> URL {
        let tempDir = FileManager.default.temporaryDirectory
        let fileName = ProcessInfo.processInfo.globallyUniqueString
        return tempDir.appendingPathComponent(fileName).appendingPathExtension("dng")
    }

    // MARK: Saves capture to photo library
    func saveToPhotoLibrary() {
        let imageCounter = AtomicCounter(procesedImage != nil ? 2 : 1)

        let completeTransaction = {
            if --imageCounter == 0 {
                DispatchQueue.main.async {
                    self.completionHandler(self)
                }
            }
        }

        if let capturedImage = self.capturedImage {
            let finalize = {
                completeTransaction()
                self.rawImage = nil
            }

            PHPhotoLibrary.requestAuthorization { status in
                if status == .authorized {
                    PHPhotoLibrary.shared().performChanges({
                        // Add the compressed (HEIF) data as the main resource for the Photos asset.
                        let creationRequest = PHAssetCreationRequest.forAsset()
                        creationRequest.addResource(with: .photo, data: capturedImage, options: nil)

                        // Add the RAW (DNG) file as an altenate resource.
                        if let rawImage = self.rawImage {
                            let options = PHAssetResourceCreationOptions()
                            options.shouldMoveFile = true
                            creationRequest.addResource(with: .alternatePhoto, fileURL: rawImage, options: options)
                        }
                    }, completionHandler: { _, error in
                        if let error = error {
                            print("Error occurred while saving photo to photo library: \(error)")
                        }
                        finalize()
                    })
                } else {
                    finalize()
                }
            }
        }

        // Save the processed RAW image as a separate file
        if let procesedImage = self.procesedImage {
            let finalize = {
                completeTransaction()
                self.procesedImage = nil
                self.imageMetadata = nil
            }

            if let cgImageSource = CGImageSourceCreateWithData(procesedImage as CFData, nil),
               let cgImage = CGImageSourceCreateImageAtIndex(cgImageSource, 0, nil),
               let heicData = cgImage.heic(withMetadata: self.imageMetadata) {
                PHPhotoLibrary.requestAuthorization { status in
                    if status == .authorized {
                        PHPhotoLibrary.shared().performChanges({
                            let creationRequest = PHAssetCreationRequest.forAsset()
                            creationRequest.addResource(with: .photo, data: heicData, options: nil)
                        }, completionHandler: { _, error in
                            if let error = error {
                                print("Error occurred while saving photo to photo library: \(error)")
                            }
                            finalize()
                        })
                    } else {
                        finalize()
                    }
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
            self.saveToPhotoLibrary()
        }
    }
}
