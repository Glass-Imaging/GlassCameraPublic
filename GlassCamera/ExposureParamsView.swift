//
//  ExposureParamsView.swift
//  GlassCamera
//
//  Created by Doug MacEwen on 5/15/23.
//

import Foundation
import SwiftUI
import Combine
import AVFoundation

struct ExposureParams: View {
    @EnvironmentObject var cameraState: CameraState

    @GestureState private var shutterSpeedGestureOffset: Float = .zero
    @GestureState private var isoGestureOffset: Float = .zero
    @GestureState private var exposureBiasGestureOffset: Float = .zero

    var FStopField : some View {
        VStack {
            Text("f/\(String(cameraState.deviceAperture))")
                .font(.system(size:16, weight: .heavy, design: .monospaced))
            Text("F STOP")
                .font(.system(size:12, weight: .light, design: .rounded))
            Text(verbatim: " ")
                .font(.system(size: 8, weight: .ultraLight, design: .monospaced))
        }
        .padding(5)
        .overlay(RoundedRectangle(cornerRadius: 5).stroke(Color.clear, lineWidth: 2))
    }

    var ExposureDurationField : some View {
        let dragGesture = DragGesture()
            .updating($shutterSpeedGestureOffset) { (value, state, _) in
                let new_state = Float(value.translation.height)
                let diff  = (state - new_state)
                state = new_state

                DispatchQueue.main.async {
                    if(!cameraState.isManualExposureDuration) { return }
                    let targetExposureDuration = Float(cameraState.userExposureDuration.seconds)
                                                    + Float(cameraState.userExposureDuration.seconds) * (1/50) * diff

                    let newExposureDuration = min(
                                                max(targetExposureDuration, Float(cameraState.deviceMinExposureDuration.seconds)),
                                                Float(cameraState.deviceMaxExposureDuration.seconds))

                    cameraState.userExposureDuration = CMTime(seconds: Double(newExposureDuration), preferredTimescale: 1_000_000_000)
                }
            }

        return VStack {
            Text(cameraState.calculatedExposureDuration.seconds == Double(0) ? "0" : "1/\(Int((1 / cameraState.calculatedExposureDuration.seconds).rounded()))")
                .font(.system(size:16, weight: .heavy, design: .monospaced))
            Text("SS")
                .font(.system(size:12, weight: .light, design: .rounded))
            Text(verbatim: cameraState.isAtUpperTargetExposureDurationLimit ? "MAX" : cameraState.isAtLowerDeviceExposureDurationLimit ? "MIN" : cameraState.isManualExposureDuration ? "MANUAL":  " ")
                .font(.system(size: 8, weight: .ultraLight, design: .monospaced))
        }
        .gesture(dragGesture)
        .padding(5)
        .overlay(RoundedRectangle(cornerRadius: 5).stroke(cameraState.isManualExposureDuration ? Color.yellow : Color.clear, lineWidth: 2))
        .onTapGesture {
            cameraState.isManualExposureDuration.toggle()
            cameraState.userExposureDuration = cameraState.calculatedExposureDuration
        }
    }

    var ISOField : some View {
        let dragGesture = DragGesture()
            .updating($isoGestureOffset) { (value, state, _) in
                let new_state = Float(value.translation.height)
                let diff  = (state - new_state)
                state = new_state

                DispatchQueue.main.async {
                    if(!cameraState.isManualISO) { return }
                    cameraState.userISO = min(max(cameraState.userISO + cameraState.userISO * (1/125) * diff, cameraState.deviceMinISO), cameraState.deviceMaxISO)
                }
            }

        return VStack {
            Text(String(Int(cameraState.calculatedISO.rounded())))
                .font(.system(size:16, weight: .heavy, design: .monospaced))
            Text("ISO")
                .font(.system(size:12, weight: .light, design: .rounded))
            Text(verbatim: cameraState.isAtUpperTargetISOLimit ? "MAX" : cameraState.isAtLowerDeviceISOLimit ? "MIN" : cameraState.isManualISO ? "MANUAL":  " ")
                .font(.system(size: 8, weight: .ultraLight, design: .monospaced))
        }
        .gesture(dragGesture)
        .padding(5)
        .overlay(RoundedRectangle(cornerRadius: 5).stroke(cameraState.isManualISO ? Color.yellow : Color.clear, lineWidth: 2))
        .onTapGesture {
            cameraState.isManualISO.toggle()
            cameraState.userISO = cameraState.calculatedISO
        }
    }

    var ExposureBiasField : some View {
        let dragGesture = DragGesture()
            .updating($exposureBiasGestureOffset) { (value, state, _) in
                let new_state = Float(value.translation.height)
                let diff  = (state - new_state)
                state = new_state

                DispatchQueue.main.async {
                    if(!cameraState.isManualEVBias) { return }
                    // if((diff < 0) && cameraState.isAtLowerTargetExposureDurationLimit && cameraState.isAtLowerTargetISOLimit) { return }
                    // if((diff > 0) && cameraState.isAtUpperTargetExposureDurationLimit && cameraState.isAtUpperTargetISOLimit) { return }
                    cameraState.userExposureBias = min(max(cameraState.userExposureBias + (diff / 100), cameraState.deviceMinExposureBias), cameraState.deviceMaxExposureBias)
                }
            }

        return VStack {
            Text(String(cameraState.isManualEVBias ? String(format: "%.1f", cameraState.userExposureBias) : String(format: "%.1f", cameraState.calculatedExposureBias)))
                .font(.system(size:16, weight: .heavy, design: .monospaced))
            Text("EV")
                .font(.system(size:12, weight: .light, design: .rounded))
            Text(verbatim: cameraState.isManualEVBias ? "MANUAL":  " ")
                .font(.system(size: 8, weight: .ultraLight, design: .monospaced))
        }
        .gesture(dragGesture)
        .padding(5)
        .overlay(RoundedRectangle(cornerRadius: 5).stroke(cameraState.isManualEVBias ? Color.yellow : Color.clear, lineWidth: 2))
        .onTapGesture { cameraState.isManualEVBias.toggle() }
    }

    var body: some View {
        HStack {
            Spacer()

            FStopField

            Spacer()

            ExposureDurationField

            Spacer()

            ISOField

            Spacer()

            ExposureBiasField

            Spacer()
        }

    }
}
