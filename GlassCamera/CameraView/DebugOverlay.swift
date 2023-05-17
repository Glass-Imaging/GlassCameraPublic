//
//  DebugOverlay.swift
//  GlassCamera
//
//  Created by Doug MacEwen on 5/16/23.
//

import Foundation
import SwiftUI



struct DebugOverlay: View {
    @EnvironmentObject var cameraState: CameraState

    var body: some View {
        HStack {
            VStack {
                VStack {
                    Text(verbatim: "Is Custom Exposure  :: \(cameraState.isCustomExposure)").frame(maxWidth: .infinity, alignment: .leading)
                }.foregroundColor(.cyan).font(.headline)
                VStack {
                    Text(verbatim: "METERED").frame(maxWidth: .infinity, alignment: .leading)
                    Text(verbatim: "Duration :: 1/\((1 / cameraState.meteredExposureDuration.seconds).rounded())").frame(maxWidth: .infinity, alignment: .leading)
                    Text(verbatim: "ISO      :: \(Int(cameraState.meteredISO.rounded()))").frame(maxWidth: .infinity, alignment: .leading)
                    Text(verbatim: "Bias     :: \(cameraState.meteredExposureBias)").frame(maxWidth: .infinity, alignment: .leading)
                    Text(verbatim: "Offset   :: \(cameraState.meteredExposureOffset)").frame(maxWidth: .infinity, alignment: .leading)
                }.foregroundColor(.red)
                VStack {
                    Text(verbatim: "CALCULATED").frame(maxWidth: .infinity, alignment: .leading)
                    Text(verbatim: "Duration :: 1/\((1 / cameraState.calculatedExposureDuration.seconds).rounded())").frame(maxWidth: .infinity, alignment: .leading)
                    Text(verbatim: "ISO      :: \(Int(cameraState.calculatedISO.rounded()))").frame(maxWidth: .infinity, alignment: .leading)
                    Text(verbatim: "Bias     :: \(cameraState.calculatedExposureBias)").frame(maxWidth: .infinity, alignment: .leading)
                }.foregroundColor(.yellow)
                VStack {
                    Text(verbatim: "USER").frame(maxWidth: .infinity, alignment: .leading)
                    Text(verbatim: "Duration :: 1/\((1 / cameraState.userExposureDuration.seconds).rounded())").frame(maxWidth: .infinity, alignment: .leading)
                    Text(verbatim: "ISO      :: \(Int(cameraState.userISO.rounded()))").frame(maxWidth: .infinity, alignment: .leading)
                    Text(verbatim: "Bias     :: \(cameraState.userExposureBias)").frame(maxWidth: .infinity, alignment: .leading)
                }.foregroundColor(.green)
                Spacer()
            }.bold(true)
            Spacer()
        }
    }
}
