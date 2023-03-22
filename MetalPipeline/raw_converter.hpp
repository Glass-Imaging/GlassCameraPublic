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

class RawConverter {
    NS::SharedPtr<MTL::Device> _mtlDevice;
    gls::size _rawImageSize;

    gls::mtl_image_2d<gls::luma_pixel_16>::unique_ptr _rawImage;
    gls::mtl_image_2d<gls::luma_pixel_float>::unique_ptr _scaledRawImage;
    gls::mtl_image_2d<gls::rgba_pixel_float>::unique_ptr _rawSobelImage;
    gls::mtl_image_2d<gls::luma_alpha_pixel_float>::unique_ptr _rawGradientImage;
    gls::mtl_image_2d<gls::luma_pixel_float>::unique_ptr _greenImage;
    gls::mtl_image_2d<gls::rgba_pixel_float>::unique_ptr _linearRGBImageA;
    gls::mtl_image_2d<gls::rgba_pixel_float>::unique_ptr _linearRGBImageB;
    gls::mtl_image_2d<gls::luma_pixel_float>::unique_ptr _ltmMaskImage;

public:
    RawConverter(NS::SharedPtr<MTL::Device> mtlDevice, gls::size rawImageSize = {0, 0}) :
        _mtlDevice(mtlDevice),
        _rawImageSize(rawImageSize) { }

    void buildTextures(const gls::size& rawImageSize) {
        assert(rawImageSize.width > 0 && rawImageSize.height > 0);

        if (_rawImageSize != rawImageSize) {
            _rawImage = std::make_unique<gls::mtl_image_2d<gls::luma_pixel_16>>(_mtlDevice.get(), rawImageSize);
            _scaledRawImage = std::make_unique<gls::mtl_image_2d<gls::luma_pixel_float>>(_mtlDevice.get(), rawImageSize);
            _rawSobelImage = std::make_unique<gls::mtl_image_2d<gls::rgba_pixel_float>>(_mtlDevice.get(), rawImageSize);
            _rawGradientImage = std::make_unique<gls::mtl_image_2d<gls::luma_alpha_pixel_float>>(_mtlDevice.get(), rawImageSize);
            _greenImage = std::make_unique<gls::mtl_image_2d<gls::luma_pixel_float>>(_mtlDevice.get(), rawImageSize);
            _linearRGBImageA = std::make_unique<gls::mtl_image_2d<gls::rgba_pixel_float>>(_mtlDevice.get(), rawImageSize);
            _linearRGBImageB = std::make_unique<gls::mtl_image_2d<gls::rgba_pixel_float>>(_mtlDevice.get(), rawImageSize);
            _ltmMaskImage = std::make_unique<gls::mtl_image_2d<gls::luma_pixel_float>>(_mtlDevice.get(), gls::size {1, 1});

            _rawImageSize = rawImageSize;
        }
    }

    gls::mtl_image_2d<gls::rgba_pixel_float>* demosaic(const gls::image<gls::luma_pixel_16>& rawImage,
                                                       const DemosaicParameters& demosaicParameters) {
        const NoiseModel<5>* noiseModel = &demosaicParameters.noiseModel;
        const auto rawVariance = getRawVariance(noiseModel->rawNlf);

        buildTextures(rawImage.size());

        _rawImage->copyPixelsFrom(rawImage);

        auto mtlContext = MetalContext(_mtlDevice);

        scaleRawData(&mtlContext, *_rawImage, _scaledRawImage.get(),
                     demosaicParameters.bayerPattern,
                     demosaicParameters.scale_mul,
                     demosaicParameters.black_level / 0xffff);

        rawImageSobel(&mtlContext, *_scaledRawImage, _rawSobelImage.get());

        gaussianBlurSobelImage(&mtlContext, *_scaledRawImage, *_rawSobelImage, rawVariance[1], 1.5, 4.5, _rawGradientImage.get());

        interpolateGreen(&mtlContext, *_scaledRawImage, *_rawGradientImage, _greenImage.get(),
                         demosaicParameters.bayerPattern, rawVariance[1]);

        interpolateRedBlue(&mtlContext, *_scaledRawImage, *_greenImage, *_rawGradientImage, _linearRGBImageA.get(),
                           demosaicParameters.bayerPattern, rawVariance[0], rawVariance[2]);

        interpolateRedBlueAtGreen(&mtlContext, *_linearRGBImageA, *_rawGradientImage, _linearRGBImageA.get(),
                                  demosaicParameters.bayerPattern, rawVariance[0], rawVariance[2]);

        blendHighlightsImage(&mtlContext, *_linearRGBImageA, /*clip=*/1.0, _linearRGBImageA.get());

        convertTosRGB(&mtlContext, *_linearRGBImageA, *_ltmMaskImage, _linearRGBImageA.get(), demosaicParameters);

        mtlContext.waitForCompletion();

        return _linearRGBImageA.get();
    }
};

#endif /* raw_converter_hpp */
