//
//  ZoomableScrollView.swift
//  Capturing Photos
//
//  Created by Doug MacEwen on 4/17/23.
//

import Foundation
import SwiftUI
import Combine


struct ZoomableScrollView<Content: View>: UIViewRepresentable {
    let MAX_ZOOM_SCALE: CGFloat = 100
    let MIN_ZOOM_SCALE: CGFloat = 1
    
    private var content: Content
    let name: String
    @Binding var zoomScale: CGFloat
    @Binding var contentOffset: CGPoint
    
    init(name: String, zoomScale: Binding<CGFloat>, contentOffset: Binding<CGPoint>, @ViewBuilder content: () -> Content) {
        self.content = content()
        self.name = name
        self._zoomScale = zoomScale
        self._contentOffset = contentOffset
    }
    
    func makeUIView(context: Context) -> UIScrollView {
        print("Making UI View")
        // set up the UIScrollView
        let scrollView = UIScrollView()
        scrollView.delegate = context.coordinator  // for viewForZooming(in:)
        scrollView.maximumZoomScale = MAX_ZOOM_SCALE
        scrollView.minimumZoomScale = MIN_ZOOM_SCALE
        scrollView.showsVerticalScrollIndicator = false
        scrollView.showsHorizontalScrollIndicator = false
        scrollView.isUserInteractionEnabled = true
        scrollView.bouncesZoom = true
        
        // create a UIHostingController to hold our SwiftUI content
        let hostedView = context.coordinator.hostingController.view!
        hostedView.translatesAutoresizingMaskIntoConstraints = true
        hostedView.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        hostedView.frame = scrollView.bounds
        hostedView.backgroundColor = UIColor(white: 0.0, alpha: 0.0)
        scrollView.addSubview(hostedView)
        
        return scrollView
    }
    
    func makeCoordinator() -> Coordinator {
        return Coordinator(name: name, zoomScale: _zoomScale, contentOffset: _contentOffset, hostingController: UIHostingController(rootView: self.content))
    }
    
    func updateUIView(_ uiView: UIScrollView, context: Context) {
        var isBigStep = (max(uiView.zoomScale, zoomScale) /  min(uiView.zoomScale, zoomScale)) > 2
        if zoomScale == 1.0 { isBigStep = true }
        uiView.setZoomScale(zoomScale, animated: isBigStep)
        
        let centerX = (uiView.contentSize.width / 2) - (uiView.bounds.size.width / 2)
        let centerY = (uiView.contentSize.height / 2) - (uiView.bounds.size.height / 2)
        let testCenterPoint = CGPoint(x: centerX, y: centerY)
        
        if (uiView.contentOffset != contentOffset) && !(zoomScale > MAX_ZOOM_SCALE) && !(zoomScale < MIN_ZOOM_SCALE) {
            if isBigStep {
                uiView.setContentOffset(testCenterPoint, animated: true)
            } else {
                uiView.setContentOffset(contentOffset, animated: false)
            }
        }
        
         
        // update the hosting controller's SwiftUI content
        context.coordinator.hostingController.rootView = self.content
        assert(context.coordinator.hostingController.view.superview == uiView)
    }
    
    
    // MARK: - Coordinator
    
    class Coordinator: NSObject, UIScrollViewDelegate {
        var name: String
        var zoomScale: Binding<CGFloat>
        var contentOffset: Binding<CGPoint>
        var hostingController: UIHostingController<Content>
        
        init(name: String, zoomScale: Binding<CGFloat>, contentOffset: Binding<CGPoint>, hostingController: UIHostingController<Content>) {
            self.name = name
            self.zoomScale = zoomScale
            self.contentOffset = contentOffset
            self.hostingController = hostingController
        }
        
        func viewForZooming(in scrollView: UIScrollView) -> UIView? {
            return hostingController.view
        }
        
        func scrollViewDidZoom(_ scrollView: UIScrollView) {
            DispatchQueue.main.async {
                self.zoomScale.wrappedValue = scrollView.zoomScale
            }
        }
        
        func scrollViewDidScroll(_ scrollView: UIScrollView) {
            DispatchQueue.main.async {
                self.contentOffset.wrappedValue = scrollView.contentOffset
            }
        }
    }
}
