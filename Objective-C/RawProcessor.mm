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

#import "RawProcessor.h"

#import <CoreGraphics/CGColorSpace.h>
#import <CoreImage/CoreImage.h>

#include <simd/simd.h>

#include "gls_tiff_metadata.hpp"
#include "raw_converter.hpp"

#include "CameraCalibration.hpp"

#include "CoreMLSupport.h"

std::vector<uint8_t> ICCProfileData(const CFStringRef colorSpaceName) {
    CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(colorSpaceName);
    if (colorSpace) {
        CGColorSpaceRetain(colorSpace);
        CFDataRef data = CGColorSpaceCopyICCData(colorSpace);

        const UInt8* icc_data = CFDataGetBytePtr(data);
        const CFIndex icc_length = CFDataGetLength(data);

        auto icc_profile_data = std::vector<uint8_t>((uint8_t*) icc_data, (uint8_t*) icc_data + icc_length);

        CGColorSpaceRelease(colorSpace);

        return icc_profile_data;
    }

    throw std::runtime_error("Cannot get CGColorSpaceCopyICCProfile()");
}

@implementation RawMetadata : NSObject

@end

@implementation RawProcessor : NSObject

static std::unique_ptr<RawConverter> _rawConverter = nullptr;

- (instancetype) init
{
    if (self = [super init]) {
        // Initialize self
    }

    if (_rawConverter == nullptr) {
        std::cout << "Allocating new RawConvwerter instance." << std::endl;

        auto icc_profile_data = ICCProfileData(kCGColorSpaceDisplayP3);

        // Create RawConverter object
        auto metalDevice = NS::RetainPtr(MTL::CreateSystemDefaultDevice());
        _rawConverter = std::make_unique<RawConverter>(metalDevice, &icc_profile_data);
    }
    return self;
}

/*
 Note: This really fast but it is just a wrapper around the gls::image data, which itself wraps a MTL::Buffer
       This method is not reentrant and the pipeline should not be invoked till the PixelBuffer is released
 */
CVPixelBufferRef CVPixelBufferFromFP16ImageBytes(const gls::image<gls::pixel_fp16_4>& image) {
    auto bytesPerRow = image.stride * sizeof(gls::pixel_fp16_4);
    CVPixelBufferRef pixelBuffer = nullptr;
    CVReturn ret = CVPixelBufferCreateWithBytes(kCFAllocatorDefault, image.width, image.height, kCVPixelFormatType_64RGBAHalf,
                                                image.pixels().data(), bytesPerRow, nullptr, nullptr, nullptr, &pixelBuffer);
    assert(ret == kCVReturnSuccess);
    return pixelBuffer;
}

// Not used, in case we want to make a copy of the pixelBuffer data
template <typename pixel_type>
CVPixelBufferRef buildCVPixelBuffer(const gls::image<gls::pixel_float4>& rgbImage) {
    typename pixel_type::value_type max_value =
        std::is_same<pixel_type, gls::pixel_fp16_4>::value ? 1.0 :
            std::numeric_limits<typename pixel_type::value_type>::max();

    OSType pixelFormatType;
    if (std::is_same<pixel_type, gls::rgb_pixel>::value) {
        pixelFormatType = kCVPixelFormatType_24RGB;
    } else if (std::is_same<pixel_type, gls::rgba_pixel_16>::value) {
        pixelFormatType = kCVPixelFormatType_64RGBALE;
    } else if (std::is_same<pixel_type, gls::pixel_fp16_4>::value) {
        pixelFormatType = kCVPixelFormatType_64RGBAHalf;
    } else {
        std::cerr << "Unexpected pixel type." << std::endl;
        return nullptr;
    }

    CVPixelBufferRef pixelBuffer = nullptr;
    CVReturn ret = CVPixelBufferCreate(kCFAllocatorDefault, rgbImage.width, rgbImage.height, pixelFormatType, nullptr, &pixelBuffer);
    if (ret == kCVReturnSuccess) {
        CVPixelBufferLockBaseAddress(pixelBuffer, 0);
        size_t width = CVPixelBufferGetWidth(pixelBuffer);
        size_t height = CVPixelBufferGetHeight(pixelBuffer);
        size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
        size_t stride = (int) bytesPerRow / sizeof(pixel_type);
        UInt32* data = (UInt32*)CVPixelBufferGetBaseAddress(pixelBuffer);

        auto pixelBufferImage = gls::image<pixel_type>((int) width, (int) height, (int) stride, std::span((pixel_type*) data, stride * height));

        if (pixel_type::channels == 3) {
            pixelBufferImage.apply([&] (pixel_type* p, int x, int y) {
                const auto ip = rgbImage[y][x];
                *p = pixel_type {
                    (typename pixel_type::value_type) (max_value * ip.red),
                    (typename pixel_type::value_type) (max_value * ip.green),
                    (typename pixel_type::value_type) (max_value * ip.blue)
                };
            });
        } else {
            pixelBufferImage.apply([&] (pixel_type* p, int x, int y) {
                const auto ip = rgbImage[y][x];
                *p = pixel_type {
                    (typename pixel_type::value_type) (max_value * ip.red),
                    (typename pixel_type::value_type) (max_value * ip.green),
                    (typename pixel_type::value_type) (max_value * ip.blue),
                    max_value
                };
            });
        }

        CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
    }
    return pixelBuffer;
}

- (CVPixelBufferRef) convertRawPixelBuffer: (CVPixelBufferRef) rawPixelBuffer withMetadata: (RawMetadata*) metadata
{
    CVPixelBufferLockBaseAddress(rawPixelBuffer, 0);
    size_t width = CVPixelBufferGetWidth(rawPixelBuffer);
    size_t height = CVPixelBufferGetHeight(rawPixelBuffer);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(rawPixelBuffer);
    size_t stride = (int) bytesPerRow / sizeof(gls::luma_pixel_16);
    gls::luma_pixel_16* pixelBufferData = (gls::luma_pixel_16*) CVPixelBufferGetBaseAddress(rawPixelBuffer);

    auto pixelFormatType = CVPixelBufferGetPixelFormatType(rawPixelBuffer);
    std::vector<uint8_t> cfaPattern = { 0, 1, 1, 2 };
    switch (pixelFormatType) {
        case kCVPixelFormatType_14Bayer_GRBG:
            cfaPattern = { 1, 0, 2, 1 };
            break;
        case kCVPixelFormatType_14Bayer_RGGB:
            cfaPattern = { 0, 1, 1, 2 };
            break;
        case kCVPixelFormatType_14Bayer_BGGR:
            cfaPattern = { 2, 1, 1, 0 };
            break;
        case kCVPixelFormatType_14Bayer_GBRG:
            cfaPattern = { 1, 2, 0, 1 };
            break;
        default:
            std::cerr << "Unrecognized Pixel Format Type: " << pixelFormatType << ", defaulting to RGGB." << std::endl;
            cfaPattern = { 0, 1, 1, 2 };
            break;
    }

    auto rawImage = gls::image<gls::luma_pixel_16>((int) width, (int) height, (int) stride, std::span(pixelBufferData, stride * height));

    gls::tiff_metadata dng_metadata, exif_metadata;

    // float exposureBiasValue = [metadata exposureBiasValue];
    float baselineExposure = [metadata baselineExposure];
    float exposureTime = [metadata exposureTime];
    int isoSpeedRating = [metadata isoSpeedRating];
    int blackLevel = [metadata blackLevel];
    int whiteLevel = [metadata whiteLevel];
    // int calibrationIlluminant1 = [metadata calibrationIlluminant1];
    // int calibrationIlluminant2 = [metadata calibrationIlluminant2];
    // NSArray<NSNumber*>* colorMatrix1 = [metadata colorMatrix1];
    NSArray<NSNumber*>* colorMatrix2 = [metadata colorMatrix2];
    NSArray<NSNumber*>* asShotNeutral = [metadata asShotNeutral];
    // NSArray<NSNumber*>* noiseProfile = [metadata noiseProfile];

    std::vector<float> as_shot_neutral(3);
    for (int i = 0; i < 3; i++) {
        as_shot_neutral[i] = [asShotNeutral[i] floatValue];
    }

    std::vector<float> color_matrix(9);
    for (int i = 0; i < 9; i++) {
        color_matrix[i] = [colorMatrix2[i] floatValue];
    }

    // Basic DNG image interpretation metadata
    dng_metadata.insert({ TIFFTAG_COLORMATRIX1, color_matrix });
    dng_metadata.insert({ TIFFTAG_ASSHOTNEUTRAL, as_shot_neutral });

    dng_metadata.insert({ TIFFTAG_BASELINEEXPOSURE, baselineExposure });
    dng_metadata.insert({ TIFFTAG_CFAREPEATPATTERNDIM, std::vector<uint16_t>{ 2, 2 } });
    dng_metadata.insert({ TIFFTAG_CFAPATTERN, cfaPattern });
    dng_metadata.insert({ TIFFTAG_BLACKLEVEL, std::vector<float>{ (float) blackLevel } });
    dng_metadata.insert({ TIFFTAG_WHITELEVEL, std::vector<uint32_t>{ (uint32_t) whiteLevel } });

    // Basic EXIF metadata
    exif_metadata.insert({ EXIFTAG_ISOSPEEDRATINGS, std::vector<uint16_t>{ (uint16_t) isoSpeedRating } });
    exif_metadata.insert({ EXIFTAG_EXPOSURETIME, std::vector<float>{ (float) exposureTime } });

    // FIXME: we need to select the right parameters for the right camera
    auto demosaicParameters = unpackiPhoneRawImage(rawImage, _rawConverter->xyz_rgb(), &dng_metadata, &exif_metadata);

    auto t_metal_start = std::chrono::high_resolution_clock::now();

    auto rgbImage = _rawConverter->demosaic(rawImage, demosaicParameters.get());

    // All done with rawImage, release rawPixelBuffer
    CVPixelBufferUnlockBaseAddress(rawPixelBuffer, 0);

    auto t_metal_end = std::chrono::high_resolution_clock::now();
    auto elapsed_time_ms = std::chrono::duration<double, std::milli>(t_metal_end - t_metal_start).count();

    std::cout << "Metal Pipeline Execution Time: " << (int)elapsed_time_ms << std::endl;

    const auto& rgbImageCPU = rgbImage->mapImage();
    CVPixelBufferRef pixelBuffer = CVPixelBufferFromFP16ImageBytes(*rgbImageCPU);

    return pixelBuffer;
}

- (CVPixelBufferRef) nnProcessRawPixelBuffer: (CVPixelBufferRef) rawPixelBuffer withMetadata: (RawMetadata*) metadata {
    // Unpack the input pixelBuffer
    CVPixelBufferLockBaseAddress(rawPixelBuffer, 0);
    size_t width = CVPixelBufferGetWidth(rawPixelBuffer);
    size_t height = CVPixelBufferGetHeight(rawPixelBuffer);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(rawPixelBuffer);
    size_t stride = (int) bytesPerRow / sizeof(uint16_t);
    gls::luma_pixel_16* pixelBufferData = (gls::luma_pixel_16*) CVPixelBufferGetBaseAddress(rawPixelBuffer);

    // Wrap the input pixel buffer into an image
    auto rawImage = gls::image<gls::luma_pixel_16>((int) width, (int) height, (int) stride, std::span(pixelBufferData, stride * height));

    // Extract some metadata
    float baselineExposure = [metadata baselineExposure];
    float exposureTime = [metadata exposureTime];
    int isoSpeedRating = [metadata isoSpeedRating];
    int blackLevel = [metadata blackLevel];
    int whiteLevel = [metadata whiteLevel];
    // int calibrationIlluminant1 = [metadata calibrationIlluminant1];
    // int calibrationIlluminant2 = [metadata calibrationIlluminant2];
    NSArray<NSNumber*>* colorMatrix1 = [metadata colorMatrix1];
    NSArray<NSNumber*>* colorMatrix2 = [metadata colorMatrix2];
    NSArray<NSNumber*>* asShotNeutral = [metadata asShotNeutral];

    std::vector<float> as_shot_neutral(3);
    for (int i = 0; i < 3; i++) {
        as_shot_neutral[i] = [asShotNeutral[i] floatValue];
    }

    std::vector<float> color_matrix(9);
    for (int i = 0; i < 9; i++) {
        color_matrix[i] = [colorMatrix2[i] floatValue];
    }

    gls::tiff_metadata dng_metadata, exif_metadata;

    // Basic DNG image interpretation metadata
    dng_metadata.insert({ TIFFTAG_COLORMATRIX1, color_matrix });
    dng_metadata.insert({ TIFFTAG_ASSHOTNEUTRAL, as_shot_neutral });

    dng_metadata.insert({ TIFFTAG_BASELINEEXPOSURE, baselineExposure });
    dng_metadata.insert({ TIFFTAG_CFAREPEATPATTERNDIM, std::vector<uint16_t>{ 2, 2 } });
    std::vector<uint8_t> cfaPattern = { 0, 1, 1, 2 };
    dng_metadata.insert({ TIFFTAG_CFAPATTERN, cfaPattern });
    dng_metadata.insert({ TIFFTAG_BLACKLEVEL, std::vector<float>{ (float) blackLevel } });
    dng_metadata.insert({ TIFFTAG_WHITELEVEL, std::vector<uint32_t>{ (uint32_t) whiteLevel } });

    // Basic EXIF metadata
    exif_metadata.insert({ EXIFTAG_ISOSPEEDRATINGS, std::vector<uint16_t>{ (uint16_t) isoSpeedRating } });
    exif_metadata.insert({ EXIFTAG_EXPOSURETIME, std::vector<float>{ (float) exposureTime } });

    // FIXME: we need to select the right parameters for the right camera
    auto demosaicParameters = unpackiPhone14TeleFEMNRawImage(rawImage, _rawConverter->xyz_rgb(), &dng_metadata, &exif_metadata);

    float exposureMultiplier = pow(2.0, baselineExposure);

    std::cout << "blackLevel: " << blackLevel << ", whiteLevel: " << whiteLevel
              << ", baselineExposure: " << baselineExposure << "EV - " << exposureMultiplier
              << " scale, isoSpeedRating: " << isoSpeedRating << ", exposureTime: " << exposureTime << std::endl;

    // Result Image
    gls::image<gls::pixel_fp16_4> processedImage((int) width, (int) height);

    // Apply model to image
    fmenApplyToImage(rawImage, whiteLevel, &processedImage);

    auto srgbImage = _rawConverter->postprocess(processedImage, demosaicParameters.get());
    const auto srgbImageCpu = srgbImage->mapImage();

    // All done with rawImage, release rawPixelBuffer
    CVPixelBufferUnlockBaseAddress(rawPixelBuffer, 0);

    // Copy the result data to a pixelBuffer
    CVPixelBufferRef pixelBuffer = buildCVPixelBuffer<gls::pixel_fp16_4>(*srgbImageCpu);

    return pixelBuffer;
}

@end
