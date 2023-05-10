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

    // Exposure params as metered by default
    @Published var meteredExposureDuration: CMTime = CMTime(seconds: 0, preferredTimescale: 1)
    @Published var meteredISO: Float = 0
    @Published var meteredExposureBias: Float = 0
    @Published var meteredExposureOffset: Float = 0

    // Exposure params as modified according to user settings
    @Published var calculatedExposureDuration: CMTime = CMTime(seconds: 0, preferredTimescale: 1)
    @Published var calculatedISO: Float = 0
    @Published var calculatedExposureBias: Float = 0

    // Exposure param limits as specified by user settings
    @Published var targetMaxExposureDuration: CMTime = CMTime(seconds: 1.0/100, preferredTimescale: 1_000_000_000)
    @Published var targetMinExposureDuration: CMTime = CMTime(seconds: 0, preferredTimescale: 1)
    @Published var targetMaxISO: Float = Float.infinity
    @Published var targetMinISO: Float = 0

    // User Options
    // ADD BUTTON STATE HERE?

    // Observers to monitor device state
    private var exposureDurationObserver: NSKeyValueObservation? = nil
    private var isoObserver: NSKeyValueObservation? = nil
    private var exposureBiasObserver: NSKeyValueObservation? = nil
    private var exposureOffsetObserver: NSKeyValueObservation? = nil
    private var apertureObserver: NSKeyValueObservation? = nil

    private var subscriptions = Set<AnyCancellable>()

    public init() {
        $meteredExposureDuration.sink { metered in
            self.calcualteExposureParams()
        }.store(in: &self.subscriptions)

        $meteredISO.sink { metered in
            self.calcualteExposureParams()
        }.store(in: &self.subscriptions)

        $meteredExposureBias.sink { metered in
            self.calcualteExposureParams()
        }.store(in: &self.subscriptions)
    }

    private func calcualteExposureParams() {
        let exposureCompensationMultiplier = 1 // currentExposureOffset > 0 ? 1 / (1 + currentExposureOffset) : -1 * currentExposureOffset

        var targetExposureDuration = meteredExposureDuration.seconds * Double(exposureCompensationMultiplier)
        var targetISO = meteredISO

        if(targetExposureDuration > targetMaxExposureDuration.seconds) {
            NSLog("Max duration exceeded! Manually correcting")
            targetISO *= Float(targetExposureDuration / targetMaxExposureDuration.seconds)
            targetExposureDuration = targetMaxExposureDuration.seconds
        }

        DispatchQueue.main.async {
            self.calculatedExposureDuration = CMTime(seconds: targetExposureDuration, preferredTimescale: self.targetMaxExposureDuration.timescale)
            self.calculatedISO = min(max(targetISO, self.targetMinISO), self.targetMaxISO)
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

