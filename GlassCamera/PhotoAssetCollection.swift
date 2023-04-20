/*
See the License.txt file for this sample’s licensing information.
*/

import Photos

class PhotoAssetCollection: RandomAccessCollection {
    private(set) var fetchResult: PHFetchResult<PHAsset>
    private var iteratorIndex: Int = 0
    
    private var cache = [Int : PhotoAsset]()
    
    var startIndex: Int { 0 }
    var endIndex: Int { fetchResult.count }
    
    init(_ fetchResult: PHFetchResult<PHAsset>) {
        self.fetchResult = fetchResult
    }
    
    subscript(position: Int) -> PhotoAsset {
        if let asset = cache[position] {
            return asset
        }
        let asset = PhotoAsset(phAsset: fetchResult.object(at: position), index: position)
        cache[position] = asset
        return asset
    }
    
    func getNeighborWithMatchingRootFileName(position: Int) -> PhotoAsset? {
        let targetRootFileName = self[position].rootFileName
        
        let lowerNeighborPosition = position - 1
        if (lowerNeighborPosition >= startIndex)  {
            if self[lowerNeighborPosition].rootFileName == targetRootFileName {
                return self[lowerNeighborPosition]
            }
        }
        
        let upperNeighborPosition = position + 1
        if (upperNeighborPosition < endIndex)  {
            if self[upperNeighborPosition].rootFileName == targetRootFileName {
                return self[upperNeighborPosition]
            }
        }
        
        return nil
    }
    
    var phAssets: [PHAsset] {
        var assets = [PHAsset]()
        fetchResult.enumerateObjects { (object, count, stop) in
            assets.append(object)
        }
        return assets
    }
}

extension PhotoAssetCollection: Sequence, IteratorProtocol {

    func next() -> PhotoAsset? {
        if iteratorIndex >= count {
            return nil
        }
        
        defer {
            iteratorIndex += 1
        }
        
        return self[iteratorIndex]
    }
}
