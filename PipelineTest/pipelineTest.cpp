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

#include "CoreMLSupport.h"

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
               gls::tiff_metadata* metadata,
               const std::vector<uint8_t>* icc_profile_data, float exposure_multiplier = 1.0) {
    gls::image<pixel_type> saveImage(image.width, image.height);
    saveImage.apply([&](pixel_type* p, int x, int y) {
        float scale = std::numeric_limits<typename pixel_type::value_type>::max();

        const auto& pi = image[y][x];
        *p = {
            (uint8_t) std::clamp(scale * exposure_multiplier * pi.red, 0.0f, scale),
            (uint8_t) std::clamp(scale * exposure_multiplier * pi.green, 0.0f, scale),
            (uint8_t) std::clamp(scale * exposure_multiplier * pi.blue, 0.0f, scale)
        };
    });
    // saveImage.write_png_file(path, /*skip_alpha=*/true, icc_profile_data);
    saveImage.write_tiff_file(path, gls::tiff_compression::NONE, metadata, icc_profile_data);
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

    std::string make, model, lens_model;
    if (!getValue(dng_metadata, TIFFTAG_MAKE, &make)) {
        std::cout << "No make?" << std::endl;
    }
    if (!getValue(dng_metadata, TIFFTAG_MODEL, &model)) {
        std::cout << "No Model?" << std::endl;
    }
    if (!getValue(exif_metadata, EXIFTAG_LENSMODEL, &lens_model)) {
        std::cout << "No Focal Lenght?" << std::endl;
    }

    std::cout << "Make: " << make << ", model: " << model << ", Focal length: " << lens_model << std::endl;

    std::unique_ptr<DemosaicParameters> demosaicParameters;
    if (make == "Apple" && (model == "iPhone 14 Pro" || model == "iPhone 14 Pro Max")) {
        const std::string tele = "iPhone 14 Pro back camera 9mm f/2.8";
        const std::string wide = "iPhone 14 Pro back camera 6.86mm f/1.78";
        const std::string ultraWide = "iPhone 14 Pro back camera 2.22mm f/2.2";
        const std::string tele_max = "iPhone 14 Pro Max back camera 9mm f/2.8";
        const std::string wide_max = "iPhone 14 Pro Max back camera 6.86mm f/1.78";
        const std::string ultraWide_max = "iPhone 14 Pro Max back camera 2.22mm f/2.2";
        const std::string selfie = "iPhone 14 Pro front camera 2.69mm f/1.9";
        const std::string selfie_max = "iPhone 14 Pro front camera 2.69mm f/1.9";

        if (lens_model == tele || lens_model == tele_max) {
            demosaicParameters = unpackiPhone14TeleRawImage(*rawImage, rawConverter->xyz_rgb(), &dng_metadata, &exif_metadata);
        } else if (lens_model == wide || lens_model == wide_max) {
            demosaicParameters = unpackiPhone14WideRawImage(*rawImage, rawConverter->xyz_rgb(), &dng_metadata, &exif_metadata);
        } else if (lens_model == ultraWide || lens_model == ultraWide_max) {
            demosaicParameters = unpackiPhone14UltraWideRawImage(*rawImage, rawConverter->xyz_rgb(), &dng_metadata, &exif_metadata);
        } else if (lens_model == selfie || lens_model == selfie_max) {
            demosaicParameters = unpackiPhone14SelfieRawImage(*rawImage, rawConverter->xyz_rgb(), &dng_metadata, &exif_metadata);
        } else {
            std::cout << "Unknown Camera - " << "Make: " << make << ", model: " << model << ", Lens Model: " << lens_model << " - Using Wide" << std::endl;
            demosaicParameters = unpackiPhone14WideRawImage(*rawImage, rawConverter->xyz_rgb(), &dng_metadata, &exif_metadata);
            // exit(-1);
        }
    } else {
        std::cout << "Unknown Device - " << "Make: " << make << ", model: " << model << std::endl;
        exit(-1);
    }

    rawConverter->allocateTextures(rawImage->size());

    auto t_start = std::chrono::high_resolution_clock::now();

    auto srgbImage = rawConverter->demosaic(*rawImage, demosaicParameters.get());

    auto t_end = std::chrono::high_resolution_clock::now();
    double elapsed_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    std::cout << "Metal Pipeline Execution Time: " << (int)elapsed_time_ms
              << "ms for image of size: " << rawImage->width << " x " << rawImage->height << std::endl;

    const auto output_dir = input_path.parent_path(); // .parent_path() / "Classic";
    const auto filename = input_path.filename().replace_extension("_t_g8b_structure_4.5c.tif");
    const auto output_path = output_dir / filename;

    const auto srgbImageCpu = srgbImage->mapImage();
    saveImage<gls::rgb_pixel_16>(*srgbImageCpu, output_path.string(), &dng_metadata, rawConverter->icc_profile_data());

    auto histogramData = rawConverter->histogramData();
//    plotHistogram(histogramData->histogram, input_path.filename().string());
//    std::array<uint32_t, 0x100> integral;
//    {
//        uint32_t sum = histogramData->histogram[0];
//        integral[0] = 0;
//        for (int i = 0; i < 0x100; i++) {
//            sum += histogramData->histogram[i];
//            integral[i] = sum;
//        }
//    }
//    plotHistogram(integral, input_path.filename().string());

    int imageSize = rawImage->width * rawImage->height / 64;
    float sum = 0;
    std::cout << "bands: ";
    for (int i = 0; i < 8; i++) {
        sum += histogramData->bands[i] / (float) imageSize;
        std::cout << histogramData->bands[i] / (float) imageSize << ", ";
    }
    std::cout << "sum: " << sum << std::endl;

    std::cout << "black_level: " << histogramData->black_level << ", white_level: " << histogramData->white_level << std::endl;
    std::cout << "mean: " << histogramData->mean << ", median: " << histogramData->median << ", mean - median: " << histogramData->mean - histogramData->median << std::endl;
    std::cout << "shadows: " << histogramData->shadows << ", highlights: " << histogramData->highlights << std::endl;

    // TODO: Add brightness value to metadata to estimate exposure adjustments
    // Bv = log2(A^2/(T * Sx)) + log2(1/0.297) => Bv = log2(A^2/(T*Sx)) + 1.751465
    // LV = 2 * log2(Aperture) - log2(ShutterSpeed) - log2(ISO/100)
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

void fmenApplyToFile(RawConverter* rawConverter, std::filesystem::path input_path, std::vector<uint8_t>* icc_profile_data) {
    std::cout << "Processing File: " << input_path.filename() << std::endl;

    gls::tiff_metadata dng_metadata, exif_metadata;
    const auto rawImage = gls::image<gls::luma_pixel_16>::read_dng_file(input_path.string(), &dng_metadata, &exif_metadata);
    auto demosaicParameters = unpackiPhone14TeleFEMNRawImage(*rawImage, rawConverter->xyz_rgb(), &dng_metadata, &exif_metadata);

    float baseline_exposure = 0;
    getValue(dng_metadata, TIFFTAG_BASELINEEXPOSURE, &baseline_exposure);
    float exposure_multiplier = pow(2.0, baseline_exposure);
    std::cout << "baseline_exposure: " << baseline_exposure << ", exposure_multiplier: " << exposure_multiplier
                  << std::endl;

    // Result Image
    gls::image<gls::rgba_pixel_fp16> processedImage(rawImage->width, rawImage->height);

    // Apply model to image
    fmenApplyToImage(*rawImage, /*whiteLevel*/ demosaicParameters->white_level, &processedImage);

    // const auto output_path = input_path.replace_extension("_fmen_1072_ltm_sharp.png");

    const auto output_dir = input_path.parent_path().parent_path() / "Neuro";
    const auto filename = input_path.filename().replace_extension("_c_sharp.tiff");
    const auto output_path = output_dir / filename;

    auto srgbImage = rawConverter->postprocess(processedImage, demosaicParameters.get());
    const auto srgbImageCpu = srgbImage->mapImage();
    saveImage<gls::rgb_pixel_16>(*srgbImageCpu, output_path.string(), &dng_metadata, rawConverter->icc_profile_data());

    auto histogramData = rawConverter->histogramData();
    int imageSize = rawImage->width * rawImage->height / 64;
    float sum = 0;
    std::cout << "bands: ";
    for (int i = 0; i < 8; i++) {
        sum += histogramData->bands[i] / (float) imageSize;
        std::cout << histogramData->bands[i] / (float) imageSize << ", ";
    }
    std::cout << "sum: " << sum << std::endl;

    std::cout << "black_level: " << histogramData->black_level << ", white_level: " << histogramData->white_level << std::endl;
    std::cout << "mean: " << histogramData->mean << ", median: " << histogramData->median << ", mean - median: " << histogramData->mean - histogramData->median << std::endl;
    std::cout << "shadows: " << histogramData->shadows << ", highlights: " << histogramData->highlights << std::endl;
}

void fmenApplyToDirectory(RawConverter* rawConverter, std::filesystem::path input_path, std::vector<uint8_t>* icc_profile_data) {
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
            fmenApplyToFile(rawConverter, input_path, icc_profile_data);
        } else if (std::filesystem::directory_entry(input_path).is_directory()) {
            fmenApplyToDirectory(rawConverter, input_path, icc_profile_data);
        }
    }
}

int main(int argc, const char * argv[]) {
    // Read ICC color profile data
    auto icc_profile_data = read_binary_file("/System/Library/ColorSync/Profiles/Display P3.icc");

    auto allMetalDevices = NS::TransferPtr(MTL::CopyAllDevices());
    auto metalDevice = NS::RetainPtr(allMetalDevices->object<MTL::Device>(0));

    // FIXME: the address sanitizer doesn't like the profile data.
    RawConverter rawConverter(metalDevice, &icc_profile_data, /*calibrateFromImage=*/ false);

    if (argc > 1) {
        auto input_path = std::filesystem::path(argv[1]);

        demosaicFile(&rawConverter, input_path);

        // demosaicDirectory(&rawConverter, input_path);

        // fmenApplyToFile(&rawConverter, input_path, &icc_profile_data);

        // fmenApplyToDirectory(&rawConverter, input_path, &icc_profile_data);

        return 0;
    } else {
        std::cout << "We need a file name..." << std::endl;
        return -1;
    }
}
