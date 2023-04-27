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

#include "raw_converter.hpp"

void RawConverter::allocateTextures(const gls::size& imageSize) {
    assert(imageSize.width > 0 && imageSize.height > 0);

    if (_rawImageSize != imageSize) {
        std::cout << "Reallocating RawConverter textures" << std::endl;

        auto mtlDevice = _mtlContext.device();

        _rawImage = std::make_unique<gls::mtl_image_2d<gls::luma_pixel_16>>(mtlDevice, imageSize);
        _scaledRawImage = std::make_unique<gls::mtl_image_2d<gls::luma_pixel_float>>(mtlDevice, imageSize);
        _rawSobelImage = std::make_unique<gls::mtl_image_2d<gls::rgba_pixel_float>>(mtlDevice, imageSize);
        _rawGradientImage = std::make_unique<gls::mtl_image_2d<gls::luma_alpha_pixel_float>>(mtlDevice, imageSize);
        _greenImage = std::make_unique<gls::mtl_image_2d<gls::luma_pixel_float>>(mtlDevice, imageSize);
        _linearRGBImageA = std::make_unique<gls::mtl_image_2d<gls::rgba_pixel_float>>(mtlDevice, imageSize);
        _linearRGBImageB = std::make_unique<gls::mtl_image_2d<gls::rgba_pixel_float>>(mtlDevice, imageSize);

        if (_calibrateFromImage) {
            _meanImage = std::make_unique<gls::mtl_image_2d<gls::rgba_pixel_float>>(mtlDevice, imageSize.width / 2, imageSize.height / 2);
            _varImage = std::make_unique<gls::mtl_image_2d<gls::rgba_pixel_float>>(mtlDevice, imageSize.width / 2, imageSize.height / 2);
        }

        _rawImageSize = imageSize;

        _pyramidProcessor = std::make_unique<PyramidProcessor<5>>(&_mtlContext, _rawImageSize.width, _rawImageSize.height);
    }
}

void RawConverter::allocateHighNoiseTextures(const gls::size& imageSize) {
    int width = imageSize.width;
    int height = imageSize.height;

    if (!_rgbaRawImage || _rgbaRawImage->width != width / 2 || _rgbaRawImage->height != height / 2) {
        auto mtlDevice = _mtlContext.device();

        _rgbaRawImage = std::make_unique<gls::mtl_image_2d<gls::rgba_pixel_float>>(mtlDevice, width / 2, height / 2);
        _denoisedRgbaRawImage =
            std::make_unique<gls::mtl_image_2d<gls::rgba_pixel_float>>(mtlDevice, width / 2, height / 2);
    }
}

gls::mtl_image_2d<gls::rgba_pixel_float>* RawConverter::denoise(const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                                                                DemosaicParameters* demosaicParameters) {
    NoiseModel<5>* noiseModel = &demosaicParameters->noiseModel;

    // Luma and Chroma Despeckling
    const auto& np = noiseModel->pyramidNlf[0];
    _despeckleImage(&_mtlContext, inputImage,
                    /*var_a=*/np.first,
                    /*var_b=*/np.second, _linearRGBImageB.get());

    gls::mtl_image_2d<gls::rgba_pixel_float>* denoisedImage = _pyramidProcessor->denoise(&_mtlContext, &(demosaicParameters->denoiseParameters),
                                                                                         *_linearRGBImageB, *_rawGradientImage,
                                                                                         &noiseModel->pyramidNlf,
                                                                                         demosaicParameters->exposure_multiplier,
                                                                                         _calibrateFromImage);

    // Use a lower level of the pyramid to compute the histogram
    const auto histogramImage = _pyramidProcessor->denoisedImagePyramid[3].get();
    _histogramImage(&_mtlContext, *histogramImage, _histogramBuffer.get());
    _histogramStatistics(&_mtlContext, _histogramBuffer.get(), histogramImage->size());

//    _mtlContext.waitForCompletion();
//    auto histogramData = this->histogramData();
//
//    auto histogramImageCPU = histogramImage->mapImage();
//    gls::image<gls::luma_pixel_16> lumaImage(histogramImageCPU->width, histogramImageCPU->height);
//    lumaImage.apply([&](gls::luma_pixel_16* p, int x, int y) {
//        float luma = std::clamp(((*histogramImageCPU)[y][x].x - histogramData->black_level) / (histogramData->white_level - histogramData->black_level), 0.0f, 1.0f);
//
//        p->luma = (uint16_t) (0xffff * std::sqrt(luma));
//    });
//    lumaImage.write_png_file("/Users/fabio/luma.png");


    if (demosaicParameters->rgbConversionParameters.localToneMapping) {
        const std::array<const gls::mtl_image_2d<gls::rgba_pixel_float>*, 3>& guideImage = {
            _pyramidProcessor->denoisedImagePyramid[4].get(),
            _pyramidProcessor->denoisedImagePyramid[2].get(),
            _pyramidProcessor->denoisedImagePyramid[0].get()
        };
        _localToneMapping->createMask(&_mtlContext, *denoisedImage, guideImage, *noiseModel,
                                      demosaicParameters->ltmParameters, _histogramBuffer.get());
    }

//        // High ISO noise texture replacement
//        if (clBlueNoise != nullptr) {
//            const gls::Vector<2> lumaVariance = {np.first[0], np.second[0]};
//
//            LOG_INFO(TAG) << "Adding Blue Noise for variance: " << std::scientific << lumaVariance << std::endl;
//
//            const auto grainAmount = 1 + 3 * smoothstep(4e-4, 6e-4, lumaVariance[1]);
//
//            blueNoiseImage(&_mtlContext, *clDenoisedImage, *clBlueNoise, 2 * grainAmount * lumaVariance,
//                           clLinearRGBImageB.get());
//            clDenoisedImage = clLinearRGBImageB.get();
//        }

    return denoisedImage;
}

//#define RUN_ON_SINGLE_COMMAND_BUFFER true

gls::mtl_image_2d<gls::rgba_pixel_float>* RawConverter::demosaic(const gls::image<gls::luma_pixel_16>& rawImage,
                                                                 DemosaicParameters* demosaicParameters) {
    allocateTextures(rawImage.size());

    // Zero histogram data
    bzero(_histogramBuffer->contents(), _histogramBuffer->length());

    if (demosaicParameters->rgbConversionParameters.localToneMapping) {
        _localToneMapping->allocateTextures(&_mtlContext, rawImage.width, rawImage.height);
    }

    bool high_noise_image = demosaicParameters->iso >= 800;
    if (high_noise_image) {
        allocateHighNoiseTextures(rawImage.size());
    }

    // Convert linear image to YCbCr for denoising
    const auto cam_to_ycbcr = cam_ycbcr(demosaicParameters->rgb_cam, xyz_rgb());

    // Convert result back to camera RGB
    const auto ycbcr_to_cam = inverse(cam_to_ycbcr);

    _rawImage->copyPixelsFrom(rawImage);

    gls::mtl_image_2d<gls::rgba_pixel_float>* denoisedImage = nullptr;

    auto context = &_mtlContext;

    // --- Image Demosaicing ---

    _scaleRawData(context, *_rawImage, _scaledRawImage.get(),
                  demosaicParameters->bayerPattern,
                  demosaicParameters->scale_mul,
                  demosaicParameters->black_level,
                  demosaicParameters->lensShadingCorrection);

    _rawImageSobel(context, *_scaledRawImage, _rawSobelImage.get());

    NoiseModel<5>* noiseModel = &demosaicParameters->noiseModel;
    if (_calibrateFromImage) {
        noiseModel->rawNlf = MeasureRawNLF(demosaicParameters->exposure_multiplier, demosaicParameters->bayerPattern);
    }
    const auto rawVariance = getRawVariance(noiseModel->rawNlf);

    _gaussianBlurSobelImage(context, *_scaledRawImage, *_rawSobelImage, rawVariance[1], _rawGradientImage.get());

    if (high_noise_image) {
        _bayerToRawRGBA(context, *_scaledRawImage, _rgbaRawImage.get(), demosaicParameters->bayerPattern);
        _despeckleRawRGBAImage(context, *_rgbaRawImage, noiseModel->rawNlf.second, _denoisedRgbaRawImage.get());
        _rawRGBAToBayer(context, *_denoisedRgbaRawImage, _scaledRawImage.get(), demosaicParameters->bayerPattern);
    }

    _demosaicImage(context, *_scaledRawImage, *_rawGradientImage,
                   _greenImage.get(), /*rgbImageTmp=*/ _linearRGBImageB.get(), _linearRGBImageA.get(),
                   demosaicParameters->bayerPattern, rawVariance);

    _blendHighlightsImage(context, *_linearRGBImageA, /*clip=*/1.0, _linearRGBImageA.get());

    // --- Image Denoising ---

    // Convert to YCbCr
    _transformImage(context, *_linearRGBImageA, _linearRGBImageA.get(), cam_to_ycbcr);

    denoisedImage = denoise(*_linearRGBImageA, demosaicParameters);

    // Convert to RGB
    _transformImage(context, *denoisedImage, _linearRGBImageA.get(), ycbcr_to_cam);

    if (_calibrateFromImage) {
        dumpNoiseModel<5>(demosaicParameters->iso, *noiseModel);
    }

    // --- Image Post Processing ---

    // FIXME: This is horrible!
    demosaicParameters->rgbConversionParameters.exposureBias += log2(demosaicParameters->exposure_multiplier);

    _convertTosRGB(context, *_linearRGBImageA, _localToneMapping->getMask(), *demosaicParameters,
                   _histogramBuffer.get(), _linearRGBImageA.get());

    _mtlContext.waitForCompletion();

    return _linearRGBImageA.get();
}

void dumpNoiseImage(const gls::image<gls::rgba_pixel_float>& image, float a, float b, const std::string& name) {
    gls::image<gls::luma_pixel_16> luma(image.size());

    luma.apply([&image, a, b](gls::luma_pixel_16* p, int x, int y) {
        *p = std::clamp((int)(0xffff * a * (image[y][x].green + b)), 0, 0xffff);
    });
    luma.write_png_file("/Users/fabio/Statistics/" + name + ".png");
}

RawNLF RawConverter::MeasureRawNLF(float exposure_multiplier, BayerPattern bayerPattern) {
    _rawNoiseStatistics(&_mtlContext, *_scaledRawImage, bayerPattern, _meanImage.get(), _varImage.get());
    _mtlContext.waitForCompletion();

    const auto meanImageCpu = _meanImage->mapImage();
    const auto varImageCpu = _varImage->mapImage();

    static int count = 0;
    dumpNoiseImage(*meanImageCpu, 1, 0, "mean9x9-" + std::to_string(count));
    dumpNoiseImage(*varImageCpu, 100, 0, "variance9x9-" + std::to_string(count));
    count++;

    using double4 = gls::DVector<4>;

    gls::DVector<6> varianceHistogram = {0, 0, 0, 0, 0, 0};   // 1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 1e-1
    varImageCpu->apply([&](const gls::rgba_pixel_float& vv, int x, int y) {
        double4 v = vv.v;

        bool validStats = !any(isnan(v));

        if (validStats) {
            const auto scale = gls::apply(std::log10, v);

            if (max_element(scale) < -5.0) {
                varianceHistogram[0]++;
            } else if (max_element(scale) >= -5.0 && max_element(scale) < -4.0) {
                varianceHistogram[1]++;
            } else if (max_element(scale) >= -4.0 && max_element(scale) < -3.0) {
                varianceHistogram[2]++;
            } else if (max_element(scale) >= -3.0 && max_element(scale) < -2.0) {
                varianceHistogram[3]++;
            } else if (max_element(scale) >= -2.0 && max_element(scale) < -1.0) {
                varianceHistogram[4]++;
            } else if (max_element(scale) >= -1.0) {
                varianceHistogram[5]++;
            }
        }
    });

    // Only consider pixels with variance lower than the expected noise value
    double4 varianceMax = varianceHistogram[0] > 1e4 ? 1.0e-5 :
                          varianceHistogram[1] > 1e4 ? 1.0e-4 :
                          varianceHistogram[2] > 1e4 ? 1.0e-3 :
                          varianceHistogram[3] > 1e4 ? 1.0e-2 :
                                                       1.0e-1;

    // std::cout << "MeasureRawNLF - varianceHistogram: " << varianceHistogram << ", varianceMax: " << varianceMax << std::endl;

    // Limit to pixels the more linear intensity zone of the sensor
    const double maxValue = 0.9;
    const double minValue = 0.001;

    // Collect pixel statistics
    double4 s_x = 0;
    double4 s_y = 0;
    double4 s_xx = 0;
    double4 s_xy = 0;

    double N1 = 0;
    meanImageCpu->apply([&](const gls::rgba_pixel_float& mm, int x, int y) {
        double4 m = mm.v;
        double4 v = (*varImageCpu)[y][x].v;

        bool validStats = !(any(isnan(m)) || any(isnan(v)));

        if (validStats && all(m >= double4(minValue)) && all(m <= double4(maxValue)) && all(v <= varianceMax)) {
            s_x += m;
            s_y += v;
            s_xx += m * m;
            s_xy += m * v;
            N1++;
        }
    });

    // Linear regression on pixel statistics to extract a linear noise model: nlf = A + B * Y
    auto nlfB = max((N1 * s_xy - s_x * s_y) / (N1 * s_xx - s_x * s_x), 1e-8);
    auto nlfA = max((s_y - nlfB * s_x) / N1, 1e-8);

//    std::cout << "RAW NLF A: " << std::setprecision(4) << std::scientific << nlfA << ", B: " << nlfB
//                  << " on " << std::setprecision(1) << std::fixed
//                  << 100 * N1 / (_rawImage->width * _rawImage->height) << "% pixels" << std::endl;

    // Estimate regression mean square error
    double4 err2 = 0;
    meanImageCpu->apply([&](const gls::rgba_pixel_float& mm, int x, int y) {
        double4 m = mm.v;
        double4 v = (*varImageCpu)[y][x].v;

        bool validStats = !(any(isnan(m)) || any(isnan(v)));

        if (validStats && all(m >= double4(minValue)) && all(m <= double4(maxValue)) && all(v <= varianceMax)) {
            auto nlfP = nlfA + nlfB * m;
            auto diff = nlfP - v;
            err2 += diff * diff;
        }
    });
    err2 /= N1;

//    std::cout << "RAW NLF A: " << std::setprecision(4) << std::scientific << nlfA << ", B: " << nlfB
//                  << ", MSE: " << sqrt(err2) << " on " << std::setprecision(1) << std::fixed
//                  << 100 * N1 / (_rawImage->width * _rawImage->height) << "% pixels" << std::endl;

    // Update the maximum variance with the model
    varianceMax = nlfB;

    // Redo the statistics collection limiting the sample to pixels that fit well the linear model
    s_x = 0;
    s_y = 0;
    s_xx = 0;
    s_xy = 0;
    double N2 = 0;
    double4 newErr2 = 0;
    meanImageCpu->apply([&](const gls::rgba_pixel_float& mm, int x, int y) {
        double4 m = mm.v;
        double4 v = (*varImageCpu)[y][x].v;

        bool validStats = !(any(isnan(m)) || any(isnan(v)));

        if (validStats && all(m >= double4(minValue)) && all(m <= double4(maxValue)) && all(v <= varianceMax)) {
            const auto nlfP = nlfA + nlfB * m;
            const auto diff = abs(nlfP - v);
            const auto diffSquare = diff * diff;

            if (all(diffSquare <= 0.5 * err2)) {
                s_x += m;
                s_y += v;
                s_xx += m * m;
                s_xy += m * v;
                N2++;
                newErr2 += diffSquare;
            }
        }
    });
    newErr2 /= N2;

    if (N2 > 0.001 * (_rawImage->width * _rawImage->height) && !any(isnan(newErr2)) && newErr2 < err2) {
        err2 = newErr2;
        N1 = N2;

        // Estimate the new regression parameters
        nlfB = max((N2 * s_xy - s_x * s_y) / (N2 * s_xx - s_x * s_x), 1e-8);
        nlfA = max((s_y - nlfB * s_x) / N2, 1e-8);

    } else {
        std::cout << "WARNING: the second noise estimate is worse than the first..." << std::endl;
    }

    std::cout << "RAW NLF A: " << std::setprecision(4) << std::scientific << nlfA << ", B: " << nlfB
              << ", MSE: " << sqrt(err2) << " on " << std::setprecision(1) << std::fixed
              << 100 * N1 / (_rawImage->width * _rawImage->height) << "% pixels" << std::endl;

//    meanImage.unmapImage(meanImageCpu);
//    varImage.unmapImage(varImageCpu);

    double varianceExposureAdjustment = exposure_multiplier * exposure_multiplier;

    nlfA *= varianceExposureAdjustment;
    nlfB *= varianceExposureAdjustment;

    return std::pair(nlfA,  // A values
                     nlfB   // B values
    );
}
