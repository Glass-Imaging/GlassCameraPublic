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
    static const std::array<NoiseModel<levels>, 9> NLFData;

public:
    NoiseModel<levels> nlfFromIso(int iso) const override {
        iso = std::clamp(iso, 32, 6400);
        if (iso >= 32 && iso < 50) {
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
        } else if (iso >= 1600 && iso < 3200) {
            float a = (iso - 1600) / 1600;
            return lerp<levels>(NLFData[6], NLFData[7], a);
        } else if (iso >= 3200 && iso <= 6400) {
            float a = (iso - 3200) / 3200;
            return lerp<levels>(NLFData[7], NLFData[8], a);
        } else {
            throw std::range_error("Unexpected ISO value: " + std::to_string(iso));
        }
    }

    std::pair<float, std::array<DenoiseParameters, levels>> getDenoiseParameters(int iso) const override {
        const float nlf_alpha = std::clamp((log2(iso) - log2(32)) / (log2(6400) - log2(32)), 0.0, 1.0);

        float lerp = std::lerp(0.125f, 2.0f, nlf_alpha);
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

std::unique_ptr<DemosaicParameters> unpackiPhoneRawImage(const gls::image<gls::luma_pixel_16>& inputImage,
                                                         const gls::Matrix<3, 3>& xyz_rgb,
                                                         gls::tiff_metadata* dng_metadata,
                                                         gls::tiff_metadata* exif_metadata) {
    iPhone11Calibration calibration;
    return calibration.getDemosaicParameters(inputImage, xyz_rgb, dng_metadata, exif_metadata);
}


// --- NLFData ---

template<>
const std::array<NoiseModel<5>, 9> iPhone11Calibration<5>::NLFData = {{
    // ISO 32
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {1.474e-03, 1.524e-03, 1.585e-03, 1.529e-03}},
        {{
            {{1.710e-06, 1.000e-08, 1.000e-08}, {3.713e-05, 2.140e-05, 1.039e-05}},
            {{8.805e-06, 1.000e-08, 1.000e-08}, {8.285e-06, 1.295e-05, 7.427e-06}},
            {{1.352e-05, 1.000e-08, 1.000e-08}, {1.000e-08, 3.743e-06, 2.071e-06}},
            {{1.225e-08, 1.000e-08, 1.000e-08}, {3.819e-05, 1.104e-06, 1.000e-08}},
            {{2.464e-08, 1.000e-08, 1.000e-08}, {1.335e-05, 2.132e-07, 1.000e-08}},
        }}
    },
    // ISO 50
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {1.559e-03, 1.612e-03, 1.679e-03, 1.619e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {5.839e-05, 3.133e-05, 1.505e-05}},
            {{9.676e-06, 1.000e-08, 1.000e-08}, {1.469e-05, 1.858e-05, 1.069e-05}},
            {{1.603e-05, 1.000e-08, 1.000e-08}, {7.554e-06, 5.367e-06, 3.615e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.810e-05, 1.478e-06, 1.000e-08}},
            {{2.085e-06, 1.000e-08, 1.000e-08}, {1.446e-05, 3.943e-07, 1.000e-08}},
        }}
    },
    // ISO 100
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {1.823e-03, 1.880e-03, 1.959e-03, 1.888e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.080e-04, 5.956e-05, 3.318e-05}},
            {{6.774e-06, 1.000e-08, 1.000e-08}, {3.579e-05, 3.685e-05, 1.996e-05}},
            {{1.448e-05, 1.000e-08, 1.000e-08}, {1.857e-05, 1.022e-05, 7.163e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.812e-04, 2.770e-06, 1.000e-08}},
            {{2.841e-06, 1.000e-08, 1.000e-08}, {2.334e-05, 7.569e-07, 1.000e-08}},
        }}
    },
    // ISO 200
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {2.459e-03, 2.531e-03, 2.643e-03, 2.542e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.917e-04, 1.041e-04, 6.623e-05}},
            {{4.807e-06, 1.000e-08, 1.000e-08}, {5.446e-05, 6.669e-05, 4.335e-05}},
            {{1.249e-05, 1.000e-08, 1.000e-08}, {1.724e-05, 2.196e-05, 1.481e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.497e-04, 4.579e-06, 1.000e-08}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.000e-08, 5.219e-07, 1.000e-08}},
        }}
    },
    // ISO 400
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {4.043e-03, 4.138e-03, 4.309e-03, 4.158e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.794e-04, 1.863e-04, 1.283e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.176e-04, 1.225e-04, 8.990e-05}},
            {{1.347e-05, 1.000e-08, 1.000e-08}, {3.361e-05, 4.122e-05, 2.801e-05}},
            {{1.109e-05, 1.000e-08, 1.000e-08}, {1.470e-05, 8.481e-06, 6.473e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {4.381e-05, 2.262e-06, 1.000e-08}},
        }}
    },
    // ISO 800
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {6.959e-03, 7.088e-03, 7.378e-03, 7.131e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {8.496e-04, 3.359e-04, 2.456e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.276e-04, 2.267e-04, 1.809e-04}},
            {{1.021e-05, 1.000e-08, 1.000e-08}, {7.019e-05, 7.829e-05, 6.218e-05}},
            {{1.440e-05, 1.000e-08, 1.000e-08}, {1.693e-05, 2.003e-05, 1.579e-05}},
            {{1.197e-04, 1.257e-07, 1.566e-06}, {1.701e-05, 4.258e-06, 1.000e-08}},
        }}
    },
    // ISO 1600
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {1.150e-02, 1.168e-02, 1.214e-02, 1.176e-02}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.534e-03, 5.040e-04, 4.187e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {4.375e-04, 3.762e-04, 3.321e-04}},
            {{5.380e-06, 1.000e-08, 1.000e-08}, {1.413e-04, 1.333e-04, 1.226e-04}},
            {{1.508e-05, 1.000e-08, 1.000e-08}, {3.908e-05, 3.242e-05, 2.943e-05}},
            {{1.303e-04, 8.337e-08, 2.377e-06}, {1.278e-05, 8.523e-06, 1.000e-08}},
        }}
    },
    // ISO 3200
    {
        {{1.539e-04, 2.197e-04, 3.202e-04, 2.201e-04}, {1.446e-02, 1.464e-02, 1.517e-02, 1.475e-02}},
        {{
            {{1.000e-08, 5.301e-05, 1.000e-08}, {1.783e-03, 6.441e-04, 5.287e-04}},
            {{1.000e-08, 2.554e-05, 1.000e-08}, {7.118e-04, 5.429e-04, 4.565e-04}},
            {{8.343e-06, 8.685e-06, 1.000e-08}, {2.031e-04, 1.929e-04, 1.836e-04}},
            {{8.704e-06, 3.116e-06, 1.000e-08}, {6.346e-05, 5.006e-05, 5.003e-05}},
            {{1.806e-06, 9.086e-07, 1.042e-07}, {4.283e-05, 1.351e-05, 1.307e-05}},
        }}
    },
    // ISO 6400
    {
        {{2.398e-03, 2.522e-03, 2.761e-03, 2.538e-03}, {1.650e-02, 1.682e-02, 1.756e-02, 1.695e-02}},
        {{
            {{2.556e-04, 2.596e-04, 7.688e-05}, {1.495e-03, 4.811e-04, 6.721e-04}},
            {{1.956e-05, 1.022e-04, 3.027e-05}, {1.153e-03, 1.050e-03, 7.816e-04}},
            {{2.011e-05, 2.117e-05, 5.836e-06}, {3.353e-04, 4.650e-04, 3.492e-04}},
            {{1.519e-05, 7.128e-06, 3.896e-06}, {7.512e-05, 1.181e-04, 9.014e-05}},
            {{1.244e-04, 2.677e-06, 3.293e-06}, {1.000e-08, 3.187e-05, 2.042e-05}},
        }}
    },
}};
