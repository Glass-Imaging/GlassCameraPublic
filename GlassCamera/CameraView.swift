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
    let photoCollection = PhotoCollection(albumNamed: "Glass Photos", createIfNotFound: true)
    let cameraState: CameraState
    let service: CameraService

    var isPhotosLoaded = false
    
    @Published var thumbnailImage: Image?
    @Published var showAlertError = false

    var session: AVCaptureSession

    private let volumeButtonListener = VolumeButtonListener()

    private var subscriptions = Set<AnyCancellable>()

    init(cameraState: CameraState) {
        self.cameraState = cameraState
        self.service = CameraService(cameraState: cameraState)
        self.session = service.session

        volumeButtonListener.onClickCallback {
            self.capturePhoto()
        }

        service.$thumbnail.sink { [weak self] (photo) in
            if let photo = photo {
                self?.thumbnailImage = Image(uiImage: photo)
            }
        }
        .store(in: &self.subscriptions)

        service.$shouldShowAlertView.sink { [weak self] (val) in
            // self?.alertError = self?.service.alertError
            self?.showAlertError = val
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
        if self.cameraState.isShutterDelayOn {
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
                self.service.capturePhoto(saveCollection: self.photoCollection)
            }
        } else {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
                self.service.capturePhoto(saveCollection: self.photoCollection)
            }
        }
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

    func loadPhotos() async {
        guard !isPhotosLoaded else { return }
        
        let authorized = await PhotoLibrary.checkAuthorization()
        guard authorized else {
            // logger.error("Photo library access was not authorized.")
            print("Photo library access was not authorized.")
            return
        }
        
        Task {
            do {
                try await self.photoCollection.load()
                await self.loadThumbnail()
            } catch let error {
                //logger.error("Failed to load photo collection: \(error.localizedDescription)")
                print("Failed to load photo collection: \(error.localizedDescription)")
            }
            self.isPhotosLoaded = true
        }
    }

    func loadThumbnail() async {
        guard let asset = photoCollection.photoAssets.first  else { return }
        await photoCollection.cache.requestImage(for: asset, targetSize: CGSize(width: 256, height: 256))  { result in
            if let result = result {
                Task { @MainActor in
                    self.thumbnailImage = result.image
                }
            }
        }
    }
}

struct CameraView: View {
    @EnvironmentObject var cameraState: CameraState
    @ObservedObject var model: CameraModel // = CameraModel(cameraState)

    @State private var zoomLevel:BackCameraConfiguration = .Wide

    init(model: CameraModel) {
        self.model = model
    }

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
                    if model.service.availableBackDevices.contains(configuration) {
                        Text(configuration.rawValue.capitalized)
                    }
                }
            }
            .pickerStyle(.segmented)
        }.onChange(of: zoomLevel) { newValue in
            model.switchCamera(model.deviceConfiguration(newValue))
        }
    }

    var topControlBar: some View {
        HStack {
            Spacer()

            Button(action: {
                self.cameraState.isFlashOn.toggle()
            }, label: {
                Image(systemName: self.cameraState.isFlashOn ? "bolt.fill" : "bolt.slash.fill")
                    .font(.system(size: 20, weight: .medium, design: .default))
            })
            .accentColor(self.cameraState.isFlashOn ? .yellow : .white)

            Spacer()

            Button(action: {
                self.cameraState.isShutterDelayOn.toggle()
            }, label: {
                Image(systemName: "timer").font(.system(size: 20, weight: .medium, design: .default))
            })
            .accentColor(model.cameraState.isShutterDelayOn ? .yellow : .white)

            Spacer()

            Button(action: {
                self.cameraState.isNNProcessingOn.toggle()
            }, label: {
                Image(systemName: "brain").font(.system(size: 20, weight: .medium, design: .default))
            })
            .accentColor(self.cameraState.isNNProcessingOn ? .yellow : .white)

            Spacer()
        }
    }

    struct CaptureSetting<T>: View {
        @GestureState var gestureOffset: Float = .zero

        var displayValue: String = "--"
        var name: String = "--"
        var isSelected: Bool = false
        var onDragCB: (Float) -> Void

        init(_ displayValue: T, name: String, isSelected: Bool, formatter: @escaping ((T) -> String), onDrag: @escaping (Float) -> Void) {
            self.displayValue = formatter(displayValue)
            self.name = name
            self.isSelected = isSelected
            self.onDragCB = onDrag
        }

        var body: some View {
            let dragGesture = DragGesture()
                .updating($gestureOffset) { (value, state, _) in
                    let new_state = Float(value.translation.height)
                    let diff  = (state - new_state)
                    state = new_state

                    DispatchQueue.main.async {
                        onDragCB(diff)
                    }
                }

            VStack {
                Text(self.displayValue)
                    .font(.system(size:16, weight: .heavy, design: .monospaced))
                Text(name)
                    .italic()
                    .font(.system(size:12, weight: .light, design: .rounded))
            }
            .gesture(dragGesture)
            .padding(5)
            .overlay(RoundedRectangle(cornerRadius: 5).stroke(isSelected ? Color.yellow : Color.clear, lineWidth: 2))
        }
    }


    var captureSettingsBar: some View {
        HStack {
            Spacer()

            CaptureSetting(self.cameraState.deviceAperture, name: "F STOP", isSelected: false, formatter: { aperture in  "f/\(aperture)" }, onDrag: {_ in })

            Spacer()

            CaptureSetting(self.cameraState.calculatedExposureDuration,
                           name: "SS",
                           isSelected: cameraState.isManualExposureDuration,
                           formatter: { duration in
                duration.seconds == 0 ? "0" : "1/\(Int((1 / duration.seconds).rounded()))"
                            },
                           onDrag: { diff in
                                if(!cameraState.isManualExposureDuration) { return }

                                let targetExposureDuration = Float(cameraState.userExposureDuration.seconds)
                                                                + Float(cameraState.userExposureDuration.seconds) * (1/50) * diff

                                let newExposureDuration = min(
                                                            max(targetExposureDuration, Float(cameraState.deviceMinExposureDuration.seconds)),
                                                            Float(cameraState.deviceMaxExposureDuration.seconds))

                                cameraState.userExposureDuration = CMTime(seconds: Double(newExposureDuration), preferredTimescale: 1_000_000_000)
                            })
                .onTapGesture {
                    cameraState.isManualExposureDuration.toggle()
                    cameraState.userExposureDuration = cameraState.calculatedExposureDuration
                }

            Spacer()

            CaptureSetting(self.cameraState.calculatedISO,
                           name: "ISO",
                           isSelected: cameraState.isManualISO,
                           formatter: { iso in String(Int(iso.rounded())) },
                           onDrag: { diff in
                                if(!cameraState.isManualISO) { return }
                                cameraState.userISO = min(max(cameraState.userISO + cameraState.userISO * (1/125) * diff, cameraState.deviceMinISO), cameraState.deviceMaxISO)
                            })
                .onTapGesture {
                    cameraState.isManualISO.toggle()
                    cameraState.userISO = cameraState.calculatedISO
                }

            Spacer()


            CaptureSetting(cameraState.isManualEVBias ? cameraState.userExposureBias : cameraState.calculatedExposureBias,
                           name: "EV",
                           isSelected: cameraState.isManualEVBias,
                           formatter: { bias in String(format: "%.1f", bias)},
                           onDrag: { diff in
                                if(!cameraState.isManualEVBias) { return }
                                cameraState.userExposureBias = min(max(cameraState.userExposureBias + (diff / 100), cameraState.deviceMinExposureBias), cameraState.deviceMaxExposureBias)
                            })
                .onTapGesture { cameraState.isManualEVBias.toggle() }
            /*
                .onLongPressGesture {
                    print("LONG PRESS BIAS!")
                }
             */

            Spacer()
        }
    }

    var body: some View {
        HideVolumeIndicator // Required to hide volume indicator when triggering capture with volume rocker
        NavigationStack {
            
            GeometryReader { reader in
                ZStack {
                    Color.black.edgesIgnoringSafeArea(.all)
                    
                    VStack {
                        topControlBar

                        Spacer()

                        ZStack {
                            CameraPreview(session: model.session)
                                .onAppear {
                                    // TODO: Fetch this from stored preferences
                                    model.configure(DeviceConfiguration(position: .back, deviceType: .builtInWideAngleCamera))
                                }
                                .alert(isPresented: $model.showAlertError, content: {
                                    Alert(title: Text(model.service.alertError.title),
                                          message: Text(model.service.alertError.message),
                                          dismissButton: .default(Text(model.service.alertError.primaryButtonTitle), action: {
                                        model.service.alertError.primaryAction?()
                                    }))
                                })
                                .overlay(Group {
                                    if model.service.shouldShowSpinner {
                                        Color.black.opacity(0.2)

                                        ProgressView()
                                            .scaleEffect(2, anchor: .center)
                                            .progressViewStyle(CircularProgressViewStyle(tint: Color.white))
                                    }

                                    if model.service.willCapturePhoto {
                                        Group {
                                            Color.black
                                        }.onAppear {
                                            withAnimation {
                                                model.service.willCapturePhoto = false
                                            }
                                        }
                                    }
                                })
                            if(cameraState.debugOverlay) {
                                HStack {
                                    VStack {
                                        VStack {
                                            // Text(verbatim: "Final Offset   :: \(cameraState.finalEVOffset)").frame(maxWidth: .infinity, alignment: .leading)
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

                        Spacer()

                        captureSettingsBar

                        Spacer()

                        if (model.service.currentDevice?.position == .back) {
                            zoomLevelPicker
                                .padding(.all, 20)
                        }

                        Spacer()

                        HStack {
                            NavigationLink {
                                PhotoCollectionView(photoCollection: model.photoCollection)
                            } label: {
                                Label {} icon: {
                                    ThumbnailView(image: model.thumbnailImage)
                                }
                            }
                            
                            Spacer()
                            
                            captureButton
                            
                            Spacer()
                            
                            flipCameraButton
                            
                        }
                        .padding(.horizontal, 20)
                        
                        
                    }
                }
            }
            .preferredColorScheme(.dark)
            .task {
                await model.loadPhotos()
                await model.loadThumbnail()
            }
        }
    }
}
