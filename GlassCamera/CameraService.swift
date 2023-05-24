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
import Combine
import AVFoundation
import CoreLocation
import Photos
import UIKit

//  MARK: Class Camera Service, handles setup of AVFoundation needed for a basic camera app.

public struct AlertError {
    public var title: String = ""
    public var message: String = ""
    public var primaryButtonTitle = "Accept"
    public var secondaryButtonTitle: String?
    public var primaryAction: (() -> ())?
    public var secondaryAction: (() -> ())?

    public init(title: String = "", message: String = "", primaryButtonTitle: String = "Accept", secondaryButtonTitle: String? = nil, primaryAction: (() -> ())? = nil, secondaryAction: (() -> ())? = nil) {
        self.title = title
        self.message = message
        self.primaryAction = primaryAction
        self.primaryButtonTitle = primaryButtonTitle
        self.secondaryAction = secondaryAction
    }
}

public enum BackCameraConfiguration: String, CaseIterable, Identifiable, Equatable {
    case UltraWide
    case Wide
    case Tele

    public var id: Self { self }
}

public struct DeviceConfiguration : Equatable {
    let position: AVCaptureDevice.Position;
    let deviceType: AVCaptureDevice.DeviceType;
}

public class CameraService: NSObject, Identifiable {
    typealias PhotoCaptureSessionID = String

    // MARK: Observed Properties UI must react to

    private var cameraState: CameraState
    private var isCameraPrepared: Task<Bool, Never>? = nil
    private var lockPreparedSettings = false

    @Published public var shouldShowAlertView = false
    @Published public var shouldShowSpinner = false

    @Published public var isPhotoCapturing = false
    @Published public var isCameraButtonDisabled = false
    @Published public var isCameraUnavailable = false
    @Published public var thumbnail: UIImage?

    @Published public var availableBackDevices: [BackCameraConfiguration] = []

    @Published public var currentDevice: DeviceConfiguration? = nil

    public let backDeviceConfigurations: [BackCameraConfiguration : DeviceConfiguration] = [
        .UltraWide : DeviceConfiguration(position: .back,  deviceType: .builtInUltraWideCamera),
        .Wide      : DeviceConfiguration(position: .back,  deviceType: .builtInWideAngleCamera),
        .Tele      : DeviceConfiguration(position: .back,  deviceType: .builtInTelephotoCamera)
    ]

    // MARK: Alert properties

    public var alertError: AlertError = AlertError()

    // MARK: Session Management Properties

    public let session = AVCaptureSession()

    var isSessionRunning = false
    var isConfigured = false
    var setupResult: SessionSetupResult = .success

    // Communicate with the session and other session objects on this queue.
    let sessionQueue = DispatchQueue(label: "session queue")

    @objc dynamic var videoDeviceInput: AVCaptureDeviceInput!

    // MARK: Device Configuration Properties

    static let videoDevices:[AVCaptureDevice.DeviceType] = [.builtInWideAngleCamera, .builtInTelephotoCamera, .builtInUltraWideCamera]
    let videoDeviceDiscoverySession = AVCaptureDevice.DiscoverySession(deviceTypes: videoDevices, mediaType: .video, position: .unspecified)

    // MARK: Capturing Photos

    let photoOutput = AVCapturePhotoOutput()

    var processingID = AtomicCounter(0)
    // var inProgressPhotoCaptureDelegates = [Int64: PhotoCaptureProcessor]()
    var inProgressPhotoCaptureDelegates = [Int: PhotoCaptureProcessor]()

    // MARK: KVO and Notifications Properties

    var keyValueObservations = [NSKeyValueObservation]()

    let locationManager = CLLocationManager()

    private var subscriptions = Set<AnyCancellable>()

    init(cameraState: CameraState) {
        self.cameraState = cameraState
        super.init()

        // Disable the UI. Enable the UI later, if and only if the session starts running.
        DispatchQueue.main.async {
            self.isCameraButtonDisabled = true
            self.isCameraUnavailable = true
        }

        // Register the list of available devices
        let devices = self.videoDeviceDiscoverySession.devices
        for configuration in backDeviceConfigurations {
            if let _ = devices.first(where: { $0.position == configuration.value.position &&
                $0.deviceType == configuration.value.deviceType }) {
                availableBackDevices.append(configuration.key)
            }
        }

        // Request location authorization so photos and videos can be tagged with their location.
        if locationManager.authorizationStatus == .notDetermined {
            locationManager.requestWhenInUseAuthorization()
        }

        cameraState.$customExposureBias.sink { newBias in
            guard let captureDevice = self.videoDeviceInput?.device else { return }
            let clampedNewBias = min(max(newBias, cameraState.deviceMinExposureBias), cameraState.deviceMaxExposureBias)
            try! captureDevice.lockForConfiguration()
            captureDevice.setExposureTargetBias(clampedNewBias) { _ in }
            captureDevice.unlockForConfiguration()
        }.store(in: &self.subscriptions)

        cameraState.$customExposureDuration.combineLatest(cameraState.$customISO)
            .filter { newExposureDuration, newISO in
                return self.videoDeviceInput != nil
                && self.cameraState.isCustomExposure
                && (newExposureDuration.seconds <= self.videoDeviceInput.device.activeFormat.maxExposureDuration.seconds)
                && (newExposureDuration.seconds > self.videoDeviceInput.device.activeFormat.minExposureDuration.seconds)
                && (newISO <= self.videoDeviceInput.device.activeFormat.maxISO)
                && (newISO >= self.videoDeviceInput.device.activeFormat.minISO)
            }
            .sink { newExposureDuration, newISO in
                try! self.videoDeviceInput.device.lockForConfiguration()
                self.videoDeviceInput.device.setExposureModeCustom(duration: newExposureDuration, iso: newISO) {_ in }
                self.videoDeviceInput.device.unlockForConfiguration()
            }.store(in: &self.subscriptions)

        cameraState.$isCustomExposure
            .removeDuplicates()
            .filter { isCustomExposure in return !isCustomExposure }
            .sink { _ in
                guard let captureDevice = self.videoDeviceInput?.device else { return }
                try! captureDevice.lockForConfiguration()
                captureDevice.exposureMode = AVCaptureDevice.ExposureMode.continuousAutoExposure
                captureDevice.unlockForConfiguration()
            }.store(in: &self.subscriptions)

        cameraState.$targetMaxExposureDuration.sink { newTargetMaxExposureDuration in
            guard let captureDevice = self.videoDeviceInput?.device else { return }
            try! captureDevice.lockForConfiguration()
            captureDevice.activeMaxExposureDuration = newTargetMaxExposureDuration
            captureDevice.unlockForConfiguration()
        }.store(in: &self.subscriptions)

        cameraState.$zoomLevel.sink { configuration in
            if(self.isConfigured) {
                print("SET ZOOM LEVEL :: \(configuration)")
                let newConfiguration = self.backDeviceConfigurations[configuration] ?? DeviceConfiguration(position: .back, deviceType: .builtInWideAngleCamera)
                self.changeCamera(newConfiguration)
            }
        }
        .store(in: &self.subscriptions)
    }

    public func configure(_ configuration: DeviceConfiguration) {
        /*
         Setup the capture session.
         In general, it's not safe to mutate an AVCaptureSession or any of its
         inputs, outputs, or connections from multiple threads at the same time.

         Don't perform these tasks on the main queue because
         AVCaptureSession.startRunning() is a blocking call, which can
         take a long time. Dispatch session setup to the sessionQueue, so
         that the main queue isn't blocked, which keeps the UI responsive.
         */
        if !self.isSessionRunning && !self.isConfigured {
            sessionQueue.async {
                self.configureSession(configuration)
            }
        }
    }

    // MARK: Checks for permisions, setup obeservers and starts running session
    public func checkForPermissions() {
        /*
         Check the video authorization status. Video access is required and audio
         access is optional. If the user denies audio access, AVCam won't
         record audio during movie recording.
         */
        switch AVCaptureDevice.authorizationStatus(for: .video) {
        case .authorized:
            // The user has previously granted access to the camera.
            break
        case .notDetermined:
            /*
             The user has not yet been presented with the option to grant
             video access. Suspend the session queue to delay session
             setup until the access request has completed.

             Note that audio access will be implicitly requested when we
             create an AVCaptureDeviceInput for audio during session setup.
             */
            sessionQueue.suspend()
            AVCaptureDevice.requestAccess(for: .video, completionHandler: { granted in
                if !granted {
                    self.setupResult = .notAuthorized
                }
                self.sessionQueue.resume()
            })

        default:
            // The user has previously denied access.
            setupResult = .notAuthorized

            // FIXME: This is terrible
            DispatchQueue.main.async {
                self.alertError = AlertError(title: "Camera Access",
                                             message: "No Camera Access Permission, please update configuration.",
                                             primaryButtonTitle: "Configuration", secondaryButtonTitle: nil, primaryAction: {
                    UIApplication.shared.open(URL(string: UIApplication.openSettingsURLString)!,
                                              options: [:], completionHandler: nil)

                }, secondaryAction: nil)
                self.shouldShowAlertView = true
                self.isCameraUnavailable = true
                self.isCameraButtonDisabled = true
            }
        }
    }

    //  MARK: Session Managment

    // Call this on the session queue.
    private func configureSession(_ configuration: DeviceConfiguration) {
        if setupResult != .success {
            return
        }

        session.beginConfiguration()

        /*
         Do not create an AVCaptureMovieFileOutput when setting up the session because
         Live Photo is not supported when AVCaptureMovieFileOutput is added to the session.
         */
        session.sessionPreset = .photo


        // Add video input.
        do {
            var videoDevice: AVCaptureDevice?

            if let desiredDevice = AVCaptureDevice.default(configuration.deviceType, for: .video, position: configuration.position) {
                // If a rear dual camera is not available, default to the rear wide angle camera.
                videoDevice = desiredDevice
            } else if let backupDevice = AVCaptureDevice.default(backDeviceConfigurations[cameraState.zoomLevel]?.deviceType ?? .builtInWideAngleCamera, for: .video, position: .unspecified) {
                // If the rear wide angle camera isn't available, default to the front wide angle camera.
                videoDevice = backupDevice
            }

            guard let videoDevice = videoDevice else {
                print("Default video device is unavailable.")
                setupResult = .configurationFailed
                session.commitConfiguration()
                return
            }

            print("Is Global Tone mapping supported :: \(videoDevice.activeFormat.isGlobalToneMappingSupported)")
            if videoDevice.activeFormat.isGlobalToneMappingSupported {
                try! videoDevice.lockForConfiguration()
                videoDevice.isGlobalToneMappingEnabled = true
                videoDevice.activeMaxExposureDuration = cameraState.targetMaxExposureDuration
                videoDevice.unlockForConfiguration()
            }


            let videoDeviceInput = try AVCaptureDeviceInput(device: videoDevice)

            if session.canAddInput(videoDeviceInput) {
                session.addInput(videoDeviceInput)
                self.videoDeviceInput = videoDeviceInput
                self.videoDeviceInput.unifiedAutoExposureDefaultsEnabled = true
                self.cameraState.updateCameraDevice(device: self.videoDeviceInput.device)

                DispatchQueue.main.async {
                    self.currentDevice = DeviceConfiguration(position: videoDeviceInput.device.position,
                                                             deviceType: videoDeviceInput.device.deviceType)
                }
            } else {
                print("Couldn't add video device input to the session.")
                setupResult = .configurationFailed
                session.commitConfiguration()
                return
            }
        } catch {
            print("Couldn't create video device input: \(error)")
            setupResult = .configurationFailed
            session.commitConfiguration()
            return
        }

        // Add the photo output.
        if session.canAddOutput(photoOutput) {
            session.addOutput(photoOutput)

            // photoOutput.isHighResolutionCaptureEnabled = true
            photoOutput.maxPhotoQualityPrioritization = .quality

            // Use the Apple ProRAW format when the environment supports it.
            photoOutput.isAppleProRAWEnabled = photoOutput.isAppleProRAWSupported
        } else {
            print("Could not add photo output to the session")
            setupResult = .configurationFailed
            session.commitConfiguration()
            return
        }

        session.commitConfiguration()
        self.isConfigured = true

        self.start()
    }

    private func resumeInterruptedSession() {
        sessionQueue.async {
            /*
             The session might fail to start running, for example, if a phone or FaceTime call is still
             using audio or video. This failure is communicated by the session posting a
             runtime error notification. To avoid repeatedly failing to start the session,
             only try to restart the session in the error handler if you aren't
             trying to resume the session.
             */
            self.session.startRunning()
            self.isSessionRunning = self.session.isRunning
            if !self.session.isRunning {
                DispatchQueue.main.async {
                    self.alertError = AlertError(title: "Camera Error", message: "Unable to resume camera", primaryButtonTitle: "Accept", secondaryButtonTitle: nil, primaryAction: nil, secondaryAction: nil)
                    self.shouldShowAlertView = true
                    self.isCameraUnavailable = true
                    self.isCameraButtonDisabled = true
                }
            } else {
                DispatchQueue.main.async {
                    self.isCameraUnavailable = false
                    self.isCameraButtonDisabled = false
                }
            }
        }
    }

    //  MARK: Device Configuration

    public func changeCamera(_ configuration: DeviceConfiguration) {
        // MARK: Here disable all camera operation related buttons due to configuration is due upon and must not be interrupted

        DispatchQueue.main.async {
            self.isCameraButtonDisabled = true
        }

        sessionQueue.async {
            let currentVideoDevice = self.videoDeviceInput.device

            let devices = self.videoDeviceDiscoverySession.devices
            var newVideoDevice: AVCaptureDevice? = nil

            // First, seek a device with both the preferred position and device type. Otherwise, seek a device with only the preferred position.
            if let device = devices.first(where: { $0.position == configuration.position && $0.deviceType == configuration.deviceType }) {
                newVideoDevice = device
            } else if let device = devices.first(where: { $0.position == configuration.position }) {
                newVideoDevice = device
            }

            if let videoDevice = newVideoDevice {
                do {
                    print("Is Global Tone mapping supported :: \(videoDevice.activeFormat.isGlobalToneMappingSupported)")
                    try! videoDevice.lockForConfiguration()
                    videoDevice.isGlobalToneMappingEnabled = true
                    videoDevice.activeMaxExposureDuration = self.cameraState.targetMaxExposureDuration
                    videoDevice.unlockForConfiguration()

                    let videoDeviceInput = try AVCaptureDeviceInput(device: videoDevice)

                    self.session.beginConfiguration()

                    // Remove the existing device input first, because AVCaptureSession doesn't support
                    // simultaneous use of the rear and front cameras.
                    self.session.removeInput(self.videoDeviceInput)

                    if self.session.canAddInput(videoDeviceInput) {
                        NotificationCenter.default.removeObserver(self,
                                                                  name: .AVCaptureDeviceSubjectAreaDidChange,
                                                                  object: currentVideoDevice)
                        NotificationCenter.default.addObserver(self,
                                                               selector: #selector(self.subjectAreaDidChange),
                                                               name: .AVCaptureDeviceSubjectAreaDidChange,
                                                               object: videoDeviceInput.device)

                        self.session.addInput(videoDeviceInput)
                        self.videoDeviceInput = videoDeviceInput
                        self.videoDeviceInput.unifiedAutoExposureDefaultsEnabled = true

                        self.cameraState.updateCameraDevice(device: self.videoDeviceInput.device)
                    } else {
                        self.session.addInput(self.videoDeviceInput)
                    }

                    if let connection = self.photoOutput.connection(with: .video) {
                        if connection.isVideoStabilizationSupported {
                            connection.preferredVideoStabilizationMode = .auto
                        }
                    }

                    self.photoOutput.maxPhotoQualityPrioritization = .quality

                    self.session.commitConfiguration()
                } catch {
                    print("Error occurred while creating video device input: \(error)")
                }

                DispatchQueue.main.async {
                    self.currentDevice = DeviceConfiguration(position: videoDevice.position,
                                                             deviceType: videoDevice.deviceType)
                }
            }

            DispatchQueue.main.async {
                // MARK: Here enable all camera operation related buttons due to succesfull setup
                self.isCameraButtonDisabled = false
            }
        }
    }

    public func focus(with focusMode: AVCaptureDevice.FocusMode, exposureMode: AVCaptureDevice.ExposureMode, at devicePoint: CGPoint, monitorSubjectAreaChange: Bool) {
        sessionQueue.async {
            guard let device = self.videoDeviceInput?.device else { return }
            do {
                try device.lockForConfiguration()

                /*
                 Setting (focus/exposure)PointOfInterest alone does not initiate a (focus/exposure) operation.
                 Call set(Focus/Exposure)Mode() to apply the new point of interest.
                 */
                if device.isFocusPointOfInterestSupported && device.isFocusModeSupported(focusMode) {
                    device.focusPointOfInterest = devicePoint
                    device.focusMode = focusMode
                }

                if device.isExposurePointOfInterestSupported && device.isExposureModeSupported(exposureMode) {
                    device.exposurePointOfInterest = devicePoint
                    device.exposureMode = exposureMode
                }

                device.isSubjectAreaChangeMonitoringEnabled = monitorSubjectAreaChange
                device.unlockForConfiguration()
            } catch {
                print("Could not lock device for configuration: \(error)")
            }
        }
    }


    public func focus(at focusPoint: CGPoint){
        let device = self.videoDeviceInput.device
        do {
            try device.lockForConfiguration()
            if device.isFocusPointOfInterestSupported {
                device.focusPointOfInterest = focusPoint
                device.exposurePointOfInterest = focusPoint
                device.exposureMode = .continuousAutoExposure
                device.focusMode = .continuousAutoFocus
                device.unlockForConfiguration()
            }
        }
        catch {
            print(error.localizedDescription)
        }
    }

    @objc public func stop(completion: (() -> ())? = nil) {
        sessionQueue.async {
            if self.isSessionRunning {
                if self.setupResult == .success {
                    self.session.stopRunning()
                    self.isSessionRunning = self.session.isRunning
                    print("CAMERA STOPPED")
                    self.removeObservers()

                    if !self.session.isRunning {
                        DispatchQueue.main.async {
                            self.isCameraButtonDisabled = true
                            self.isCameraUnavailable = true
                            completion?()
                        }
                    }
                }
            }
        }
    }

    @objc public func start() {
        sessionQueue.async {
            if !self.isSessionRunning && self.isConfigured {
                switch self.setupResult {
                case .success:
                    // Only setup observers and start the session if setup succeeded.
                    self.addObservers()
                    self.session.startRunning()
                    self.isSessionRunning = self.session.isRunning

                    if self.session.isRunning {
                        DispatchQueue.main.async {
                            self.isCameraButtonDisabled = false
                            self.isCameraUnavailable = false
                        }
                    }

                case .notAuthorized:
                    print("Application not authorized to use camera")
                    DispatchQueue.main.async {
                        self.isCameraButtonDisabled = true
                        self.isCameraUnavailable = true
                    }

                case .configurationFailed:
                    DispatchQueue.main.async {
                        self.alertError = AlertError(title: "Camera Error", message: "Camera configuration failed. Either your device camera is not available or other application is using it", primaryButtonTitle: "Accept", secondaryButtonTitle: nil, primaryAction: nil, secondaryAction: nil)
                        self.shouldShowAlertView = true
                        self.isCameraButtonDisabled = true
                        self.isCameraUnavailable = true
                    }
                }
            }
        }
    }

    func preparePhotoSettings(exposureDuration: CMTime, iso: Float) {
        if lockPreparedSettings {
            print("Cannot prepare settings, settings locked")
            return
        }

        if cameraState.isBurstCaptureOn {
            prepareBurstPhotoSettings(exposureCount: 4, exposureDuration: exposureDuration, iso: iso)
        } else {
            prepareBurstPhotoSettings(exposureCount: 1, exposureDuration: exposureDuration, iso: iso)
        }
    }

    func prepareBurstPhotoSettings(exposureCount: Int, exposureDuration: CMTime, iso: Float) {
        self.isCameraPrepared = Task(priority: .high) {
            let videoPreviewLayerOrientation = await AVCaptureVideoOrientation(deviceOrientation: UIDevice.current.orientation)

            if let photoOutputConnection = self.photoOutput.connection(with: .video) {
                if let videoPreviewLayerOrientation = videoPreviewLayerOrientation {
                    photoOutputConnection.videoOrientation = videoPreviewLayerOrientation
                } else {
                    photoOutputConnection.videoOrientation = .portrait
                }
            }

            let query = self.photoOutput.isAppleProRAWEnabled ? { !AVCapturePhotoOutput.isAppleProRAWPixelFormat($0) } : { AVCapturePhotoOutput.isBayerRAWPixelFormat($0) }

            var photoSettings: AVCapturePhotoBracketSettings

            // Retrieve the RAW format, favoring the Apple ProRAW format when it's in an enabled state.
            guard let rawFormat = self.photoOutput.availableRawPhotoPixelFormatTypes.first(where: query) else {
                print("DEVICE DOES NOT SUPPRT RAW CAPTURE!")
                return false
            }

            let bracketSettings = stride(from: 0, to: exposureCount, by: 1).map { _ in AVCaptureManualExposureBracketedStillImageSettings.manualExposureSettings(exposureDuration: exposureDuration, iso: iso) }

            // Capture a RAW format photo, along with a processed format photo.
            let processedFormat = [AVVideoCodecKey: AVVideoCodecType.hevc]
            photoSettings = AVCapturePhotoBracketSettings(rawPixelFormatType: rawFormat, processedFormat: processedFormat, bracketedSettings: bracketSettings)

            if self.photoOutput.isLensStabilizationDuringBracketedCaptureSupported {
                photoSettings.isLensStabilizationEnabled = true
            }

            // Select the first available codec type, which is JPEG.
            guard let thumbnailPhotoCodecType =
                    photoSettings.availableRawEmbeddedThumbnailPhotoCodecTypes.first else {
                // Handle the failure to find an available thumbnail photo codec type.
                fatalError("Failed configuring RAW tumbnail.")
            }

            if self.videoDeviceInput.device.isFlashAvailable {
                photoSettings.flashMode = self.cameraState.isFlashOn ? .on : .off
            }

            // Select the maximum photo dimensions as thumbnail dimensions if a full-size thumbnail is desired.
            // The system clamps these dimensions to the photo dimensions if the capture produces a photo with smaller than maximum dimensions.
            let dimensions = photoSettings.maxPhotoDimensions
            photoSettings.rawEmbeddedThumbnailPhotoFormat = [
                AVVideoCodecKey: thumbnailPhotoCodecType,
                AVVideoWidthKey: dimensions.width,
                AVVideoHeightKey: dimensions.height
            ]


            do {
                try await self.photoOutput.setPreparedPhotoSettingsArray([photoSettings])
            } catch {
                // TODO: Handle this better?
                NSLog("DONE FAILED Preparing Photo Settings!")
                return false
            }

            return true
        }
    }

    // MARK: Capture Photo
    func capturePhoto(saveCollection: PhotoCollection) {
        /*
         Retrieve the video preview layer's video orientation on the main queue before
         entering the session queue. This to ensures that UI elements are accessed on
         the main thread and session configuration is done on the session queue.
         */

        if self.setupResult != .configurationFailed {
            self.isCameraButtonDisabled = true

            self.preparePhotoSettings(exposureDuration: self.cameraState.finalExposureDuration, iso: self.cameraState.finalISO)

            let photoCaptureProcessor = PhotoCaptureProcessor(saveCollection: saveCollection,
                                                              id: ++self.processingID,
                                                              isNNProcessingOn: self.cameraState.isNNProcessingOn,
                                                              completionHandler: { (photoCaptureProcessor) in

                // When the capture is complete, remove a reference to the photo capture delegate so it can be deallocated.
                if let data = photoCaptureProcessor.capturedImage {
                    UIImage(data: data)!.prepareThumbnail(of: CGSize(width: 100, height: 100)) { thumbnail in
                        DispatchQueue.main.async {
                            self.thumbnail = thumbnail
                        }
                    }
                } else {
                    print("No photo data")
                }

                self.isCameraButtonDisabled = false
                self.lockPreparedSettings = false
                self.inProgressPhotoCaptureDelegates[photoCaptureProcessor.id] = nil
                // Create new photo settings!
                // self.preparePhotoSettings(exposureDuration: self.cameraState.finalExposureDuration, iso: self.cameraState.finalISO)
            }, photoCapturingHandler: { isCapturing in self.isPhotoCapturing = isCapturing
            }, photoProcessingHandler: { isProcessing in self.shouldShowSpinner = isProcessing })

            // Specify the location the photo was taken
            photoCaptureProcessor.location = self.locationManager.location

            // The photo output holds a weak reference to the photo capture delegate and stores it in an array to maintain a strong reference.
            self.inProgressPhotoCaptureDelegates[photoCaptureProcessor.id] = photoCaptureProcessor


            Task(priority: .userInitiated) {
                // TODO: Handle locked settings better. Might be nice to allow capture to wait for previous one to be done
                if !self.lockPreparedSettings {
                    if let isCameraPrepared = self.isCameraPrepared {
                        self.lockPreparedSettings = true
                        let _ = await isCameraPrepared.value
                        self.photoOutput.capturePhoto(with: self.photoOutput.preparedPhotoSettingsArray.first!, delegate: photoCaptureProcessor)
                    } else {
                        print("Camera not Prepared! Skipping")
                    }
                } else {
                    print("Camera Prep is locked! Skipping")
                }
            }
        }
    }


    //  MARK: KVO & Observers

    private func addObservers() {
        let systemPressureStateObservation = observe(\.videoDeviceInput.device.systemPressureState, options: .new) { _, change in
            guard let systemPressureState = change.newValue else { return }
            self.setRecommendedFrameRateRangeForPressureState(systemPressureState: systemPressureState)
        }
        keyValueObservations.append(systemPressureStateObservation)

        // NotificationCenter.default.addObserver(self, selector: #selector(self.onOrientationChange), name: UIDevice.orientationDidChangeNotification, object: nil)

        NotificationCenter.default.addObserver(self,
                                               selector: #selector(subjectAreaDidChange),
                                               name: .AVCaptureDeviceSubjectAreaDidChange,
                                               object: videoDeviceInput.device)

        NotificationCenter.default.addObserver(self, selector: #selector(uiRequestedNewFocusArea), name: .init(rawValue: "UserDidRequestNewFocusPoint"), object: nil)

        NotificationCenter.default.addObserver(self,
                                               selector: #selector(sessionRuntimeError),
                                               name: .AVCaptureSessionRuntimeError,
                                               object: session)

        /*
         A session can only run when the app is full screen. It will be interrupted
         in a multi-app layout, introduced in iOS 9, see also the documentation of
         AVCaptureSessionInterruptionReason. Add observers to handle these session
         interruptions and show a preview is paused message. See the documentation
         of AVCaptureSessionWasInterruptedNotification for other interruption reasons.
         */
        NotificationCenter.default.addObserver(self,
                                               selector: #selector(sessionWasInterrupted),
                                               name: .AVCaptureSessionWasInterrupted,
                                               object: session)
        NotificationCenter.default.addObserver(self,
                                               selector: #selector(sessionInterruptionEnded),
                                               name: .AVCaptureSessionInterruptionEnded,
                                               object: session)
    }

    private func removeObservers() {
        NotificationCenter.default.removeObserver(self)

        for keyValueObservation in keyValueObservations {
            keyValueObservation.invalidate()
        }
        keyValueObservations.removeAll()
    }

    @objc private func uiRequestedNewFocusArea(notification: NSNotification) {
        guard let userInfo = notification.userInfo as? [String: Any], let devicePoint = userInfo["devicePoint"] as? CGPoint else { return }
        self.focus(at: devicePoint)
    }

    @objc
    private func subjectAreaDidChange(notification: NSNotification) {
        let devicePoint = CGPoint(x: 0.5, y: 0.5)
        focus(with: .continuousAutoFocus, exposureMode: .continuousAutoExposure, at: devicePoint, monitorSubjectAreaChange: false)
    }

    @objc
    private func sessionRuntimeError(notification: NSNotification) {
        guard let error = notification.userInfo?[AVCaptureSessionErrorKey] as? AVError else { return }

        print("Capture session runtime error: \(error)")
        // If media services were reset, and the last start succeeded, restart the session.
        if error.code == .mediaServicesWereReset {
            sessionQueue.async {
                if self.isSessionRunning {
                    self.session.startRunning()
                    self.isSessionRunning = self.session.isRunning
                }
            }
        }
    }

    private func setRecommendedFrameRateRangeForPressureState(systemPressureState: AVCaptureDevice.SystemPressureState) {
        /*
         The frame rates used here are only for demonstration purposes.
         Your frame rate throttling may be different depending on your app's camera configuration.
         */
        let pressureLevel = systemPressureState.level
        if pressureLevel == .serious || pressureLevel == .critical {
            do {
                try self.videoDeviceInput.device.lockForConfiguration()
                print("WARNING: Reached elevated system pressure level: \(pressureLevel). Throttling frame rate.")
                self.videoDeviceInput.device.activeVideoMinFrameDuration = CMTime(value: 1, timescale: 20)
                self.videoDeviceInput.device.activeVideoMaxFrameDuration = CMTime(value: 1, timescale: 15)
                self.videoDeviceInput.device.unlockForConfiguration()
            } catch {
                print("Could not lock device for configuration: \(error)")
            }
        } else if pressureLevel == .shutdown {
            print("Session stopped running due to shutdown system pressure level.")
        }
    }

    @objc
    private func sessionWasInterrupted(notification: NSNotification) {
        /*
         In some scenarios you want to enable the user to resume the session.
         For example, if music playback is initiated from Control Center while
         using Campus, then the user can let Campus resume
         the session running, which will stop music playback. Note that stopping
         music playback in Control Center will not automatically resume the session.
         Also note that it's not always possible to resume, see `resumeInterruptedSession(_:)`.
         */
        DispatchQueue.main.async {
            self.isCameraUnavailable = true
        }

        // TODO: Fixme
        if let userInfoValue = notification.userInfo?[AVCaptureSessionInterruptionReasonKey] as AnyObject?,
           let reasonIntegerValue = userInfoValue.integerValue,
           let reason = AVCaptureSession.InterruptionReason(rawValue: reasonIntegerValue) {
            print("Capture session was interrupted with reason \(reason)")

            if reason == .audioDeviceInUseByAnotherClient || reason == .videoDeviceInUseByAnotherClient {
                print("Session stopped running due to video devies in use by another client.")
            } else if reason == .videoDeviceNotAvailableWithMultipleForegroundApps {
                // Fade-in a label to inform the user that the camera is unavailable.
                print("Session stopped running due to video devies is not available with multiple foreground apps.")
            } else if reason == .videoDeviceNotAvailableDueToSystemPressure {
                print("Session stopped running due to shutdown system pressure level.")
            }
        }
    }

    @objc
    private func sessionInterruptionEnded(notification: NSNotification) {
        print("Capture session interruption ended")
        DispatchQueue.main.async {
            self.isCameraUnavailable = false
        }
    }
}
