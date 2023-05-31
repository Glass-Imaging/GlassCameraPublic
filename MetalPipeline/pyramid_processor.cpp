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

#include <iomanip>

#include "gls_logging.h"
#include "pyramid_processor.hpp"
#include "PCA.hpp"

static const char* TAG = "DEMOSAIC";

template <size_t levels>
PyramidProcessor<levels>::PyramidProcessor(MetalContext* context, int _width, int _height)
    : width(_width), height(_height), fusedFrames(0),
    _denoiseImage(context),
    _collectPatches(context),
    _pcaProjection(context),
    _blockMatchingDenoiseImage(context),
    _subtractNoiseImage(context),
    _resampleImage(context, "downsampleImageXYZ"),
    _resampleGradientImage(context, "downsampleImageXY"),
    _basicNoiseStatistics(context),
    _hfNoiseTransferImage(context, 0.4)
{
    auto mtlDevice = context->device();
    for (int i = 0, scale = 2; i < levels - 1; i++, scale *= 2) {
        imagePyramid[i] = std::make_unique<imageType>(mtlDevice, width / scale, height / scale);
        gradientPyramid[i] = std::make_unique<gls::mtl_image_2d<gls::rgba_pixel_float>>(
            mtlDevice, width / scale, height / scale);
    }
    for (int i = 0, scale = 1; i < levels; i++, scale *= 2) {
        denoisedImagePyramid[i] = std::make_unique<imageType>(mtlDevice, width / scale, height / scale);
        subtractedImagePyramid[i] = std::make_unique<imageType>(mtlDevice, width / scale, height / scale);
        pcaImagePyramid[i] = std::make_unique<gls::mtl_image_2d<gls::pixel<uint32_t, 4>>>(mtlDevice, width / scale, height / scale);
    }

    pcaPatches = std::make_unique<gls::Buffer<std::array<float, pcaPatchSize>>>(context->device(), width * height / 64);
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

void savePatchMap(const gls::mtl_image_2d<gls::rgba_pixel_float>& denoisedImage) {
    gls::image<gls::luma_pixel> out(denoisedImage.width, denoisedImage.height);
    const auto denoisedImageCPU = denoisedImage.mapImage();
    out.apply([&denoisedImageCPU](gls::luma_pixel* p, int x, int y) {
        const auto& ip = (*denoisedImageCPU)[y][x];
        *p = gls::luma_pixel{(uint8_t)(ip.w)};
    });
    // denoisedImage.unmapImage(denoisedImageCPU);
    static int count = 1;
    out.write_png_file("/Users/fabio/patch_map" + std::to_string(count++) + ".png");
}

// TODO: Make this a tunable
static const constexpr float lumaDenoiseWeight[4] = {1, 1, 1, 1};

template <size_t levels>
typename PyramidProcessor<levels>::imageType* PyramidProcessor<levels>::denoise(
    MetalContext* context, std::array<DenoiseParameters, levels>* denoiseParameters, const imageType& image,
    const gls::mtl_image_2d<gls::rgba_pixel_float>& gradientImage, std::array<YCbCrNLF, levels>* nlfParameters,
    float exposure_multiplier, float lensShadingCorrection, bool calibrateFromImage) {
    std::array<gls::Vector<3>, levels> thresholdMultipliers;

    // Create gaussian image pyramid an setup noise model
    for (int i = 0; i < levels; i++) {
        const auto currentLayer = i > 0 ? imagePyramid[i - 1].get() : &image;
        const auto currentGradientLayer = i > 0 ? gradientPyramid[i - 1].get() : &gradientImage;

        if (i < levels - 1) {
            // Generate next layer in the pyramid
            _resampleImage(context, *currentLayer, imagePyramid[i].get());
            _resampleImage(context, *currentGradientLayer, gradientPyramid[i].get());
        }

        if (calibrateFromImage) {
            // Use the denoisedImagePyramid to collect the noise statistics
            (*nlfParameters)[i] =
                MeasureYCbCrNLF(context, *currentLayer, denoisedImagePyramid[i].get(), exposure_multiplier);
        }

        thresholdMultipliers[i] = nflMultiplier((*denoiseParameters)[i]);
    }

    // Denoise pyramid layers from the bottom to the top, subtracting the noise of the previous layer from the next
    for (int i = levels - 1; i >= 0; i--) {
        const auto denoiseInput = i > 0 ? imagePyramid[i - 1].get() : &image;
        const auto gradientInput = i > 0 ? gradientPyramid[i - 1].get() : &gradientImage;

        if (i < levels - 1) {
            const auto np = YCbCrNLF{(*nlfParameters)[i].first * thresholdMultipliers[i],
                                     (*nlfParameters)[i].second * thresholdMultipliers[i]};
            _subtractNoiseImage(context, *denoiseInput, *(imagePyramid[i]), *(denoisedImagePyramid[i + 1]),
                                *gradientInput, lumaDenoiseWeight[i], (*denoiseParameters)[i].sharpening,
                                {np.first[0], np.second[0]}, subtractedImagePyramid[i].get());
        }

        const auto layerImage = i < levels - 1 ? subtractedImagePyramid[i].get() : denoiseInput;

        if (usePatchSimiliarity) {
            assert(layerImage->size() == pcaImagePyramid[i]->size());

            const auto imageCPU = layerImage->mapImage();

            const int sample_size = imageCPU->width * imageCPU->height / 64;

            assert(pcaPatches->size() >= sample_size);

            _collectPatches(context, *layerImage, pcaPatches->buffer());

            context->waitForCompletion();
            build_pca_space(std::span(pcaPatches->data(), sample_size), &pcaSpace);

            _pcaProjection(context, *layerImage, pcaSpace, pcaImagePyramid[i].get());

            // Denoise current layer
            _blockMatchingDenoiseImage(context, *layerImage, *gradientInput, *pcaImagePyramid[i],
                                       (*nlfParameters)[i].first, (*nlfParameters)[i].second, thresholdMultipliers[i],
                                       (*denoiseParameters)[i].chromaBoost, (*denoiseParameters)[i].gradientBoost,
                                       (*denoiseParameters)[i].gradientThreshold, lensShadingCorrection,
                                       denoisedImagePyramid[i].get());

//            context->waitForCompletion();
//            savePatchMap(*(denoisedImagePyramid[i]));
        } else {
            // Denoise current layer
            _denoiseImage(context, *layerImage, *gradientInput,
                          (*nlfParameters)[i].first, (*nlfParameters)[i].second, thresholdMultipliers[i],
                          (*denoiseParameters)[i].chromaBoost, (*denoiseParameters)[i].gradientBoost,
                          (*denoiseParameters)[i].gradientThreshold, denoisedImagePyramid[i].get());
        }
    }

    return denoisedImagePyramid[0].get();
}

template <size_t levels>
YCbCrNLF PyramidProcessor<levels>::MeasureYCbCrNLF(MetalContext* context,
                                                   const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                                                   gls::mtl_image_2d<gls::rgba_pixel_float> *noiseStats,
                                                   float exposure_multiplier) {
    assert(inputImage.size() == noiseStats->size());

    _basicNoiseStatistics(context, inputImage, noiseStats);
    context->waitForCompletion();

    // applyKernel(glsContext, "noiseStatistics_old", inputImage, &noiseStats);
    const auto noiseStatsCpu = noiseStats->mapImage();

    using double3 = gls::DVector<3>;

    gls::DVector<6> varianceHistogram = {0, 0, 0, 0, 0, 0};   // 1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 1e-1
    noiseStatsCpu->apply([&](const gls::rgba_pixel_float& ns, int x, int y) {
        double3 v = {ns[1], ns[2], ns[3]};

        bool validStats = !any(isnan(v)) && all(v > double3(0.0));

        if (validStats) {
            const auto scale = gls::apply(std::log10, v);

            int entry = 6 + (int) std::clamp(max_element(scale), -6.0, -1.0);
            varianceHistogram[entry]++;
        }
    });

    // Only consider pixels with variance lower than the expected noise value
    double3 varianceMax = varianceHistogram[0] > 1e3 ? 1.0e-5 :
                          varianceHistogram[1] > 1e3 ? 1.0e-4 :
                          varianceHistogram[2] > 1e3 ? 1.0e-3 :
                          varianceHistogram[3] > 1e3 ? 1.0e-2 :
                          varianceHistogram[4] > 1e3 ? 1.0e-1 : 1;

    // std::cout << "MeasureYCbCrNLF - varianceHistogram: " << varianceHistogram << ", varianceMax: " << varianceMax << std::endl;

    // Limit to pixels the more linear intensity zone of the sensor
    const double maxValue = 0.9;
    const double minValue = 0.001;

    // Collect pixel statistics
    double s_x = 0;
    double s_xx = 0;
    double3 s_y = 0;
    double3 s_xy = 0;

    double N = 0;
    noiseStatsCpu->apply([&](const gls::rgba_pixel_float& ns, int x, int y) {
        double m = ns[0];
        double3 v = {ns[1], ns[2], ns[3]};

        bool validStats = !(std::isnan(m) || any(isnan(v)));

        if (validStats && m >= minValue && m <= maxValue && all(v <= varianceMax)) {
            s_x += m;
            s_y += v;
            s_xx += m * m;
            s_xy += m * v;
            N++;
        }
    });

    // Linear regression on pixel statistics to extract a linear noise model: nlf = A + B * Y
    auto nlfB = max((N * s_xy - s_x * s_y) / (N * s_xx - s_x * s_x), 1e-8);
    auto nlfA = max((s_y - nlfB * s_x) / N, 1e-8);

    // Estimate regression mean square error
    double3 err2 = 0;
    noiseStatsCpu->apply([&](const gls::rgba_pixel_float& ns, int x, int y) {
        double m = ns[0];
        double3 v = {ns[1], ns[2], ns[3]};

        bool validStats = !(std::isnan(m) || any(isnan(v)));

        if (validStats && m >= minValue && m <= maxValue && all(v <= varianceMax)) {
            auto nlfP = nlfA + nlfB * m;
            auto diff = nlfP - v;
            err2 += diff * diff;
        }
    });
    err2 /= N;

    // Update the maximum variance with the model
    varianceMax = nlfB;

    // Redo the statistics collection limiting the sample to pixels that fit well the linear model
    s_x = 0;
    s_xx = 0;
    s_y = 0;
    s_xy = 0;
    N = 0;
    double3 newErr2 = 0;
    int discarded = 0;
    noiseStatsCpu->apply([&](const gls::rgba_pixel_float& ns, int x, int y) {
        double m = ns[0];
        double3 v = {ns[1], ns[2], ns[3]};

        bool validStats = !(std::isnan(m) || any(isnan(v)));

        if (validStats && m >= minValue && m <= maxValue && all(v <= varianceMax)) {
            auto nlfP = nlfA + nlfB * m;
            auto diff = abs(nlfP - v);
            auto diffSquare = diff * diff;

            if (all(diffSquare <= err2)) {
                s_x += m;
                s_y += v;
                s_xx += m * m;
                s_xy += m * v;
                N++;
                newErr2 += diffSquare;
            } else {
                discarded++;
            }
        }
    });
    newErr2 /= N;

    if (all(newErr2 <= err2) && N / (inputImage.width * inputImage.height) > 0.01) {
        // Estimate the new regression parameters
        nlfB = max((N * s_xy - s_x * s_y) / (N * s_xx - s_x * s_x), 1e-8);
        nlfA = max((s_y - nlfB * s_x) / N, 1e-8);

        std::cout << "Pyramid NLF A: " << std::setprecision(4) << std::scientific << nlfA << ", B: " << nlfB
                  << ", MSE: " << sqrt(newErr2) << " on " << std::setprecision(1) << std::fixed
                  << 100 * N / (inputImage.width * inputImage.height) << "% pixels" << std::endl;
    } else {
        std::cout << "Pyramid NLF A: " << std::setprecision(4) << std::scientific << nlfA << ", B: " << nlfB << ", MSE: " << sqrt(err2)
                  << " on " << N << "(" << std::setprecision(1) << std::fixed << 100 * N / (inputImage.width * inputImage.height)
                  << "%) pixels of " << inputImage.width << " x " << inputImage.height << std::endl;

        std::cout << "*** WARNING *** Pyramid NLF second iteration is worse: MSE: " << sqrt(newErr2) << " on "
                  << std::setprecision(1) << std::fixed << 100 * N / (inputImage.width * inputImage.height)
                  << "% pixels" << std::endl;
    }

    // assert(all(newErr2 < err2));

    // noiseStats->unmapImage(noiseStatsCpu);

    double varianceExposureAdjustment = exposure_multiplier * exposure_multiplier;

    nlfA *= varianceExposureAdjustment;
    nlfB *= varianceExposureAdjustment;

    return std::pair(nlfA,  // A values
                     nlfB   // B values
    );
}

template struct PyramidProcessor<5>;
