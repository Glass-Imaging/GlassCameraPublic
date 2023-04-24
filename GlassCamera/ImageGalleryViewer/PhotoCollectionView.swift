import SwiftUI
import os.log

struct PhotoCollectionView: View {
    @ObservedObject var photoCollection : PhotoCollection
    
    @Environment(\.displayScale) private var displayScale
        
    private static let itemSpacing = 4.0
    private static let itemCornerRadius = 0.0
    
    private static var itemSize: CGSize {
        // We want 2 items per row on the phone, make each item slightly smaller than half screen width
        let sideLength = 0.49 * UIScreen.main.bounds.width
        return CGSize(width: sideLength, height: sideLength)
    }
    
    private var imageSize: CGSize {
        return CGSize(width: Self.itemSize.width * min(displayScale, 2), height: Self.itemSize.height * min(displayScale, 2))
    }
    
    private let columns = [
        GridItem(.adaptive(minimum: itemSize.width, maximum: itemSize.height), spacing: itemSpacing)
    ]
    
    var body: some View {
        ScrollView {
            LazyVGrid(columns: columns, spacing: Self.itemSpacing) {
                ForEach(photoCollection.photoAssets) { asset in
                    NavigationLink {
                        let siblingAsset = photoCollection.photoAssets.getNeighborWithMatchingRootFileName(position: asset.index ?? 0) ?? asset
                        if (asset.photoCategory.company == PhotoCategories.GlassNN.company) {
                            PhotoView(assetA: asset, assetB: siblingAsset, cache: photoCollection.cache)
                        } else {
                            PhotoView(assetA: siblingAsset, assetB: asset, cache: photoCollection.cache)
                        }
                    } label: {
                        photoItemView(asset: asset)
                    }
                    .buttonStyle(.borderless)
                    .accessibilityLabel(asset.accessibilityLabel)
                }
            }
            .padding([.vertical], Self.itemSpacing)
        }
        .background(.black)
        .toolbarColorScheme(.dark, for: .navigationBar)
        .navigationTitle(photoCollection.albumName ?? "Gallery")
        .navigationBarTitleDisplayMode(.inline)
        .statusBar(hidden: false)
    }
    
    private func photoItemView(asset: PhotoAsset) -> some View {
        PhotoItemView(asset: asset, cache: photoCollection.cache, imageSize: imageSize)
            .frame(width: Self.itemSize.width, height: Self.itemSize.height)
            .clipped()
            .cornerRadius(Self.itemCornerRadius)
            .overlay(alignment: .bottomLeading) {
                Text(verbatim: asset.photoCategory.name)
                        .foregroundColor(.white)
                        .shadow(color: .black.opacity(0.3), radius: 5, x: 0, y: 1)
                        .font(.callout)
                        .offset(x: 4, y: -4)
            }
            .onAppear {
                Task {
                    await photoCollection.cache.startCaching(for: [asset], targetSize: imageSize)
                }
            }
            .onDisappear {
                Task {
                    await photoCollection.cache.stopCaching(for: [asset], targetSize: imageSize)
                }
            }
    }
}
