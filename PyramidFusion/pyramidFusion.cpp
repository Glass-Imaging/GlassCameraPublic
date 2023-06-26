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

#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <cmath>
#include <chrono>
#include <memory>
#include <ranges>
#include <set>

#include "gls_logging.h"
#include "gls_image.hpp"
#include "gls_linalg.hpp"

#include "raw_converter.hpp"
#include "CameraCalibration.hpp"

#include "SURF.hpp"
#include "Homography.hpp"

std::vector<std::filesystem::path> parseDirectory(const std::string& dir) {
    std::set<std::filesystem::path> directory_listing;
    for (const auto &entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && (entry.path().extension() == ".dng" || entry.path().extension() == ".DNG")) {
            directory_listing.insert(entry.path());
        }
    }
    return std::vector<std::filesystem::path>(directory_listing.begin(), directory_listing.end());
}

gls::mtl_image_2d<gls::pixel_float4>* runPipeline(RawConverter* rawConverter,
                                                  const std::filesystem::path& input_path,
                                                  std::unique_ptr<DemosaicParameters>* demosaicParameters) {
    gls::tiff_metadata dng_metadata, exif_metadata;
    const auto inputImage = gls::image<gls::luma_pixel_16>::read_dng_file(input_path.string(), &dng_metadata, &exif_metadata);

    if (*demosaicParameters == nullptr) {
        const auto cameraCalibration = getiPhone14TeleCalibration(); // getLeicaQ2Calibration(); // getIPhone11Calibration();
        *demosaicParameters = cameraCalibration->getDemosaicParameters(*inputImage, rawConverter->xyz_rgb(), &dng_metadata, &exif_metadata);
    }

    const auto demosaicedImage = rawConverter->demosaic(*inputImage, demosaicParameters->get(), /*noiseReduction=*/ true, /*postProcess=*/ true);

    return demosaicedImage;
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

std::pair<gls::mtl_image_2d<float>::unique_ptr,
          gls::mtl_image_2d<gls::pixel_float4>::unique_ptr> convertImage(RawConverter* rawConverter,
                                                                         const convertToGrayscale& convertToGrayscale,
                                                                         const std::string& image_path)
{
    auto context = rawConverter->context();
    std::unique_ptr<DemosaicParameters> demosaicParameters = nullptr;
    auto rgb_image_ptr = runPipeline(rawConverter, image_path, &demosaicParameters);
    auto rgb_image = std::make_unique<gls::mtl_image_2d<gls::pixel_float4>>(context->device(), rgb_image_ptr->size());
    rgb_image->copyPixelsFrom(*rgb_image_ptr->mapImage());
    const auto& transform = demosaicParameters->rgb_cam;
    auto luma_image = std::make_unique<gls::mtl_image_2d<float>>(context->device(), rgb_image_ptr->size());
    convertToGrayscale(context, *rgb_image, luma_image.get(), transform[0]);
    context->waitForCompletion();
    return std::make_pair(std::move(luma_image), std::move(rgb_image));
}

float sigmoid(float x, float s) {
    return 0.5 * (tanh(s * x - 0.3 * s) + 1);
}

// This tone curve is designed to mostly match the default curve from DNG files

float toneCurve(float x, float s) {
    return (sigmoid(pow(0.95 * x, 0.5), s) - sigmoid(0.0, s)) / (sigmoid(1.0, s) - sigmoid(0.0, s));
}

std::array<gls::image<float>::unique_ptr, 4> RawChannels(const gls::image<gls::luma_pixel_16>& inputImage,
                                                         const gls::tiff_metadata& dng_metadata,
                                                         const gls::tiff_metadata& exif_metadata) {
    float baseline_exposure = 0;
    getValue(dng_metadata, TIFFTAG_BASELINEEXPOSURE, &baseline_exposure);
    float exposure_multiplier = pow(2.0, baseline_exposure);

    const auto black_level_vec = getVector<float>(dng_metadata, TIFFTAG_BLACKLEVEL);
    const auto white_level_vec = getVector<uint32_t>(dng_metadata, TIFFTAG_WHITELEVEL);

    const float black_level = black_level_vec.empty() ? 0 : black_level_vec[0];
    const float white_level = white_level_vec.empty() ? 0xffff : white_level_vec[0];

    const auto cfa_pattern = getVector<uint8_t>(dng_metadata, TIFFTAG_CFAPATTERN);

    const auto bayerPattern = std::memcmp(cfa_pattern.data(), "\00\01\01\02", 4) == 0 ? BayerPattern::rggb :
                              std::memcmp(cfa_pattern.data(), "\02\01\01\00", 4) == 0 ? BayerPattern::bggr :
                              std::memcmp(cfa_pattern.data(), "\01\00\02\01", 4) == 0 ? BayerPattern::grbg :
                                                                                        BayerPattern::gbrg;
    const auto offsets = bayerOffsets[bayerPattern];

    std::array<gls::image<float>::unique_ptr, 4> channels = {
        std::make_unique<gls::image<float>>(inputImage.size() / 2),
        std::make_unique<gls::image<float>>(inputImage.size() / 2),
        std::make_unique<gls::image<float>>(inputImage.size() / 2),
        std::make_unique<gls::image<float>>(inputImage.size() / 2)
    };

    for (int y = 0; y < inputImage.height; y += 2) {
        for (int x = 0; x < inputImage.width; x += 2) {
            for (int c = 0; c < 4; c++) {
                (*channels[c])[y / 2][x / 2] = toneCurve(std::clamp(exposure_multiplier * (inputImage[y + offsets[c].y][x + offsets[c].x] - black_level) / white_level, 0.0f, 1.0f), 3.5);
            }
        }
    }

    return channels;
}

std::array<gls::image<float>::unique_ptr, 4> RawChannels(const std::string& input_path) {
    gls::tiff_metadata dng_metadata, exif_metadata;
    const auto inputImage = gls::image<gls::luma_pixel_16>::read_dng_file(input_path, &dng_metadata, &exif_metadata);

    return RawChannels(*inputImage, dng_metadata, exif_metadata);
}

template <typename pixel_type>
void saveFusedImage(const gls::image<pixel_type>& fused_image, const std::string& output_path) {
    gls::image<gls::rgb_pixel> output(fused_image.size());
    output.apply([&](gls::rgb_pixel* p, int x, int y) {
        const auto& ip = fused_image[y][x];
        *p = {
            (uint8_t) (255 * std::clamp((float) ip.red, 0.0f, 1.0f)),
            (uint8_t) (255 * std::clamp((float) ip.green, 0.0f, 1.0f)),
            (uint8_t) (255 * std::clamp((float) ip.blue, 0.0f, 1.0f))
        };
    });
    output.write_tiff_file(output_path);
}

std::vector<std::vector<std::filesystem::path>> findBursts(std::vector<std::filesystem::path> input_files) {
    std::vector<std::vector<std::filesystem::path>> bursts;

    const std::filesystem::path* current_first = nullptr;
    std::string::size_type current_found = std::string::npos;
    std::vector<std::filesystem::path> current_burst = {};
    for (const auto& f : input_files) {
        const auto found = f.stem().string().find("_1_");
        if (found != std::string::npos) {
            current_first = &f;
            current_found = found;
            if (!current_burst.empty()) {
                bursts.push_back(current_burst);
            }
            current_burst = { f };
        } else if (current_found != std::string::npos) {
            const auto& start = f.stem().string().substr(0, current_found);
            if (current_first->stem().string().substr(0, current_found) == start) {
                current_burst.push_back(f);
            }
        }
    }
    if (!current_burst.empty()) {
        bursts.push_back(current_burst);
        current_burst = {};
    }

    std::cout << "bursts: " << bursts.size() << std::endl;
    return bursts;
}

int main_full(int argc, const char * argv[]) {
    if (argc < 2) {
        std::cout << "Please provide a directory path..." << std::endl;
    }

    const auto& input_files = parseDirectory(argv[1]);
    const auto& bursts = findBursts(input_files);

    for (const auto& burst : bursts) {
        if (burst.size() == 4) {
            const auto& reference_image_path = burst[3];
            std::cout << "Reference Image: " << reference_image_path.filename() << std::endl;

            // Read ICC color profile data
            auto icc_profile_data = read_binary_file("/System/Library/ColorSync/Profiles/Display P3.icc");

            auto allMetalDevices = NS::TransferPtr(MTL::CopyAllDevices());
            auto metalDevice = NS::RetainPtr(allMetalDevices->object<MTL::Device>(0));

            // FIXME: the address sanitizer doesn't like the profile data.
            RawConverter rawConverter(metalDevice, &icc_profile_data, /*calibrateFromImage=*/ false);
            auto context = rawConverter.context();

            convertToGrayscale _convertToGrayscale(context);
            RegisterAndFuseKernel _registerAndFuse(context);

            const auto reference_image = convertImage(&rawConverter, _convertToGrayscale, reference_image_path.string());

            auto fused_image = std::make_unique<gls::mtl_image_2d<gls::pixel_float4>>(context->device(), reference_image.first->size());
            fused_image->copyPixelsFrom(*reference_image.second->mapImage());

            auto surf = gls::SURF::makeInstance(context, reference_image.first->width, reference_image.first->height,
                                                /*max_features=*/ 1500, /*nOctaves=*/ 4, /*nOctaveLayers=*/ 2, /*hessianThreshold=*/ 0.02);

            auto reference_keypoints = std::make_unique<std::vector<KeyPoint>>();
            gls::image<float>::unique_ptr reference_descriptors;
            surf->detectAndCompute(*reference_image.first->mapImage(), reference_keypoints.get(), &reference_descriptors);

            std::cout << "Found " << reference_keypoints->size() << " reference keypoints" << std::endl;

            for (int i = 0; i < 3; i++) {
                const auto image = convertImage(&rawConverter, _convertToGrayscale, burst[i].string());

                auto image_keypoints = std::make_unique<std::vector<KeyPoint>>();
                gls::image<float>::unique_ptr image_descriptors;
                surf->detectAndCompute(*image.first->mapImage(), image_keypoints.get(), &image_descriptors);

                std::cout << "Found " << image_keypoints->size() << " keypoints for image " << i + 1 << std::endl;

                const auto matches = surf->findMatches(*reference_descriptors, *reference_keypoints, *image_descriptors, *image_keypoints);

                std::vector<int> inliers;
                const auto homography = gls::FindHomography(matches, /*threshold=*/ 1, /*max_iterations=*/ 2000, &inliers);
                std::cout << "Homography:\n" << homography << std::endl;
                std::cout << "Found " << inliers.size() << " inliers." << std::endl;

                _registerAndFuse(context, *fused_image, *image.second, fused_image.get(), homography, i + 1);
            }

            auto fused_image_cpu = fused_image->mapImage();
            const auto filename = reference_image_path.stem().string().substr(0, reference_image_path.stem().string().find("_4_"));
            saveFusedImage(*fused_image_cpu, reference_image_path.parent_path().parent_path() / "Fusion" / (filename + "_fullaRH.tiff"));
        }
    }
    return 0;
}

int main(int argc, const char * argv[]) {
    if (argc < 2) {
        std::cout << "Please provide a directory path..." << std::endl;
    }

    const auto& input_files = parseDirectory(argv[1]);
    const auto& bursts = findBursts(input_files);

    // Read ICC color profile data
    auto icc_profile_data = read_binary_file("/System/Library/ColorSync/Profiles/Display P3.icc");

    auto allMetalDevices = NS::TransferPtr(MTL::CopyAllDevices());
    auto metalDevice = NS::RetainPtr(allMetalDevices->object<MTL::Device>(0));

    // FIXME: the address sanitizer doesn't like the profile data.
    RawConverter rawConverter(metalDevice, &icc_profile_data, /*calibrateFromImage=*/ false);
    auto context = rawConverter.context();

    RegisterAndFuseKernel _registerAndFuse(context);
    RegisterBayerImageKernel _registerBayerImageKernel(context);

    gls::mtl_image_2d<gls::pixel_float4>::unique_ptr fused_image = nullptr;

    for (const auto& burst : bursts) {
        if (burst.size() == 4) {
            const auto& reference_image_path = burst[3];
            std::cout << "Reference Image: " << reference_image_path.filename() << std::endl;

            const auto base_filename = reference_image_path.stem().string().substr(0, reference_image_path.stem().string().find("_4_"));

            std::array<gls::image<gls::luma_pixel_16>::unique_ptr, 4> rawImages;

            gls::tiff_metadata dng_metadata, exif_metadata;
            rawImages[0] = gls::image<gls::luma_pixel_16>::read_dng_file(reference_image_path.string(), &dng_metadata, &exif_metadata);
            const auto referenceChannels = RawChannels(*rawImages[0], dng_metadata, exif_metadata);
            const std::array<int, 2> channels = {1, 3};
            const int channel_count = 2;

            auto surf = gls::SURF::makeInstance(context, referenceChannels[1]->width, referenceChannels[1]->height,
                                                /*max_features=*/ 1500, /*nOctaves=*/ 4, /*nOctaveLayers=*/ 2, /*hessianThreshold=*/ 0.02);

            std::unique_ptr<DemosaicParameters> demosaicParameters = nullptr;
            auto rgb_image_ptr = runPipeline(&rawConverter, reference_image_path.string(), &demosaicParameters);
            if (!fused_image) {
                fused_image = std::make_unique<gls::mtl_image_2d<gls::pixel_float4>>(context->device(), rgb_image_ptr->size());
            }
            fused_image->copyPixelsFrom(*rgb_image_ptr->mapImage());

            std::array<gls::image<float>::unique_ptr, 2> reference_descriptors;
            std::array<std::unique_ptr<std::vector<KeyPoint>>, 2> reference_keypoints;
            for (int c = 0; c < channel_count; c++) {
                reference_keypoints[c] = std::make_unique<std::vector<KeyPoint>>();
                surf->detectAndCompute(*referenceChannels[channels[c]], reference_keypoints[c].get(), &reference_descriptors[c]);
                std::cout << "Found " << reference_keypoints[c]->size() << " reference keypoints for channel " << channels[c] << std::endl;
            }

            for (int i = 0; i < 3; i++) {
                gls::tiff_metadata dng_metadata, exif_metadata;
                rawImages[i + 1] = gls::image<gls::luma_pixel_16>::read_dng_file(burst[i].string(), &dng_metadata, &exif_metadata);
                const auto imageChannels = RawChannels(*rawImages[i + 1], dng_metadata, exif_metadata);

                // const auto imageChannels = RawChannels(burst[i].string());
                std::array<std::unique_ptr<std::vector<KeyPoint>>, 2> image_keypoints;
                gls::Matrix<3, 3> homography = gls::Matrix<3, 3>::zeros();
                for (int c = 0; c < channel_count; c++) {
                    std::array<gls::image<float>::unique_ptr, 2> image_descriptors;
                    image_keypoints[c] = std::make_unique<std::vector<KeyPoint>>();
                    surf->detectAndCompute(*imageChannels[channels[c]], image_keypoints[c].get(), &image_descriptors[c]);
                    std::cout << "Found " << image_keypoints[c]->size() << " keypoints for channel " << channels[c] << " of image " << i + 1 << std::endl;
                    const auto matches = surf->findMatches(*reference_descriptors[c], *reference_keypoints[c], *image_descriptors[c], *image_keypoints[c]);
                    std::vector<int> inliers;
                    const auto channel_homography = gls::FindHomography(matches, /*threshold=*/ 1, /*max_iterations=*/ 2000, &inliers);
                    std::cout << "Channel " << c << " homography:\n" << channel_homography << std::endl;
                    std::cout << "Found " << inliers.size() << " inliers." << std::endl;

                    // Running average of the homography
                    homography = (channel_homography + homography * (float) c) / (float) (c + 1);
                }
                std::cout << "Homography:\n" << homography << std::endl;

                auto rgb_image_ptr = runPipeline(&rawConverter, burst[i].string(), &demosaicParameters);
                _registerAndFuse(context, *fused_image, *rgb_image_ptr, fused_image.get(), ScaleHomography(homography, 2), i + 1);
            }

            auto fused_image_cpu = fused_image->mapImage();
            saveFusedImage(*fused_image_cpu, reference_image_path.parent_path().parent_path() / "Fusion" / (base_filename + "aRH_fork.tiff"));
        } else {
            std::cout << "Weird burst: " << burst[0].string() << std::endl;
        }
    }

    return 0;
}
