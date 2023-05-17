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
        .padding([.leading, .trailing], 15)
        .padding([.top, .bottom], 2)
        .contentShape(Rectangle())
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

        func getHint() -> String {
            return cameraState.isAtUpperDeviceExposureDurationLimit ? "MAX"
            : cameraState.isAtLowerDeviceExposureDurationLimit ? "MIN"
            : cameraState.isManualExposureDuration ? "MANUAL"
            : cameraState.isAtUpperTargetExposureDurationLimit ? "MAX AUTO"
            : cameraState.isAtLowerTargetExposureDurationLimit ? "MIN AUTO"
            : "AUTO"
        }

        return VStack {
            Text(cameraState.calculatedExposureDuration.seconds == Double(0) ? "0" : "1/\(Int((1 / cameraState.calculatedExposureDuration.seconds).rounded()))")
                .font(.system(size:16, weight: .heavy, design: .monospaced))
            Text("SS")
                .font(.system(size:12, weight: .light, design: .rounded))
            Text(getHint())
                .font(.system(size: 8, weight: .ultraLight, design: .monospaced))
        }
        .gesture(dragGesture)
        .padding(5)
        .overlay(RoundedRectangle(cornerRadius: 5).stroke(cameraState.isManualExposureDuration ? Color.yellow : Color.clear, lineWidth: 2))
        .onTapGesture {
            cameraState.isManualExposureDuration.toggle()
            cameraState.userExposureDuration = cameraState.calculatedExposureDuration
        }
        .padding([.leading, .trailing], 15)
        .padding([.top, .bottom], 2)
        .contentShape(Rectangle())
        .contextMenu {
            if cameraState.isManualExposureDuration {
                Text("Manual Shutter Speed").font(.system(.title))
                ForEach([2, 1, -1, -2], id: \.self) { evBias in
                    let exposureDuration = cameraState.calculatedExposureDuration.seconds * pow(2, evBias)
                    let biasText = evBias < 0 ? "\(Int(evBias))" : "+\(Int(evBias))"
                    Button("\(biasText) EV  1/\(Int((1/exposureDuration).rounded()))") {
                        cameraState.userExposureDuration = CMTime(seconds: exposureDuration, preferredTimescale: 1_000_000_000)
                    }
                }
            } else {
                Text("Auto Exposure Max").font(.system(.title))
                Button("Unlimited - No Max") {
                    cameraState.targetMaxExposureDuration = CMTime(seconds: cameraState.deviceMaxExposureDuration.seconds, preferredTimescale: 1_000_000_000)
                    cameraState.calculateExposureParams()
                }
                Button("Still - Max 1/40s") {
                    cameraState.targetMaxExposureDuration = CMTime(seconds: 1/40, preferredTimescale: 1_000_000_000)
                    cameraState.calculateExposureParams()
                }
                Button("Default - Max 1/80s") {
                    cameraState.targetMaxExposureDuration = CMTime(seconds: 1/80, preferredTimescale: 1_000_000_000)
                    cameraState.calculateExposureParams()
                }
                Button("Motion - Max 1/160s") {
                    cameraState.targetMaxExposureDuration = CMTime(seconds: 1/160, preferredTimescale: 1_000_000_000)
                    cameraState.calculateExposureParams()
                }
            }
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

        func getHint() -> String {
            return cameraState.isAtUpperDeviceISOLimit ? "MAX"
            : cameraState.isAtLowerDeviceISOLimit ? "MIN"
            : cameraState.isManualISO ? "MANUAL"
            : cameraState.isAtLowerTargetISOLimit ? "MIN AUTO"
            : cameraState.isAtUpperTargetISOLimit ? "MAX AUTO"
            : "AUTO"
        }


        var isoPresets: Array<Int> = [100]
        if(cameraState.deviceMaxISO != cameraState.deviceMinISO) {
            let scale = Int(log2((cameraState.deviceMaxISO / cameraState.deviceMinISO)).rounded(.down))
            isoPresets = stride(from: scale,  through: 0, by: -1).map { Int(cameraState.deviceMinISO * pow(2, Float($0))) }
        }

        return VStack {
            Text(String(Int(cameraState.calculatedISO.rounded())))
                .font(.system(size:16, weight: .heavy, design: .monospaced))
            Text("ISO")
                .font(.system(size:12, weight: .light, design: .rounded))
            Text(getHint())
                .font(.system(size: 8, weight: .ultraLight, design: .monospaced))
        }
        .gesture(dragGesture)
        .padding(5)
        .overlay(RoundedRectangle(cornerRadius: 5).stroke(cameraState.isManualISO ? Color.yellow : Color.clear, lineWidth: 2))
        .onTapGesture {
            cameraState.isManualISO.toggle()
            cameraState.userISO = cameraState.calculatedISO
        }
        .padding([.leading, .trailing], 15)
        .padding([.top, .bottom], 2)
        .contentShape(Rectangle())
        .contextMenu {
            if cameraState.isManualISO {
                Text("Manual ISO").font(.system(.title))
                ForEach(isoPresets, id: \.self) { iso in
                    Button(String(iso)) { cameraState.userISO = Float(iso) }
                }
            }
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
        .padding([.leading, .trailing], 15)
        .padding([.top, .bottom], 2)
        .contentShape(Rectangle())
    }

    var body: some View {
        VStack {

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
}
