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
    @Published var zoomLevel: BackCameraConfiguration = .Wide

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

        $zoomLevel.sink { configuration in
            if(self.service.isConfigured) {
                print("SET ZOOM LEVEL :: \(configuration)")
                let newConfiguration = self.service.backDeviceConfigurations[configuration] ??
                DeviceConfiguration(position: .back, deviceType: .builtInWideAngleCamera)
                self.switchCamera(newConfiguration)
            }

        }
        .store(in: &self.subscriptions)

        configure(DeviceConfiguration(position: .back, deviceType: .builtInWideAngleCamera))
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
                service.backDeviceConfigurations[self.zoomLevel]!

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
    @ObservedObject var model: CameraModel

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
            Picker("Camera Configuration", selection: $model.zoomLevel) {
                // Preserve the order in the BackCameraConfiguration definition
                ForEach(BackCameraConfiguration.allCases) { configuration in
                    if model.service.availableBackDevices.contains(configuration) {
                        Text(configuration.rawValue.capitalized)
                    }
                }
            }
            .pickerStyle(.segmented)
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
                            /*
                                .onAppear {
                                    // TODO: Fetch this from stored preferences
                                    model.configure(DeviceConfiguration(position: .back, deviceType: .builtInWideAngleCamera))
                                }
                             */
                                .alert(isPresented: $model.showAlertError, content: {
                                    Alert(title: Text(model.service.alertError.title),
                                          message: Text(model.service.alertError.message),
                                          dismissButton: .default(Text(model.service.alertError.primaryButtonTitle), action: {
                                        model.service.alertError.primaryAction?()
                                    }))
                                })
                                .overlay(Group {
                                    if model.service.isPhotoCapturing {
                                        Group {
                                            Color.black
                                        }
                                    }
                                })
                            if(cameraState.debugOverlay) { DebugOverlay() }
                        }

                        Spacer()

                        ExposureParams()

                        Spacer()

                        if (model.service.currentDevice?.position == .back) {
                            zoomLevelPicker
                                .padding(.top, 5)
                                .padding(.bottom, 10)
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
