import Foundation
import SwiftUI
import Combine

struct ZoomState {
    var syncedImageCount = 0 // This might not be needed anymore
    var zoomScale = CGFloat(1.0)
    var contentOffset = CGPoint(x: 0.0, y: 0.0)
}

struct ZoomableScrollView<Content: View>: UIViewRepresentable {
    let MAX_ZOOM_SCALE: CGFloat = 100
    let MIN_ZOOM_SCALE: CGFloat = 1
    
    private var content: Content
    @State private var newView: Bool = false
    
    let name: String
    @Binding var zoomState: ZoomState
    
    init(name: String, imageLocation: Binding<ZoomState>, @ViewBuilder content: () -> Content) {
        self.content = content()
        self.name = name
        self._zoomState = imageLocation
    }
    
    func makeUIView(context: Context) -> UIScrollView {
        let scrollView = UIScrollView()
        scrollView.delegate = context.coordinator
        scrollView.maximumZoomScale = MAX_ZOOM_SCALE
        scrollView.minimumZoomScale = MIN_ZOOM_SCALE
        scrollView.showsVerticalScrollIndicator = false
        scrollView.showsHorizontalScrollIndicator = false
        scrollView.isUserInteractionEnabled = true
        scrollView.bouncesZoom = false
        scrollView.bounces = false
        scrollView.decelerationRate = UIScrollView.DecelerationRate.fast
        
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
            zoomState.syncedImageCount += 1
            
            scrollView.zoomScale = zoomState.zoomScale
            
            let x = max(zoomState.contentOffset.x * scrollView.contentSize.width, 0)
            let y = max(zoomState.contentOffset.y * scrollView.contentSize.height, 0)
            scrollView.setContentOffset(CGPoint(x: x, y: y), animated: false)
        }
        
        return scrollView
    }
    
    static func dismantleUIView(_ uiView: UIScrollView, coordinator: Coordinator) {
        DispatchQueue.main.async {
            coordinator.zoomState?.syncedImageCount.wrappedValue -= 1
            coordinator.zoomState = nil
        }
    }
    
    func makeCoordinator() -> Coordinator {
        return Coordinator(name: name, imageLocation: _zoomState, hostingController: UIHostingController(rootView: self.content))
    }
    
    func updateUIView(_ uiView: UIScrollView, context: Context) {
        DispatchQueue.main.async { [zoomState] in
            // Preserve scroll momentum
            if uiView.isDecelerating { return }
            
            var isBigStep = (max(uiView.zoomScale, zoomState.zoomScale) /  min(uiView.zoomScale, zoomState.zoomScale)) > 2
            if zoomState.zoomScale == 1.0 { isBigStep = true }
            
            uiView.setZoomScale(zoomState.zoomScale, animated: isBigStep && !newView)
            
            if (uiView.contentOffset != zoomState.contentOffset)
                && !(zoomState.zoomScale > MAX_ZOOM_SCALE)
                && !(zoomState.zoomScale < MIN_ZOOM_SCALE) {
                
                if isBigStep {
                    let x = (uiView.contentSize.width / 2) - (uiView.bounds.size.width / 2)
                    let y = (uiView.contentSize.height / 2) - (uiView.bounds.size.height / 2)
                    uiView.setContentOffset(CGPoint(x: x, y: y), animated: true && !newView)
                } else {
                    let x = max(zoomState.contentOffset.x  * uiView.contentSize.width, 0)
                    let y = max(zoomState.contentOffset.y  * uiView.contentSize.height, 0)
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
        var zoomState: Binding<ZoomState>?
        var hostingController: UIHostingController<Content>
        
        init(name: String, imageLocation: Binding<ZoomState>,/*zoomScale: Binding<CGFloat>, contentOffset: Binding<CGPoint>,*/ hostingController: UIHostingController<Content>) {
            self.name = name
            self.zoomState = imageLocation
            self.hostingController = hostingController
        }
        
        func viewForZooming(in scrollView: UIScrollView) -> UIView? {
            return hostingController.view
        }
        
        
        func scrollViewDidZoom(_ scrollView: UIScrollView) {
            DispatchQueue.main.async {
                if(scrollView.contentSize.width == 0 || scrollView.contentSize.height == 0) { return }
                let x = scrollView.contentOffset.x / scrollView.contentSize.width
                let y = scrollView.contentOffset.y / scrollView.contentSize.height
                self.zoomState?.wrappedValue = ZoomState(zoomScale: scrollView.zoomScale, contentOffset: CGPoint(x: x, y: y))
            }
        }
        
        func scrollViewDidScroll(_ scrollView: UIScrollView) {
            DispatchQueue.main.async {
                if(scrollView.contentSize.width == 0 || scrollView.contentSize.height == 0) { return }
                let x = scrollView.contentOffset.x / scrollView.contentSize.width
                let y = scrollView.contentOffset.y / scrollView.contentSize.height
                self.zoomState?.wrappedValue = ZoomState(zoomScale: scrollView.zoomScale, contentOffset: CGPoint(x: x, y: y))
            }
        }
    }
}
