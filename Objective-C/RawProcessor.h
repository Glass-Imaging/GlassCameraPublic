// Copyright (c) 2021-2023 Glass Imaging Inc.
// Author: Fabio Riccardi <fabio@glass-imaging.com>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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

- (CVPixelBufferRef) convertRawPixelBuffer: (CVPixelBufferRef) rawPixelBuffer withMetadata: (RawMetadata*) metadata;

- (CVPixelBufferRef) nnProcessRawPixelBuffer: (CVPixelBufferRef) rawPixelBuffer withMetadata: (RawMetadata*) metadata;

@end

NS_ASSUME_NONNULL_END
