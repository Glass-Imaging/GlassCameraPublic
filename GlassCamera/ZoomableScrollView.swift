//
//  ZoomableScrollView.swift
//  Capturing Photos
//
//  Created by Doug MacEwen on 4/17/23.
//

import Foundation
import SwiftUI
import Combine

struct ImageLocation {
    var syncedImageCount = 0
    var zoomScale = CGFloat(1.0)
    var contentOffset = CGPoint(x: 0.0, y: 0.0)
}


struct ZoomableScrollView<Content: View>: UIViewRepresentable {
    let MAX_ZOOM_SCALE: CGFloat = 100
    let MIN_ZOOM_SCALE: CGFloat = 1
    
    private var content: Content
    @State private var newView: Bool = false
    
    let name: String
    @Binding var imageLocation: ImageLocation
    
    init(name: String, imageLocation: Binding<ImageLocation>,/*zoomScale: Binding<CGFloat>, contentOffset: Binding<CGPoint>,*/ @ViewBuilder content: () -> Content) {
        self.content = content()
        self.name = name
        self._imageLocation = imageLocation
    }
    
    func makeUIView(context: Context) -> UIScrollView {
        // set up the UIScrollView
        let scrollView = UIScrollView()
        scrollView.delegate = context.coordinator  // for viewForZooming(in:)
        scrollView.maximumZoomScale = MAX_ZOOM_SCALE
        scrollView.minimumZoomScale = MIN_ZOOM_SCALE
        scrollView.showsVerticalScrollIndicator = false
        scrollView.showsHorizontalScrollIndicator = false
        scrollView.isUserInteractionEnabled = true
        scrollView.bouncesZoom = false //true
        scrollView.bounces = false
        
        
        // create a UIHostingController to hold our SwiftUI content
        let hostedView = context.coordinator.hostingController.view!
        hostedView.translatesAutoresizingMaskIntoConstraints = true
        hostedView.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        hostedView.frame = scrollView.bounds
        hostedView.backgroundColor = UIColor(white: 0.0, alpha: 0.0)
        scrollView.addSubview(hostedView)
        
        // This is needed to prevent a single frame of the unzoomed image from showing
        hostedView.isHidden = true
        DispatchQueue.main.async {
            newView = true
            
            hostedView.isHidden = false
            imageLocation.syncedImageCount += 1
            
            scrollView.zoomScale = imageLocation.zoomScale
            
            let x = max(imageLocation.contentOffset.x * scrollView.contentSize.width, 0)
            let y = max(imageLocation.contentOffset.y * scrollView.contentSize.height, 0)
            scrollView.setContentOffset(CGPoint(x: x, y: y), animated: false/* && !newView*/)
        }
        
        DispatchQueue.main.async {
        }
        
        return scrollView
    }
    
    static func dismantleUIView(_ uiView: UIScrollView, coordinator: Coordinator) {
        DispatchQueue.main.async {
            NSLog("\(coordinator.name) : Dismanteling")
            coordinator.imageLocation?.syncedImageCount.wrappedValue -= 1
            coordinator.imageLocation = nil
        }
    }
    
    
    func makeCoordinator() -> Coordinator {
        return Coordinator(name: name, imageLocation: _imageLocation, hostingController: UIHostingController(rootView: self.content))
    }
    
    func updateUIView(_ uiView: UIScrollView, context: Context) {
        NSLog("\(name) Update UI View \(imageLocation)")
        
        DispatchQueue.main.async {
            var isBigStep = (max(uiView.zoomScale, imageLocation.zoomScale) /  min(uiView.zoomScale, imageLocation.zoomScale)) > 2
            if imageLocation.zoomScale == 1.0 { isBigStep = true }
            
            uiView.setZoomScale(imageLocation.zoomScale, animated: isBigStep && !newView)
            
            if (uiView.contentOffset != imageLocation.contentOffset)
                && !(imageLocation.zoomScale > MAX_ZOOM_SCALE)
                && !(imageLocation.zoomScale < MIN_ZOOM_SCALE) {
                
                if isBigStep {
                    let x = (uiView.contentSize.width / 2) - (uiView.bounds.size.width / 2)
                    let y = (uiView.contentSize.height / 2) - (uiView.bounds.size.height / 2)
                    uiView.setContentOffset(CGPoint(x: x, y: y), animated: true && !newView)
                } else {
                    let x = max(imageLocation.contentOffset.x  * uiView.contentSize.width, 0)
                    let y = max(imageLocation.contentOffset.y  * uiView.contentSize.height, 0)
                    uiView.setContentOffset(CGPoint(x: x, y: y), animated: false && !newView)
                }
            }
            
            newView = false
        }
         
        // update the hosting controller's SwiftUI content
        context.coordinator.hostingController.rootView = self.content
        assert(context.coordinator.hostingController.view.superview == uiView)
    }
    
    
    // MARK: - Coordinator
    
    class Coordinator: NSObject, UIScrollViewDelegate {
        var name: String
        var imageLocation: Binding<ImageLocation>?
        var hostingController: UIHostingController<Content>
        
        init(name: String, imageLocation: Binding<ImageLocation>,/*zoomScale: Binding<CGFloat>, contentOffset: Binding<CGPoint>,*/ hostingController: UIHostingController<Content>) {
            self.name = name
            self.imageLocation = imageLocation
            self.hostingController = hostingController
        }
        
        func viewForZooming(in scrollView: UIScrollView) -> UIView? {
            return hostingController.view
        }
        
        
        func scrollViewDidZoom(_ scrollView: UIScrollView) {
            DispatchQueue.main.async {
                if(scrollView.contentSize.width == 0 || scrollView.contentSize.height == 0) { return }
                // NSLog("\(self.name) zoomChanged \(scrollView.zoomScale)")
                let x = scrollView.contentOffset.x / scrollView.contentSize.width
                let y = scrollView.contentOffset.y / scrollView.contentSize.height
                self.imageLocation?.wrappedValue = ImageLocation(zoomScale: scrollView.zoomScale, contentOffset: CGPoint(x: x, y: y))
            }
        }
        
        func scrollViewDidScroll(_ scrollView: UIScrollView) {
            DispatchQueue.main.async {
                if(scrollView.contentSize.width == 0 || scrollView.contentSize.height == 0) { return }
                // NSLog("\(self.name) coordChanged \(scrollView.contentOffset)")
                let x = scrollView.contentOffset.x / scrollView.contentSize.width
                let y = scrollView.contentOffset.y / scrollView.contentSize.height
                self.imageLocation?.wrappedValue = ImageLocation(zoomScale: scrollView.zoomScale, contentOffset: CGPoint(x: x, y: y))
            }
        }
    }
}
