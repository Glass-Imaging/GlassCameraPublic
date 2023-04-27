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

    std::pair<float, std::array<DenoiseParameters, levels>> getDenoiseParameters(int iso) const override {
        const float nlf_alpha = std::clamp((log2(iso) - log2(50)) / (log2(12500) - log2(50)), 0.0, 1.0);

        float lerp = std::lerp(1, 4.0f, nlf_alpha);
        float lerp_c = 1;

        std::cout << "iPhone 14 Wide DenoiseParameters nlf_alpha: " << nlf_alpha << ", ISO: " << iso << ", lerp: " << lerp << std::endl;

        float lmult[5] = { 2, 1, 0.5, 0.25, 0.125 };
        float cmult[5] = { 1, 0.5, 0.25, 0.125, 0.125 };

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
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.990e-05, 7.715e-06, 7.710e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {8.462e-06, 6.251e-06, 5.549e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.021e-06, 2.249e-06, 1.803e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {5.627e-06, 5.193e-07, 3.994e-07}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.403e-05, 2.541e-07, 8.732e-08}},
        }}
    },
    // ISO 100
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {1.913e-04, 1.888e-04, 1.886e-04, 1.890e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.512e-05, 1.375e-05, 1.415e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.340e-05, 1.102e-05, 9.922e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {4.452e-06, 4.170e-06, 3.518e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.089e-06, 1.176e-06, 8.787e-07}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.212e-05, 4.073e-07, 2.700e-07}},
        }}
    },
    // ISO 200
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {2.298e-04, 2.267e-04, 2.290e-04, 2.284e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.946e-05, 1.901e-05, 2.008e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.199e-05, 1.761e-05, 1.626e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {6.262e-06, 7.484e-06, 6.409e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {6.943e-06, 2.197e-06, 1.771e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.208e-05, 6.289e-07, 4.563e-07}},
        }}
    },
    // ISO 400
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {3.414e-04, 3.349e-04, 3.357e-04, 3.361e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.564e-05, 2.865e-05, 3.056e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.244e-05, 2.529e-05, 2.501e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.070e-05, 1.400e-05, 1.202e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.540e-06, 4.265e-06, 3.562e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.180e-05, 1.240e-06, 9.761e-07}},
        }}
    },
    // ISO 800
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {1.558e-03, 1.530e-03, 1.528e-03, 1.528e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {6.635e-05, 3.633e-05, 3.867e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.387e-05, 2.845e-05, 3.047e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.710e-05, 2.434e-05, 2.167e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {9.746e-06, 8.665e-06, 7.100e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.228e-05, 2.576e-06, 2.081e-06}},
        }}
    },
    // ISO 1600
    {
        {{3.532e-06, 1.000e-08, 1.000e-08, 1.000e-08}, {5.932e-04, 6.107e-04, 6.352e-04, 6.166e-04}},
        {{
            {{4.893e-07, 1.000e-08, 1.000e-08}, {5.630e-05, 3.934e-05, 2.672e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.272e-05, 4.623e-05, 3.637e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.963e-05, 1.781e-05, 1.783e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.189e-05, 1.199e-05, 1.085e-05}},
            {{2.234e-03, 8.918e-06, 9.243e-06}, {1.000e-08, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 3200
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {7.953e-03, 7.738e-03, 7.636e-03, 7.723e-03}},
        {{
            {{6.039e-06, 2.234e-06, 5.374e-06}, {1.433e-05, 2.766e-05, 1.000e-08}},
            {{1.000e-08, 2.704e-06, 2.660e-06}, {5.166e-05, 3.037e-05, 2.851e-05}},
            {{1.000e-08, 1.450e-06, 2.080e-08}, {4.754e-05, 2.709e-05, 3.634e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.589e-05, 1.526e-05, 1.461e-05}},
            {{1.991e-03, 8.037e-06, 1.039e-05}, {3.751e-04, 5.344e-06, 1.000e-08}},
        }}
    },
    // ISO 6400
    {
        {{6.763e-04, 6.444e-04, 6.323e-04, 6.446e-04}, {1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}},
        {{
            {{6.075e-05, 3.648e-05, 3.889e-05}, {8.390e-05, 1.000e-08, 1.000e-08}},
            {{1.455e-05, 3.311e-05, 3.188e-05}, {1.848e-04, 4.421e-05, 6.778e-05}},
            {{2.947e-06, 6.606e-06, 6.602e-06}, {1.484e-05, 1.000e-08, 1.000e-08}},
            {{1.000e-08, 2.603e-06, 2.551e-06}, {1.918e-05, 1.248e-05, 1.343e-05}},
            {{2.210e-03, 9.349e-06, 1.410e-05}, {1.000e-08, 8.335e-06, 1.000e-08}},
        }}
    },
    // ISO 12500
    {
        {{2.052e-03, 1.942e-03, 1.908e-03, 1.944e-03}, {1.210e-02, 1.183e-02, 1.176e-02, 1.183e-02}},
        {{
            {{7.533e-05, 5.226e-05, 5.402e-05}, {3.348e-06, 1.000e-08, 1.000e-08}},
            {{5.229e-05, 6.166e-05, 6.199e-05}, {1.000e-08, 1.000e-08, 1.000e-08}},
            {{1.123e-05, 4.121e-05, 3.874e-05}, {1.224e-04, 3.500e-05, 5.576e-05}},
            {{1.843e-06, 6.698e-06, 6.344e-06}, {5.874e-05, 7.597e-05, 7.456e-05}},
            {{2.247e-03, 1.176e-05, 2.156e-05}, {1.000e-08, 1.593e-05, 1.000e-08}},
        }}
    },
}};
