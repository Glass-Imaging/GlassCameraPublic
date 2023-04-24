import SwiftUI
import Photos
import Combine


struct PhotoView: View {
    var assetA: PhotoAsset
    
    var assetB: PhotoAsset
    
    var cache: CachedImageManager?
    @Environment(\.dismiss) var dismiss
    
    @State private var imageA: Image?
    @State private var imageB: Image?
    
    @State private var imageARequestID: PHImageRequestID?
    @State private var imageBRequestID: PHImageRequestID?
    
    @State private var showImageA = true
    @State private var showImageB = true
    
    @State var zoomState = ZoomState()
    
    private let imageSize = CGSize(width: 4032, height: 4032)
    private let doubleTapZoomScale = CGFloat(10)
    
    var body: some View {
        Group {
            if showImageA && !showImageB {
                getZoomableView(name: assetA.photoCategory.name, image: imageA, imageLocation: $zoomState)
            } else if showImageB && !showImageA {
                getZoomableView(name: assetB.photoCategory.name, image: imageB, imageLocation: $zoomState)
            } else {
                VStack {
                    getZoomableView(name: assetA.photoCategory.name, image: imageA, imageLocation: $zoomState)
                    getZoomableView(name: assetB.photoCategory.name, image: imageB, imageLocation: $zoomState)
                }
            }
        }
        .onTapGesture(count: 2) {
            if zoomState.zoomScale == 1 {
                zoomState.zoomScale = doubleTapZoomScale
            } else {
                zoomState.zoomScale = 1
            }
        }
        .overlay(alignment: .bottomTrailing) {
            if !(showImageA && showImageB) {
                Text(verbatim: "Switch")
                    .onTapGesture {
                        let temp = showImageB
                        showImageB = showImageA
                        showImageA = temp
                    }
                    .foregroundColor(.white)
                    .padding(15)
                    .background(alignment: .center) {
                        RoundedRectangle(cornerRadius: 5, style: .continuous)
                            .fill(.black.opacity(0.5))
                    }
                    .font(.callout)
                    .offset(x: -10, y: -10)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .toolbarColorScheme(.dark, for: .navigationBar)
        .background(Color.black)
        .navigationTitle("Comparison")
        .navigationBarItems(trailing: Button("Split/Single", action: {
            if showImageA && showImageB {
                showImageB = false
            } else {
                showImageA = true
                showImageB = true
            }
        }))
        .navigationBarTitleDisplayMode(.inline)
        .task {
            guard imageA == nil, imageB == nil, let cache = cache else { return }
            imageARequestID = await cache.requestImage(for: assetA, targetSize: imageSize) { result in
                Task {
                    if let result = result {
                        self.imageA = result.image
                    }
                }
            }
            imageBRequestID = await cache.requestImage(for: assetB, targetSize: imageSize) { result in
                Task {
                    if let result = result {
                        self.imageB = result.image
                    }
                }
            }
        }
    }
    
    func getZoomableView(name: String, image: Image?, imageLocation: Binding<ZoomState>) -> some View {
            return ZoomableScrollView(name: name, imageLocation: $zoomState) {
                image?
                    .resizable()
                    .interpolation(.none)
                    .scaledToFit()
            }
            .overlay(alignment: .topLeading) {
                Text(verbatim: name)
                    .foregroundColor(.white)
                    .padding(5)
                    .background(alignment: .center) {
                        RoundedRectangle(cornerRadius: 5, style: .continuous)
                            .fill(.black.opacity(0.5))
                    }
                    .font(.callout)
                    .offset(x: 4, y: 4)
            }
    }
}
