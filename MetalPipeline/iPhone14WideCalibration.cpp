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
class iPhone14WideCalibration : public CameraCalibration<levels> {
    static const std::array<NoiseModel<levels>, 9> NLFData;

public:
    NoiseModel<levels> nlfFromIso(int iso) const override {
        iso = std::clamp(iso, 50, 12500);

        if (iso >= 50 && iso < 100) {
            float a = (iso - 50) / 50;
            return lerp<levels>(NLFData[0], NLFData[1], a);
        } else if (iso >= 100 && iso < 200) {
            float a = (iso - 100) / 100;
            return lerp<levels>(NLFData[1], NLFData[2], a);
        } else if (iso >= 200 && iso < 400) {
            float a = (iso - 200) / 200;
            return lerp<levels>(NLFData[2], NLFData[3], a);
        } else if (iso >= 400 && iso < 800) {
            float a = (iso - 400) / 400;
            return lerp<levels>(NLFData[3], NLFData[4], a);
        } else if (iso >= 800 && iso < 1600) {
            float a = (iso - 800) / 800;
            return lerp<levels>(NLFData[4], NLFData[5], a);
        } else if (iso >= 1600 && iso < 3200) {
            float a = (iso - 1600) / 1600;
            return lerp<levels>(NLFData[5], NLFData[6], a);
        } else if (iso >= 3200 && iso < 6400) {
            float a = (iso - 3200) / 3200;
            return lerp<levels>(NLFData[6], NLFData[7], a);
        } else if (iso >= 6400 && iso <= 12500) {
            float a = (iso - 6100) / 6100;
            return lerp<levels>(NLFData[7], NLFData[8], a);
        } else {
            throw std::range_error("Unexpected ISO value: " + std::to_string(iso));
        }
    }

    std::pair<RAWDenoiseParameters, std::array<DenoiseParameters, levels>> getDenoiseParameters(int iso) const override {
        const float highNoiseISO = 100;

        const float nlf_alpha = std::clamp((log2(iso) - log2(50)) / (log2(12500) - log2(50)), 0.0, 1.0);
        const float raw_nlf_alpha = std::clamp((log2(iso) - log2(highNoiseISO)) / (log2(12500) - log2(highNoiseISO)), 0.0, 1.0);

        float lerp = std::lerp(1.0, 2.0, nlf_alpha);
        float lerp_c = std::lerp(1.0, 2.0, nlf_alpha);

        std::cout << "iPhone 14 Wide DenoiseParameters nlf_alpha: " << nlf_alpha << ", ISO: " << iso << ", lerp: " << lerp << std::endl;

        float lmult[5] = { 3, 1.5, 1, 1, 1 };
        float cmult[5] = { 1, 1, 1, 1, 1 };

        std::array<DenoiseParameters, 5> denoiseParameters = {{
            {
                .luma = lmult[0] * lerp,
                .chroma = cmult[0] * lerp_c,
                .chromaBoost = 8,
                .gradientBoost = 2 * (2 - smoothstep(0.3, 0.6, nlf_alpha)),
                .gradientThreshold = 2,
                .sharpening = std::lerp(1.5f, 1.0f, nlf_alpha)
            },
            {
                .luma = lmult[1] * lerp,
                .chroma = cmult[1] * lerp_c,
                .chromaBoost = 4,
                .gradientBoost = (2 - smoothstep(0.3, 0.6, nlf_alpha)),
                .gradientThreshold = 2,
                .sharpening = 1.1
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
            .strength = std::lerp(0.5f, 3.0f, raw_nlf_alpha)
        };

        return { rawDenoiseParameters, denoiseParameters };
    }

    DemosaicParameters buildDemosaicParameters() const override {
        return {
            .lensShadingCorrection = 1.6,
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
                .detail = { 1, 1.2, 2.0 }
            }
        };
    }
};

std::unique_ptr<DemosaicParameters> unpackiPhone14WideRawImage(const gls::image<gls::luma_pixel_16>& inputImage,
                                                               const gls::Matrix<3, 3>& xyz_rgb,
                                                               gls::tiff_metadata* dng_metadata,
                                                               gls::tiff_metadata* exif_metadata) {
    iPhone14WideCalibration calibration;
    return calibration.getDemosaicParameters(inputImage, xyz_rgb, dng_metadata, exif_metadata);
}


// --- NLFData ---

template<>
const std::array<NoiseModel<5>, 9> iPhone14WideCalibration<5>::NLFData = {{
    // ISO 50
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {9.605e-05, 9.510e-05, 9.561e-05, 9.557e-05}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.021e-05, 7.708e-06, 7.702e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {8.517e-06, 6.253e-06, 5.550e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.021e-06, 2.249e-06, 1.803e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {5.619e-06, 5.195e-07, 3.991e-07}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.400e-05, 2.553e-07, 8.757e-08}},
        }}
    },
    // ISO 100
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {2.374e-04, 2.378e-04, 2.410e-04, 2.379e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.549e-05, 1.454e-05, 1.487e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.348e-05, 1.136e-05, 1.020e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {4.445e-06, 4.221e-06, 3.552e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.080e-06, 1.181e-06, 8.815e-07}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.212e-05, 4.146e-07, 2.701e-07}},
        }}
    },
    // ISO 200
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {3.383e-04, 3.348e-04, 3.385e-04, 3.367e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.960e-05, 2.143e-05, 2.230e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.195e-05, 1.877e-05, 1.727e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {6.240e-06, 7.698e-06, 6.556e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {6.939e-06, 2.222e-06, 1.782e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.201e-05, 6.308e-07, 4.537e-07}},
        }}
    },
    // ISO 400
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {5.231e-04, 5.169e-04, 5.199e-04, 5.173e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {9.127e-05, 3.411e-05, 3.599e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.295e-05, 2.704e-05, 2.671e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.065e-05, 1.447e-05, 1.233e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.496e-06, 4.304e-06, 3.577e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.186e-05, 1.246e-06, 9.749e-07}},
        }}
    },
    // ISO 800
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {1.558e-03, 1.530e-03, 1.528e-03, 1.528e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.300e-04, 6.057e-05, 6.208e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.819e-05, 3.034e-05, 3.293e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.778e-05, 2.436e-05, 2.191e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {9.662e-06, 8.288e-06, 6.826e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.220e-05, 2.436e-06, 1.986e-06}},
        }}
    },
    // ISO 1600
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {2.151e-03, 2.117e-03, 2.120e-03, 2.120e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.068e-04, 1.126e-04, 1.192e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.200e-04, 1.024e-04, 9.988e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.003e-05, 1.876e-05, 1.880e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.185e-05, 1.193e-05, 1.091e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {9.118e-06, 3.885e-06, 3.580e-06}},
        }}
    },
    // ISO 3200
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {7.953e-03, 7.738e-03, 7.636e-03, 7.723e-03}},
        {{
            {{6.746e-06, 2.499e-06, 2.275e-07}, {3.749e-04, 1.464e-04, 1.595e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.553e-04, 1.263e-04, 1.281e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {5.782e-05, 8.328e-05, 7.481e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.547e-05, 1.517e-05, 1.414e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {9.535e-06, 6.713e-06, 6.464e-06}},
        }}
    },
    // ISO 6400
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {1.035e-02, 1.011e-02, 1.006e-02, 1.011e-02}},
        {{
            {{1.952e-05, 1.387e-05, 1.172e-05}, {1.357e-03, 5.091e-04, 5.520e-04}},
            {{1.000e-08, 3.489e-06, 5.335e-06}, {5.252e-04, 4.425e-04, 4.211e-04}},
            {{1.000e-08, 3.391e-06, 3.670e-06}, {9.430e-05, 1.116e-04, 1.068e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.541e-05, 4.552e-05, 4.070e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.231e-05, 1.034e-05, 9.625e-06}},
        }}
    },
    // ISO 12500
    {
        {{2.052e-03, 1.942e-03, 1.908e-03, 1.944e-03}, {1.210e-02, 1.183e-02, 1.176e-02, 1.183e-02}},
        {{
            {{4.272e-04, 1.447e-04, 1.263e-04}, {4.242e-04, 4.366e-04, 5.834e-04}},
            {{3.231e-05, 7.428e-05, 6.601e-05}, {8.655e-04, 6.859e-04, 6.985e-04}},
            {{1.364e-05, 2.021e-05, 2.055e-05}, {1.985e-04, 3.210e-04, 3.016e-04}},
            {{1.549e-06, 6.073e-06, 5.722e-06}, {5.832e-05, 7.483e-05, 7.428e-05}},
            {{6.605e-07, 1.290e-06, 1.209e-06}, {3.724e-05, 2.416e-05, 2.349e-05}},
        }}
    },
}};
