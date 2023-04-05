//
//  NSObject+RawConverter.m
//  GlassCamera
//
//  Created by Fabio Riccardi on 3/22/23.
//

#import "RawProcessor.h"

#import <CoreGraphics/CGColorSpace.h>
#import <CoreImage/CoreImage.h>

#include <simd/simd.h>

#include "gls_tiff_metadata.hpp"
#include "raw_converter.hpp"

#include "CameraCalibration.hpp"

template <typename pixel_type>
void saveImage(const gls::image<gls::rgba_pixel_float>& image, const std::string& path,
               const std::vector<uint8_t>* icc_profile_data) {
    gls::image<pixel_type> saveImage(image.width, image.height);
    saveImage.apply([&](pixel_type* p, int x, int y) {
        float scale = std::numeric_limits<typename pixel_type::value_type>::max();

        const auto& pi = image[y][x];
        *p = {
            (uint8_t) (scale * pi.red),
            (uint8_t) (scale * pi.green),
            (uint8_t) (scale * pi.blue)
        };
    });
    saveImage.write_png_file(path, /*skip_alpha=*/true, icc_profile_data);
}

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

- (instancetype)init {
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

- (NSString*) convertDngFile: (NSString*) path {
    std::string input_path = std::string([path UTF8String]);

    auto t_start = std::chrono::high_resolution_clock::now();

    gls::tiff_metadata dng_metadata, exif_metadata;
    const auto rawImage =
        gls::image<gls::luma_pixel_16>::read_dng_file(input_path, &dng_metadata, &exif_metadata);
    auto demosaicParameters = unpackiPhoneRawImage(*rawImage, _rawConverter->xyz_rgb(), &dng_metadata, &exif_metadata);

    auto t_dng_end = std::chrono::high_resolution_clock::now();
    double elapsed_time_ms = std::chrono::duration<double, std::milli>(t_dng_end - t_start).count();

    std::cout << "DNG file read time: " << (int)elapsed_time_ms << std::endl;

    _rawConverter->allocateTextures(rawImage->size());

    auto t_metal_start = std::chrono::high_resolution_clock::now();

    auto srgbImage = _rawConverter->demosaic(*rawImage, demosaicParameters.get());

    auto t_metal_end = std::chrono::high_resolution_clock::now();
    elapsed_time_ms = std::chrono::duration<double, std::milli>(t_metal_end - t_metal_start).count();

    std::cout << "Metal Pipeline Execution Time: " << (int)elapsed_time_ms << std::endl;

    auto position = input_path.find_last_of(".dng");
    assert(position != std::string::npos);
    const auto output_path = input_path.replace(position - 3, 4, ".png");

    const auto srgbImageCpu = srgbImage->mapImage();
    saveImage<gls::rgb_pixel>(*srgbImageCpu, output_path, _rawConverter->icc_profile_data());

    auto t_end = std::chrono::high_resolution_clock::now();
    elapsed_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    std::cout << "Total Pipeline Execution Time: " << (int)elapsed_time_ms << std::endl;

    return [NSString stringWithUTF8String:output_path.c_str()];
}

/*
 Note: This really fast but it is just a wrapper around the gls::image data, which itself wraps a MTL::Buffer
       This method is not reentrant and the pipeline should not be invoked till the PixelBuffer is released
 */
CVPixelBufferRef CVPixelBufferFromFP16ImageBytes(const gls::image<gls::rgba_pixel_fp16>& image) {
    auto bytesPerRow = image.stride * sizeof(gls::rgba_pixel_fp16);
    CVPixelBufferRef pixelBuffer = nullptr;
    CVReturn ret = CVPixelBufferCreateWithBytes(kCFAllocatorDefault, image.width, image.height, kCVPixelFormatType_64RGBAHalf,
                                                image.pixels().data(), bytesPerRow, nullptr, nullptr, nullptr, &pixelBuffer);
    assert(ret == kCVReturnSuccess);
    return pixelBuffer;
}

template <typename pixel_type>
CVPixelBufferRef buildCVPixelBuffer(const gls::image<gls::rgba_pixel_float>& rgbImage) {
    typename pixel_type::value_type max_value =
        std::is_same<pixel_type, gls::rgba_pixel_fp16>::value ? 1.0 :
            std::numeric_limits<typename pixel_type::value_type>::max();

    OSType pixelFormatType;
    if (std::is_same<pixel_type, gls::rgb_pixel>::value) {
        pixelFormatType = kCVPixelFormatType_24RGB;
    } else if (std::is_same<pixel_type, gls::rgba_pixel_16>::value) {
        pixelFormatType = kCVPixelFormatType_64RGBALE;
    } else if (std::is_same<pixel_type, gls::rgba_pixel_fp16>::value) {
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

- (CVPixelBufferRef) CVPixelBufferFromDngFile: (NSString*) path {
    std::string input_path = std::string([path UTF8String]);

    auto t_start = std::chrono::high_resolution_clock::now();

    gls::tiff_metadata dng_metadata, exif_metadata;
    const auto rawImage =
        gls::image<gls::luma_pixel_16>::read_dng_file(input_path, &dng_metadata, &exif_metadata);
    auto demosaicParameters = unpackiPhoneRawImage(*rawImage, _rawConverter->xyz_rgb(), &dng_metadata, &exif_metadata);

    auto t_dng_end = std::chrono::high_resolution_clock::now();
    double elapsed_time_ms = std::chrono::duration<double, std::milli>(t_dng_end - t_start).count();

    std::cout << "DNG file read time: " << (int)elapsed_time_ms << std::endl;

    _rawConverter->allocateTextures(rawImage->size());

    auto t_metal_start = std::chrono::high_resolution_clock::now();

    auto rgbImage = _rawConverter->demosaic(*rawImage, demosaicParameters.get());

    auto t_metal_end = std::chrono::high_resolution_clock::now();
    elapsed_time_ms = std::chrono::duration<double, std::milli>(t_metal_end - t_metal_start).count();

    std::cout << "Metal Pipeline Execution Time: " << (int)elapsed_time_ms << std::endl;

    /*
     Supported image types are:
         gls::rgb_pixel
         gls::rgba_pixel_16
         gls::rgba_pixel_fp16
     */

    t_start = std::chrono::high_resolution_clock::now();

    const auto& rgbImageCPU = rgbImage->mapImage();
    // CVPixelBufferRef pixelBuffer = buildCVPixelBuffer<gls::rgba_pixel_16>(*rgbImageCPU);
    CVPixelBufferRef pixelBuffer = CVPixelBufferFromFP16ImageBytes(*rgbImageCPU);

    auto t_end = std::chrono::high_resolution_clock::now();
    elapsed_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    std::cout << "CVPixelBuffer Creation Time: " << (int)elapsed_time_ms << std::endl;

    return pixelBuffer;
}

- (CVPixelBufferRef) convertRawPixelBuffer: (CVPixelBufferRef) rawPixelBuffer withMetadata: (RawMetadata*) metadata {

    CVPixelBufferLockBaseAddress(rawPixelBuffer, 0);
    size_t width = CVPixelBufferGetWidth(rawPixelBuffer);
    size_t height = CVPixelBufferGetHeight(rawPixelBuffer);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(rawPixelBuffer);
    size_t stride = (int) bytesPerRow / sizeof(gls::luma_pixel_16);
    gls::luma_pixel_16* data = (gls::luma_pixel_16*) CVPixelBufferGetBaseAddress(rawPixelBuffer);

    auto rawImage = gls::image<gls::luma_pixel_16>((int) width, (int) height, (int) stride, std::span(data, stride * height));

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
    dng_metadata.insert({ TIFFTAG_CFAPATTERN, std::vector<uint8_t>{ 0, 1, 1, 2 } });
    dng_metadata.insert({ TIFFTAG_BLACKLEVEL, std::vector<float>{ (float) blackLevel } });
    dng_metadata.insert({ TIFFTAG_WHITELEVEL, std::vector<uint32_t>{ (uint32_t) whiteLevel } });

    // Basic EXIF metadata
    exif_metadata.insert({ EXIFTAG_ISOSPEEDRATINGS, std::vector<uint16_t>{ (uint16_t) isoSpeedRating } });
    exif_metadata.insert({ EXIFTAG_EXPOSURETIME, std::vector<float>{ (float) exposureTime } });

    auto demosaicParameters = unpackiPhoneRawImage(rawImage, _rawConverter->xyz_rgb(), &dng_metadata, &exif_metadata);

    _rawConverter->allocateTextures(rawImage.size());

    auto t_metal_start = std::chrono::high_resolution_clock::now();

    auto rgbImage = _rawConverter->demosaic(rawImage, demosaicParameters.get());

    CVPixelBufferUnlockBaseAddress(rawPixelBuffer, 0);

    auto t_metal_end = std::chrono::high_resolution_clock::now();
    auto elapsed_time_ms = std::chrono::duration<double, std::milli>(t_metal_end - t_metal_start).count();

    std::cout << "Metal Pipeline Execution Time: " << (int)elapsed_time_ms << std::endl;

    const auto& rgbImageCPU = rgbImage->mapImage();
    CVPixelBufferRef pixelBuffer = CVPixelBufferFromFP16ImageBytes(*rgbImageCPU);

    return pixelBuffer;
}

@end
