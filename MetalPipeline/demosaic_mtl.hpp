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

#ifndef demosaic_mtl_hpp
#define demosaic_mtl_hpp

#include "gls_image.hpp"
#include "gls_mtl_image.hpp"
#include "gls_mtl.hpp"

#include "demosaic.hpp"

static inline std::array<gls::Vector<2>, 3> getRawVariance(const RawNLF& rawNLF) {
    const gls::Vector<2> greenVariance = {(rawNLF.first[1] + rawNLF.first[3]) / 2,
                                          (rawNLF.second[1] + rawNLF.second[3]) / 2};
    const gls::Vector<2> redVariance = {rawNLF.first[0], rawNLF.second[0]};
    const gls::Vector<2> blueVariance = {rawNLF.first[2], rawNLF.second[2]};

    return {redVariance, greenVariance, blueVariance};
}

void scaleRawData(MetalContext* mtlContext, const gls::mtl_image_2d<gls::luma_pixel_16>& rawImage,
                  gls::mtl_image_2d<gls::luma_pixel_float>* scaledRawImage, BayerPattern bayerPattern,
                  gls::Vector<4> scaleMul, float blackLevel, float lensShadingCorrection);

void rawImageSobel(MetalContext* mtlContext, const gls::mtl_image_2d<gls::luma_pixel_float>& rawImage,
                   gls::mtl_image_2d<gls::rgba_pixel_float>* gradientImage);

void gaussianBlurSobelImage(MetalContext* mtlContext,
                            const gls::mtl_image_2d<gls::luma_pixel_float>& rawImage,
                            const gls::mtl_image_2d<gls::rgba_pixel_float>& sobelImage,
                            std::array<float, 2> rawNoiseModel, float radius1, float radius2,
                            gls::mtl_image_2d<gls::luma_alpha_pixel_float>* outputImage);

void interpolateGreen(MetalContext* mtlContext, const gls::mtl_image_2d<gls::luma_pixel_float>& rawImage,
                      const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
                      gls::mtl_image_2d<gls::luma_pixel_float>* greenImage, BayerPattern bayerPattern,
                      gls::Vector<2> greenVariance);

void interpolateRedBlue(MetalContext* mtlContext, const gls::mtl_image_2d<gls::luma_pixel_float>& rawImage,
                        const gls::mtl_image_2d<gls::luma_pixel_float>& greenImage,
                        const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
                        gls::mtl_image_2d<gls::rgba_pixel_float>* rgbImage, BayerPattern bayerPattern,
                        gls::Vector<2> redVariance, gls::Vector<2> blueVariance);

void interpolateRedBlueAtGreen(MetalContext* mtlContext,
                               const gls::mtl_image_2d<gls::rgba_pixel_float>& rgbImageIn,
                               const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
                               gls::mtl_image_2d<gls::rgba_pixel_float>* rgbImageOut, BayerPattern bayerPattern,
                               gls::Vector<2> redVariance, gls::Vector<2> blueVariance);

void blendHighlightsImage(MetalContext* mtlContext,
                          const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                          float clip, gls::mtl_image_2d<gls::rgba_pixel_float>* outputImage);

void transformImage(MetalContext* mtlContext, const gls::mtl_image_2d<gls::rgba_pixel_float>& linearImage,
                    gls::mtl_image_2d<gls::rgba_pixel_float>* rgbImage, const gls::Matrix<3, 3>& transform);

void denoiseImage(MetalContext* mtlContext, const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                  const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage, const gls::Vector<3>& var_a,
                  const gls::Vector<3>& var_b, const gls::Vector<3> thresholdMultipliers, float chromaBoost,
                  float gradientBoost, float gradientThreshold, gls::mtl_image_2d<gls::rgba_pixel_float>* outputImage);

template <typename T>
void resampleImage(MetalContext* mtlContext, const std::string& kernelName, const gls::mtl_image_2d<T>& inputImage,
                   gls::mtl_image_2d<T>* outputImage);

void subtractNoiseImage(MetalContext* mtlContext,
                        const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                        const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage1,
                        const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImageDenoised1,
                        const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
                        float luma_weight, float sharpening, const gls::Vector<2>& nlf,
                        gls::mtl_image_2d<gls::rgba_pixel_float>* outputImage);

void bayerToRawRGBA(MetalContext* mtlContext, const gls::mtl_image_2d<gls::luma_pixel_float>& rawImage,
                    gls::mtl_image_2d<gls::rgba_pixel_float>* rgbaImage, BayerPattern bayerPattern);

void rawRGBAToBayer(MetalContext* mtlContext, const gls::mtl_image_2d<gls::rgba_pixel_float>& rgbaImage,
                    gls::mtl_image_2d<gls::luma_pixel_float>* rawImage, BayerPattern bayerPattern);

void convertTosRGB(MetalContext* mtlContext, const gls::mtl_image_2d<gls::rgba_pixel_float>& linearImage,
                   const gls::mtl_image_2d<gls::luma_pixel_float>& ltmMaskImage,
                   gls::mtl_image_2d<gls::rgba_pixel_float>* rgbImage, const DemosaicParameters& demosaicParameters);

void despeckleRawRGBAImage(MetalContext* mtlContext, const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                           const gls::Vector<4> rawVariance, gls::mtl_image_2d<gls::rgba_pixel_float>* outputImage);

void despeckleImage(MetalContext* mtlContext, const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                    const gls::Vector<3>& var_a, const gls::Vector<3>& var_b,
                    gls::mtl_image_2d<gls::rgba_pixel_float>* outputImage);

void localToneMappingMask(MetalContext* mtlContext, const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                          const std::array<const gls::mtl_image_2d<gls::rgba_pixel_float>*, 3>& guideImage,
                          const std::array<const gls::mtl_image_2d<gls::luma_alpha_pixel_float>*, 3>& abImage,
                          const std::array<const gls::mtl_image_2d<gls::luma_alpha_pixel_float>*, 3>& abMeanImage,
                          const LTMParameters& ltmParameters, const gls::Matrix<3, 3>& ycbcr_srgb,
                          const gls::Vector<2>& nlf, gls::mtl_image_2d<gls::luma_pixel_float>* outputImage);

#endif /* demosaic_mtl_hpp */
