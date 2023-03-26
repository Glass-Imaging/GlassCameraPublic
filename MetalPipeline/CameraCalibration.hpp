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

#ifndef CameraCalibration_hpp
#define CameraCalibration_hpp

#include <filesystem>

#include "demosaic.hpp"
#include "raw_converter.hpp"

template <size_t levels = 5>
class CameraCalibration {
   public:
    virtual ~CameraCalibration() {}

    virtual NoiseModel<levels> nlfFromIso(int iso) const = 0;

    virtual std::pair<float, std::array<DenoiseParameters, levels>> getDenoiseParameters(int iso) const = 0;

    virtual DemosaicParameters buildDemosaicParameters() const = 0;

    std::unique_ptr<DemosaicParameters> getDemosaicParameters(const gls::image<gls::luma_pixel_16>& inputImage,
                                                              const gls::Matrix<3, 3>& xyz_rgb,
                                                              gls::tiff_metadata* dng_metadata,
                                                              gls::tiff_metadata* exif_metadata) const;
};

std::unique_ptr<DemosaicParameters> unpackSonya6400RawImage(const gls::image<gls::luma_pixel_16>& inputImage,
                                                            const gls::Matrix<3, 3>& xyz_rgb,
                                                            gls::tiff_metadata* dng_metadata,
                                                            gls::tiff_metadata* exif_metadata);

std::unique_ptr<DemosaicParameters> unpackiPhoneRawImage(const gls::image<gls::luma_pixel_16>& inputImage,
                                                         const gls::Matrix<3, 3>& xyz_rgb,
                                                         gls::tiff_metadata* dng_metadata,
                                                         gls::tiff_metadata* exif_metadata);

#endif /* CameraCalibration_hpp */
