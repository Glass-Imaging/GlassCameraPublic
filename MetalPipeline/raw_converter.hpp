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

#ifndef raw_converter_hpp
#define raw_converter_hpp

#include "gls_mtl_image.hpp"
#include "gls_mtl.hpp"

#include "demosaic_mtl.hpp"
#include "pyramid_processor.hpp"

class RawConverter {
    MetalContext _mtlContext;
    gls::size _rawImageSize;

    gls::mtl_image_2d<gls::luma_pixel_16>::unique_ptr _rawImage;
    gls::mtl_image_2d<gls::luma_pixel_float>::unique_ptr _scaledRawImage;
    gls::mtl_image_2d<gls::rgba_pixel_float>::unique_ptr _rawSobelImage;
    gls::mtl_image_2d<gls::luma_alpha_pixel_float>::unique_ptr _rawGradientImage;
    gls::mtl_image_2d<gls::luma_pixel_float>::unique_ptr _greenImage;
    gls::mtl_image_2d<gls::rgba_pixel_float>::unique_ptr _linearRGBImageA;
    gls::mtl_image_2d<gls::rgba_pixel_float>::unique_ptr _linearRGBImageB;
    gls::mtl_image_2d<gls::luma_pixel_float>::unique_ptr _ltmMaskImage;

    // RawConverter HighNoise textures
    gls::mtl_image_2d<gls::rgba_pixel_float>::unique_ptr _rgbaRawImage;
    gls::mtl_image_2d<gls::rgba_pixel_float>::unique_ptr _denoisedRgbaRawImage;
    gls::mtl_image_2d<gls::luma_pixel_16>::unique_ptr _blueNoise;

    std::unique_ptr<PyramidProcessor<5>> pyramidProcessor;

public:
    RawConverter(NS::SharedPtr<MTL::Device> mtlDevice, gls::size rawImageSize = {0, 0}) :
        _mtlContext(mtlDevice),
        _rawImageSize(rawImageSize) { }

    void allocateTextures(const gls::size& rawImageSize) {
        assert(rawImageSize.width > 0 && rawImageSize.height > 0);

        if (_rawImageSize != rawImageSize) {
            std::cout << "Reallocating RawConverter textures" << std::endl;

            auto mtlDevice = _mtlContext.device();

            _rawImage = std::make_unique<gls::mtl_image_2d<gls::luma_pixel_16>>(mtlDevice, rawImageSize);
            _scaledRawImage = std::make_unique<gls::mtl_image_2d<gls::luma_pixel_float>>(mtlDevice, rawImageSize);
            _rawSobelImage = std::make_unique<gls::mtl_image_2d<gls::rgba_pixel_float>>(mtlDevice, rawImageSize);
            _rawGradientImage = std::make_unique<gls::mtl_image_2d<gls::luma_alpha_pixel_float>>(mtlDevice, rawImageSize);
            _greenImage = std::make_unique<gls::mtl_image_2d<gls::luma_pixel_float>>(mtlDevice, rawImageSize);
            _linearRGBImageA = std::make_unique<gls::mtl_image_2d<gls::rgba_pixel_float>>(mtlDevice, rawImageSize);
            _linearRGBImageB = std::make_unique<gls::mtl_image_2d<gls::rgba_pixel_float>>(mtlDevice, rawImageSize);
            _ltmMaskImage = std::make_unique<gls::mtl_image_2d<gls::luma_pixel_float>>(mtlDevice, gls::size {1, 1});

            _rawImageSize = rawImageSize;

            pyramidProcessor = std::make_unique<PyramidProcessor<5>>(&_mtlContext, _rawImageSize.width, _rawImageSize.height);
        }
    }

    void allocateHighNoiseTextures() {
        int width = _rawImage->width;
        int height = _rawImage->height;

        if (!_rgbaRawImage || _rgbaRawImage->width != width / 2 || _rgbaRawImage->height != height / 2) {
            auto mtlDevice = _mtlContext.device();

            _rgbaRawImage = std::make_unique<gls::mtl_image_2d<gls::rgba_pixel_float>>(mtlDevice, width / 2, height / 2);
            _denoisedRgbaRawImage =
                std::make_unique<gls::mtl_image_2d<gls::rgba_pixel_float>>(mtlDevice, width / 2, height / 2);
        }
    }

    gls::mtl_image_2d<gls::rgba_pixel_float>* denoise(
        const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage, DemosaicParameters* demosaicParameters,
        bool calibrateFromImage) {
        NoiseModel<5>* noiseModel = &demosaicParameters->noiseModel;

        // Luma and Chroma Despeckling
//        const auto& np = noiseModel->pyramidNlf[0];
//        despeckleImage(&_mtlContext, inputImage,
//                       /*var_a=*/np.first,
//                       /*var_b=*/np.second, clLinearRGBImageB.get());

        gls::mtl_image_2d<gls::rgba_pixel_float>* clDenoisedImage = pyramidProcessor->denoise(
            &_mtlContext, &(demosaicParameters->denoiseParameters), inputImage /*_linearRGBImageB*/, *_rawGradientImage,
            &(noiseModel->pyramidNlf), demosaicParameters->exposure_multiplier, calibrateFromImage);

//        if (demosaicParameters->rgbConversionParameters.localToneMapping) {
//            const std::array<const gls::mtl_image_2d<gls::rgba_pixel_float>*, 3>& guideImage = {
//                pyramidProcessor->denoisedImagePyramid[4].get(), pyramidProcessor->denoisedImagePyramid[2].get(),
//                pyramidProcessor->denoisedImagePyramid[0].get()};
//            localToneMapping->createMask(&_mtlContext, *_denoisedImage, guideImage, *noiseModel, *demosaicParameters);
//        }

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

        return clDenoisedImage;
    }

    gls::mtl_image_2d<gls::rgba_pixel_float>* demosaic(const gls::image<gls::luma_pixel_16>& rawImage,
                                                       DemosaicParameters* demosaicParameters) {
        const NoiseModel<5>* noiseModel = &demosaicParameters->noiseModel;
        const auto rawVariance = getRawVariance(noiseModel->rawNlf);

        allocateTextures(rawImage.size());

        _rawImage->copyPixelsFrom(rawImage);

        scaleRawData(&_mtlContext, *_rawImage, _scaledRawImage.get(),
                     demosaicParameters->bayerPattern,
                     demosaicParameters->scale_mul,
                     demosaicParameters->black_level / 0xffff);

        rawImageSobel(&_mtlContext, *_scaledRawImage, _rawSobelImage.get());

        bool high_noise_image = true;
        if (high_noise_image) {
            std::cout << "Despeckeling RAW Image" << std::endl;

            allocateHighNoiseTextures();

            bayerToRawRGBA(&_mtlContext, *_scaledRawImage, _rgbaRawImage.get(), demosaicParameters->bayerPattern);

            despeckleRawRGBAImage(&_mtlContext, *_rgbaRawImage, noiseModel->rawNlf.second, _denoisedRgbaRawImage.get());

            rawRGBAToBayer(&_mtlContext, *_denoisedRgbaRawImage, _scaledRawImage.get(), demosaicParameters->bayerPattern);
        }

        gaussianBlurSobelImage(&_mtlContext, *_scaledRawImage, *_rawSobelImage, rawVariance[1], 1.5, 4.5, _rawGradientImage.get());

        interpolateGreen(&_mtlContext, *_scaledRawImage, *_rawGradientImage, _greenImage.get(),
                         demosaicParameters->bayerPattern, rawVariance[1]);

        interpolateRedBlue(&_mtlContext, *_scaledRawImage, *_greenImage, *_rawGradientImage, _linearRGBImageA.get(),
                           demosaicParameters->bayerPattern, rawVariance[0], rawVariance[2]);

        interpolateRedBlueAtGreen(&_mtlContext, *_linearRGBImageA, *_rawGradientImage, _linearRGBImageA.get(),
                                  demosaicParameters->bayerPattern, rawVariance[0], rawVariance[2]);

        blendHighlightsImage(&_mtlContext, *_linearRGBImageA, /*clip=*/1.0, _linearRGBImageA.get());

        // --- Image Denoising ---

        // Convert linear image to YCbCr for denoising
        const auto cam_to_ycbcr = cam_ycbcr(demosaicParameters->rgb_cam);

        transformImage(&_mtlContext, *_linearRGBImageA, _linearRGBImageA.get(), cam_to_ycbcr);

        const auto _denoisedImage = denoise(*_linearRGBImageA, demosaicParameters, /*calibrateFromImage*/ false);

        // Convert result back to camera RGB
        const auto normalized_ycbcr_to_cam = inverse(cam_to_ycbcr) * demosaicParameters->exposure_multiplier;
        transformImage(&_mtlContext, *_denoisedImage, _linearRGBImageA.get(), normalized_ycbcr_to_cam);

        convertTosRGB(&_mtlContext, *_linearRGBImageA, *_ltmMaskImage, _linearRGBImageA.get(), *demosaicParameters);

        _mtlContext.waitForCompletion();

        return _linearRGBImageA.get();
    }
};

#endif /* raw_converter_hpp */
