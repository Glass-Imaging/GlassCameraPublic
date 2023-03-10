import AVFoundation

class CameraManager: ObservableObject {

    /** enums to represent the CameraManager statuses */
    enum Status {
        case unconfigured
        case configured
        case unauthorized
        case failed
    }

    /** enums to represent errors related to using, acessing, IO etc of the camera device */
    enum CameraError: Error {
        case cameraUnavailable
        case cannotAddInput
        case cannotAddOutput
        case deniedAuthorization
        case restrictedAuthorization
        case unknownAuthorization
        case thrownError(message: Error)
    }

    /** ``error`` stores the current error related to camera */
    @Published var error: CameraError?

    /** ``session`` stores camera capture session */
    let session = AVCaptureSession()

    /** ``shared`` a single reference to instance of CameraManager
     All the other codes in the app must use this single instance */
    static let shared = CameraManager()

    private let sessionQueue = DispatchQueue(label: "com.glass-imaging.CameraApp.SessionQ")

    private let videoOutput = AVCaptureVideoDataOutput()

    private var status = Status.unconfigured

    /** ``set(_:queue:)`` configures `delegate` and `queue`
     this should be configured before using the camera output */
    func set(
        _ delegate: AVCaptureVideoDataOutputSampleBufferDelegate,
        queue: DispatchQueue
    ) {
        sessionQueue.async {
            self.videoOutput.setSampleBufferDelegate(delegate, queue: queue)
        }
    }

    private func setError(_ error: CameraError?) {
        DispatchQueue.main.async {
            self.error = error
        }
    }

    private init() {
        configure()
    }

    private func configure() {
        checkPermissions()
        sessionQueue.async {
            self.configureCaptureSession()
            self.session.startRunning()
        }
    }

    private func configureCaptureSession() {
        guard status == .unconfigured else {
            return
        }
        session.beginConfiguration()
        defer {
            session.commitConfiguration()
        }
        let device = AVCaptureDevice.default(
            .builtInWideAngleCamera,
            for: .video,
            position: .back)
        guard let camera = device else {
            setError( .cameraUnavailable)
            status = .failed
            return
        }
        do {
            let cameraInput = try AVCaptureDeviceInput(device: camera)

            if session.canAddInput(cameraInput) {
                session.addInput(cameraInput)
            } else {
                setError( .cannotAddInput)
                status = .failed
                return
            }
        } catch {
            debugPrint(error)
            setError(.thrownError(message: error))
            status = .failed
            return
        }

        if session.canAddOutput(videoOutput) {
            session.addOutput(videoOutput)

            videoOutput.videoSettings =
            [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA]

            let videoConnection = videoOutput.connection(with: .video)
            videoConnection?.videoOrientation = .portrait
        } else {
            setError( .cannotAddOutput)
            status = .failed
            return
        }
        status = .configured
    }

    private func checkPermissions() {
        switch AVCaptureDevice.authorizationStatus(for: .video) {
        case .notDetermined:
            sessionQueue.suspend()
            AVCaptureDevice.requestAccess(for: .video) { authorized in
                if !authorized {
                    self.status = .unauthorized
                    self.setError(.deniedAuthorization)
                }
                self.sessionQueue.resume()
            }
        case .restricted:
            status = .unauthorized
            setError(.restrictedAuthorization)
        case .denied:
            status = .unauthorized
            setError(.deniedAuthorization)
        case .authorized:
            /** ``Status.authorized-enum.case`` represents all success to get the camera access */
            break
        @unknown default:
            status = .unauthorized
            setError(.unknownAuthorization)
        }
    }
}
