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
    // Unchanging per camera
    @Published var deviceAperture: Float = 0
    @Published var deviceMaxExposureDuration: CMTime = CMTime(seconds: 0, preferredTimescale: 1)
    @Published var deviceMinExposureDuration: CMTime = CMTime(seconds: 0, preferredTimescale: 1)
    @Published var deviceMaxISO: Float = 0
    @Published var deviceMinISO: Float = 0
    @Published var deviceMinExposureBias: Float = 0
    @Published var deviceMaxExposureBias: Float = 0

    // Exposure params as metered by default
    @Published var meteredExposureDuration: CMTime = CMTime(seconds: 0, preferredTimescale: 1)
    @Published var meteredISO: Float = 0
    @Published var meteredExposureBias: Float = 0
    @Published var meteredExposureOffset: Float = 0

    // Exposure params as modified according to user settings
    @Published var calculatedExposureDuration: CMTime = CMTime(seconds: 1/100, preferredTimescale: 1_000_000_000)
    @Published var calculatedISO: Float = 100
    @Published var calculatedExposureBias: Float = 0
    @Published var calculatedExposureOffset: Float = 0

    // User provided exposure params
    @Published var userExposureDuration: CMTime = CMTime(seconds: 1/100, preferredTimescale: 1_000_000_000)
    @Published var userISO: Float = 100
    @Published var userExposureBias: Float = 0

    // Exposure param limits as specified by user settings
    @Published var targetMaxExposureDuration: CMTime = CMTime(seconds: 1.0/60, preferredTimescale: 1_000_000_000)
    @Published var targetMinExposureDuration: CMTime = CMTime(seconds: 0, preferredTimescale: 1)
    @Published var targetMaxISO: Float = Float.infinity
    @Published var targetMinISO: Float = 0

    // User Options
    @Published public var isFlashOn  = false
    @Published public var isNNProcessingOn = true
    @Published public var isShutterDelayOn = false

    @Published public var isManualExposureDuration = false
    @Published public var isManualISO = false
    @Published public var isManualEVBias = false

    // Observers to monitor device state
    private var exposureDurationObserver: NSKeyValueObservation? = nil
    private var isoObserver: NSKeyValueObservation? = nil
    private var exposureBiasObserver: NSKeyValueObservation? = nil
    private var exposureOffsetObserver: NSKeyValueObservation? = nil
    private var apertureObserver: NSKeyValueObservation? = nil

    private var subscriptions = Set<AnyCancellable>()

    public init() {
        $meteredExposureDuration.sink { metered in
            self.calculateExposureParams()
        }.store(in: &self.subscriptions)

        $meteredISO.sink { metered in
            self.calculateExposureParams()
        }.store(in: &self.subscriptions)

        $meteredExposureBias.sink { metered in
            self.calculateExposureParams()
        }.store(in: &self.subscriptions)

        $userExposureDuration.sink { user in
            print("New User Provided Exposure Duration :: \(user)")
            self.calculateExposureParams()
        }.store(in: &self.subscriptions)

        $userISO.sink { user in
            print("New User Provided ISO :: \(user)")
            self.calculateExposureParams()
        }.store(in: &self.subscriptions)

        $userExposureBias.sink { user in
            print("New User Provided Exposure Bias :: \(user)")
            self.calculatedExposureBias = self.isManualEVBias ? user : 0
        }.store(in: &self.subscriptions)

        $isManualExposureDuration.sink { isManual in
            self.calculateExposureParams()
        }.store(in: &self.subscriptions)

        $isManualISO.sink { isManual in
            self.calculateExposureParams()
        }.store(in: &self.subscriptions)

        $isManualEVBias.sink { isManual in
            // TODO: I dont think this field should show calculated exposure offset forever. Make ExposureOffset its own indicator?
            self.calculatedExposureBias = isManual ? self.userExposureBias : 0 //self.calculatedExposureOffset
        }.store(in: &self.subscriptions)
    }

    private func calculateExposureParams() {
        if(isManualExposureDuration && isManualISO) {
            calculateExposureParamsManual()
        } else if (isManualExposureDuration) {
            calculateExposureParamsManualExposureDuration()
        } else if (isManualISO) {
            calculateExposureParamsManualISO()
        } else {
            calculateExposureParamsAuto()
        }
    }

    private func calculateExposureParamsAuto() {
        let exposureCompensationMultiplier = pow(2, meteredExposureOffset)

        var targetExposureDuration = meteredExposureDuration.seconds * Double(exposureCompensationMultiplier)
        var targetISO = meteredISO

        if(targetExposureDuration > targetMaxExposureDuration.seconds) {
            targetISO *= Float(targetExposureDuration / targetMaxExposureDuration.seconds)
            targetExposureDuration = targetMaxExposureDuration.seconds
        }

        DispatchQueue.main.async {
            self.calculatedExposureDuration = CMTime(seconds: targetExposureDuration, preferredTimescale: self.targetMaxExposureDuration.timescale)
            self.calculatedISO = min(max(targetISO, self.targetMinISO), self.targetMaxISO)
        }


        // TODO: FEED CALCULATED EXPOSURE OFFSET BACK TO CAMERA SO PREVIEW ACCURATELY SHOWS SCENE?
    }

    private func calculateExposureParamsManualISO() {
        // This should represent a neutral exposure according to the phone
        //   Negative bias because we want to counteract it to find the neutrally exposed EV
        let meteredEVZero = meteredISO * Float(meteredExposureDuration.seconds) * pow(2, -1 * meteredExposureBias) * pow(2, meteredExposureOffset)

        let bias = isManualEVBias ? userExposureBias : 0
        // Should be able calculate required exposure duration. Any remaining EV will be set to calculated exposure bias
        var targetExposureDuration = meteredEVZero / (userISO * pow(2, -1 * bias))
        targetExposureDuration = min(max(targetExposureDuration, Float(targetMinExposureDuration.seconds)), Float(targetMaxExposureDuration.seconds))

        let evOffset = meteredEVZero / (userISO * targetExposureDuration * pow(2, -1 * bias))

        DispatchQueue.main.async {
            print("MANUAL ISO - SETTING EXPOSURE DURATION TO \(targetExposureDuration)s  | EV OFFSET \(evOffset)")
            self.calculatedExposureDuration = CMTime(seconds: Double(targetExposureDuration), preferredTimescale: self.targetMaxExposureDuration.timescale)
            self.calculatedISO = self.userISO
            self.calculatedExposureOffset = evOffset
        }
    }

    private func calculateExposureParamsManualExposureDuration() {
        // This should represent a neutral exposure according to the phone
        //   Negative bias because we want to counteract it to find the neutrally exposed EV
        let meteredEVZero = meteredISO * Float(meteredExposureDuration.seconds) * pow(2, -1 * meteredExposureBias) * pow(2, meteredExposureOffset)

        let bias = isManualEVBias ? userExposureBias : 0
        // Should be able calculate required exposure duration. Any remaining EV will be set to calculated exposure bias
        var targetISO = meteredEVZero / (Float(userExposureDuration.seconds) * pow(2, -1 * bias))
        targetISO = min(max(targetISO, targetMinISO), targetMaxISO)

        let evOffset = meteredEVZero / (targetISO * Float(userExposureDuration.seconds) * pow(2, -1 * bias))

        DispatchQueue.main.async {
            print("MANUAL Exposure Duration - SETTING ISO TO \(targetISO)s | EV OFFSET \(evOffset)")
            self.calculatedExposureDuration = self.userExposureDuration
            self.calculatedISO = targetISO
            self.calculatedExposureOffset = evOffset
        }
    }

    private func calculateExposureParamsManual() {
        // This should represent a neutral exposure according to the phone
        //   Negative bias because we want to counteract it to find the neutrally exposed EV
        let meteredEVZero = meteredISO * Float(meteredExposureDuration.seconds) * pow(2, -1 * meteredExposureBias) * pow(2, meteredExposureOffset)

        let bias = isManualEVBias ? userExposureBias : 0

        let evOffset = meteredEVZero / (userISO * Float(userExposureDuration.seconds) * pow(2, -1 * bias))

        DispatchQueue.main.async {
            print("FULL MANUAL | EV OFFSET \(evOffset)")
            self.calculatedExposureDuration = self.userExposureDuration
            self.calculatedISO = self.userISO
            self.calculatedExposureOffset = evOffset
        }
    }

    func updateCameraDevice(device: AVCaptureDevice) {
        NSLog("CAMERA STATE - UPDATING CAMERA DEVICE")

        DispatchQueue.main.async {
            self.deviceMinISO = device.activeFormat.minISO
            self.deviceMaxISO = device.activeFormat.maxISO

            self.targetMinISO = device.activeFormat.minISO
            self.targetMaxISO = device.activeFormat.maxISO

            self.deviceMinExposureDuration = device.activeFormat.minExposureDuration
            self.deviceMaxExposureDuration = device.activeFormat.maxExposureDuration

            self.deviceMinExposureBias = device.minExposureTargetBias
            self.deviceMaxExposureBias = device.maxExposureTargetBias

            self.meteredExposureDuration = device.exposureDuration
            self.meteredExposureBias = device.exposureTargetBias
            self.meteredExposureOffset = device.exposureTargetOffset
            self.meteredISO = device.iso
            self.deviceAperture = device.lensAperture
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

