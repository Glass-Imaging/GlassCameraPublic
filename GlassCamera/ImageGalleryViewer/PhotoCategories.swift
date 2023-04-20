//
//  PhotoCategories.swift
//  GlassCamera
//
//  Created by Doug MacEwen on 4/20/23.
//

import Foundation

struct PhotoCategory {
    let name: String
    let suffix: String
    let company: String
}

extension PhotoCategory: Equatable {
    static func ==(lhs: PhotoCategory, rhs: PhotoCategory) -> Bool {
        (lhs.name == rhs.name) && (lhs.suffix == rhs.suffix)
    }
}

struct PhotoCategories {
    static private let glass = "GlassImaging"
    
    static let GlassNN =  PhotoCategory(name: "GlassNN", suffix: "_GLS", company: glass)
    static let GlassTraditional = PhotoCategory(name: "Glass Traditional", suffix: "_GLST", company: glass)
    static let ISP =  PhotoCategory(name: "ISP", suffix: "_ISP", company: "Apple")
    static let Other = PhotoCategory(name: "Other", suffix: "", company: "Unknown")
    
    static func getPhotoCategory(photoName: String) -> PhotoCategory {
        if photoName.hasSuffix(GlassNN.suffix) { return GlassNN }
        if photoName.hasSuffix(GlassTraditional.suffix) { return GlassTraditional }
        if photoName.hasSuffix(ISP.suffix) { return ISP }
        return Other
    }
}
