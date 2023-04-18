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
    
    @State var zoomScale = CGFloat(1.0)
    @State var contentOffset = CGPoint(x: 0.0, y: 0.0)
    
    private let imageSize = CGSize(width: 4032, height: 4032)
    private let doubleTapZoomScale = CGFloat(10)
    
    var body: some View {
        Group {
            VStack {
                ZoomableScrollView(name: "GlassNN", zoomScale: $zoomScale, contentOffset: $contentOffset) {
                    image?
                        .resizable()
                        .scaledToFit()
                        .accessibilityLabel(asset.accessibilityLabel)
                }
                .overlay(alignment: .topLeading) {
                    Text(verbatim: "GlassNN")
                        .foregroundColor(.white)
                        .padding(5)
                        .background(alignment: .center) {
                            RoundedRectangle(cornerRadius: 5, style: .continuous)
                                .fill(.black.opacity(0.5))
                        }
                        .font(.callout)
                        .offset(x: 4, y: 4)
                }
                ZoomableScrollView(name: "ISP", zoomScale: $zoomScale, contentOffset: $contentOffset) {
                    image?
                        .resizable()
                        .scaledToFit()
                        .accessibilityLabel(asset.accessibilityLabel)
                }
                .overlay(alignment: .topLeading) {
                    Text(verbatim: "ISP")
                        .foregroundColor(.white)
                        .padding(5)
                        .background(alignment: .center) {
                            RoundedRectangle(cornerRadius: 5, style: .continuous)
                                .fill(.black.opacity(0.5))
                        }
                        .font(.callout)
                        .offset(x: 4, y: 4)
                }
            }.onTapGesture(count: 2) {
                if zoomScale == 1 {
                    zoomScale = doubleTapZoomScale
                } else {
                    zoomScale = 1
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .toolbarColorScheme(.dark, for: .navigationBar)
        .background(Color.black)
        .navigationTitle("Comparison")
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
}
