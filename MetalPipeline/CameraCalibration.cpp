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

#include <filesystem>

#include "CameraCalibration.hpp"
#include "demosaic.hpp"
#include "gls_logging.h"
#include "raw_converter.hpp"

static const char* TAG = "DEMOSAIC";

template <size_t levels>
std::unique_ptr<DemosaicParameters>
    CameraCalibration<levels>::getDemosaicParameters(const gls::image<gls::luma_pixel_16>& inputImage,
                                                     const gls::Matrix<3, 3>& xyz_rgb,
                                                     gls::tiff_metadata* dng_metadata,
                                                     gls::tiff_metadata* exif_metadata) const {
    auto demosaicParameters = std::make_unique<DemosaicParameters>();

    *demosaicParameters = buildDemosaicParameters();

    unpackDNGMetadata(inputImage, dng_metadata, demosaicParameters.get(), xyz_rgb,
                      /*auto_white_balance=*/false, /* &gmb_position */ nullptr, /*rotate_180=*/false);

    uint32_t iso = 0;
    std::vector<uint16_t> iso_16;
    if (!(iso_16 = getVector<uint16_t>(*dng_metadata, TIFFTAG_ISO)).empty()) {
        iso = iso_16[0];
    } else if (!(iso_16 = getVector<uint16_t>(*exif_metadata, EXIFTAG_ISOSPEEDRATINGS)).empty()) {
        iso = iso_16[0];
    } else if (getValue(*exif_metadata, EXIFTAG_RECOMMENDEDEXPOSUREINDEX, &iso)) {
        iso = iso;
    }

    LOG_INFO(TAG) << "EXIF ISO: " << iso << std::endl;

    const auto nlfParams = nlfFromIso(iso);
    const auto denoiseParameters = getDenoiseParameters(iso);
    demosaicParameters->noiseModel = nlfParams;
    demosaicParameters->noiseLevel = denoiseParameters.first;
    demosaicParameters->denoiseParameters = denoiseParameters.second;

    return demosaicParameters;
}

template std::unique_ptr<DemosaicParameters> CameraCalibration<5>::getDemosaicParameters(
    const gls::image<gls::luma_pixel_16>& inputImage, const gls::Matrix<3, 3>& xyz_rgb,
    gls::tiff_metadata* dng_metadata, gls::tiff_metadata* exif_metadata) const;
