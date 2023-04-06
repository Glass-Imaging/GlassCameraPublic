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

#include <filesystem>
#include <fstream>
#include <iostream>
#include <simd/simd.h>

#include "gls_tiff_metadata.hpp"
#include "raw_converter.hpp"
#include "tinyicc.hpp"

#include "CameraCalibration.hpp"

#include <sciplot/sciplot.hpp>
using namespace sciplot;

template <size_t histogram_size>
void plotHistogram(const std::array<uint32_t, histogram_size>& histogram, const std::string& image_name) {
    Vec vec(histogram.size());
    Vec x = linspace(0.0, 1.0, histogram_size);

    for (int i = 0; i < histogram.size(); i++) {
        vec[i] = histogram[i];
    }

    Plot2D plot;
    plot.drawCurve(x, vec).label("luma");

    Figure fig = {{plot}};
    Canvas canvas = {{fig}};
    canvas.title(image_name);

    canvas.show();
}

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

static std::vector<unsigned char> read_binary_file(const std::string filename) {
    std::ifstream file(filename, std::ios::binary);
    file.unsetf(std::ios::skipws);

    std::streampos file_size;
    file.seekg(0, std::ios::end);
    file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<unsigned char> vec;
    vec.reserve(file_size);
    vec.insert(vec.begin(),
               std::istream_iterator<unsigned char>(file),
               std::istream_iterator<unsigned char>());
    return (vec);
}

void demosaicFile(RawConverter* rawConverter, std::filesystem::path input_path) {
    std::cout << "Processing File: " << input_path.filename() << std::endl;

    gls::tiff_metadata dng_metadata, exif_metadata;
    const auto rawImage =
    gls::image<gls::luma_pixel_16>::read_dng_file(input_path.string(), &dng_metadata, &exif_metadata);
    auto demosaicParameters = unpackiPhoneRawImage(*rawImage, rawConverter->xyz_rgb(), &dng_metadata, &exif_metadata);

    rawConverter->allocateTextures(rawImage->size());

    auto t_start = std::chrono::high_resolution_clock::now();

    auto srgbImage = rawConverter->demosaic(*rawImage, demosaicParameters.get());

    auto t_end = std::chrono::high_resolution_clock::now();
    double elapsed_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    std::cout << "Metal Pipeline Execution Time: " << (int)elapsed_time_ms
    << "ms for image of size: " << rawImage->width << " x " << rawImage->height << std::endl;

    const auto output_path = input_path.replace_extension("_b.png");

    const auto srgbImageCpu = srgbImage->mapImage();
    saveImage<gls::rgb_pixel_16>(*srgbImageCpu, output_path.string(), rawConverter->icc_profile_data());

    auto histogramData = rawConverter->histogramData();
    // plotHistogram(histogramData->histogram, input_path.filename().string());

    int imageSize = rawImage->width * rawImage->height;
    std::cout << "bands: ";
    for (int i = 0; i < 8; i++) {
        std::cout << histogramData->bands[i] / (float) imageSize << ", ";
    }
    std::cout << std::endl;

    std::cout << "black_level: " << histogramData->black_level << ", white_level: " << histogramData->white_level << std::endl;
    std::cout << "shadows: " << histogramData->shadows << ", highlights: " << histogramData->highlights << std::endl;
}

void demosaicDirectory(RawConverter* rawConverter, std::filesystem::path input_path) {
    std::cout << "Processing Directory: " << input_path.filename() << std::endl;

    auto input_dir = std::filesystem::directory_entry(input_path).is_directory() ? input_path : input_path.parent_path();
    std::vector<std::filesystem::path> directory_listing;
    std::copy(std::filesystem::directory_iterator(input_dir), std::filesystem::directory_iterator(),
              std::back_inserter(directory_listing));
    std::sort(directory_listing.begin(), directory_listing.end());

    for (const auto& input_path : directory_listing) {
        if (input_path.filename().string().starts_with(".")) {
            continue;
        }

        if (std::filesystem::directory_entry(input_path).is_regular_file()) {
            const auto extension = input_path.extension();
            if ((extension != ".dng" && extension != ".DNG")) {
                continue;
            }
            demosaicFile(rawConverter, input_path);
        } else if (std::filesystem::directory_entry(input_path).is_directory()) {
            demosaicDirectory(rawConverter, input_path);
        }
    }
}

int main(int argc, const char * argv[]) {
    // Read ICC color profile data
    auto icc_profile_data = read_binary_file("/System/Library/ColorSync/Profiles/Display P3.icc");

    auto allMetalDevices = NS::TransferPtr(MTL::CopyAllDevices());
    auto metalDevice = NS::RetainPtr(allMetalDevices->object<MTL::Device>(0));

    RawConverter rawConverter(metalDevice, &icc_profile_data);

    if (argc > 1) {
        auto input_path = std::filesystem::path(argv[1]);

        // demosaicFile(&rawConverter, input_path);

        demosaicDirectory(&rawConverter, input_path);

        return 0;
    } else {
        std::cout << "We need a file name..." << std::endl;
        return -1;
    }
}
