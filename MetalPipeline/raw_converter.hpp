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

#ifndef raw_converter_hpp
#define raw_converter_hpp

#include "gls_mtl_image.hpp"
#include "gls_mtl.hpp"

#include "pyramid_processor.hpp"
#include "demosaic_kernels.hpp"

class LocalToneMapping {
    gls::mtl_image_2d<gls::pixel_float>::unique_ptr ltmMaskImage;
    gls::mtl_image_2d<gls::pixel_float2>::unique_ptr lfAbGfImage;
    gls::mtl_image_2d<gls::pixel_float2>::unique_ptr lfAbGfMeanImage;
    gls::mtl_image_2d<gls::pixel_float2>::unique_ptr mfAbGfImage;
    gls::mtl_image_2d<gls::pixel_float2>::unique_ptr mfAbGfMeanImage;
    gls::mtl_image_2d<gls::pixel_float2>::unique_ptr hfAbGfImage;
    gls::mtl_image_2d<gls::pixel_float2>::unique_ptr hfAbGfMeanImage;

    localToneMappingMaskKernel _localToneMappingMask;

   public:
    LocalToneMapping(MetalContext* context) :
        _localToneMappingMask(context) {
        // Placeholder, only allocated if LTM is used
        ltmMaskImage = std::make_unique<gls::mtl_image_2d<gls::pixel_float>>(context->device(), 1, 1);
    }

    void allocateTextures(MetalContext* context, int width, int height) {
        auto mtlDevice = context->device();

        if (ltmMaskImage->width != width || ltmMaskImage->height != height) {
            ltmMaskImage = std::make_unique<gls::mtl_image_2d<gls::pixel_float>>(mtlDevice, width, height);
            lfAbGfImage =
                std::make_unique<gls::mtl_image_2d<gls::pixel_float2>>(mtlDevice, width / 16, height / 16);
            lfAbGfMeanImage =
                std::make_unique<gls::mtl_image_2d<gls::pixel_float2>>(mtlDevice, width / 16, height / 16);
            mfAbGfImage =
                std::make_unique<gls::mtl_image_2d<gls::pixel_float2>>(mtlDevice, width / 4, height / 4);
            mfAbGfMeanImage =
                std::make_unique<gls::mtl_image_2d<gls::pixel_float2>>(mtlDevice, width / 4, height / 4);
            hfAbGfImage = std::make_unique<gls::mtl_image_2d<gls::pixel_float2>>(mtlDevice, width, height);
            hfAbGfMeanImage = std::make_unique<gls::mtl_image_2d<gls::pixel_float2>>(mtlDevice, width, height);
        }
    }

    void createMask(MetalContext* context, const gls::mtl_image_2d<gls::pixel_float4>& image,
                    const gls::mtl_image_2d<gls::pixel_float2>& gradientImage,
                    const std::array<const gls::mtl_image_2d<gls::pixel_float4>*, 3>& guideImage,
                    const NoiseModel<5>& noiseModel, const LTMParameters& ltmParameters,
                    MTL::Buffer* histogramBuffer) {
        const std::array<const gls::mtl_image_2d<gls::pixel_float2>*, 3>& abImage = {
            lfAbGfImage.get(), mfAbGfImage.get(), hfAbGfImage.get()};
        const std::array<const gls::mtl_image_2d<gls::pixel_float2>*, 3>& abMeanImage = {
            lfAbGfMeanImage.get(), mfAbGfMeanImage.get(), hfAbGfMeanImage.get()};

        gls::Vector<2> nlf = {noiseModel.pyramidNlf[0].first[0], noiseModel.pyramidNlf[0].second[0]};

        _localToneMappingMask(context, image, gradientImage, guideImage, abImage, abMeanImage, ltmParameters,
                              nlf, histogramBuffer, ltmMaskImage.get());
    }

    const gls::mtl_image_2d<gls::pixel_float>& getMask() { return *ltmMaskImage; }
};

class RawConverter {
    const bool _calibrateFromImage;
    MetalContext _mtlContext;
    gls::size _rawImageSize;

    gls::mtl_image_2d<gls::luma_pixel_16>::unique_ptr _rawImage;
    gls::mtl_image_2d<gls::pixel_float>::unique_ptr _scaledRawImage;
    gls::mtl_image_2d<gls::pixel_float4>::unique_ptr _rawSobelImage;
    gls::mtl_image_2d<gls::pixel_float2>::unique_ptr _rawGradientImage;
    gls::mtl_image_2d<gls::pixel_float>::unique_ptr _greenImage;
    gls::mtl_image_2d<gls::pixel_float4>::unique_ptr _linearRGBImageA;
    gls::mtl_image_2d<gls::pixel_float4>::unique_ptr _linearRGBImageB;
    gls::mtl_image_2d<gls::pixel_float>::unique_ptr _ltmMaskImage;

    gls::mtl_image_2d<gls::pixel_float4>::unique_ptr _meanImage;
    gls::mtl_image_2d<gls::pixel_float4>::unique_ptr _varImage;

    // RawConverter HighNoise textures
    gls::mtl_image_2d<gls::pixel_float4>::unique_ptr _rgbaRawImage;
    gls::mtl_image_2d<gls::pixel_float4>::unique_ptr _denoisedRgbaRawImage;

    std::array<gls::mtl_image_2d<gls::pixel_float4>::unique_ptr, 4> _ltmImagePyramid;

    std::unique_ptr<PyramidProcessor<5>> _pyramidProcessor;

    std::unique_ptr<LocalToneMapping> _localToneMapping;

    std::unique_ptr<std::vector<uint8_t>> _icc_profile_data;
    gls::Matrix<3, 3> _xyz_rgb;

    // Kernels
    scaleRawDataKernel _scaleRawData;
    rawImageSobelKernel _rawImageSobel;
    gaussianBlurSobelImageKernel _gaussianBlurSobelImage;
    demosaicImageKernel _demosaicImage;
    bayerToRawRGBAKernel _bayerToRawRGBA;
    rawRGBAToBayerKernel _rawRGBAToBayer;
    despeckleRawRGBAImageKernel _despeckleRawRGBAImage;
    crossDenoiseRawRGBAImageKernel _crossDenoiseRawRGBAImage;
    blendHighlightsImageKernel _blendHighlightsImage;
    transformImageKernel _transformImage;
    convertTosRGBKernel _convertTosRGB;
    despeckleImageKernel _despeckleImage;
    histogramImageKernel _histogramImage;
    basicRawNoiseStatisticsKernel _rawNoiseStatistics;

public:

    RawConverter(NS::SharedPtr<MTL::Device> mtlDevice, const std::vector<uint8_t>* icc_profile_data = nullptr, bool calibrateFromImage = false) :
        _calibrateFromImage(calibrateFromImage),
        _mtlContext(mtlDevice),
        _rawImageSize(gls::size {0, 0}),
        _scaleRawData(&_mtlContext),
        _rawImageSobel(&_mtlContext),
        _gaussianBlurSobelImage(&_mtlContext, 1.5f, 4.5f),
        _demosaicImage(&_mtlContext),
        _bayerToRawRGBA(&_mtlContext),
        _rawRGBAToBayer(&_mtlContext),
        _despeckleRawRGBAImage(&_mtlContext),
        _crossDenoiseRawRGBAImage(&_mtlContext),
        _blendHighlightsImage(&_mtlContext),
        _transformImage(&_mtlContext),
        _convertTosRGB(&_mtlContext),
        _despeckleImage(&_mtlContext),
        _histogramImage(&_mtlContext),
        _rawNoiseStatistics(&_mtlContext)
    {
        _localToneMapping = std::make_unique<LocalToneMapping>(&_mtlContext);

        if (icc_profile_data) {
            _icc_profile_data = std::make_unique<std::vector<uint8_t>>(*icc_profile_data);

            _xyz_rgb = icc_profile_xyz_matrix(*_icc_profile_data);
        } else {
            _xyz_rgb = xyz_sRGB;
        }
    }

    MetalContext* context() {
        return &_mtlContext;
    }

    const std::vector<uint8_t>* icc_profile_data() const {
        return _icc_profile_data.get();
    }

    const gls::Matrix<3, 3>& xyz_rgb() const {
        return _xyz_rgb;
    }

    histogramImageKernel::histogram_data* histogramData() {
        return _histogramImage.histogramData();
    }

    void allocateTextures(const gls::size& imageSize);

    void allocateHighNoiseTextures(const gls::size& imageSize);

    void allocateLtmImagePyramid(const gls::size& imageSize);

    gls::mtl_image_2d<gls::pixel_float4>* denoise(const gls::mtl_image_2d<gls::pixel_float4>& inputImage, DemosaicParameters* demosaicParameters);

    gls::mtl_image_2d<gls::pixel_float4>* demosaic(const gls::image<gls::luma_pixel_16>& rawImage, DemosaicParameters* demosaicParameters,
                                                   bool denoise = true, bool postProcess = true);

    gls::mtl_image_2d<gls::pixel_float4>* postprocess(gls::image<gls::pixel_float4>& rgbImage, DemosaicParameters* demosaicParameters);

    RawNLF MeasureRawNLF(float exposure_multiplier, BayerPattern bayerPattern);
};

#endif /* raw_converter_hpp */
