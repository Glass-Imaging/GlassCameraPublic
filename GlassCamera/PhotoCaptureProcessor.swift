// Copyright (c) 2021-2023 Glass Imaging Inc.
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

    /*
    static prefix func --(x:AtomicCounter) -> Int {
        x.lock.lock()
        defer { x.lock.unlock() }

        x.value -= 1
        return x.value
    }
     */
}

extension [String : Any] {
    subscript<T>(section: String, key: String) -> T? {
        return (self[section] as? [String : Any])?[key] as? T
    }
}

extension RawMetadata {
    convenience init(from metadata: [String : Any]) {
        self.init()

        // let metadata = metadata as NSDictionary

        exposureBiasValue = Float32(metadata["{Exif}", "ExposureBiasValue"] as Double? ?? 0)
        baselineExposure = Float32(metadata["{DNG}", "BaselineExposure"] as Double? ?? 0)
        exposureTime = Float32(metadata["{Exif}", "ExposureTime"] as Double? ?? 0)
        isoSpeedRating = Int32((metadata["{Exif}", "ISOSpeedRatings"] as [Int]?)?[0] ?? 0)
        blackLevel = metadata["{DNG}", "BlackLevel"] as Int32? ?? 0
        whiteLevel = metadata["{DNG}", "WhiteLevel"] as Int32? ?? 0
        calibrationIlluminant1 = metadata["{DNG}", "CalibrationIlluminant1"] as Int32? ?? 0
        calibrationIlluminant2 = metadata["{DNG}", "CalibrationIlluminant2"] as Int32? ?? 0
        colorMatrix1 = metadata["{DNG}", "ColorMatrix1"] as [NSNumber]? ?? []
        colorMatrix2 = metadata["{DNG}", "ColorMatrix2"] as [NSNumber]? ?? []
        asShotNeutral = metadata["{DNG}", "AsShotNeutral"] as [NSNumber]? ?? []
        noiseProfile = metadata["{DNG}", "NoiseProfile"] as [NSNumber]? ?? []
    }
}

class PhotoCaptureProcessor: NSObject {
    // private(set) var requestedPhotoSettings: AVCapturePhotoSettings
    let id: Int

    private(set) var dngFile: URL?
    // private(set) var procesedImage: Data?
    private(set) var capturedImage: Data?
    private(set) var imageMetadata: NSDictionary?

    private let completionHandler: (PhotoCaptureProcessor) -> Void
    private let photoCapturingHandler: (Bool) -> Void
    private let photoProcessingHandler: (Bool) -> Void
    private var maxPhotoProcessingTime: CMTime?

    private let rawProcessor = RawProcessor()
    private var saveRawDataTasks = [Task<Bool, Never>]()
    private var processRawDataTask: Task<Data?, Never>? = nil

    private let saveCollection: PhotoCollection

    // Select the image processing pipeline
    private let isNNProcessingOn: Bool
    private var rawCount = AtomicCounter(0)

    // Save the location of captured photos
    var location: CLLocation?
    let timestamp: String

    init(saveCollection: PhotoCollection,
         // with requestedPhotoSettings: AVCapturePhotoSettings,
         id: Int,
         isNNProcessingOn: Bool,
         completionHandler: @escaping (PhotoCaptureProcessor) -> Void,
         photoCapturingHandler: @escaping (Bool) -> Void,
         photoProcessingHandler: @escaping (Bool) -> Void) {

        self.saveCollection = saveCollection
        //self.requestedPhotoSettings = requestedPhotoSettings
        self.id = id
        self.isNNProcessingOn = isNNProcessingOn
        self.completionHandler = completionHandler
        self.photoCapturingHandler = photoCapturingHandler
        self.photoProcessingHandler = photoProcessingHandler


        let dateFMT = DateFormatter()
        dateFMT.locale = Locale(identifier: "en_US_POSIX")
        dateFMT.dateFormat = "yyyy-MM-dd-HH-mm-ss-SSSS"

        let now = Date()
        timestamp = String(format: "%@", dateFMT.string(from: now))
    }

    func startRawProcessingTasks(photo: AVCapturePhoto) {
        let rawIndex = ++rawCount

        if rawIndex == 1 {
            processRawDataTask = Task(priority: .userInitiated) {
                if let displayP3 = CGColorSpace(name: CGColorSpace.displayP3), let rawPixelBuffer = photo.pixelBuffer {
                    let rawMetadata = RawMetadata(from: photo.metadata)

                    let pixelBuffer = self.isNNProcessingOn
                    ? self.rawProcessor.nnProcessRawPixelBuffer(rawPixelBuffer, with: rawMetadata).takeRetainedValue()
                    : self.rawProcessor.convertRawPixelBuffer(rawPixelBuffer, with: rawMetadata).takeRetainedValue()


                    let cgImage = pixelBuffer.createCGImage(colorSpace: displayP3)

                    return encodeImageToHeif(CIImage(cgImage: cgImage!),
                                             compressionQuality: 0.8, colorSpace: displayP3,
                                             use10BitRepresentation: false /*cgImage!.bitsPerComponent > 8*/)
                }
                return nil
            }


            // saveRawDataTask = Task(priority: .userInitiated) {
            saveRawDataTasks.append(Task(priority: .userInitiated) {
                self.dngFile = makeUniqueDNGFileURL()
                do {
                    if let captureData = photo.fileDataRepresentation() {
                        try captureData.write(to: self.dngFile!)
                    } else {
                        return false
                    }
                } catch {
                    fatalError("Couldn't write DNG file to the URL.")
                }

                return true
            })
        } else {
            saveRawDataTasks.append(Task(priority: .userInitiated) {
                // TODO: Move this out of here!
                let file = makeUniqueDNGFileURL()
                do {
                    if let captureData = photo.fileDataRepresentation() {
                        try captureData.write(to: file)
                        try! await self.saveCollection.addImage(captureData, timestamp: self.timestamp, photoCategory: PhotoCategories.GlassRawBurst, alternateResource: nil, location: self.location, index: rawIndex)
                    } else {
                        return false
                    }
                } catch {
                    fatalError("Couldn't write DNG file to the URL.")
                }

                return true
            })
        }
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

func encodeImageToHeif(_ image: CIImage, compressionQuality: CGFloat, colorSpace: CGColorSpace, use10BitRepresentation: Bool = false ) -> Data? {
    return autoreleasepool(invoking: { () -> Data? in
        let ciContext = CIContext()
        if use10BitRepresentation {
            do {
                return try ciContext.heif10Representation(of: image,
                                                          colorSpace: image.colorSpace!,
                                                          options: [kCGImageDestinationLossyCompressionQuality
                                                                    as CIImageRepresentationOption : compressionQuality] )
            } catch {
                return nil
            }
        } else {
            return ciContext.heifRepresentation(of: image,
                                                format: CIFormat.RGBA8,
                                                colorSpace: image.colorSpace!,
                                                options: [kCGImageDestinationLossyCompressionQuality
                                                          as CIImageRepresentationOption : compressionQuality] )
        }
    })
}

extension CVPixelBuffer {
    func createCGImage(colorSpace: CGColorSpace) -> CGImage? {
        let pixelFormatType = CVPixelBufferGetPixelFormatType(self)

        let imageFormat:CIFormat
        if (pixelFormatType == kCVPixelFormatType_24RGB) {
            // NOTE: kCVPixelFormatType_32RGBA doesn't seem to work
            imageFormat = CIFormat.RGBA8
        } else if (pixelFormatType == kCVPixelFormatType_64RGBALE) {
            imageFormat =  CIFormat.RGBA16
        } else if (pixelFormatType == kCVPixelFormatType_64RGBAHalf) {
            imageFormat = CIFormat.RGBAh
        } else {
            print("Unsupported pixelFormatType: ", pixelFormatType)
            return nil
        }

        let ciImage = CIImage(cvPixelBuffer: self,
                              options: [CIImageOption.colorSpace : colorSpace as Any])
        let ciContext = CIContext()
        return ciContext.createCGImage(ciImage, from: ciImage.extent, format: imageFormat, colorSpace: colorSpace)
    }
}

private func makeUniqueDNGFileURL() -> URL {
    let tempDir = FileManager.default.temporaryDirectory
    let fileName = ProcessInfo.processInfo.globallyUniqueString
    return tempDir.appendingPathComponent(fileName).appendingPathExtension("dng")
}

extension PhotoCaptureProcessor: AVCapturePhotoCaptureDelegate {
    // This extension adopts all of the AVCapturePhotoCaptureDelegate protocol methods.

    func photoOutput(_ output: AVCapturePhotoOutput, willBeginCaptureFor resolvedSettings: AVCaptureResolvedPhotoSettings) {
        maxPhotoProcessingTime = resolvedSettings.photoProcessingTimeRange.start + resolvedSettings.photoProcessingTimeRange.duration
    }

    func photoOutput(_ output: AVCapturePhotoOutput, willCapturePhotoFor resolvedSettings: AVCaptureResolvedPhotoSettings) {
        DispatchQueue.main.async {
            self.photoCapturingHandler(true)
            self.photoProcessingHandler(true)
         }
    }

    func photoOutput(_ output: AVCapturePhotoOutput, didFinishProcessingPhoto photo: AVCapturePhoto, error: Error?) {
        if let error = error {
            print("Error capturing photo: \(error)")
        }

        if photo.isRawPhoto {
            self.startRawProcessingTasks(photo: photo)
        } else {
            // Store compressed bitmap data.
            capturedImage = photo.fileDataRepresentation()

            // Save metadata for reprocessed image.
            imageMetadata = photo.metadata as NSDictionary
        }
    }

    // MARK: Saves capture to photo library
    func saveToPhotoLibrary() {
        // Wait for RAW data to be processed
        Task {
            if let capturedImage = self.capturedImage {
                try! await saveCollection.addImage(capturedImage, timestamp: self.timestamp, photoCategory: PhotoCategories.ISP, alternateResource: self.dngFile, location: self.location)
            }

            if let procesedImage = await self.processRawDataTask?.value {
                if let cgImageSource = CGImageSourceCreateWithData(procesedImage as CFData, nil),
                   let cgImage = CGImageSourceCreateImageAtIndex(cgImageSource, 0, nil),
                   let heicData = cgImage.heic(withMetadata: self.imageMetadata) {

                    try! await saveCollection.addImage(heicData, timestamp: self.timestamp,
                                                       photoCategory: isNNProcessingOn ? PhotoCategories.GlassNN : PhotoCategories.GlassTraditional,
                                                       alternateResource: nil, location: self.location)
                    self.imageMetadata = nil
                }
            }

            for t in saveRawDataTasks { await t.value }
            self.completionHandler(self)
        }
    }

    func photoOutput(_ output: AVCapturePhotoOutput, didFinishCaptureFor resolvedSettings: AVCaptureResolvedPhotoSettings, error: Error?) {
        DispatchQueue.main.async {
            self.photoProcessingHandler(false)
            self.photoCapturingHandler(false)
        }

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
