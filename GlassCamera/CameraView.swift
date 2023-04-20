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

import SwiftUI
import Combine
import AVFoundation

final class CameraModel: ObservableObject {
    private let service = CameraService()

    @Published var photo: UIImage!

    @Published var showAlertError = false

    @Published var isFlashOn = false

    @Published var willCapturePhoto = false

    @Published var showSpinner = false

    @Published var currentDevice: DeviceConfiguration? = nil

    @Published var availableBackDevices: [BackCameraConfiguration] = []

    var alertError: AlertError!

    var session: AVCaptureSession

    private var subscriptions = Set<AnyCancellable>()

    init() {
        self.session = service.session

        service.$thumbnail.sink { [weak self] (photo) in
            guard let pic = photo else { return }
            self?.photo = pic
        }
        .store(in: &self.subscriptions)

        service.$shouldShowAlertView.sink { [weak self] (val) in
            self?.alertError = self?.service.alertError
            self?.showAlertError = val
        }
        .store(in: &self.subscriptions)

        service.$flashMode.sink { [weak self] (mode) in
            self?.isFlashOn = mode == .on
        }
        .store(in: &self.subscriptions)

        service.$willCapturePhoto.sink { [weak self] (val) in
            self?.willCapturePhoto = val
        }
        .store(in: &self.subscriptions)

        service.$shouldShowSpinner.sink { [weak self] (val) in
            self?.showSpinner = val
        }
        .store(in: &self.subscriptions)

        service.$currentDevice.sink { [weak self] (val) in
            self?.currentDevice = val
        }
        .store(in: &self.subscriptions)

        service.$availableBackDevices.sink { [weak self] (val) in
            self?.availableBackDevices = val
        }
        .store(in: &self.subscriptions)
    }

    func deviceConfiguration(_ configuration : BackCameraConfiguration) -> DeviceConfiguration {
        return service.backDeviceConfigurations[configuration] ??
                DeviceConfiguration(position: .back, deviceType: .builtInWideAngleCamera)
    }

    func configure(_ configuration: DeviceConfiguration) {
        service.checkForPermissions()
        service.configure(configuration)
    }

    func capturePhoto() {
        service.capturePhoto()
    }

    func flipCamera() {
        let configuration = service.currentDevice?.position == .back ?
                DeviceConfiguration(position: .front, deviceType: .builtInWideAngleCamera) :
                DeviceConfiguration(position: .back,  deviceType: .builtInWideAngleCamera)
        service.changeCamera(configuration)
    }

    func switchCamera(_ configuration: DeviceConfiguration) {
        service.changeCamera(configuration)
    }

    func switchFlash() {
        service.flashMode = service.flashMode == .on ? .off : .on
    }
}

struct CameraView: View {
    @StateObject var model = CameraModel()

    @State private var zoomLevel:BackCameraConfiguration = .Wide

    var captureButton: some View {
        Button(action: {
            model.capturePhoto()
        }, label: {
            Circle()
                .foregroundColor(.white)
                .frame(width: 80, height: 80, alignment: .center)
                .overlay(
                    Circle()
                        .stroke(Color.black.opacity(0.8), lineWidth: 2)
                        .frame(width: 65, height: 65, alignment: .center)
                )
        })
    }

    var capturedPhotoThumbnail: some View {
        Group {
            if let thumbnail = model.photo {
                Image(uiImage: thumbnail)
                    .resizable()
                    .aspectRatio(contentMode: .fill)
                    .frame(width: 60, height: 60)
                    .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
                    .animation(.spring(), value: model.willCapturePhoto)
            } else {
                RoundedRectangle(cornerRadius: 10)
                    .frame(width: 60, height: 60, alignment: .center)
                    .foregroundColor(.black)
            }
        }.onTapGesture {
            UIApplication.shared.open(URL(string:"photos-redirect://")!)
        }
    }

    var flipCameraButton: some View {
        Button(action: {
            model.flipCamera()
        }, label: {
            Circle()
                .foregroundColor(Color.gray.opacity(0.2))
                .frame(width: 45, height: 45, alignment: .center)
                .overlay(
                    Image(systemName: "camera.rotate.fill")
                        .foregroundColor(.white))
        })
    }

    var zoomLevelPicker: some View {
        VStack {
            Picker("Camera Configuration", selection: $zoomLevel) {
                // Preserve the order in the BackCameraConfiguration definition
                ForEach(BackCameraConfiguration.allCases) { configuration in
                    if model.availableBackDevices.contains(configuration) {
                        Text(configuration.rawValue.capitalized)
                    }
                }
            }
            .pickerStyle(.segmented)
        }.onChange(of: zoomLevel) { newValue in
            model.switchCamera(model.deviceConfiguration(newValue))
        }
    }

    var body: some View {
        GeometryReader { reader in
            ZStack {
                Color.black.edgesIgnoringSafeArea(.all)

                VStack {
                    Button(action: {
                        model.switchFlash()
                    }, label: {
                        Image(systemName: model.isFlashOn ? "bolt.fill" : "bolt.slash.fill")
                            .font(.system(size: 20, weight: .medium, design: .default))
                    })
                    .accentColor(model.isFlashOn ? .yellow : .white)

                    ZStack {
                        CameraPreview(session: model.session)
                            .onAppear {
                                // TODO: Fetch this from stored preferences
                                model.configure(DeviceConfiguration(position: .back, deviceType: .builtInWideAngleCamera))
                            }
                            .alert(isPresented: $model.showAlertError, content: {
                                Alert(title: Text(model.alertError.title),
                                      message: Text(model.alertError.message),
                                      dismissButton: .default(Text(model.alertError.primaryButtonTitle), action: {
                                    model.alertError.primaryAction?()
                                }))
                            })
                            .overlay(Group {
                                if model.showSpinner {
                                    Color.black.opacity(0.2)

                                    ProgressView()
                                        .scaleEffect(2, anchor: .center)
                                        .progressViewStyle(CircularProgressViewStyle(tint: Color.white))
                                }

                                if model.willCapturePhoto {
                                    Group {
                                        Color.black
                                    }.onAppear {
                                        withAnimation {
                                            model.willCapturePhoto = false
                                        }
                                    }
                                }
                            })

                        if (model.currentDevice?.position == .back) {
                            VStack {
                                Spacer()

                                zoomLevelPicker
                                    .padding(.all, 20)
                            }
                        }
                    }

                    HStack {
                        capturedPhotoThumbnail

                        Spacer()

                        captureButton

                        Spacer()

                        flipCameraButton
                    }
                    .padding(.horizontal, 20)
                }
            }
        }
    }
}
