import CoreImage
import Combine
import UniformTypeIdentifiers

class PhotoManager {
    private static var cancellable: AnyCancellable?
    static func take() {
        debugPrint("Clicked PhotoManager.take()")
        cancellable = FrameManager.shared.$current.first().sink { receiveValue in
            guard receiveValue != nil else {
                debugPrint("[W] PhotoManager.take: buffer returned nil")
                return
            }
            
            let inputImage = CIImage(cvPixelBuffer: receiveValue!)
            let context = CIContext(options: nil)
            let cgImage = context.createCGImage(inputImage, from: inputImage.extent)
            guard cgImage != nil else {
                debugPrint("[W] PhotoManager.take: CGImage is nil")
                return
            }
            self.save(image: cgImage!, filename: "my-image-test.png")
        }
        
    }
    
    static func save(image: CGImage, filename: String) {
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
