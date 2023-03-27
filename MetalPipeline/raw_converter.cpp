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

gls::mtl_image_2d<gls::rgba_pixel_float>* RawConverter::denoise(const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                                                                DemosaicParameters* demosaicParameters, bool calibrateFromImage) {
    NoiseModel<5>* noiseModel = &demosaicParameters->noiseModel;

    // Luma and Chroma Despeckling
    const auto& np = noiseModel->pyramidNlf[0];
    despeckleImage(&_mtlContext, inputImage,
                   /*var_a=*/np.first,
                   /*var_b=*/np.second, _linearRGBImageB.get());

    gls::mtl_image_2d<gls::rgba_pixel_float>* denoisedImage = _pyramidProcessor->denoise(
        &_mtlContext, &(demosaicParameters->denoiseParameters), *_linearRGBImageB, *_rawGradientImage,
        &(noiseModel->pyramidNlf), demosaicParameters->exposure_multiplier, calibrateFromImage);

        if (demosaicParameters->rgbConversionParameters.localToneMapping) {
            const std::array<const gls::mtl_image_2d<gls::rgba_pixel_float>*, 3>& guideImage = {
                _pyramidProcessor->denoisedImagePyramid[4].get(), _pyramidProcessor->denoisedImagePyramid[2].get(),
                _pyramidProcessor->denoisedImagePyramid[0].get()};
            _localToneMapping->createMask(&_mtlContext, *denoisedImage, guideImage, *noiseModel, *demosaicParameters);
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

gls::mtl_image_2d<gls::rgba_pixel_float>* RawConverter::demosaic(const gls::image<gls::luma_pixel_16>& rawImage,
                                                                 DemosaicParameters* demosaicParameters) {
    const NoiseModel<5>* noiseModel = &demosaicParameters->noiseModel;
    const auto rawVariance = getRawVariance(noiseModel->rawNlf);

    allocateTextures(rawImage.size());

    if (demosaicParameters->rgbConversionParameters.localToneMapping) {
        _localToneMapping->allocateTextures(&_mtlContext, rawImage.width, rawImage.height);
    }

    _rawImage->copyPixelsFrom(rawImage);

    scaleRawData(&_mtlContext, *_rawImage, _scaledRawImage.get(),
                 demosaicParameters->bayerPattern,
                 demosaicParameters->scale_mul,
                 demosaicParameters->black_level / 0xffff);

    rawImageSobel(&_mtlContext, *_scaledRawImage, _rawSobelImage.get());

    gaussianBlurSobelImage(&_mtlContext, *_scaledRawImage, *_rawSobelImage, rawVariance[1], 1.5, 4.5, _rawGradientImage.get());

    bool high_noise_image = true;
    if (high_noise_image) {
        allocateHighNoiseTextures(rawImage.size());

        bayerToRawRGBA(&_mtlContext, *_scaledRawImage, _rgbaRawImage.get(), demosaicParameters->bayerPattern);

        despeckleRawRGBAImage(&_mtlContext, *_rgbaRawImage, noiseModel->rawNlf.second, _denoisedRgbaRawImage.get());

        rawRGBAToBayer(&_mtlContext, *_denoisedRgbaRawImage, _scaledRawImage.get(), demosaicParameters->bayerPattern);
    }

    interpolateGreen(&_mtlContext, *_scaledRawImage, *_rawGradientImage, _greenImage.get(),
                     demosaicParameters->bayerPattern, rawVariance[1]);

    interpolateRedBlue(&_mtlContext, *_scaledRawImage, *_greenImage, *_rawGradientImage, _linearRGBImageA.get(),
                       demosaicParameters->bayerPattern, rawVariance[0], rawVariance[2]);

    interpolateRedBlueAtGreen(&_mtlContext, *_linearRGBImageA, *_rawGradientImage, _linearRGBImageA.get(),
                              demosaicParameters->bayerPattern, rawVariance[0], rawVariance[2]);

    blendHighlightsImage(&_mtlContext, *_linearRGBImageA, /*clip=*/1.0, _linearRGBImageA.get());

    // --- Image Denoising ---

    // Convert linear image to YCbCr for denoising
    const auto cam_to_ycbcr = cam_ycbcr(demosaicParameters->rgb_cam, xyz_rgb());

    transformImage(&_mtlContext, *_linearRGBImageA, _linearRGBImageA.get(), cam_to_ycbcr);

    const auto _denoisedImage = denoise(*_linearRGBImageA, demosaicParameters, /*calibrateFromImage*/ false);

    // Convert result back to camera RGB
    const auto normalized_ycbcr_to_cam = inverse(cam_to_ycbcr) * demosaicParameters->exposure_multiplier;
    transformImage(&_mtlContext, *_denoisedImage, _linearRGBImageA.get(), normalized_ycbcr_to_cam);

    convertTosRGB(&_mtlContext, *_linearRGBImageA, _localToneMapping->getMask(), _linearRGBImageA.get(), *demosaicParameters);

    _mtlContext.waitForCompletion();

    return _linearRGBImageA.get();
}

// Alternative ways to run the Metal pipeline

//        auto kernel = Kernel<MTL::Texture*,     // rawImage
//                             MTL::Texture*,     // scaledRawImage
//                             int,               // bayerPattern
//                             simd::float4,      // scaleMul
//                             float              // blackLevel
//                             >(&mtlContext, "scaleRawData");

//        const auto scaleMul = demosaicParameters->scale_mul;

//        kernel(&mtlContext, /*gridSize=*/ { (unsigned) scaledRawImage.width / 2, (unsigned) scaledRawImage.height / 2, 1 },
//               rawImage->texture(), scaledRawImage.texture(), demosaicParameters->bayerPattern,
//               simd::float4 { scaleMul[0], scaleMul[1], scaleMul[2], scaleMul[3] },
//               demosaicParameters->black_level / 0xffff);

//        mtlContext.scheduleOnCommandBuffer([&] (MTL::CommandBuffer* commandBuffer) {
//            for (int channel = 0; channel < 3; channel++) {
//                mtlContext.wait(commandBuffer);
//
//                kernel(commandBuffer, /*gridSize=*/ { (unsigned) scaledRawImage.width / 2, (unsigned) scaledRawImage.height / 2, 1 },
//                       rawImage->texture(), scaledRawImage.texture(), demosaicParameters->bayerPattern,
//                       simd::float4 { scaleMul[0], scaleMul[1], scaleMul[2], scaleMul[3] },
//                       demosaicParameters->black_level / 0xffff);
//
//                mtlContext.signal(commandBuffer);
//            }
//        }, [&] (MTL::CommandBuffer* commandBuffer) {
//            if (commandBuffer->status() == MTL::CommandBufferStatusCompleted) {
//                const auto start = commandBuffer->GPUStartTime();
//                const auto end = commandBuffer->GPUEndTime();
//
//                std::cout << "Metal execution done, execution time: " << end - start << std::endl;
//
//                const auto scaledRawImageCpu = scaledRawImage.mapImage();
//
//                gls::image<gls::luma_pixel> saveImage(scaledRawImageCpu->width, scaledRawImageCpu->height);
//
//                saveImage.apply([&](gls::luma_pixel* p, int x, int y) {
//                    p->luma = 255 * (*scaledRawImageCpu)[y][x].luma;
//                });
//
//                saveImage.write_png_file("/Users/fabio/scaled.png");
//            } else {
//                std::cout << "Something wrong with Metal execution: " << commandBuffer->status() << std::endl;
//            }
//        });
