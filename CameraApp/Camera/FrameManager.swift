import AVFoundation

class FrameManager: NSObject, ObservableObject {
    
    /** ``shared`` a single instance of FrameManager.
     All the other codes in the app must use this single instance */
    static let shared = FrameManager()
    
    /** ``current`` stores the current pixel data from camera */
    @Published var current: CVPixelBuffer?
    
    /** ``videoOutputQueue`` a queue to receive camera video output */
    let videoOutputQueue = DispatchQueue(
        label: "com.glass-imaging.CameraApp.VideoOutputQ",
        qos: .userInitiated,
        attributes: [],
        autoreleaseFrequency: .workItem)
    
    private override init() {
        super.init()
        CameraManager.shared.set(self, queue: videoOutputQueue)
    }
}

extension FrameManager: AVCaptureVideoDataOutputSampleBufferDelegate {
    /** ``captureOutput(_:didOutput:from:)`` sets the buffer data to ``current`` */
    func captureOutput(
        _ output: AVCaptureOutput,
        didOutput sampleBuffer: CMSampleBuffer,
        from connection: AVCaptureConnection
    ) {
        if let buffer = sampleBuffer.imageBuffer {
            DispatchQueue.main.async {
                self.current = buffer
            }
        }
    }
}
