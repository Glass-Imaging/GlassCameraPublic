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
                             nlf, ltmMaskImage.get());
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

    NS::SharedPtr<MTL::Buffer> _histogramBuffer;

    std::unique_ptr<PyramidProcessor<5>> _pyramidProcessor;

    std::unique_ptr<LocalToneMapping> _localToneMapping;

    std::unique_ptr<std::vector<uint8_t>> _icc_profile_data;
    gls::Matrix<3, 3> _xyz_rgb;

public:
    struct histogram_data {
        std::array<uint32_t, 0x10000> histogram;
        uint32_t black_level;
        uint32_t white_level;
    };

    RawConverter(NS::SharedPtr<MTL::Device> mtlDevice, const std::vector<uint8_t>* icc_profile_data = nullptr) :
        _mtlContext(mtlDevice),
        _rawImageSize(gls::size {0, 0}) {
            _localToneMapping = std::make_unique<LocalToneMapping>(&_mtlContext);

            _histogramBuffer = NS::TransferPtr(mtlDevice->newBuffer(sizeof(histogram_data),
                                                                    MTL::ResourceStorageModeShared));

            assert(_histogramBuffer->length() == sizeof(histogram_data));

            if (icc_profile_data) {
                _icc_profile_data = std::make_unique<std::vector<uint8_t>>(*icc_profile_data);

                _xyz_rgb = icc_profile_xyz_matrix(*_icc_profile_data);
            }
        }

    const std::vector<uint8_t>* icc_profile_data() const {
        return _icc_profile_data.get();
    }

    const gls::Matrix<3, 3>& xyz_rgb() const {
        return _xyz_rgb;
    }

    MTL::Buffer* histogramBuffer() {
        return _histogramBuffer.get();
    }

    histogram_data* histogramData() {
        auto buffer = histogramBuffer();
        assert(buffer->length() == sizeof(histogram_data));
        return (histogram_data*) buffer->contents();
    }

    void allocateTextures(const gls::size& imageSize);

    void allocateHighNoiseTextures(const gls::size& imageSize);

    gls::mtl_image_2d<gls::rgba_pixel_float>* denoise(const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                                                      DemosaicParameters* demosaicParameters, bool calibrateFromImage);

    gls::mtl_image_2d<gls::rgba_pixel_float>* demosaic(const gls::image<gls::luma_pixel_16>& rawImage,
                                                       DemosaicParameters* demosaicParameters);
};

#endif /* raw_converter_hpp */
