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
class iPhone14TeleCalibration : public CameraCalibration<levels> {
    static const std::array<NoiseModel<levels>, 9> NLFData;

public:
    NoiseModel<levels> nlfFromIso(int iso) const override {
        iso = std::clamp(iso, 20, 2500);

        if (iso >= 20 && iso < 32) {
            float a = (iso - 12) / 12;
            return lerp<levels>(NLFData[0], NLFData[1], a);
        } else if (iso >= 32 && iso < 50) {
            float a = (iso - 14) / 14;
            return lerp<levels>(NLFData[0], NLFData[1], a);
        } else if (iso >= 50 && iso < 100) {
            float a = (iso - 50) / 50;
            return lerp<levels>(NLFData[1], NLFData[2], a);
        } else if (iso >= 100 && iso < 200) {
            float a = (iso - 100) / 100;
            return lerp<levels>(NLFData[2], NLFData[3], a);
        } else if (iso >= 200 && iso < 400) {
            float a = (iso - 200) / 200;
            return lerp<levels>(NLFData[3], NLFData[4], a);
        } else if (iso >= 400 && iso < 800) {
            float a = (iso - 400) / 400;
            return lerp<levels>(NLFData[4], NLFData[5], a);
        } else if (iso >= 800 && iso < 1600) {
            float a = (iso - 800) / 800;
            return lerp<levels>(NLFData[5], NLFData[6], a);
        } else if (iso >= 1600 && iso <= 2500) {
            float a = (iso - 900) / 900;
            return lerp<levels>(NLFData[6], NLFData[7], a);
        } else {
            throw std::range_error("Unexpected ISO value: " + std::to_string(iso));
        }
    }

    std::pair<RAWDenoiseParameters, std::array<DenoiseParameters, levels>> getDenoiseParameters(int iso) const override {
        const float highNoiseISO = 100;

        const float nlf_alpha = std::clamp((log2(iso) - log2(20)) / (log2(2500) - log2(20)), 0.0, 1.0);
        const float raw_nlf_alpha = std::clamp((log2(iso) - log2(highNoiseISO)) / (log2(2500) - log2(highNoiseISO)), 0.0, 1.0);

        float lerp = std::lerp(1.0, 2.0, nlf_alpha);
        float lerp_c = 1;

        std::cout << "iPhone 14 Tele DenoiseParameters nlf_alpha: " << nlf_alpha << ", ISO: " << iso << ", lerp: " << lerp << std::endl;

        float lmult[5] = { 3, 1.5, 1, 1, 1 };
        float cmult[5] = { 1, 1, 1, 1, 1 };

        float chromaBoost = 8;

        std::array<DenoiseParameters, 5> denoiseParameters = {{
            {
                .luma = lmult[0] * lerp,
                .chroma = cmult[0] * lerp_c,
                .chromaBoost = chromaBoost,
                .gradientBoost = 8, // 4 * (2 - smoothstep(0.25, 0.5, nlf_alpha)),
                .gradientThreshold = 1,
                .sharpening = std::lerp(1.5f, 1.0f, nlf_alpha)
            },
            {
                .luma = lmult[1] * lerp,
                .chroma = cmult[1] * lerp_c,
                .chromaBoost = chromaBoost,
                .gradientBoost = 2, // (2 - smoothstep(0.25, 0.5, nlf_alpha)),
                .gradientThreshold = 1,
                .sharpening = 1
            },
            {
                .luma = lmult[2] * lerp,
                .chroma = cmult[2] * lerp_c,
                .chromaBoost = chromaBoost,
            },
            {
                .luma = lmult[3] * lerp,
                .chroma = cmult[3] * lerp_c,
                .chromaBoost = chromaBoost,
            },
            {
                .luma = lmult[4] * lerp,
                .chroma = cmult[4] * lerp_c,
                .chromaBoost = chromaBoost,
            }
        }};

        RAWDenoiseParameters rawDenoiseParameters = {
            .highNoiseImage = iso >= highNoiseISO,
            .strength = std::lerp(1.0f, 3.0f, raw_nlf_alpha)
        };

        return { rawDenoiseParameters, denoiseParameters };
    }

    DemosaicParameters buildDemosaicParameters() const override {
        return {
            .lensShadingCorrection = 0,
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
                .detail = { 1, 1, 3 }
            }
        };
    }
};

std::unique_ptr<DemosaicParameters> unpackiPhone14TeleRawImage(const gls::image<gls::luma_pixel_16>& inputImage,
                                                               const gls::Matrix<3, 3>& xyz_rgb,
                                                               gls::tiff_metadata* dng_metadata,
                                                               gls::tiff_metadata* exif_metadata) {
    iPhone14TeleCalibration calibration;
    return calibration.getDemosaicParameters(inputImage, xyz_rgb, dng_metadata, exif_metadata);
}

// --- NLFData ---

template<>
const std::array<NoiseModel<5>, 9> iPhone14TeleCalibration<5>::NLFData = {{
    // ISO 20
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {2.684e-04, 2.693e-04, 2.709e-04, 2.677e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.908e-05, 1.501e-05, 1.569e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.726e-05, 1.249e-05, 1.193e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.533e-06, 5.253e-06, 4.651e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.196e-05, 1.550e-06, 1.294e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.102e-05, 5.457e-07, 2.650e-07}},
        }}
    },
    // ISO 32
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {3.046e-04, 3.048e-04, 3.077e-04, 3.038e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.104e-05, 1.776e-05, 1.837e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.192e-05, 1.620e-05, 1.523e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {8.968e-06, 7.325e-06, 6.416e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.206e-05, 2.163e-06, 1.870e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.123e-05, 6.909e-07, 3.744e-07}},
        }}
    },
    // ISO 50
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {3.895e-04, 3.862e-04, 3.877e-04, 3.855e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {4.734e-05, 2.449e-05, 2.600e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.633e-05, 1.999e-05, 2.002e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.088e-05, 1.034e-05, 9.333e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.222e-05, 3.163e-06, 2.853e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.075e-05, 9.701e-07, 6.323e-07}},
        }}
    },
    // ISO 100
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {1.672e-03, 1.674e-03, 1.703e-03, 1.673e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.321e-04, 8.771e-05, 8.831e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.912e-05, 2.367e-05, 2.543e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.543e-05, 1.732e-05, 1.622e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.242e-05, 5.706e-06, 5.478e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.117e-05, 1.667e-06, 1.419e-06}},
        }}
    },
    // ISO 200
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {2.212e-03, 2.198e-03, 2.219e-03, 2.194e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.465e-04, 1.389e-04, 1.304e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.000e-04, 8.189e-05, 8.381e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.116e-05, 2.330e-05, 2.279e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.373e-05, 1.090e-05, 9.882e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.119e-05, 3.032e-06, 2.572e-06}},
        }}
    },
    // ISO 400
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {2.575e-03, 2.561e-03, 2.589e-03, 2.559e-03}},
        {{
            {{4.895e-06, 1.000e-08, 1.000e-08}, {2.670e-04, 1.707e-04, 1.663e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.957e-04, 1.645e-04, 1.522e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.690e-05, 2.520e-05, 2.495e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.533e-05, 1.687e-05, 1.607e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.293e-05, 5.374e-06, 4.982e-06}},
        }}
    },
    // ISO 800
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {8.964e-03, 8.899e-03, 9.028e-03, 8.905e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.733e-03, 5.795e-04, 4.901e-04}},
            {{1.002e-06, 7.099e-06, 2.700e-06}, {2.044e-04, 1.654e-04, 1.650e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {9.930e-05, 1.228e-04, 1.112e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.641e-05, 1.838e-05, 1.756e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.355e-05, 9.054e-06, 7.726e-06}},
        }}
    },
    // ISO 1600
    {
        {{4.915e-04, 4.557e-04, 4.500e-04, 4.564e-04}, {1.381e-02, 1.371e-02, 1.388e-02, 1.372e-02}},
        {{
            {{1.697e-04, 3.861e-05, 2.863e-05}, {1.527e-03, 8.016e-04, 7.250e-04}},
            {{1.000e-08, 4.554e-06, 6.253e-06}, {8.667e-04, 7.306e-04, 6.271e-04}},
            {{2.439e-06, 1.212e-05, 1.133e-05}, {1.326e-04, 1.342e-04, 1.276e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {5.513e-05, 6.896e-05, 6.339e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {4.524e-05, 1.914e-05, 1.731e-05}},
        }}
    },
    // ISO 2500
    {
        {{4.944e-04, 4.373e-04, 4.033e-04, 4.342e-04}, {3.310e-02, 3.276e-02, 3.313e-02, 3.277e-02}},
        {{
            {{5.160e-04, 1.557e-04, 1.233e-04}, {5.549e-04, 8.191e-04, 8.316e-04}},
            {{5.163e-05, 7.425e-05, 6.644e-05}, {1.154e-03, 9.784e-04, 8.773e-04}},
            {{9.903e-06, 1.084e-05, 1.418e-05}, {3.227e-04, 4.420e-04, 3.903e-04}},
            {{1.782e-06, 4.517e-06, 5.266e-06}, {7.599e-05, 9.591e-05, 9.071e-05}},
            {{1.000e-08, 7.734e-07, 8.922e-07}, {5.720e-05, 2.997e-05, 2.952e-05}},
        }}
    },
}};
