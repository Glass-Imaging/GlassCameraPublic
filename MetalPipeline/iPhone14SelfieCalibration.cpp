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
class iPhone14SelfieCalibration : public CameraCalibration<levels> {
    static const std::array<NoiseModel<levels>, 8> NLFData;

public:
    NoiseModel<levels> nlfFromIso(int iso) const override {
        iso = std::clamp(iso, 20, 2000);

        if (iso >= 20 && iso < 40) {
            float a = (iso - 20) / 20;
            return lerp<levels>(NLFData[0], NLFData[1], a);
        } else if (iso >= 40 && iso < 80) {
            float a = (iso - 40) / 40;
            return lerp<levels>(NLFData[1], NLFData[2], a);
        } else if (iso >= 80 && iso < 160) {
            float a = (iso - 80) / 80;
            return lerp<levels>(NLFData[2], NLFData[3], a);
        } else if (iso >= 160 && iso < 320) {
            float a = (iso - 160) / 160;
            return lerp<levels>(NLFData[3], NLFData[4], a);
        } else if (iso >= 320 && iso < 640) {
            float a = (iso - 320) / 320;
            return lerp<levels>(NLFData[4], NLFData[5], a);
        } else if (iso >= 640 && iso < 1250) {
            float a = (iso - 640) / 640;
            return lerp<levels>(NLFData[5], NLFData[6], a);
        } else if (iso >= 1250 && iso <= 2000) {
            float a = (iso - 1250) / 750;
            return lerp<levels>(NLFData[6], NLFData[7], a);
        } else {
            throw std::range_error("Unexpected ISO value: " + std::to_string(iso));
        }
    }

    std::pair<RAWDenoiseParameters, std::array<DenoiseParameters, levels>> getDenoiseParameters(int iso) const override {
        const float highNoiseISO = 100;

        const float nlf_alpha = std::clamp((log2(iso) - log2(20)) / (log2(2000) - log2(20)), 0.0, 1.0);
        const float raw_nlf_alpha = std::clamp((log2(iso) - log2(highNoiseISO)) / (log2(2000) - log2(highNoiseISO)), 0.0, 1.0);

        float lerp = std::lerp(1.0, 2.0, nlf_alpha);
        float lerp_c = 1; // std::lerp(1.0, 2.0, nlf_alpha);

        std::cout << "iPhone 14 Selfie DenoiseParameters nlf_alpha: " << nlf_alpha << ", ISO: " << iso << ", lerp: " << lerp << std::endl;

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
                .gradientBoost = 2 - smoothstep(0.3, 0.6, nlf_alpha),
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
            .strength = std::lerp(0.5f, 3.0f, raw_nlf_alpha)
        };

        return { rawDenoiseParameters, denoiseParameters };
    }

    DemosaicParameters buildDemosaicParameters() const override {
        return {
            .lensShadingCorrection = 2,
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
                .detail = { 1, 1.5, 2.0 }
            }
        };
    }
};

std::unique_ptr<DemosaicParameters> unpackiPhone14SelfieRawImage(const gls::image<gls::luma_pixel_16>& inputImage,
                                                                 const gls::Matrix<3, 3>& xyz_rgb,
                                                                 gls::tiff_metadata* dng_metadata,
                                                                 gls::tiff_metadata* exif_metadata) {
    iPhone14SelfieCalibration calibration;
    return calibration.getDemosaicParameters(inputImage, xyz_rgb, dng_metadata, exif_metadata);
}


// --- NLFData ---

template<>
const std::array<NoiseModel<5>, 8> iPhone14SelfieCalibration<5>::NLFData = {{
    // ISO 20
    {
        {{1.938e-05, 1.918e-05, 1.910e-05, 1.920e-05}, {1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {5.611e-05, 3.046e-05, 2.344e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.409e-05, 1.745e-05, 1.399e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.000e-08, 3.794e-06, 4.797e-06}},
            {{2.396e-06, 1.000e-08, 1.000e-08}, {1.000e-08, 1.000e-08, 1.000e-08}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.000e-08, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 40
    {
        {{4.949e-05, 4.858e-05, 4.811e-05, 4.868e-05}, {1.000e-08, 1.000e-08, 2.110e-06, 1.000e-08}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.598e-04, 5.939e-05, 5.116e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.831e-05, 3.706e-05, 3.104e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.517e-06, 1.153e-05, 1.042e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.522e-06, 2.838e-06, 2.054e-06}},
            {{3.175e-06, 1.000e-08, 1.000e-08}, {1.000e-08, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 80
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {9.669e-04, 9.618e-04, 9.717e-04, 9.545e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.306e-04, 8.472e-05, 8.105e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.120e-05, 6.373e-05, 5.624e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.893e-05, 2.488e-05, 2.208e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {6.637e-06, 5.831e-06, 5.550e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.492e-05, 1.781e-06, 1.664e-06}},
        }}
    },
    // ISO 160
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {2.111e-03, 2.096e-03, 2.114e-03, 2.090e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {5.069e-04, 2.155e-04, 1.937e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.631e-05, 5.515e-05, 5.574e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.371e-05, 3.860e-05, 3.622e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.688e-05, 1.232e-05, 1.157e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.824e-05, 2.443e-06, 1.611e-06}},
        }}
    },
    // ISO 320
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {3.845e-03, 3.814e-03, 3.846e-03, 3.810e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.046e-04, 2.250e-04, 2.218e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.535e-04, 2.096e-04, 1.908e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.987e-05, 3.366e-05, 3.165e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.266e-05, 1.976e-05, 1.906e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.254e-05, 6.116e-06, 5.742e-06}},
        }}
    },
    // ISO 640
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {8.560e-03, 8.482e-03, 8.578e-03, 8.481e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.102e-03, 6.510e-04, 5.282e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.625e-04, 2.160e-04, 2.107e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.089e-04, 1.403e-04, 1.211e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.651e-05, 2.389e-05, 2.337e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.075e-05, 1.103e-05, 1.014e-05}},
        }}
    },
    // ISO 1250
    {
        {{8.227e-04, 7.537e-04, 7.283e-04, 7.562e-04}, {1.450e-02, 1.440e-02, 1.454e-02, 1.438e-02}},
        {{
            {{1.813e-04, 1.971e-05, 1.926e-05}, {1.795e-03, 1.018e-03, 9.371e-04}},
            {{1.000e-08, 3.612e-06, 1.490e-05}, {9.524e-04, 7.847e-04, 6.674e-04}},
            {{9.873e-07, 8.670e-06, 1.228e-05}, {1.643e-04, 1.671e-04, 1.528e-04}},
            {{1.000e-08, 2.873e-07, 1.715e-06}, {5.466e-05, 6.418e-05, 6.027e-05}},
            {{1.000e-08, 1.538e-07, 6.219e-07}, {4.831e-05, 1.591e-05, 1.468e-05}},
        }}
    },
    // ISO 2000
    {
        {{1.784e-03, 1.685e-03, 1.648e-03, 1.682e-03}, {2.488e-02, 2.464e-02, 2.492e-02, 2.465e-02}},
        {{
            {{1.517e-04, 9.163e-05, 1.002e-04}, {7.253e-03, 1.851e-03, 1.467e-03}},
            {{3.774e-05, 5.216e-05, 5.969e-05}, {1.273e-03, 1.078e-03, 9.257e-04}},
            {{1.650e-05, 1.374e-05, 2.006e-05}, {2.818e-04, 3.856e-04, 3.356e-04}},
            {{1.865e-06, 3.542e-06, 4.970e-06}, {7.830e-05, 9.636e-05, 8.998e-05}},
            {{1.000e-08, 9.271e-07, 1.312e-06}, {5.640e-05, 2.615e-05, 2.403e-05}},
        }}
    },
}};
