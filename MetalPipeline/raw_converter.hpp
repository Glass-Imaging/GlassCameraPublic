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

class LocalToneMapping {
    gls::mtl_image_2d<gls::luma_pixel_float>::unique_ptr ltmMaskImage;
    gls::mtl_image_2d<gls::luma_alpha_pixel_float>::unique_ptr lfAbGfImage;
    gls::mtl_image_2d<gls::luma_alpha_pixel_float>::unique_ptr lfAbGfMeanImage;
    gls::mtl_image_2d<gls::luma_alpha_pixel_float>::unique_ptr mfAbGfImage;
    gls::mtl_image_2d<gls::luma_alpha_pixel_float>::unique_ptr mfAbGfMeanImage;
    gls::mtl_image_2d<gls::luma_alpha_pixel_float>::unique_ptr hfAbGfImage;
    gls::mtl_image_2d<gls::luma_alpha_pixel_float>::unique_ptr hfAbGfMeanImage;

   public:
    LocalToneMapping(MetalContext* mtlContext) {
        // Placeholder, only allocated if LTM is used
        ltmMaskImage = std::make_unique<gls::mtl_image_2d<gls::luma_pixel_float>>(mtlContext->device(), 1, 1);
    }

    void allocateTextures(MetalContext* mtlContext, int width, int height) {
        auto mtlDevice = mtlContext->device();

        if (ltmMaskImage->width != width || ltmMaskImage->height != height) {
            ltmMaskImage = std::make_unique<gls::mtl_image_2d<gls::luma_pixel_float>>(mtlDevice, width, height);
            lfAbGfImage =
                std::make_unique<gls::mtl_image_2d<gls::luma_alpha_pixel_float>>(mtlDevice, width / 16, height / 16);
            lfAbGfMeanImage =
                std::make_unique<gls::mtl_image_2d<gls::luma_alpha_pixel_float>>(mtlDevice, width / 16, height / 16);
            mfAbGfImage =
                std::make_unique<gls::mtl_image_2d<gls::luma_alpha_pixel_float>>(mtlDevice, width / 4, height / 4);
            mfAbGfMeanImage =
                std::make_unique<gls::mtl_image_2d<gls::luma_alpha_pixel_float>>(mtlDevice, width / 4, height / 4);
            hfAbGfImage = std::make_unique<gls::mtl_image_2d<gls::luma_alpha_pixel_float>>(mtlDevice, width, height);
            hfAbGfMeanImage = std::make_unique<gls::mtl_image_2d<gls::luma_alpha_pixel_float>>(mtlDevice, width, height);
        }
    }

    void createMask(MetalContext* mtlContext, const gls::mtl_image_2d<gls::rgba_pixel_float>& image,
                    const std::array<const gls::mtl_image_2d<gls::rgba_pixel_float>*, 3>& guideImage,
                    const NoiseModel<5>& noiseModel, const DemosaicParameters& demosaicParameters) {
        const std::array<const gls::mtl_image_2d<gls::luma_alpha_pixel_float>*, 3>& abImage = {
            lfAbGfImage.get(), mfAbGfImage.get(), hfAbGfImage.get()};
        const std::array<const gls::mtl_image_2d<gls::luma_alpha_pixel_float>*, 3>& abMeanImage = {
            lfAbGfMeanImage.get(), mfAbGfMeanImage.get(), hfAbGfMeanImage.get()};

        gls::Vector<2> nlf = {noiseModel.pyramidNlf[0].first[0], noiseModel.pyramidNlf[0].second[0]};
        localToneMappingMask(mtlContext, image, guideImage, abImage, abMeanImage, demosaicParameters.ltmParameters,
                             ycbcr_srgb, nlf, ltmMaskImage.get());
    }

    const gls::mtl_image_2d<gls::luma_pixel_float>& getMask() { return *ltmMaskImage; }
};

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

    std::unique_ptr<LocalToneMapping> localToneMapping;

public:
    RawConverter(NS::SharedPtr<MTL::Device> mtlDevice, gls::size rawImageSize = {0, 0}) :
        _mtlContext(mtlDevice),
        _rawImageSize(rawImageSize) {
            localToneMapping = std::make_unique<LocalToneMapping>(&_mtlContext);
        }

    void allocateTextures(const gls::size& imageSize);

    void allocateHighNoiseTextures(const gls::size& imageSize);

    gls::mtl_image_2d<gls::rgba_pixel_float>* denoise(const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                                                      DemosaicParameters* demosaicParameters, bool calibrateFromImage);

    gls::mtl_image_2d<gls::rgba_pixel_float>* demosaic(const gls::image<gls::luma_pixel_16>& rawImage,
                                                       DemosaicParameters* demosaicParameters);
};

#endif /* raw_converter_hpp */
