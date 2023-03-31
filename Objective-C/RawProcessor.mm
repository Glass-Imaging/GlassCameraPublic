//
//  NSObject+RawConverter.m
//  GlassCamera
//
//  Created by Fabio Riccardi on 3/22/23.
//

#import "RawProcessor.h"

#import <CoreGraphics/CGColorSpace.h>

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

@end
