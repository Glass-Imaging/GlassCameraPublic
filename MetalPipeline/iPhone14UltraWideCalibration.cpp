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
class iPhone14UltraWideCalibration : public CameraCalibration<levels> {
    static const std::array<NoiseModel<levels>, 8> NLFData;

public:
    NoiseModel<levels> nlfFromIso(int iso) const override {
        iso = std::clamp(iso, 32, 6400);

        if (iso >= 32 && iso < 50) {
            float a = (iso - 18) / 18;
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
        } else if (iso >= 1600 && iso <= 3200) {
            float a = (iso - 1600) / 1600;
            return lerp<levels>(NLFData[6], NLFData[7], a);
        } else {
            throw std::range_error("Unexpected ISO value: " + std::to_string(iso));
        }
    }

    std::pair<RAWDenoiseParameters, std::array<DenoiseParameters, levels>> getDenoiseParameters(int iso) const override {
        const float highNoiseISO = 100;

        const float nlf_alpha = std::clamp((log2(iso) - log2(32)) / (log2(3200) - log2(32)), 0.0, 1.0);
        const float raw_nlf_alpha = std::clamp((log2(iso) - log2(highNoiseISO)) / (log2(3200) - log2(highNoiseISO)), 0.0, 1.0);

        float lerp = std::lerp(1.0, 2.0, nlf_alpha);
        float lerp_c = 1; // std::lerp(1.0, 2.0, nlf_alpha);

        std::cout << "iPhone 14 UltraWide DenoiseParameters nlf_alpha: " << nlf_alpha << ", ISO: " << iso << ", lerp: " << lerp << std::endl;

        float lmult[5] = { 2, 1, 1, 1, 1 };
        float cmult[5] = { 1, 1, 1, 1, 1 };

        std::array<DenoiseParameters, 5> denoiseParameters = {{
            {
                .luma = lmult[0] * lerp,
                .chroma = cmult[0] * lerp_c,
                .chromaBoost = 8,
                .gradientBoost = 4 * (2 - smoothstep(0.3, 0.6, nlf_alpha)),
                .gradientThreshold = 2,
                .sharpening = std::lerp(1.5f, 1.0f, nlf_alpha)
            },
            {
                .luma = lmult[1] * lerp,
                .chroma = cmult[1] * lerp_c,
                .chromaBoost = 4,
                .gradientBoost = 2 * (2 - smoothstep(0.3, 0.6, nlf_alpha)),
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

std::unique_ptr<DemosaicParameters> unpackiPhone14UltraWideRawImage(const gls::image<gls::luma_pixel_16>& inputImage,
                                                                    const gls::Matrix<3, 3>& xyz_rgb,
                                                                    gls::tiff_metadata* dng_metadata,
                                                                    gls::tiff_metadata* exif_metadata) {
    iPhone14UltraWideCalibration calibration;
    return calibration.getDemosaicParameters(inputImage, xyz_rgb, dng_metadata, exif_metadata);
}


// --- NLFData ---

template<>
const std::array<NoiseModel<5>, 8> iPhone14UltraWideCalibration<5>::NLFData = {{
	// ISO 32
	{
		{{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {2.822e-04, 2.760e-04, 2.753e-04, 2.783e-04}},
		{{
			{{1.000e-08, 1.000e-08, 1.000e-08}, {3.269e-05, 1.659e-05, 1.792e-05}},
			{{1.000e-08, 1.000e-08, 1.000e-08}, {1.600e-05, 1.100e-05, 1.182e-05}},
			{{1.000e-08, 1.000e-08, 1.000e-08}, {1.004e-05, 4.222e-06, 4.605e-06}},
			{{1.000e-08, 1.000e-08, 1.000e-08}, {1.399e-05, 1.320e-06, 1.429e-06}},
			{{1.000e-08, 1.000e-08, 1.000e-08}, {9.826e-05, 3.491e-07, 2.475e-07}},
		}}
	},
    // ISO 50
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {4.049e-04, 3.948e-04, 3.934e-04, 3.983e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {4.338e-05, 2.410e-05, 2.607e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.072e-05, 1.573e-05, 1.662e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.128e-05, 6.208e-06, 6.844e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.447e-05, 1.884e-06, 2.062e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {9.327e-05, 4.969e-07, 4.085e-07}},
        }}
    },
    // ISO 100
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {8.081e-04, 7.864e-04, 7.817e-04, 7.928e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.061e-04, 4.880e-05, 5.221e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.319e-05, 2.811e-05, 2.924e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.418e-05, 1.103e-05, 1.214e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.437e-05, 3.525e-06, 3.870e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {9.741e-05, 8.975e-07, 8.086e-07}},
        }}
    },
    // ISO 200
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {1.529e-03, 1.473e-03, 1.449e-03, 1.486e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.314e-04, 9.410e-05, 1.062e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {6.998e-05, 5.747e-05, 5.633e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.129e-05, 1.964e-05, 2.082e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.558e-05, 6.675e-06, 7.580e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {9.733e-05, 1.788e-06, 2.169e-06}},
        }}
    },
    // ISO 400
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {2.528e-03, 2.428e-03, 2.390e-03, 2.455e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.982e-04, 1.702e-04, 1.817e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.520e-04, 1.159e-04, 1.228e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.382e-05, 3.476e-05, 3.480e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.840e-05, 1.217e-05, 1.351e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.467e-05, 4.759e-06, 4.873e-06}},
        }}
    },
    // ISO 800
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {4.535e-03, 4.380e-03, 4.341e-03, 4.436e-03}},
        {{
            {{1.248e-05, 1.000e-08, 1.000e-08}, {2.914e-04, 1.762e-04, 1.793e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.193e-04, 1.730e-04, 1.753e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.823e-05, 7.525e-05, 8.489e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.351e-05, 1.905e-05, 2.015e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.913e-05, 7.682e-06, 7.510e-06}},
        }}
    },
    // ISO 1600
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {9.908e-03, 9.475e-03, 9.315e-03, 9.588e-03}},
        {{
            {{3.558e-05, 3.410e-06, 1.885e-06}, {1.426e-03, 5.386e-04, 6.218e-04}},
            {{1.000e-08, 1.000e-08, 1.543e-06}, {6.212e-04, 4.087e-04, 4.409e-04}},
            {{1.000e-08, 1.000e-08, 2.539e-06}, {1.203e-04, 1.143e-04, 1.193e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {6.643e-05, 3.711e-05, 4.281e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.916e-05, 1.393e-05, 1.318e-05}},
        }}
    },
    // ISO 3200
    {
        {{1.509e-03, 1.407e-03, 1.369e-03, 1.412e-03}, {1.406e-02, 1.343e-02, 1.318e-02, 1.361e-02}},
        {{
            {{4.127e-04, 8.893e-05, 9.854e-05}, {7.346e-04, 6.898e-04, 8.190e-04}},
            {{3.326e-05, 4.838e-05, 5.891e-05}, {9.729e-04, 6.541e-04, 7.299e-04}},
            {{1.167e-05, 1.238e-05, 1.765e-05}, {2.361e-04, 2.451e-04, 2.882e-04}},
            {{1.000e-08, 2.640e-06, 4.235e-06}, {8.589e-05, 6.445e-05, 7.738e-05}},
            {{1.000e-08, 5.761e-07, 9.327e-07}, {1.074e-04, 1.917e-05, 2.314e-05}},
        }}
    },
}};
