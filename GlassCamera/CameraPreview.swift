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

import UIKit
import AVFoundation
import SwiftUI

public struct CameraPreview: UIViewRepresentable {
    static let focusViewRadius = 25.0

    public class VideoPreviewView: UIView {
        public override class var layerClass: AnyClass {
            AVCaptureVideoPreviewLayer.self
        }

        var videoPreviewLayer: AVCaptureVideoPreviewLayer {
            return layer as! AVCaptureVideoPreviewLayer
        }

        let focusView: UIView = {
            let focusView = UIView(frame: CGRect(x: 0, y: 0, width: 2 * focusViewRadius, height: 2 * focusViewRadius))
            focusView.layer.borderColor = UIColor.white.cgColor
            focusView.layer.borderWidth = 1.5
            focusView.layer.cornerRadius = focusViewRadius
            focusView.layer.opacity = 0
            focusView.backgroundColor = .clear
            return focusView
        }()

        @objc func focusAndExposeTap(gestureRecognizer: UITapGestureRecognizer) {
            let layerPoint = gestureRecognizer.location(in: gestureRecognizer.view)
            let devicePoint = videoPreviewLayer.captureDevicePointConverted(fromLayerPoint: layerPoint)

            // Center the focusView circle where the user tapped
            self.focusView.layer.frame = CGRect(origin: CGPoint(x: layerPoint.x - focusViewRadius,
                                                                y: layerPoint.y - focusViewRadius),
                                                size: CGSize(width: 2 * focusViewRadius,
                                                             height: 2 * focusViewRadius))

            NotificationCenter.default.post(.init(name: .init("UserDidRequestNewFocusPoint"), object: nil, userInfo: ["devicePoint": devicePoint] as [AnyHashable: Any]))

            UIView.animate(withDuration: 0.3, animations: {
                self.focusView.layer.opacity = 1
            }) { (completed) in
                if completed {
                    UIView.animate(withDuration: 0.3) {
                        self.focusView.layer.opacity = 0
                    }
                }
            }
        }

        public override func layoutSubviews() {
            super.layoutSubviews()

            self.layer.addSublayer(focusView.layer)

            let gRecognizer = UITapGestureRecognizer(target: self, action: #selector(VideoPreviewView.focusAndExposeTap(gestureRecognizer:)))
            self.addGestureRecognizer(gRecognizer)
        }
    }

    public let session: AVCaptureSession

    public init(session: AVCaptureSession) {
        self.session = session
    }

    public func makeUIView(context: Context) -> VideoPreviewView {
        let viewFinder = VideoPreviewView()
        viewFinder.backgroundColor = .black
        viewFinder.videoPreviewLayer.cornerRadius = 0
        viewFinder.videoPreviewLayer.session = session
        viewFinder.videoPreviewLayer.connection?.videoOrientation = .portrait
        return viewFinder
    }

    public func updateUIView(_ uiView: VideoPreviewView, context: Context) {

    }
}
