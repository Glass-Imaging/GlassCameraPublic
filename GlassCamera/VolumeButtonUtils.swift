//
//  VolumeShutterButton.swift
//  GlassCamera
//
//  Created by Doug MacEwen on 5/4/23.
//

import Foundation
import SwiftUI
import MediaPlayer

class VolumeButtonListener: NSObject {
    private var volume: Float = 0
    private let slider: UISlider?
    private var rateLimitTimer: Timer?
    private var volumeChangedCB: (() -> Void)?

    override init() {
        let audioSession = AVAudioSession.sharedInstance()
        try! audioSession.setActive(true)
        
        // Store the current volume so user volume doesnt change too much while using app
        //  Clamp so there is always room to detect and increase or decreate in volume
        volume = min(max(audioSession.outputVolume, 0.2), 0.8)
        let volumeView = MPVolumeView(frame: .zero)
        slider = volumeView.subviews.first(where: { $0 is UISlider }) as? UISlider
        super.init()

        self.slider?.setValue(self.volume, animated: false)

        audioSession.addObserver(self, forKeyPath: "outputVolume", options: NSKeyValueObservingOptions.new, context: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(willEnterForeground), name: UIScene.willEnterForegroundNotification, object: nil)
    }

    // Need to re-activeate audio session when the app enters the foreground
    @objc func willEnterForeground() {
        let audioSession = AVAudioSession.sharedInstance()
        try! audioSession.setActive(true)
        
        self.rateLimitTimer?.invalidate()
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
    }

    override func observeValue(forKeyPath keyPath: String?, of object: Any?, change: [NSKeyValueChangeKey : Any]?, context: UnsafeMutableRawPointer?) {
        // Set volume back to the original value
        DispatchQueue.main.asyncAfter(deadline: DispatchTime.now() + 0.03) {
            self.slider?.setValue(self.volume, animated: false)
        }

        // Limit the rate that the callback is called
        if(!(self.rateLimitTimer?.isValid ?? false)) {
            volumeChangedCB?()
        }

        // Always reset the timer. This stops users from triggering many captures by holding the button down
        self.rateLimitTimer?.invalidate()
        self.rateLimitTimer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: false) {_ in }
    }

    func onClickCallback(_ _volumeChangedCB: @escaping () -> Void) {
        volumeChangedCB = _volumeChangedCB
    }
}

// Volume overlay. iOS requires that some view show the volume status. This hides it since we use volume buttons as capture buttons
private struct HideVolumeOverlay: UIViewRepresentable {
    func makeUIView(context: Context) -> MPVolumeView {
       let volumeView = MPVolumeView(frame: CGRect.zero)
       volumeView.alpha = 0.001
       return volumeView
    }

    func updateUIView(_ uiView: MPVolumeView, context: Context) {}
}

var HideVolumeIndicator: some View {
    HideVolumeOverlay().frame(width: 0, height: 0)
}
