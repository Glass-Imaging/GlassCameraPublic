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

    std::pair<float, std::array<DenoiseParameters, levels>> getDenoiseParameters(int iso) const override {
        const float nlf_alpha = std::clamp((log2(iso) - log2(20)) / (log2(2500) - log2(20)), 0.0, 1.0);

        float lerp = std::lerp(0.5f, 2.5f, nlf_alpha);
        float lerp_c = 1;

        std::cout << "iPhone DenoiseParameters nlf_alpha: " << nlf_alpha << ", ISO: " << iso << ", lerp: " << lerp << std::endl;

        float lmult[5] = { 2, 2, 1, 0.5, 0.25 };
        float cmult[5] = { 1, 0.5, 0.5, 0.5, 0.25 };

        float chromaBoost = 8 * (2 - smoothstep(0.25, 0.35, nlf_alpha));

        std::array<DenoiseParameters, 5> denoiseParameters = {{
            {
                .luma = lmult[0] * lerp,
                .chroma = cmult[0] * lerp_c,
                .chromaBoost = chromaBoost,
                .gradientBoost = 2 * (2 - smoothstep(0.7, 1.0, nlf_alpha)),
                .sharpening = std::lerp(1.5f, 1.0f, nlf_alpha)
            },
            {
                .luma = lmult[1] * lerp,
                .chroma = cmult[1] * lerp_c,
                .chromaBoost = chromaBoost,
                .gradientBoost = (2 - smoothstep(0.7, 1.0, nlf_alpha)),
                .sharpening = 1.1
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

//std::pair<float, std::array<DenoiseParameters, levels>> getDenoiseParameters(int iso) const override {
//    const float nlf_alpha = std::clamp((log2(iso) - log2(20)) / (log2(2500) - log2(20)), 0.0, 1.0);
//
//    float lerp = std::lerp(1.0f, 2.0f, nlf_alpha);
//    float lerp_c = 1;
//
//    std::cout << "iPhone DenoiseParameters nlf_alpha: " << nlf_alpha << ", ISO: " << iso << ", lerp: " << lerp << std::endl;
//
//    float lmult[5] = { 1, 0.25, 0.25, 0.125, 0.125 };
//    float cmult[5] = { 0.5, 0.5, 0.5, 0.5, 0.5 };
//
//    float chromaBoost = 4 * (2 - smoothstep(0.25, 0.35, nlf_alpha));
//
//    std::array<DenoiseParameters, 5> denoiseParameters = {{
//        {
//            .luma = lmult[0] * lerp,
//            .chroma = cmult[0] * lerp_c,
//            .chromaBoost = chromaBoost,
//            // .gradientBoost = 2 * (2 - smoothstep(0.7, 1.0, nlf_alpha)),
//            .sharpening = std::lerp(1.5f, 1.0f, nlf_alpha)
//        },
//        {
//            .luma = lmult[1] * lerp,
//            .chroma = cmult[1] * lerp_c,
//            .chromaBoost = chromaBoost,
//            // .gradientBoost = (2 - smoothstep(0.7, 1.0, nlf_alpha)),
//            .sharpening = 1.1
//        },
//        {
//            .luma = lmult[2] * lerp,
//            .chroma = cmult[2] * lerp_c,
//            .chromaBoost = chromaBoost,
//        },
//        {
//            .luma = lmult[3] * lerp,
//            .chroma = cmult[3] * lerp_c,
//            .chromaBoost = chromaBoost,
//        },
//        {
//            .luma = lmult[4] * lerp,
//            .chroma = cmult[4] * lerp_c,
//            .chromaBoost = chromaBoost,
//        }
//    }};
//
//    return { nlf_alpha, denoiseParameters };
//}


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
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {3.695e-04, 3.727e-04, 3.791e-04, 3.732e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {6.260e-05, 1.788e-05, 1.449e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.428e-05, 1.026e-05, 9.016e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {6.001e-05, 3.997e-06, 1.000e-08}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.347e-05, 1.000e-08, 1.000e-08}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.124e-05, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 32
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {3.405e-04, 3.383e-04, 3.451e-04, 3.420e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {8.970e-05, 2.506e-05, 1.846e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.898e-05, 1.459e-05, 1.235e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {5.455e-05, 6.503e-06, 1.000e-08}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.041e-04, 4.192e-07, 1.000e-08}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.620e-05, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 50
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {6.048e-04, 6.045e-04, 6.130e-04, 6.048e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.307e-04, 4.153e-05, 3.051e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.761e-05, 2.469e-05, 1.950e-05}},
            {{4.783e-07, 1.000e-08, 1.000e-08}, {7.724e-06, 7.515e-06, 4.977e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.969e-04, 1.815e-06, 1.000e-08}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {4.574e-05, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 100
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {1.048e-03, 1.044e-03, 1.058e-03, 1.044e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.404e-04, 8.364e-05, 6.635e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {5.340e-05, 5.216e-05, 3.733e-05}},
            {{1.469e-07, 1.000e-08, 1.000e-08}, {1.339e-05, 1.458e-05, 1.239e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.894e-04, 6.233e-06, 1.000e-08}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.470e-04, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 200
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {1.917e-03, 1.903e-03, 1.929e-03, 1.905e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {4.805e-04, 1.766e-04, 1.424e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.053e-04, 1.106e-04, 8.301e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.599e-05, 3.154e-05, 2.573e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.152e-04, 9.591e-06, 1.000e-08}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.655e-04, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 400
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {3.599e-03, 3.571e-03, 3.624e-03, 3.576e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.001e-03, 3.454e-04, 2.760e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.036e-04, 2.173e-04, 1.724e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {4.788e-05, 6.649e-05, 4.942e-05}},
            {{3.195e-06, 1.000e-08, 1.000e-08}, {3.313e-05, 1.490e-05, 1.118e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {6.305e-04, 6.408e-06, 1.000e-08}},
        }}
    },
    // ISO 800
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {7.388e-03, 7.336e-03, 7.449e-03, 7.343e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.228e-03, 5.650e-04, 4.260e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.147e-04, 4.044e-04, 3.110e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {9.150e-05, 1.423e-04, 1.057e-04}},
            {{3.448e-06, 1.000e-08, 1.000e-08}, {4.105e-05, 3.121e-05, 2.761e-05}},
            {{1.409e-04, 1.507e-06, 3.280e-06}, {2.401e-04, 5.250e-06, 1.000e-08}},
        }}
    },
    // ISO 1600
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {1.608e-02, 1.596e-02, 1.617e-02, 1.597e-02}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.947e-03, 9.855e-04, 7.988e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {6.498e-04, 7.913e-04, 6.168e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.816e-04, 2.817e-04, 2.281e-04}},
            {{2.580e-06, 1.000e-08, 1.000e-08}, {6.527e-05, 6.847e-05, 5.778e-05}},
            {{1.137e-05, 1.000e-08, 1.000e-08}, {1.098e-04, 1.740e-05, 9.417e-06}},
        }}
    },
    // ISO 2500
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {2.287e-02, 2.266e-02, 2.289e-02, 2.266e-02}},
        {{
            {{2.664e-05, 3.021e-06, 4.059e-06}, {1.956e-03, 1.195e-03, 1.031e-03}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.053e-03, 1.111e-03, 8.851e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.978e-04, 4.376e-04, 3.531e-04}},
            {{2.939e-06, 1.000e-08, 1.000e-08}, {9.513e-05, 1.117e-04, 9.307e-05}},
            {{1.648e-05, 1.000e-08, 1.000e-08}, {8.524e-05, 2.715e-05, 2.416e-05}},
        }}
    },
}};
