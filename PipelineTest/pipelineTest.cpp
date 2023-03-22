//
//  main.cpp
//  PipelineTest
//
//  Created by Fabio Riccardi on 3/17/23.
//

#include <filesystem>
#include <iostream>
#include <simd/simd.h>

#include "demosaic_mtl.hpp"
#include "gls_tiff_metadata.hpp"
#include "raw_converter.hpp"

std::unique_ptr<DemosaicParameters> unpackSonya6400RawImage(const gls::image<gls::luma_pixel_16>& inputImage,
                                                            gls::tiff_metadata* dng_metadata, gls::tiff_metadata* exif_metadata);

template <typename T>
void dumpGradientImage(const gls::mtl_image_2d<T>& image, const std::string& path) {
    gls::image<gls::rgb_pixel> out(image.width, image.height);
    const auto image_cpu = image.mapImage();
    out.apply([&](gls::rgb_pixel* p, int x, int y) {
        const auto& ip = (*image_cpu)[y][x];

        // float direction = (1 + atan2(ip.y, ip.x) / M_PI) / 2;
        // float direction = atan2(abs(ip.y), ip.x) / M_PI;
        float direction = std::atan2(std::abs(ip.y), std::abs(ip.x)) / M_PI_2;
        float magnitude = std::sqrt((float)(ip.x * ip.x + ip.y * ip.y));

        uint8_t val = std::clamp(255 * std::sqrt(magnitude), 0.0f, 255.0f);

        *p = gls::rgb_pixel{
            (uint8_t)(val * std::lerp(1.0f, 0.0f, direction)),
            0,
            (uint8_t)(val * std::lerp(1.0f, 0.0f, 1 - direction)),
        };
    });
    out.write_png_file(path);
}

void saveImage(const gls::image<gls::rgba_pixel_float>& image, const std::string& path) {
    gls::image<gls::rgb_pixel> saveImage(image.width, image.height);
    saveImage.apply([&](gls::rgb_pixel* p, int x, int y) {
        const auto& pi = image[y][x];
        *p = {
            (uint8_t) (255 * pi.red),
            (uint8_t) (255 * pi.green),
            (uint8_t) (255 * pi.blue)
        };
    });
    saveImage.write_png_file(path);
}

int main(int argc, const char * argv[]) {
    if (argc > 1) {
        auto input_path = std::filesystem::path(argv[1]);

        gls::tiff_metadata dng_metadata, exif_metadata;
        const auto rawImage =
        gls::image<gls::luma_pixel_16>::read_dng_file(input_path.string(), &dng_metadata, &exif_metadata);
        auto demosaicParameters = unpackSonya6400RawImage(*rawImage, &dng_metadata, &exif_metadata);

        auto allMetalDevices = NS::TransferPtr(MTL::CopyAllDevices());
        auto metalDevice = NS::RetainPtr(allMetalDevices->object<MTL::Device>(0));

        RawConverter rawConverter(metalDevice);
        rawConverter.buildTextures(rawImage->size());

        auto t_start = std::chrono::high_resolution_clock::now();

        auto srgbImage = rawConverter.demosaic(*rawImage, *demosaicParameters);

        auto t_end = std::chrono::high_resolution_clock::now();
        double elapsed_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

        std::cout << "Metal Pipeline Execution Time: " << (int)elapsed_time_ms
                      << "ms for image of size: " << rawImage->width << " x " << rawImage->height << std::endl;

        const auto srgbImageCpu = srgbImage->mapImage();
        saveImage(*srgbImageCpu, "/Users/fabio/srgbImage.png");

        std::cout << "It all went very well..." << std::endl;
        return 0;
    } else {
        std::cout << "We need a file name..." << std::endl;
        return -1;
    }
}
