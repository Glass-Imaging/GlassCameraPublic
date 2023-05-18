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

#include <array>
#include <cmath>
#include <filesystem>

#include "CameraCalibration.hpp"
#include "demosaic.hpp"
#include "gls_logging.h"

// clang-format off

static const char* TAG = "DEMOSAIC";

template <size_t levels = 5>
class iPhone14TeleFEMNCalibration : public CameraCalibration<levels> {
    static const std::array<NoiseModel<levels>, 1> NLFData;

public:
    NoiseModel<levels> nlfFromIso(int iso) const override {
        return NLFData[0];
    }

    std::pair<RAWDenoiseParameters, std::array<DenoiseParameters, levels>> getDenoiseParameters(int iso) const override {
        std::array<DenoiseParameters, 5> denoiseParameters;
        RAWDenoiseParameters rawDenoiseParameters;
        return { rawDenoiseParameters, denoiseParameters };
    }

    DemosaicParameters buildDemosaicParameters() const override {
        return {
            .rgbConversionParameters = {
                .contrast = 1.05,
                .saturation = 1.0,
                .toneCurveSlope = 3.5,
                .localToneMapping = true
            },
            .ltmParameters = {
                .eps = 0.01,
                .shadows = 1.0,
                .highlights = 1.0,
                .detail = { 1, 1, 1 }
            }
        };
    }
};

std::unique_ptr<DemosaicParameters> unpackiPhone14TeleFEMNRawImage(const gls::image<gls::luma_pixel_16>& inputImage,
                                                                   const gls::Matrix<3, 3>& xyz_rgb,
                                                                   gls::tiff_metadata* dng_metadata,
                                                                   gls::tiff_metadata* exif_metadata) {
    iPhone14TeleFEMNCalibration calibration;
    return calibration.getDemosaicParameters(inputImage, xyz_rgb, dng_metadata, exif_metadata);
}

// --- NLFData ---

template<>
const std::array<NoiseModel<5>, 1> iPhone14TeleFEMNCalibration<5>::NLFData = {{
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.000e-08, 1.000e-08, 1.000e-08}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.000e-08, 1.000e-08, 1.000e-08}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.000e-08, 1.000e-08, 1.000e-08}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.000e-08, 1.000e-08, 1.000e-08}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.000e-08, 1.000e-08, 1.000e-08}},
        }}
    }
}};
