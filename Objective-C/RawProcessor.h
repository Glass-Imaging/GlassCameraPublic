//
//  NSObject+RawConverter.h
//  GlassCamera
//
//  Created by Fabio Riccardi on 3/22/23.
//

#import <Foundation/Foundation.h>
#import <CoreImage/CoreImage.h>

NS_ASSUME_NONNULL_BEGIN

@interface RawMetadata : NSObject

@property float exposureBiasValue;
@property float baselineExposure;

@property float exposureTime;

@property int isoSpeedRating;
@property int blackLevel;
@property int whiteLevel;
@property int calibrationIlluminant1;
@property int calibrationIlluminant2;

@property NSArray<NSNumber*>* colorMatrix1;
@property NSArray<NSNumber*>* colorMatrix2;

@property NSArray<NSNumber*>* asShotNeutral;
@property NSArray<NSNumber*>* noiseProfile;

@end

@interface RawProcessor : NSObject

- (NSString*) convertDngFile: (NSString*) path;

- (CVPixelBufferRef) CVPixelBufferFromDngFile: (NSString*) path;

- (CVPixelBufferRef) convertRawPixelBuffer: (CVPixelBufferRef) rawPixelBuffer withMetadata: (RawMetadata*) metadata;

@end

NS_ASSUME_NONNULL_END
