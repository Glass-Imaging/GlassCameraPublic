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
class CanonEOSRPCalibration : public CameraCalibration<levels> {
    static const std::array<NoiseModel<levels>, 10> NLFData;

public:
    NoiseModel<levels> nlfFromIso(int iso) const override {
        iso = std::clamp(iso, 100, 40000);
        if (iso >= 100 && iso < 200) {
            float a = (iso - 100) / 100;
            return lerp<levels>(NLFData[0], NLFData[1], a);
        } else if (iso >= 200 && iso < 400) {
            float a = (iso - 200) / 200;
            return lerp<levels>(NLFData[1], NLFData[2], a);
        } else if (iso >= 400 && iso < 800) {
            float a = (iso - 400) / 400;
            return lerp<levels>(NLFData[2], NLFData[3], a);
        } else if (iso >= 800 && iso < 1600) {
            float a = (iso - 800) / 800;
            return lerp<levels>(NLFData[3], NLFData[4], a);
        } else if (iso >= 1600 && iso < 3200) {
            float a = (iso - 1600) / 1600;
            return lerp<levels>(NLFData[4], NLFData[5], a);
        } else if (iso >= 3200 && iso < 6400) {
            float a = (iso - 3200) / 3200;
            return lerp<levels>(NLFData[5], NLFData[6], a);
        } else if (iso >= 6400 && iso < 12800) {
            float a = (iso - 6400) / 6400;
            return lerp<levels>(NLFData[6], NLFData[7], a);
        } else if (iso >= 12800 && iso < 25600) {
            float a = (iso - 12800) / 12800;
            return lerp<levels>(NLFData[7], NLFData[8], a);
        } else { // if (iso >= 25600 && iso <= 40000) {
            float a = (iso - 25600) / 14400;
            return lerp<levels>(NLFData[8], NLFData[9], a);
        }
    }

    std::pair<RAWDenoiseParameters, std::array<DenoiseParameters, levels>> getDenoiseParameters(int iso) const override {
        const float highNoiseISO = 6400;

        const float nlf_alpha = std::clamp((log2(iso) - log2(100)) / (log2(40000) - log2(100)), 0.0, 1.0);
        const float raw_nlf_alpha = std::clamp((log2(iso) - log2(highNoiseISO)) / (log2(40000) - log2(highNoiseISO)), 0.0, 1.0);

        LOG_INFO(TAG) << "CanonEOSRP DenoiseParameters nlf_alpha: " << nlf_alpha << ", ISO: " << iso << std::endl;

        float lerp = std::lerp(1.0, 2.0, nlf_alpha);
        float lerp_c = 1;

        float lmult[5] = { 1, 1, 1, 1, 1 };
        float cmult[5] = { 1, 1, 1, 1, 1 };

        std::array<DenoiseParameters, 5> denoiseParameters = {{
            {
                .luma = lmult[0] * lerp,
                .chroma = cmult[0] * lerp_c,
                .chromaBoost = 8,
                .gradientBoost = 4, // 2 * (2 - smoothstep(0.3, 0.6, nlf_alpha)),
                .gradientThreshold = 2,
                .sharpening = std::lerp(1.5f, 1.0f, nlf_alpha),
            },
            {
                .luma = lmult[1] * lerp,
                .chroma = cmult[1] * lerp_c,
                .chromaBoost = 4,
                .gradientBoost = 2, // (2 - smoothstep(0.3, 0.6, nlf_alpha)),
                .gradientThreshold = 2,
                .sharpening = 1
            },
            {
                .luma = lmult[2] * lerp,
                .chroma = cmult[2] * lerp_c,
                .chromaBoost = 2,
            },
            {
                .luma = lmult[3] * lerp,
                .chroma = cmult[3] * lerp_c,
                .chromaBoost = 2,
            },
            {
                .luma = lmult[4] * lerp,
                .chroma = cmult[4] * lerp_c,
                .chromaBoost = 2,
            }
        }};

        RAWDenoiseParameters rawDenoiseParameters = {
            .highNoiseImage = iso >= highNoiseISO,
            .strength = std::lerp(0.5f, 1.5f, raw_nlf_alpha)
        };

        return { rawDenoiseParameters, denoiseParameters };
    }

    DemosaicParameters buildDemosaicParameters() const override {
        return {
            .rgbConversionParameters = {
                .contrast = 1.05,
                .saturation = 1.0,
                .toneCurveSlope = 3.5,
                .localToneMapping = false
            },
            .ltmParameters = {
                .eps = 0.01,
                .shadows = 1, // 0.9,
                .highlights = 1, // 1.5,
                .detail = { 1, 1.2, 2 }
            }
        };
    }
};

std::unique_ptr<DemosaicParameters> unpackCanonEOSRPRawImage(const gls::image<gls::luma_pixel_16>& inputImage,
                                                            const gls::Matrix<3, 3>& xyz_rgb,
                                                            gls::tiff_metadata* dng_metadata,
                                                            gls::tiff_metadata* exif_metadata) {
    CanonEOSRPCalibration calibration;
    auto demosaicParameters = calibration.getDemosaicParameters(inputImage, xyz_rgb, dng_metadata, exif_metadata);

    unpackDNGMetadata(inputImage, dng_metadata, demosaicParameters.get(), xyz_rgb, /*auto_white_balance=*/false, nullptr, false);

    return demosaicParameters;
}


// --- NLFData ---

template<>
const std::array<NoiseModel<5>, 10> CanonEOSRPCalibration<5>::NLFData = {{
    // ISO 100
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {4.983e-05, 5.019e-05, 5.096e-05, 5.008e-05}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.356e-05, 2.256e-06, 2.503e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.705e-06, 1.435e-06, 1.852e-06}},
            {{1.127e-07, 1.000e-08, 1.000e-08}, {1.142e-06, 3.966e-07, 6.889e-07}},
            {{1.289e-07, 1.000e-08, 1.000e-08}, {6.170e-07, 1.000e-08, 1.300e-07}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.091e-04, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 200
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {6.819e-05, 6.807e-05, 6.870e-05, 6.804e-05}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.901e-05, 3.431e-06, 4.205e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {5.510e-06, 2.257e-06, 3.013e-06}},
            {{9.368e-08, 1.000e-08, 1.000e-08}, {1.542e-06, 6.855e-07, 1.137e-06}},
            {{1.252e-07, 1.000e-08, 1.000e-08}, {7.138e-07, 1.218e-07, 2.436e-07}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.121e-04, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 400
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {1.544e-04, 1.539e-04, 1.560e-04, 1.547e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.615e-05, 6.428e-06, 8.745e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {9.088e-06, 4.222e-06, 6.013e-06}},
            {{1.249e-08, 1.000e-08, 1.000e-08}, {2.624e-06, 1.434e-06, 2.309e-06}},
            {{1.523e-07, 1.000e-08, 1.000e-08}, {9.311e-07, 2.678e-07, 5.439e-07}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.037e-04, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 800
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {2.149e-04, 2.100e-04, 2.084e-04, 2.104e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {4.337e-05, 1.071e-05, 1.519e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.419e-05, 7.170e-06, 1.063e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {4.475e-06, 2.657e-06, 4.207e-06}},
            {{1.419e-07, 1.000e-08, 1.000e-08}, {1.307e-06, 6.444e-07, 1.115e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.007e-04, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 1600
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {4.071e-04, 3.931e-04, 3.852e-04, 3.932e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.072e-05, 1.992e-05, 2.999e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.014e-05, 1.199e-05, 1.699e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.728e-06, 4.985e-06, 8.025e-06}},
            {{7.220e-08, 1.000e-08, 1.000e-08}, {2.329e-06, 1.273e-06, 2.229e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.066e-04, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 3200
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {7.117e-04, 6.866e-04, 6.733e-04, 6.876e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.143e-04, 3.890e-05, 5.791e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.813e-05, 2.292e-05, 3.231e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.266e-05, 9.268e-06, 1.394e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {4.382e-06, 2.633e-06, 4.413e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.401e-05, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 6400
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {1.556e-03, 1.492e-03, 1.449e-03, 1.493e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.038e-04, 7.145e-05, 1.110e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {6.304e-05, 4.061e-05, 5.271e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.911e-05, 1.561e-05, 2.062e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {6.865e-06, 5.020e-06, 8.443e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {4.024e-06, 1.103e-06, 2.015e-06}},
        }}
    },
    // ISO 12800
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {2.332e-03, 2.238e-03, 2.174e-03, 2.239e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {4.390e-04, 1.202e-04, 1.994e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.883e-04, 9.420e-05, 1.428e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.964e-05, 3.231e-05, 4.319e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.291e-05, 1.105e-05, 1.609e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {6.178e-06, 2.668e-06, 4.942e-06}},
        }}
    },
    // ISO 25600
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {6.959e-03, 6.726e-03, 6.535e-03, 6.729e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.606e-03, 2.079e-04, 3.314e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.580e-04, 1.362e-04, 1.905e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.122e-04, 7.778e-05, 1.191e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.143e-05, 1.933e-05, 2.284e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.162e-05, 6.118e-06, 9.475e-06}},
        }}
    },
    // ISO 40000
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {8.069e-03, 7.798e-03, 7.567e-03, 7.804e-03}},
        {{
            {{1.000e-08, 2.505e-06, 1.000e-08}, {1.731e-03, 2.454e-04, 4.096e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.280e-04, 2.386e-04, 3.851e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.546e-04, 1.052e-04, 1.401e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {6.527e-05, 3.395e-05, 5.564e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.630e-05, 9.534e-06, 1.468e-05}},
        }}
    },
}};
