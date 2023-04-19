/*
See the License.txt file for this sample’s licensing information.
*/

import SwiftUI
import Photos
import Combine


struct PhotoView: View {
    var asset: PhotoAsset
    var cache: CachedImageManager?
    @State private var image: Image?
    @State private var imageRequestID: PHImageRequestID?
    @Environment(\.dismiss) var dismiss
    
    @State var imageLocation = ImageLocation()
    
    private let imageSize = CGSize(width: 4032, height: 4032)
    private let doubleTapZoomScale = CGFloat(10)
    
    @State private var showImageA = true
    @State private var showImageB = true
    
    var body: some View {
        Group {
            if showImageA && !showImageB {
                getZoomableView(name: "GlassNN", imageLocation: $imageLocation)
            } else if showImageB && !showImageA {
                getZoomableView(name: "ISP", imageLocation: $imageLocation)
            } else {
                VStack {
                    getZoomableView(name: "GlassNN", imageLocation: $imageLocation)
                    getZoomableView(name: "ISP", imageLocation: $imageLocation)
                }
            }
        }
        .onTapGesture(count: 2) {
            if imageLocation.zoomScale == 1 {
                imageLocation.zoomScale = doubleTapZoomScale
            } else {
                imageLocation.zoomScale = 1
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
            guard image == nil, let cache = cache else { return }
            imageRequestID = await cache.requestImage(for: asset, targetSize: imageSize) { result in
                Task {
                    if let result = result {
                        self.image = result.image
                    }
                }
            }
        }
    }
    
    func getZoomableView(name: String, imageLocation: Binding<ImageLocation>) -> some View {
            return ZoomableScrollView(name: name, imageLocation: $imageLocation) {
                image?
                    .resizable()
                    .scaledToFit()
                    .accessibilityLabel(asset.accessibilityLabel)
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
