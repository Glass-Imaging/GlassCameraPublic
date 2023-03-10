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

import AVFoundation

class CameraManager: ObservableObject {

    enum Status {
        case unconfigured
        case configured
        case unauthorized
        case failed
    }

    enum CameraError: Error {
        case cameraUnavailable
        case cannotAddInput
        case cannotAddOutput
        case deniedAuthorization
        case restrictedAuthorization
        case unknownAuthorization
        case thrownError(message: Error)
    }

    @Published var cameraError: CameraError?

    let captureSession = AVCaptureSession()

    // singleton shared object
    static let cameraManager = CameraManager()

    private let sessionQueue = DispatchQueue(label: "com.glass-imaging.CameraApp.SessionQ")

    private let videoOutput = AVCaptureVideoDataOutput()

    private var status = Status.unconfigured

    func setCaptureDelegate(
        _ delegate: AVCaptureVideoDataOutputSampleBufferDelegate,
        queue: DispatchQueue
    ) {
        sessionQueue.async {
            self.videoOutput.setSampleBufferDelegate(delegate, queue: queue)
        }
    }

    private func setError(_ error: CameraError?) {
        DispatchQueue.main.async {
            self.cameraError = error
        }
    }

    private init() {
        checkPermissions()
        sessionQueue.async {
            self.configureCaptureSession()
            self.captureSession.startRunning()
        }
    }

    private func configureCaptureSession() {
        guard status == .unconfigured else {
            return
        }

        captureSession.beginConfiguration()
        defer {
            captureSession.commitConfiguration()
        }

        var defaultVideoDevice: AVCaptureDevice?

        // Find an available camera
        if let backCameraDevice = AVCaptureDevice.default(.builtInWideAngleCamera, for: .video, position: .back) {
            defaultVideoDevice = backCameraDevice
        } else if let frontCameraDevice = AVCaptureDevice.default(.builtInWideAngleCamera, for: .video, position: .front) {
            defaultVideoDevice = frontCameraDevice
        }

        guard let camera = defaultVideoDevice else {
            setError(.cameraUnavailable)
            status = .failed
            return
        }

        do {
            let cameraInput = try AVCaptureDeviceInput(device: camera)

            if captureSession.canAddInput(cameraInput) {
                captureSession.addInput(cameraInput)
            } else {
                setError(.cannotAddInput)
                status = .failed
                return
            }
        } catch {
            debugPrint(error)
            setError(.thrownError(message: error))
            status = .failed
            return
        }

        if captureSession.canAddOutput(videoOutput) {
            captureSession.addOutput(videoOutput)

            videoOutput.videoSettings =
            [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA]

            let videoConnection = videoOutput.connection(with: .video)
            videoConnection?.videoOrientation = .portrait
        } else {
            setError(.cannotAddOutput)
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
            // We got this...
            break
        @unknown default:
            status = .unauthorized
            setError(.unknownAuthorization)
        }
    }
}
