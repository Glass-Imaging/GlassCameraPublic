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

#include "SimplexNoise.hpp"

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

void RawConverter::allocateLtmImagePyramid(const gls::size& imageSize) {
    if (_ltmImagePyramid[0] == nullptr || _ltmImagePyramid[0]->width != imageSize.width / 2 || _ltmImagePyramid[0]->height != imageSize.height / 2) {
        auto mtlDevice = _mtlContext.device();
        int levels = (int) _ltmImagePyramid.size() + 1;
        for (int i = 0, scale = 2; i < levels - 1; i++, scale *= 2) {
            _ltmImagePyramid[i] = std::make_unique<gls::mtl_image_2d<gls::rgba_pixel_float>>(mtlDevice, imageSize.width / scale, imageSize.height / scale);
        }
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
                                                                                         demosaicParameters->lensShadingCorrection,
                                                                                         _calibrateFromImage);

    // Use a lower level of the pyramid to compute the histogram
    const auto histogramImage = _pyramidProcessor->denoisedImagePyramid[3].get();
    _histogramImage(&_mtlContext, *histogramImage);
    _histogramImage.statistics(&_mtlContext, histogramImage->size());

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
        _localToneMapping->createMask(&_mtlContext, *denoisedImage, *_rawGradientImage, guideImage, *noiseModel,
                                      demosaicParameters->ltmParameters, _histogramImage.buffer());
    }

    return denoisedImage;
}

void saveLumaImage(const gls::mtl_image_2d<gls::luma_pixel_float>& denoisedImage) {
    gls::image<gls::luma_pixel_16> out(denoisedImage.width, denoisedImage.height);
    const auto denoisedImageCPU = denoisedImage.mapImage();
    out.apply([&denoisedImageCPU](gls::luma_pixel_16* p, int x, int y) {
        const auto& ip = (*denoisedImageCPU)[y][x];
        *p = {
            (uint16_t) std::clamp((int)(0xffff * ip), 0, 0xffff)
        };
    });
    // denoisedImage.unmapImage(denoisedImageCPU);
    static int count = 1;
    out.write_png_file("/Users/fabio/green" + std::to_string(count++) + ".png");
}

void saveRawChannels(gls::image<gls::rgba_pixel_float>& rgbaRawImage, const std::string& postfix) {
    std::array<gls::image<gls::luma_pixel_16>::unique_ptr, 4> saveImages;
    for (auto& img : saveImages) {
        img = std::make_unique<gls::image<gls::luma_pixel_16>>(rgbaRawImage.size());
    }
    rgbaRawImage.apply([&] (const gls::rgba_pixel_float& p, int x, int y){
        for (int c = 0; c < 4; c++) {
            (*saveImages[c])[y][x] = std::clamp(0xffff * p[c], 0.0f, (float) 0xffff);
        }
    });
    saveImages[0]->write_png_file("/Users/fabio/red_" + postfix + ".png");
    saveImages[1]->write_png_file("/Users/fabio/green1_" + postfix + ".png");
    saveImages[2]->write_png_file("/Users/fabio/blue_" + postfix + ".png");
    saveImages[3]->write_png_file("/Users/fabio/green2_" + postfix + ".png");
}

gls::mtl_image_2d<gls::rgba_pixel_float>* RawConverter::demosaic(const gls::image<gls::luma_pixel_16>& rawImage,
                                                                 DemosaicParameters* demosaicParameters) {
    allocateTextures(rawImage.size());

    // Zero histogram data
    _histogramImage.reset();

    if (demosaicParameters->rgbConversionParameters.localToneMapping) {
        _localToneMapping->allocateTextures(&_mtlContext, rawImage.width, rawImage.height);
    }

    bool high_noise_image = _calibrateFromImage ? false : demosaicParameters->rawDenoiseParameters.highNoiseImage;
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
                  demosaicParameters->black_level / 0xffff,
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

        _despeckleRawRGBAImage(context, *_rgbaRawImage, *_rawGradientImage, noiseModel->rawNlf.second, _denoisedRgbaRawImage.get());
        _crossDenoiseRawRGBAImage(context, *_denoisedRgbaRawImage, noiseModel->rawNlf.second, demosaicParameters->rawDenoiseParameters.strength, _rgbaRawImage.get());

        _rawRGBAToBayer(context, *_rgbaRawImage, _scaledRawImage.get(), demosaicParameters->bayerPattern);
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

    // Use the first pixel value of the image as a seed for the noise to have a stable noise pattern for every given image
    _convertTosRGB.randomSeed(rawImage[0][0]);
    _convertTosRGB.initGradients();

    _convertTosRGB(context, *_linearRGBImageA, _localToneMapping->getMask(), *demosaicParameters,
                   _histogramImage.buffer(), /*luma_nlf=*/ 2.0f * rawVariance[1], _linearRGBImageA.get());

    _mtlContext.waitForCompletion();

    return _linearRGBImageA.get();
}

gls::mtl_image_2d<gls::rgba_pixel_float>* RawConverter::postprocess(gls::image<gls::rgba_pixel_float>& rgbImage, DemosaicParameters* demosaicParameters) {
    allocateTextures(rgbImage.size());

    // Zero histogram data
    _histogramImage.reset();

    if (demosaicParameters->rgbConversionParameters.localToneMapping) {
        _localToneMapping->allocateTextures(&_mtlContext, rgbImage.width, rgbImage.height);
    }

    allocateLtmImagePyramid(rgbImage.size());

    gls::Vector<3> normalized_scale_mul = { demosaicParameters->scale_mul[0], demosaicParameters->scale_mul[1], demosaicParameters->scale_mul[2] };
    normalized_scale_mul /= *std::max_element(std::begin(normalized_scale_mul), std::end(normalized_scale_mul));

    const auto exposure_multiplier = std::max(demosaicParameters->raw_exposure_multiplier, 1.0f);

    const auto scaled_black_level = demosaicParameters->black_level / demosaicParameters->white_level;

    rgbImage.apply([&] (gls::rgba_pixel_float* p, int x, int y) {
        gls::Vector<3> v = { (*p)[0], (*p)[1], (*p)[2] };

        v = clamp((2 * exposure_multiplier * max(v - scaled_black_level, 0.0f) * normalized_scale_mul) * 0.9f + 0.1f, 0.0f, 1.0f);

        *p = {
            (float16_t) v[0],
            (float16_t) v[1],
            (float16_t) v[2],
            1
        };
    });

    _linearRGBImageA->copyPixelsFrom(rgbImage);

    // Convert linear image to YCbCr for denoising
    const auto cam_to_ycbcr = cam_ycbcr(demosaicParameters->rgb_cam, xyz_rgb());

    // Convert result back to camera RGB
    const auto ycbcr_to_cam = inverse(cam_to_ycbcr);

    NoiseModel<5>* noiseModel = &demosaicParameters->noiseModel;

    auto context = &_mtlContext;

    // histogram_data* hd = histogramData();

    // Convert to YCbCr
    _transformImage(context, *_linearRGBImageA, _linearRGBImageB.get(), cam_to_ycbcr);

    for (int i = 0; i < 4; i++) {
        const auto currentLayer = i > 0 ? _ltmImagePyramid[i - 1].get() : _linearRGBImageB.get();
        _pyramidProcessor->_resampleImage(context, *currentLayer, _ltmImagePyramid[i].get());
    }

    // Use a lower level of the pyramid to compute the histogram
    const auto histogramImage = _ltmImagePyramid[2].get();
    _histogramImage(&_mtlContext, *histogramImage);
    _histogramImage.statistics(&_mtlContext, histogramImage->size());

    if (demosaicParameters->rgbConversionParameters.localToneMapping) {
        const std::array<const gls::mtl_image_2d<gls::rgba_pixel_float>*, 3>& guideImage = {
            _ltmImagePyramid[3].get(),
            _ltmImagePyramid[1].get(),
            _linearRGBImageB.get()
        };
        _localToneMapping->createMask(&_mtlContext, *_linearRGBImageB, *_rawGradientImage, guideImage, *noiseModel,
                                      demosaicParameters->ltmParameters, _histogramImage.buffer());
    }

    // Convert to RGB
    _transformImage(context, *_linearRGBImageB, _linearRGBImageA.get(), ycbcr_to_cam);

    _convertTosRGB(context, *_linearRGBImageA, _localToneMapping->getMask(), *demosaicParameters,
                   _histogramImage.buffer(), /*lumaVariance=*/{0, 0}, _linearRGBImageA.get());

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

//    static int count = 0;
//    dumpNoiseImage(*meanImageCpu, 1, 0, "mean9x9-" + std::to_string(count));
//    dumpNoiseImage(*varImageCpu, 100, 0, "variance9x9-" + std::to_string(count));
//    count++;

    using double4 = gls::DVector<4>;

    gls::DVector<6> varianceHistogram = {0, 0, 0, 0, 0, 0};   // 1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 1e-1
    varImageCpu->apply([&](const gls::rgba_pixel_float& vv, int x, int y) {
        double4 v = vv.v;

        bool validStats = !any(isnan(v));

        if (validStats) {
            const auto scale = gls::apply(std::log10, v);

            int entry = 6 + (int) std::clamp(max_element(scale), -6.0, -1.0);
            varianceHistogram[entry]++;
        }
    });

    // Only consider pixels with variance lower than the expected noise value
    double4 varianceMax = varianceHistogram[0] > 1e3 ? 1.0e-5 :
                          varianceHistogram[1] > 1e3 ? 1.0e-4 :
                          varianceHistogram[2] > 1e3 ? 1.0e-3 :
                          varianceHistogram[3] > 1e3 ? 1.0e-2 :
                          varianceHistogram[4] > 1e3 ? 1.0e-1 : 1;

    std::cout << "MeasureRawNLF - varianceHistogram: " << varianceHistogram << ", varianceMax: " << varianceMax << std::endl;

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
