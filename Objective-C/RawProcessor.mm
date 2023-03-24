//
//  NSObject+RawConverter.m
//  GlassCamera
//
//  Created by Fabio Riccardi on 3/22/23.
//

#import "RawProcessor.h"

#import <CoreGraphics/CGColorSpace.h>

#include <simd/simd.h>

#include "demosaic_mtl.hpp"
#include "gls_tiff_metadata.hpp"
#include "raw_converter.hpp"

#include "tinyicc.hpp"

std::unique_ptr<DemosaicParameters> unpackSonya6400RawImage(const gls::image<gls::luma_pixel_16>& inputImage,
                                                            gls::tiff_metadata* dng_metadata, gls::tiff_metadata* exif_metadata);

void saveImage(const gls::image<gls::rgba_pixel_float>& image, const std::string& path) {
    gls::image<gls::rgb_pixel_16> saveImage(image.width, image.height);
    saveImage.apply([&](gls::rgb_pixel_16* p, int x, int y) {
        const auto& pi = image[y][x];
        *p = {
            (uint8_t) (0xffff * pi.red),
            (uint8_t) (0xffff * pi.green),
            (uint8_t) (0xffff * pi.blue)
        };
    });
    saveImage.write_png_file(path);
}

std::unique_ptr<std::array<std::array<float, 3>, 3>> ICCProfileToXYZ(const CFStringRef colorSpaceName) {
    std::unique_ptr<std::array<std::array<float, 3>, 3>> matrix = nullptr;

    CGColorSpaceRef displayP3 = CGColorSpaceCreateWithName(colorSpaceName);
    CGColorSpaceRetain(displayP3);
    CFDataRef data = CGColorSpaceCopyICCData(displayP3);
    if ( !data ) {
        std::cout<<"Cannot get CGColorSpaceCopyICCProfile()"<<std::endl;
    } else {
        const UInt8* icc_data = CFDataGetBytePtr(data);
        const CFIndex icc_length = CFDataGetLength(data);

        TinyICC::Profile icc_profile;

        if (loadFromMem(icc_profile, icc_data, icc_length)) {
            matrix = std::make_unique<std::array<std::array<float, 3>, 3>>();
            *matrix = icc_profile.xyz_matrix();
        }

        CGColorSpaceRelease(displayP3);
    }

    return matrix;
}

@implementation RawProcessor : NSObject

static std::unique_ptr<RawConverter> _rawConverter = nullptr;

- (instancetype)init {
    if (self = [super init]) {
        // Initialize self
    }

    if (_rawConverter == nullptr) {
        std::cout << "Allocating new RawConvwerter instance." << std::endl;

        // Create RawConverter object
        auto metalDevice = NS::RetainPtr(MTL::CreateSystemDefaultDevice());
        _rawConverter = std::make_unique<RawConverter>(metalDevice);
    }
    return self;
}

- (NSString*) convertDngFile: (NSString*) path {
    std::string input_path = std::string([path UTF8String]);

    auto t_start = std::chrono::high_resolution_clock::now();

    gls::tiff_metadata dng_metadata, exif_metadata;
    const auto rawImage =
        gls::image<gls::luma_pixel_16>::read_dng_file(input_path, &dng_metadata, &exif_metadata);
    auto demosaicParameters = unpackSonya6400RawImage(*rawImage, &dng_metadata, &exif_metadata);

    auto t_end = std::chrono::high_resolution_clock::now();
    double elapsed_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    std::cout << "DNG file read time: " << (int)elapsed_time_ms
                  << "ms for image of size: " << rawImage->width << " x " << rawImage->height << std::endl;

    _rawConverter->buildTextures(rawImage->size());

    // Retrieve "Display P3" to XYZ transformation matrix
    const auto matrix = ICCProfileToXYZ(kCGColorSpaceDisplayP3);
    if (matrix) {
        std::cout << "Display P3 -> XYZ matrix: " << std::endl;
        for (int j = 0; j < 3; j++) {
            for (int i = 0; i < 3; i++) {
                std::cout << (*matrix)[j][i] << " ";
            }
            std::cout << std::endl;
        }
    }

    t_start = std::chrono::high_resolution_clock::now();

    auto srgbImage = _rawConverter->demosaic(*rawImage, demosaicParameters.get());

    t_end = std::chrono::high_resolution_clock::now();
    elapsed_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    std::cout << "Metal Pipeline Execution Time: " << (int)elapsed_time_ms
                  << "ms for image of size: " << rawImage->width << " x " << rawImage->height << std::endl;

    auto position = input_path.find_last_of(".dng");
    assert(position != std::string::npos);
    const auto output_path = input_path.replace(position - 3, 4, ".png");

    const auto srgbImageCpu = srgbImage->mapImage();
    saveImage(*srgbImageCpu, output_path);

    std::cout << "It all went very well..." << std::endl;

    return [NSString stringWithUTF8String:output_path.c_str()];
}

@end
