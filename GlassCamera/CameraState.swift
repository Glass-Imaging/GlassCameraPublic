//
//  CameraState.swift
//  GlassCamera
//
//  Created by Doug MacEwen on 5/9/23.
//

import Foundation
import Combine
import AVFoundation

final class CameraState: ObservableObject {
    @Published var debugOverlay = false

    // Indicates whether we are using device AE params to calculate custom AE params
    @Published var isCustomExposure: Bool = false

    // Unchanging per camera
    @Published var deviceAperture: Float = 0
    @Published var deviceMaxExposureDuration: CMTime = CMTime(seconds: 1/100, preferredTimescale: 1_000_000)
    @Published var deviceMinExposureDuration: CMTime = CMTime(seconds: 1/100, preferredTimescale: 1_000_000)
    @Published var deviceMaxISO: Float = 100
    @Published var deviceMinISO: Float = 100
    @Published var deviceMinExposureBias: Float = 0
    @Published var deviceMaxExposureBias: Float = 0

    // Exposure params as metered by default
    @Published var meteredExposureDuration: CMTime = CMTime(seconds: 1/100, preferredTimescale: 1_000_000_000)
    @Published var meteredISO: Float = 100
    @Published var meteredExposureBias: Float = 0
    @Published var meteredExposureOffset: Float = 0

    // Exposure params as modified according to user settings
    @Published var customExposureDuration: CMTime = CMTime(seconds: 1/100, preferredTimescale: 1_000_000_000)
    @Published var customISO: Float = 100
    @Published var customExposureBias: Float = 0

    // Exposure params published to display and to be used in some custom exposure requests
    @Published var finalExposureDuration: CMTime = CMTime(seconds: 1/100, preferredTimescale: 1_000_000_000)
    @Published var finalISO: Float = 100
    @Published var finalExposureBias: Float = 0

    // User provided exposure params
    @Published var userExposureDuration: CMTime = CMTime(seconds: 1/100, preferredTimescale: 1_000_000_000)
    @Published var userISO: Float = 100
    @Published var userExposureBias: Float = 0

    // Exposure param limits as specified by user settings
    @Published var targetMaxExposureDuration: CMTime = CMTime(seconds: 1.0/80, preferredTimescale: 1_000_000_000)

    // User Options
    @Published public var isFlashOn  = false
    @Published public var isNNProcessingOn = true
    @Published public var isShutterDelayOn = false

    @Published public var isManualExposureDuration = false
    @Published public var isManualISO = false
    @Published public var isManualEVBias = false

    @Published public var isAtUpperTargetExposureDurationLimit = false

    @Published public var isAtLowerDeviceExposureDurationLimit = false
    @Published public var isAtUpperDeviceExposureDurationLimit = false
    @Published public var isAtLowerDeviceISOLimit = false
    @Published public var isAtUpperDeviceISOLimit = false

    // Observers to monitor device state
    private var exposureDurationObserver: NSKeyValueObservation? = nil
    private var isoObserver: NSKeyValueObservation? = nil
    private var exposureBiasObserver: NSKeyValueObservation? = nil
    private var exposureOffsetObserver: NSKeyValueObservation? = nil
    private var apertureObserver: NSKeyValueObservation? = nil

    private var currentMeteredExposureOffset: Float = 0
    private var subscriptions = Set<AnyCancellable>()

    public init() {
        $meteredExposureDuration.sink { metered in
            self.calculateExposureParams()
        }.store(in: &self.subscriptions)

        $meteredISO.sink { metered in
            self.calculateExposureParams()
        }.store(in: &self.subscriptions)

        $meteredExposureOffset
            .filter { newOffset in abs(newOffset - self.currentMeteredExposureOffset) > 0.1 }
            .sink { metered in
                self.currentMeteredExposureOffset = metered
                self.calculateExposureParams()
        }.store(in: &self.subscriptions)

        $userExposureDuration.sink { user in
            self.calculateExposureParams()
        }.store(in: &self.subscriptions)

        $userISO.sink { user in
            self.calculateExposureParams()
        }.store(in: &self.subscriptions)

        $userExposureBias.sink { user in
            self.customExposureBias = self.isManualEVBias ? user : 0
            self.finalExposureBias = self.customExposureBias
        }.store(in: &self.subscriptions)

        $isManualExposureDuration.sink { isManual in
            self.calculateExposureParams()
        }.store(in: &self.subscriptions)

        $isManualISO.sink { isManual in
            self.calculateExposureParams()
        }.store(in: &self.subscriptions)

        $isManualEVBias.sink { isManual in
            self.customExposureBias = isManual ? self.userExposureBias : 0
            self.finalExposureBias = self.customExposureBias
            self.calculateExposureParams()
        }.store(in: &self.subscriptions)
    }

    func calculateExposureParams() {
        DispatchQueue.main.async {
            let exposureDuration = self.meteredExposureDuration
            let iso = self.meteredISO
            let exposureBias = self.meteredExposureBias
            let offset = self.meteredExposureOffset

            if(self.isManualExposureDuration && self.isManualISO) {
                self.calculateExposureParamsManual(exposureDuration, iso, exposureBias, offset)
            } else if (self.isManualExposureDuration) {
                self.calculateExposureParamsManualExposureDuration(exposureDuration, iso, exposureBias, offset)
            } else if (self.isManualISO) {
                self.calculateExposureParamsManualISO(exposureDuration, iso, exposureBias, offset)
            } else {
                if self.targetMaxExposureDuration.seconds > 1/16 {
                    // Apple wont ever use an exposure slower than 1/15 so we need to use a custom exposure mode
                    self.calculateExposureParamsMinimizeISO(exposureDuration, iso, exposureBias, offset)
                } else {
                    self.autoExposure(exposureDuration, iso)
                }
            }

            let scale: Double = 1_000_000
            let currentRoundedExposure = (scale*self.finalExposureDuration.seconds).rounded(.down)
            let currentRoundedISO = self.finalISO.rounded(.down)

            self.isAtUpperTargetExposureDurationLimit = currentRoundedExposure == (scale*self.targetMaxExposureDuration.seconds).rounded(.down)

            self.isAtLowerDeviceExposureDurationLimit = currentRoundedExposure == (scale*self.deviceMinExposureDuration.seconds).rounded(.down)
            self.isAtLowerDeviceISOLimit = currentRoundedISO == self.deviceMinISO

            self.isAtUpperDeviceExposureDurationLimit = currentRoundedExposure == (scale*self.deviceMaxExposureDuration.seconds).rounded(.down)
            self.isAtUpperDeviceISOLimit = currentRoundedISO == self.deviceMaxISO
        }
    }
    private func autoExposure(_ duration: CMTime, _ iso: Float) {
        self.isCustomExposure = false
        self.finalExposureDuration = duration
        self.finalISO = iso
    }

    private func calculateExposureParamsMinimizeISO(_ duration: CMTime, _ iso: Float, _ bias: Float, _ offset: Float) {
        self.isCustomExposure = true
        let meteredEVZero = iso * duration.secondsF * pow(2, bias) * pow(2, -1 * offset)

        self.customExposureDuration = CMTime(secondsF: meteredEVZero / (deviceMinISO * pow(2, bias))).clamp(lower: deviceMinExposureDuration, upper: targetMaxExposureDuration)

        let targetISO = meteredEVZero / (self.customExposureDuration.secondsF * pow(2, bias))
        self.customISO = targetISO.clamp(lower: self.deviceMinISO, upper: self.deviceMaxISO).ifNotFinite(replacement: deviceMinISO)

        self.finalExposureDuration = self.customExposureDuration
        self.finalISO = self.customISO
    }

    // TODO: CHANGE TO USE ActiveMaxExposureDuration
    private func calculateExposureParamsAuto(_ duration: CMTime, _ iso: Float, _ bias: Float, _ offset: Float) {
        let meteredEVZero = iso * duration.secondsF * pow(2, bias) * pow(2, -1 * offset)

        self.customExposureDuration = duration.clamp(lower: deviceMinExposureDuration, upper: targetMaxExposureDuration)

        let targetISO = meteredEVZero / (self.customExposureDuration.secondsF * pow(2, bias))
        self.customISO = targetISO.clamp(lower: self.deviceMinISO, upper: self.deviceMaxISO).ifNotFinite(replacement: deviceMinISO)

        self.isCustomExposure = (duration < deviceMinExposureDuration)
                                || (duration > targetMaxExposureDuration)
                                || (targetISO < deviceMinISO)
                                || (targetISO > deviceMaxISO)

        self.finalExposureDuration = self.customExposureDuration
        self.finalISO = self.customISO
    }

    private func calculateExposureParamsManualISO(_ duration: CMTime, _ iso: Float, _ bias: Float, _ offset: Float) {
        self.isCustomExposure = true
        let meteredEVZero = iso * duration.secondsF * pow(2, bias) * pow(2, -1 * offset)

        let bias = isManualEVBias ? userExposureBias : 0
        // Should be able calculate required exposure duration. Any remaining EV will be set to calculated exposure bias
        let targetExposureDuration = meteredEVZero / (userISO * pow(2, bias))

        self.customExposureDuration = CMTime(secondsF: targetExposureDuration).clamp(lower: deviceMinExposureDuration, upper: targetMaxExposureDuration)
        self.customISO = self.userISO.clamp(lower: deviceMinISO, upper: deviceMaxISO)

        self.finalExposureDuration = self.customExposureDuration
        self.finalISO = self.customISO
    }

    private func calculateExposureParamsManualExposureDuration(_ duration: CMTime, _ iso: Float, _ bias: Float, _ offset: Float) {
        self.isCustomExposure = true
        let meteredEVZero = iso * duration.secondsF * pow(2, bias) * pow(2, -1 * offset)

        let bias = isManualEVBias ? userExposureBias : 0
        // Should be able calculate required exposure duration. Any remaining EV will be set to calculated exposure bias
        var targetISO = meteredEVZero / (userExposureDuration.secondsF * pow(2, bias))
        targetISO = targetISO.clamp(lower: deviceMinISO, upper: deviceMaxISO)

        self.customExposureDuration = self.userExposureDuration.clamp(lower: deviceMinExposureDuration, upper: deviceMaxExposureDuration)
        self.customISO = targetISO

        self.finalExposureDuration = self.customExposureDuration
        self.finalISO = self.customISO
    }

    private func calculateExposureParamsManual(_ duration: CMTime, _ iso: Float, _ bias: Float, _ offset: Float) {
        self.isCustomExposure = true
        self.customExposureDuration = self.userExposureDuration
        self.customISO = self.userISO

        self.finalExposureDuration = self.customExposureDuration
        self.finalISO = self.customISO
    }

    func updateCameraDevice(device: AVCaptureDevice) {
        DispatchQueue.main.async {
            self.deviceMinISO = device.activeFormat.minISO
            self.deviceMaxISO = device.activeFormat.maxISO

            // Need to exclude lowest value
            self.deviceMinExposureDuration = CMTime(seconds: 1/((1 / device.activeFormat.minExposureDuration.seconds) - 5), preferredTimescale: 1_000_000_000)
            self.deviceMaxExposureDuration = device.activeFormat.maxExposureDuration

            self.deviceMinExposureBias = device.minExposureTargetBias
            self.deviceMaxExposureBias = device.maxExposureTargetBias

            self.meteredExposureDuration = device.exposureDuration
            self.meteredExposureBias = device.exposureTargetBias
            self.meteredExposureOffset = device.exposureTargetOffset
            self.meteredISO = device.iso
            self.deviceAperture = device.lensAperture

            self.isManualISO = false
            self.isManualExposureDuration = false
        }

        exposureDurationObserver = device.observe(\.exposureDuration) { captureDevice, error in
            DispatchQueue.main.async {
                self.meteredExposureDuration = captureDevice.exposureDuration
            }
        }

        exposureBiasObserver = device.observe(\.exposureTargetBias) { captureDevice, error in
            DispatchQueue.main.async {
                self.meteredExposureBias = captureDevice.exposureTargetBias
            }
        }

        exposureOffsetObserver = device.observe(\.exposureTargetOffset) { captureDevice, error in
            DispatchQueue.main.async {
                self.meteredExposureOffset = captureDevice.exposureTargetOffset
            }
        }

        isoObserver = device.observe(\.iso) { captureDevice, error in
            DispatchQueue.main.async {
                self.meteredISO = captureDevice.iso
            }
        }

        apertureObserver = device.observe(\.lensAperture) { captureDevice, error in
            DispatchQueue.main.async {
                self.deviceAperture = captureDevice.lensAperture
            }
        }
    }
}

extension CMTime {
    var secondsF: Float {
        get {
            return Float(self.seconds)
        }
    }

    init(secondsF: Float) {
        self.init(seconds: Double(secondsF), preferredTimescale: 1_000_000_000)
    }

    func clamp(lower: CMTime, upper: CMTime) -> CMTime {
        return CMTime(secondsF: self.secondsF.clamp(lower: lower.secondsF, upper: upper.secondsF))
    }
}

extension Float {
    func clamp(lower: Float, upper: Float) -> Float {
        return min(max(self, lower), upper)
    }

    func ifNotFinite(replacement: Float) -> Float {
        return self.isFinite ? self : replacement
    }
}
