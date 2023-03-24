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

#ifndef pyramid_processor_hpp
#define pyramid_processor_hpp

#include "demosaic.hpp"
#include "demosaic_mtl.hpp"

template <size_t levels>
struct PyramidProcessor {
    const int width, height;
    int fusedFrames;

    typedef gls::mtl_image_2d<gls::rgba_pixel_float> imageType;
    std::array<imageType::unique_ptr, levels - 1> imagePyramid;
    std::array<gls::mtl_image_2d<gls::luma_alpha_pixel_float>::unique_ptr, levels - 1> gradientPyramid;
    std::array<imageType::unique_ptr, levels> subtractedImagePyramid;
    std::array<imageType::unique_ptr, levels> denoisedImagePyramid;

//    std::array<imageType::unique_ptr, levels> fusionImagePyramidA;
//    std::array<imageType::unique_ptr, levels> fusionImagePyramidB;
//    std::array<imageType::unique_ptr, levels> fusionReferenceImagePyramid;
//    std::array<gls::mtl_image_2d<gls::luma_alpha_pixel_float>::unique_ptr, levels> fusionReferenceGradientPyramid;
//    std::array<imageType::unique_ptr, levels>* fusionBuffer[2];

    PyramidProcessor(MetalContext* mtlContext, int width, int height);

    imageType* denoise(MetalContext* mtlContext, std::array<DenoiseParameters, levels>* denoiseParameters,
                       const imageType& image, const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
                       std::array<YCbCrNLF, levels>* nlfParameters, float exposure_multiplier,
                       bool calibrateFromImage = false);

//    void fuseFrame(MetalContext* mtlContext, std::array<DenoiseParameters, levels>* denoiseParameters,
//                   const imageType& image, const gls::Matrix<3, 3>& homography,
//                   const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
//                   std::array<YCbCrNLF, levels>* nlfParameters, float exposure_multiplier,
//                   bool calibrateFromImage = false);
//
//    imageType* getFusedImage(MetalContext* mtlContext);
};

#endif /* pyramid_processor_hpp */
