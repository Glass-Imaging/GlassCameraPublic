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
class iPhone11Calibration : public CameraCalibration<levels> {
    static const std::array<NoiseModel<levels>, 8> NLFData;

public:
    NoiseModel<levels> nlfFromIso(int iso) const override {
        iso = std::clamp(iso, 32, 2500);
        if (iso >= 32 && iso < 64) {
            float a = (iso - 32) / 32;
            return lerp<levels>(NLFData[0], NLFData[1], a);
        } else if (iso >= 64 && iso < 100) {
            float a = (iso - 64) / 36;
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
        } /* else if (iso >= 1600 && iso < 2500) */ {
            float a = (iso - 1600) / 900;
            return lerp<levels>(NLFData[6], NLFData[7], a);
        }
    }

    std::pair<float, std::array<DenoiseParameters, levels>> getDenoiseParameters(int iso) const override {
        const float nlf_alpha = std::clamp((log2(iso) - log2(32)) / (log2(3200) - log2(32)), 0.0, 1.0);

        std::cout << "iPhone DenoiseParameters nlf_alpha: " << nlf_alpha << ", ISO: " << iso << std::endl;

        float lerp = 0.55 * std::lerp(0.125f, 2.0f, nlf_alpha);
        float lerp_c = 1;

        float lmult[5] = { 0.5, 1, 0.5, 0.25, 0.125 };
        float cmult[5] = { 1, 0.5, 0.5, 0.5, 0.25 };

        float chromaBoost = 8;

        float gradientBoost = 4 * smoothstep(0, 0.3, nlf_alpha);
        float gradientThreshold = 1 + 2 * smoothstep(0, 0.3, nlf_alpha);

        std::cout << "gradientBoost: " << gradientBoost << ", gradientThreshold: " << gradientThreshold << std::endl;

        std::array<DenoiseParameters, 5> denoiseParameters = {{
            {
                .luma = lmult[0] * lerp,
                .chroma = cmult[0] * lerp_c,
                .chromaBoost = chromaBoost,
                .gradientBoost = 4 * gradientBoost,
                .gradientThreshold = gradientThreshold,
                .sharpening = std::lerp(1.5f, 1.0f, nlf_alpha)
            },
            {
                .luma = lmult[1] * lerp,
                .chroma = cmult[1] * lerp_c,
                .chromaBoost = chromaBoost,
                .gradientBoost = gradientBoost,
                .sharpening = 1.1
            },
            {
                .luma = lmult[2] * lerp,
                .chroma = cmult[2] * lerp_c,
                .chromaBoost = chromaBoost,
                .gradientBoost = gradientBoost,
                .sharpening = 1
            },
            {
                .luma = lmult[3] * lerp,
                .chroma = cmult[3] * lerp_c,
                .chromaBoost = chromaBoost,
                .gradientBoost = gradientBoost,
                .sharpening = 1
            },
            {
                .luma = lmult[4] * lerp,
                .chroma = cmult[4] * lerp_c,
                .chromaBoost = chromaBoost,
                .gradientBoost = gradientBoost,
                .sharpening = 1
            }
        }};

        return { nlf_alpha, denoiseParameters };
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

std::unique_ptr<DemosaicParameters> unpackiPhoneRawImage(const gls::image<gls::luma_pixel_16>& inputImage,
                                                         const gls::Matrix<3, 3>& xyz_rgb,
                                                         gls::tiff_metadata* dng_metadata,
                                                         gls::tiff_metadata* exif_metadata) {
    iPhone11Calibration calibration;
    return calibration.getDemosaicParameters(inputImage, xyz_rgb, dng_metadata, exif_metadata);
}


// --- NLFData ---

template<>
const std::array<NoiseModel<5>, 8> iPhone11Calibration<5>::NLFData = {{
    // ISO 32
    {
        {{9.462e-06, 1.124e-05, 1.009e-05, 1.159e-05}, {3.359e-04, 1.143e-04, 3.730e-04, 1.352e-04}},
        {{
            {{1.684e-05, 6.679e-07, 1.234e-06}, {9.303e-05, 1.775e-05, 1.687e-05}},
            {{2.406e-05, 9.927e-07, 1.968e-06}, {5.867e-06, 9.615e-06, 7.823e-06}},
            {{2.993e-05, 1.696e-06, 2.354e-06}, {1.000e-08, 1.000e-08, 1.000e-08}},
            {{4.175e-05, 1.725e-06, 2.400e-06}, {1.000e-08, 1.000e-08, 1.000e-08}},
            {{1.288e-04, 5.087e-06, 6.974e-06}, {1.049e-04, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 64
    {
        {{9.059e-06, 1.183e-05, 9.381e-06, 1.205e-05}, {6.179e-04, 1.930e-04, 7.074e-04, 2.320e-04}},
        {{
            {{1.676e-05, 6.408e-07, 1.562e-06}, {1.743e-04, 3.259e-05, 3.117e-05}},
            {{2.403e-05, 1.044e-06, 2.101e-06}, {2.134e-05, 1.900e-05, 1.866e-05}},
            {{3.051e-05, 1.787e-06, 3.055e-06}, {1.000e-08, 2.836e-06, 2.594e-07}},
            {{4.171e-05, 1.881e-06, 2.602e-06}, {1.000e-08, 1.000e-08, 1.000e-08}},
            {{1.282e-04, 5.147e-06, 7.079e-06}, {1.068e-04, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 100
    {
        {{1.101e-05, 1.296e-05, 1.086e-05, 1.316e-05}, {9.290e-04, 2.776e-04, 1.075e-03, 3.368e-04}},
        {{
            {{1.592e-05, 4.663e-07, 1.820e-06}, {2.757e-04, 5.071e-05, 4.690e-05}},
            {{2.387e-05, 1.136e-06, 2.265e-06}, {3.884e-05, 2.900e-05, 3.067e-05}},
            {{3.107e-05, 1.769e-06, 3.100e-06}, {1.000e-08, 6.557e-06, 4.896e-06}},
            {{4.187e-05, 2.049e-06, 2.846e-06}, {1.000e-08, 1.000e-08, 1.000e-08}},
            {{1.276e-04, 5.143e-06, 7.123e-06}, {1.074e-04, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 200
    {
        {{1.970e-05, 1.495e-05, 1.862e-05, 1.498e-05}, {1.732e-03, 5.315e-04, 1.991e-03, 6.495e-04}},
        {{
            {{1.357e-05, 3.728e-07, 1.905e-06}, {5.787e-04, 1.003e-04, 9.756e-05}},
            {{2.454e-05, 1.358e-06, 2.918e-06}, {8.124e-05, 5.640e-05, 6.104e-05}},
            {{3.239e-05, 1.896e-06, 3.292e-06}, {2.879e-06, 1.675e-05, 1.718e-05}},
            {{4.236e-05, 2.607e-06, 3.499e-06}, {1.000e-08, 1.000e-08, 1.000e-08}},
            {{1.278e-04, 5.390e-06, 7.127e-06}, {1.154e-04, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 400
    {
        {{4.664e-05, 1.791e-05, 4.449e-05, 1.896e-05}, {3.170e-03, 1.078e-03, 3.545e-03, 1.286e-03}},
        {{
            {{1.429e-05, 1.128e-06, 3.052e-06}, {6.968e-04, 1.488e-04, 1.520e-04}},
            {{2.517e-05, 1.534e-06, 3.940e-06}, {1.306e-04, 1.010e-04, 1.073e-04}},
            {{3.276e-05, 2.313e-06, 3.799e-06}, {2.053e-05, 3.632e-05, 4.144e-05}},
            {{4.362e-05, 3.070e-06, 4.703e-06}, {1.000e-08, 4.686e-06, 1.900e-06}},
            {{1.286e-04, 5.896e-06, 7.708e-06}, {1.160e-04, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 800
    {
        {{1.362e-04, 3.155e-05, 1.263e-04, 3.453e-05}, {5.551e-03, 2.220e-03, 6.054e-03, 2.611e-03}},
        {{
            {{2.439e-05, 4.008e-06, 7.094e-06}, {1.306e-03, 2.957e-04, 3.205e-04}},
            {{2.634e-05, 2.803e-06, 6.180e-06}, {3.051e-04, 2.117e-04, 2.422e-04}},
            {{3.376e-05, 3.533e-06, 5.791e-06}, {5.677e-05, 7.318e-05, 8.935e-05}},
            {{4.552e-05, 3.468e-06, 5.437e-06}, {1.000e-08, 1.602e-05, 1.653e-05}},
            {{1.286e-04, 6.393e-06, 8.568e-06}, {1.207e-04, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 1600
    {
        {{8.129e-05, 7.693e-06, 3.514e-05, 5.903e-06}, {1.779e-02, 7.791e-03, 2.038e-02, 8.951e-03}},
        {{
            {{8.760e-05, 1.248e-05, 1.858e-05}, {1.980e-03, 6.189e-04, 7.015e-04}},
            {{3.040e-05, 6.244e-06, 1.337e-05}, {7.118e-04, 4.614e-04, 5.471e-04}},
            {{3.810e-05, 4.306e-06, 9.439e-06}, {1.317e-04, 1.759e-04, 2.055e-04}},
            {{4.886e-05, 4.135e-06, 7.119e-06}, {6.791e-06, 4.486e-05, 5.131e-05}},
            {{1.269e-04, 7.618e-06, 1.017e-05}, {9.000e-05, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 2500
    {
        {{7.087e-05, 1.000e-08, 8.272e-05, 1.000e-08}, {2.722e-02, 1.401e-02, 2.702e-02, 1.598e-02}},
        {{
            {{1.902e-04, 2.307e-05, 3.353e-05}, {2.236e-03, 9.970e-04, 1.170e-03}},
            {{3.882e-05, 1.150e-05, 2.147e-05}, {1.155e-03, 7.807e-04, 9.602e-04}},
            {{3.834e-05, 5.670e-06, 1.174e-05}, {2.709e-04, 3.062e-04, 3.873e-04}},
            {{4.783e-05, 5.935e-06, 9.429e-06}, {4.375e-05, 7.575e-05, 9.467e-05}},
            {{1.319e-04, 8.472e-06, 1.255e-05}, {9.194e-05, 5.087e-06, 1.000e-08}},
        }}
    },
}};
