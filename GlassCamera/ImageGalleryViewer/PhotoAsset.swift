import Photos

struct PhotoAsset: Identifiable {
    var id: String { identifier }
    var identifier: String = UUID().uuidString
    var name: String
    var index: Int?
    var phAsset: PHAsset?
    
    var accessibilityLabel: String = "Photo"
    
    typealias MediaType = PHAssetMediaType
    
    var photoCategory: PhotoCategory {
        return PhotoCategories.getPhotoCategory(photoName: name)
    }
    
    var rootFileName: Substring {
        let end = name.index(name.endIndex, offsetBy: -1 * self.photoCategory.suffix.count)
        return name[name.startIndex..<end]
    }
    
    var mediaType: MediaType {
        phAsset?.mediaType ?? .unknown
    }
    

    init(phAsset: PHAsset, index: Int?) {
        self.phAsset = phAsset
        self.index = index
        self.identifier = phAsset.localIdentifier
        let resources = PHAssetResource.assetResources(for: self.phAsset!)
        if let name = resources.first?.originalFilename {
            self.name = name
            NSLog("Loading :: \(self.name)")
        } else {
            self.name = self.identifier
        }
    }
    
    init(identifier: String) {
        self.identifier = identifier
        let fetchedAssets = PHAsset.fetchAssets(withLocalIdentifiers: [identifier], options: nil)
        self.phAsset = fetchedAssets.firstObject
        let resources = PHAssetResource.assetResources(for: self.phAsset!)
        if let name = resources.first?.originalFilename {
            self.name = name
            NSLog("Loading :: \(self.name)")
        } else {
            self.name = self.identifier
        }
    }
}

extension PhotoAsset: Equatable {
    static func ==(lhs: PhotoAsset, rhs: PhotoAsset) -> Bool {
        (lhs.identifier == rhs.identifier) && (lhs.name == rhs.name)
    }
}

extension PhotoAsset: Hashable {
    func hash(into hasher: inout Hasher) {
        hasher.combine(identifier)
    }
}

extension PHObject: Identifiable {
    public var id: String { localIdentifier }
}
