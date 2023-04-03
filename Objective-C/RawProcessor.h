//
//  NSObject+RawConverter.h
//  GlassCamera
//
//  Created by Fabio Riccardi on 3/22/23.
//

#import <Foundation/Foundation.h>
#import <CoreImage/CoreImage.h>

NS_ASSUME_NONNULL_BEGIN

@interface RawProcessor : NSObject

- (NSString*) convertDngFile: (NSString*) path;

- (CVPixelBufferRef) CVPixelBufferFromDngFile: (NSString*) path;

@end

NS_ASSUME_NONNULL_END
