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

gls::mtl_image_2d<gls::rgba_pixel_float>* RawConverter::denoise(MetalContext* context, const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                                                                DemosaicParameters* demosaicParameters, bool calibrateFromImage) {
    NoiseModel<5>* noiseModel = &demosaicParameters->noiseModel;

    // Luma and Chroma Despeckling
    const auto& np = noiseModel->pyramidNlf[0];
    _despeckleImage(context, inputImage,
                    /*var_a=*/np.first,
                    /*var_b=*/np.second, _linearRGBImageB.get());

    gls::mtl_image_2d<gls::rgba_pixel_float>* denoisedImage = _pyramidProcessor->denoise(context, &(demosaicParameters->denoiseParameters),
                                                                                         *_linearRGBImageB, *_rawGradientImage,
                                                                                         &(noiseModel->pyramidNlf), demosaicParameters->exposure_multiplier,
                                                                                         calibrateFromImage);

    // Use a lower level of the pyramid to compute the histogram
    const auto histogramImage = _pyramidProcessor->denoisedImagePyramid[3].get();
    _histogramImage(context, *histogramImage, _histogramBuffer.get());
    _histogramStatistics(context, _histogramBuffer.get(), histogramImage->size());

    if (demosaicParameters->rgbConversionParameters.localToneMapping) {
        const std::array<const gls::mtl_image_2d<gls::rgba_pixel_float>*, 3>& guideImage = {
            _pyramidProcessor->denoisedImagePyramid[4].get(),
            _pyramidProcessor->denoisedImagePyramid[2].get(),
            _pyramidProcessor->denoisedImagePyramid[0].get()
        };
        _localToneMapping->createMask(context, *denoisedImage, guideImage, *noiseModel,
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
//            blueNoiseImage(context, *clDenoisedImage, *clBlueNoise, 2 * grainAmount * lumaVariance,
//                           clLinearRGBImageB.get());
//            clDenoisedImage = clLinearRGBImageB.get();
//        }

    return denoisedImage;
}

//#define RUN_ON_SINGLE_COMMAND_BUFFER true

gls::mtl_image_2d<gls::rgba_pixel_float>* RawConverter::demosaic(const gls::image<gls::luma_pixel_16>& rawImage,
                                                                 DemosaicParameters* demosaicParameters) {
    const NoiseModel<5>* noiseModel = &demosaicParameters->noiseModel;
    const auto rawVariance = getRawVariance(noiseModel->rawNlf);

    allocateTextures(rawImage.size());

    // Zero histogram data
    bzero(_histogramBuffer->contents(), _histogramBuffer->length());

    if (demosaicParameters->rgbConversionParameters.localToneMapping) {
        _localToneMapping->allocateTextures(&_mtlContext, rawImage.width, rawImage.height);
    }

    bool high_noise_image = true;
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

    denoisedImage = denoise(context, *_linearRGBImageA, demosaicParameters, /*calibrateFromImage=*/ false);

    // Convert to RGB
    _transformImage(context, *denoisedImage, _linearRGBImageA.get(), ycbcr_to_cam);

    // --- Image Post Processing ---

    // FIXME: This is horrible!
    demosaicParameters->rgbConversionParameters.exposureBias += log2(demosaicParameters->exposure_multiplier);

    _convertTosRGB(context, *_linearRGBImageA, _localToneMapping->getMask(), *demosaicParameters,
                   _histogramBuffer.get(), _linearRGBImageA.get());

    _mtlContext.waitForCompletion();

    return _linearRGBImageA.get();
}
