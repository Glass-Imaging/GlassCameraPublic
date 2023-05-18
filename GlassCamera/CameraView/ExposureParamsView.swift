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


private struct ExposureParamField: View {
    let value: String
    let name: String
    let hint: String
    let isSelected: Bool

    init(value: String, name: String, hint: String, isSelected: Bool) {
        self.value = value
        self.name = name
        self.hint = hint
        self.isSelected = isSelected
    }

    var body: some View {
        VStack {
            Text(value)
                .font(.system(size:16, weight: .heavy, design: .monospaced))
            Text(name)
                .font(.system(size:12, weight: .light, design: .rounded))
            Text(hint)
                .font(.system(size: 8, weight: .ultraLight, design: .monospaced))
        }
        .padding(5) // Add room around text for yellow border
        .overlay(RoundedRectangle(cornerRadius: 5).stroke(isSelected ? Color.yellow : Color.clear, lineWidth: 2))
        .padding([.leading, .trailing], 15) // Increase size of touch area
        .padding([.top, .bottom], 2) // Increase height so context menu doesnt clip yellow border
        .contentShape(Rectangle()) // Allows the padding to be treated as a touchable area
    }
}

private func getDragGesture(_ value: GestureState<Float>, onUpdate: @escaping (Float) -> Void) -> GestureStateGesture<DragGesture, Float> {
        return  DragGesture().updating(value) { (newValue, state, _) in
                let newState = Float(newValue.translation.height)
                let diff  = (state - newState)
                state = newState

                DispatchQueue.main.async {
                    onUpdate(diff)
                }
            }
}


struct ExposureParams: View {
    @EnvironmentObject var cameraState: CameraState

    @GestureState private var shutterSpeedGestureOffset: Float = .zero
    @GestureState private var isoGestureOffset: Float = .zero
    @GestureState private var exposureBiasGestureOffset: Float = .zero

    var FStopField : some View {
        ExposureParamField(value: "f/\(String(cameraState.deviceAperture))",
                           name: "F STOP",
                           hint: " ",
                           isSelected: false)
    }

    var ExposureDurationField : some View {
        let dragGesture = getDragGesture($shutterSpeedGestureOffset) { diff in
            if(!cameraState.isManualExposureDuration) { return }
            let targetExposureDuration = Float(cameraState.userExposureDuration.seconds)
                                            + Float(cameraState.userExposureDuration.seconds) * (1/50) * diff

            let newExposureDuration = min(
                                        max(targetExposureDuration, Float(cameraState.deviceMinExposureDuration.seconds)),
                                        Float(cameraState.deviceMaxExposureDuration.seconds))

            cameraState.userExposureDuration = CMTime(seconds: Double(newExposureDuration), preferredTimescale: 1_000_000_000)
        }

        func getHint() -> String {
            return cameraState.isAtUpperDeviceExposureDurationLimit ? "MAX"
            : cameraState.isAtLowerDeviceExposureDurationLimit ? "MIN"
            : cameraState.isManualExposureDuration ? "MANUAL"
            : cameraState.isAtUpperTargetExposureDurationLimit ? "MAX AUTO"
            : cameraState.isAtLowerTargetExposureDurationLimit ? "MIN AUTO"
            : "AUTO"
        }

        func getValue() -> String {
            return cameraState.calculatedExposureDuration.seconds == Double(0) ? "0"
            : "1/\(Int((1 / cameraState.calculatedExposureDuration.seconds).rounded()))"
        }

        var manualExposureContextMenu : some View {
            Group {
                Text("Manual Shutter Speed").font(.system(.title))
                ForEach([2, 1, -1, -2], id: \.self) { evBias in
                    let exposureDuration = cameraState.calculatedExposureDuration.seconds * pow(2, evBias)
                    let biasText = evBias < 0 ? "\(Int(evBias))" : "+\(Int(evBias))"
                    Button("\(biasText) EV  1/\(Int((1/exposureDuration).rounded()))") {
                        cameraState.userExposureDuration = CMTime(seconds: exposureDuration, preferredTimescale: 1_000_000_000)
                    }
                }
            }
        }

        var autoExposureContextMenu : some View {
            Group {
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

        return ExposureParamField(value: getValue(),
                           name: "SS",
                           hint: getHint(),
                           isSelected: cameraState.isManualExposureDuration)
            .gesture(dragGesture)
            .onTapGesture {
                cameraState.isManualExposureDuration.toggle()
                cameraState.userExposureDuration = cameraState.calculatedExposureDuration
            }
            .contextMenu {
                if cameraState.isManualExposureDuration {
                    manualExposureContextMenu
                } else {
                    autoExposureContextMenu
                }
            }
    }

    var ISOField : some View {
        let dragGesture = getDragGesture($isoGestureOffset) { diff in
            if(!cameraState.isManualISO) { return }
            cameraState.userISO = min(max(cameraState.userISO + cameraState.userISO * (1/125) * diff, cameraState.deviceMinISO), cameraState.deviceMaxISO)
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
            let scale = Int(log2((cameraState.deviceMaxISO / cameraState.deviceMinISO)).rounded(.up))
            isoPresets = stride(from: scale,  through: 0, by: -1).map {
                Int(min(max(cameraState.deviceMinISO * pow(2, Float($0)), cameraState.deviceMinISO), cameraState.deviceMaxISO))
            }
        }

        return ExposureParamField(value: String(Int(cameraState.calculatedISO.rounded())),
                           name: "ISO",
                           hint: getHint(),
                           isSelected: cameraState.isManualISO)
            .gesture(dragGesture)
            .onTapGesture {
                cameraState.isManualISO.toggle()
                cameraState.userISO = cameraState.calculatedISO
            }
            .contextMenu {
                if cameraState.isManualISO {
                    Text("Manual ISO").font(.system(.title))
                    ForEach(isoPresets, id: \.self) { iso in
                        Button(String(iso)) { cameraState.userISO = Float(iso) }
                    }
                } else {
                    Text("Auto ISO Mode").font(.system(.title))
                    Button("Minimize ISO") {}
                }
            }
    }

    var ExposureBiasField : some View {
        let dragGesture = getDragGesture($exposureBiasGestureOffset) { diff in
            if(!cameraState.isManualEVBias) { return }
            cameraState.userExposureBias = min(max(cameraState.userExposureBias + (diff / 100), cameraState.deviceMinExposureBias), cameraState.deviceMaxExposureBias)
        }

        func getValue() -> String {
            return String(cameraState.isManualEVBias ? String(format: "%.1f", cameraState.userExposureBias)
                          : String(format: "%.1f", cameraState.calculatedExposureBias))
        }

        return ExposureParamField(value: getValue(),
                           name: "EV",
                           hint: cameraState.isManualEVBias ? "MANUAL":  " ",
                           isSelected: cameraState.isManualEVBias)
            .gesture(dragGesture)
            .onTapGesture { cameraState.isManualEVBias.toggle() }
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
