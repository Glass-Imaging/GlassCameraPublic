import CoreImage

class ContentViewModel: ObservableObject {

    @Published var frame: CGImage?
    @Published var error: Error?

    private let context = CIContext()

    private let cameraManager = CameraManager.shared
    private let frameManager = FrameManager.shared

    init() {
        setupSubscriptions()
    }

    func setupSubscriptions() {
        cameraManager.$error
            .receive(on: RunLoop.main)
            .map { $0 }
            .assign(to: &$error)

        frameManager.$current
            .receive(on: RunLoop.main)
            .compactMap { buffer in
                guard let pixelBuffer = buffer else {
                    return nil
                }

                let inputImage = CIImage(cvPixelBuffer: pixelBuffer)
                let image = self.context.createCGImage(inputImage, from: inputImage.extent)

                return image
            }
            .assign(to: &$frame)
    }
}
