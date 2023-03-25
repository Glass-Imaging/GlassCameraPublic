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

    void allocateTextures(const gls::size& rawImageSize);

    void allocateHighNoiseTextures();

    gls::mtl_image_2d<gls::rgba_pixel_float>* denoise(const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                                                      DemosaicParameters* demosaicParameters, bool calibrateFromImage);

    gls::mtl_image_2d<gls::rgba_pixel_float>* demosaic(const gls::image<gls::luma_pixel_16>& rawImage,
                                                       DemosaicParameters* demosaicParameters);
};

#endif /* raw_converter_hpp */
