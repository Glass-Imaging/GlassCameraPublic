// Copyright (c) 2021-2022 Glass Imaging Inc.
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

#include <iomanip>

#include "gls_logging.h"
#include "pyramid_processor.hpp"

static const char* TAG = "DEMOSAIC";

template <size_t levels>
PyramidProcessor<levels>::PyramidProcessor(MetalContext* mtlContext, int _width, int _height)
    : width(_width), height(_height), fusedFrames(0) {
    auto mtlDevice = mtlContext->device();
    for (int i = 0, scale = 2; i < levels - 1; i++, scale *= 2) {
        imagePyramid[i] = std::make_unique<imageType>(mtlDevice, width / scale, height / scale);
        gradientPyramid[i] = std::make_unique<gls::mtl_image_2d<gls::luma_alpha_pixel_float>>(
            mtlDevice, width / scale, height / scale);
    }
    for (int i = 0, scale = 1; i < levels; i++, scale *= 2) {
        denoisedImagePyramid[i] = std::make_unique<imageType>(mtlDevice, width / scale, height / scale);
        subtractedImagePyramid[i] = std::make_unique<imageType>(mtlDevice, width / scale, height / scale);
    }
}

gls::Vector<3> nflMultiplier(const DenoiseParameters& denoiseParameters) {
    float luma_mul = denoiseParameters.luma;
    float chroma_mul = denoiseParameters.chroma;
    return {luma_mul, chroma_mul, chroma_mul};
}

#if DEBUG_PYRAMID
extern const gls::Matrix<3, 3> ycbcr_srgb;

void dumpYCbCrImage(const gls::mtl_image_2d<gls::rgba_pixel_float>& image) {
    gls::image<gls::rgb_pixel> out(image.width, image.height);
    const auto downsampledCPU = image.mapImage();
    out.apply([&downsampledCPU](gls::rgb_pixel* p, int x, int y) {
        const auto& ip = downsampledCPU[y][x];
        const auto& v = ycbcr_srgb * gls::Vector<3>{ip.x, ip.y, ip.z};
        *p = gls::rgb_pixel{(uint8_t)(255 * std::sqrt(std::clamp(v[0], 0.0f, 1.0f))),
                            (uint8_t)(255 * std::sqrt(std::clamp(v[1], 0.0f, 1.0f))),
                            (uint8_t)(255 * std::sqrt(std::clamp(v[2], 0.0f, 1.0f)))};
    });
    image.unmapImage(downsampledCPU);
    static int count = 1;
    out.write_png_file("/Users/fabio/pyramid_7x7" + std::to_string(count++) + ".png");
}

void dumpGradientImage(const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& image);

#endif  // DEBUG_PYRAMID

// TODO: Make this a tunable
static const constexpr float lumaDenoiseWeight[4] = {1, 1, 1, 1};

template <size_t levels>
typename PyramidProcessor<levels>::imageType* PyramidProcessor<levels>::denoise(
    MetalContext* mtlContext, std::array<DenoiseParameters, levels>* denoiseParameters, const imageType& image,
    const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage, std::array<YCbCrNLF, levels>* nlfParameters,
    float exposure_multiplier, bool calibrateFromImage) {
    std::array<gls::Vector<3>, levels> thresholdMultipliers;

    // Create gaussian image pyramid an setup noise model
    for (int i = 0; i < levels; i++) {
        const auto currentLayer = i > 0 ? imagePyramid[i - 1].get() : &image;
        const auto currentGradientLayer = i > 0 ? gradientPyramid[i - 1].get() : &gradientImage;

        if (i < levels - 1) {
            // Generate next layer in the pyramid
            resampleImage(mtlContext, "downsampleImageXYZ", *currentLayer, imagePyramid[i].get());
            resampleImage(mtlContext, "downsampleImageXY", *currentGradientLayer, gradientPyramid[i].get());
        }

//        if (calibrateFromImage) {
//            (*nlfParameters)[i] =
//                MeasureYCbCrNLF(mtlContext, *currentLayer, *currentGradientLayer, exposure_multiplier);
//        }

        thresholdMultipliers[i] = nflMultiplier((*denoiseParameters)[i]);
    }

    // Denoise pyramid layers from the bottom to the top, subtracting the noise of the previous layer from the next
    for (int i = levels - 1; i >= 0; i--) {
        const auto denoiseInput = i > 0 ? imagePyramid[i - 1].get() : &image;
        const auto gradientInput = i > 0 ? gradientPyramid[i - 1].get() : &gradientImage;

        if (i < levels - 1) {
            // Subtract the previous layer's noise from the current one
            // LOG_INFO(TAG) << "Reassembling layer " << i + 1 << " with sharpening: " <<
            // (*denoiseParameters)[i].sharpening << std::endl;

            const auto np = YCbCrNLF{(*nlfParameters)[i].first * thresholdMultipliers[i],
                                     (*nlfParameters)[i].second * thresholdMultipliers[i]};
            subtractNoiseImage(mtlContext, *denoiseInput, *(imagePyramid[i]), *(denoisedImagePyramid[i + 1]),
                               *gradientInput, lumaDenoiseWeight[i], (*denoiseParameters)[i].sharpening,
                               {np.first[0], np.second[0]}, subtractedImagePyramid[i].get());
        }

        // LOG_INFO(TAG) << "Denoising image level " << i << " with multipliers " << thresholdMultipliers[i] <<
        // std::endl;

        // Denoise current layer
        denoiseImage(mtlContext, i < levels - 1 ? *(subtractedImagePyramid[i]) : *denoiseInput, *gradientInput,
                     (*nlfParameters)[i].first, (*nlfParameters)[i].second, thresholdMultipliers[i],
                     (*denoiseParameters)[i].chromaBoost, (*denoiseParameters)[i].gradientBoost,
                     (*denoiseParameters)[i].gradientThreshold, denoisedImagePyramid[i].get());
    }

    return denoisedImagePyramid[0].get();
}

template struct PyramidProcessor<5>;
