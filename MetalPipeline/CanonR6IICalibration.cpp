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
class CanonR6IICalibration : public CameraCalibration<levels> {
    static const std::array<NoiseModel<levels>, 11> NLFData;

public:
    NoiseModel<levels> nlfFromIso(int iso) const override {
        iso = std::clamp(iso, 100, 102400);
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
        } else if (iso >= 25600 && iso < 51200) {
            float a = (iso - 25600) / 25600;
            return lerp<levels>(NLFData[8], NLFData[9], a);
        } else /* if (iso >= 51200 && iso <= 102400) */ {
            float a = (iso - 51200) / 51200;
            return lerp<levels>(NLFData[9], NLFData[10], a);
        }
    }

    std::pair<RAWDenoiseParameters, std::array<DenoiseParameters, levels>> getDenoiseParameters(int iso) const override {
        const float nlf_alpha = std::clamp((log2(iso) - log2(100)) / (log2(102400) - log2(100)), 0.0, 1.0);
        const float raw_nlf_alpha = std::clamp((log2(iso) - log2(6400)) / (log2(102400) - log2(6400)), 0.0, 1.0);

        LOG_INFO(TAG) << "CanonR6II DenoiseParameters nlf_alpha: " << nlf_alpha << ", ISO: " << iso << std::endl;

        float lerp = std::lerp(1.0, 3.0, nlf_alpha);
        float lerp_c = 1;

        float lmult[5] = { 1, 1, 1, 1, 1 };
        float cmult[5] = { 1, 1, 1, 1, 1 };

        float chromaBoost = 8;

        std::array<DenoiseParameters, 5> denoiseParameters = {{
            {
                .luma = lmult[0] * lerp,
                .chroma = cmult[0] * lerp_c,
                .chromaBoost = chromaBoost,
                .gradientBoost = 2 * (2 - smoothstep(0.3, 0.6, nlf_alpha)),
                .sharpening = std::lerp(1.1f, 1.0f, nlf_alpha),
            },
            {
                .luma = lmult[1] * lerp,
                .chroma = cmult[1] * lerp_c,
                .chromaBoost = chromaBoost,
                .gradientBoost = (2 - smoothstep(0.3, 0.6, nlf_alpha)),
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
            .highNoiseImage = iso >= 6400,
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
                .shadows = 0.9,
                .highlights = 1.5,
                .detail = { 1, 1.2, 2.0 }
            }
        };
    }
};

std::unique_ptr<DemosaicParameters> unpackCanonR6IIRawImage(const gls::image<gls::luma_pixel_16>& inputImage,
                                                            const gls::Matrix<3, 3>& xyz_rgb,
                                                            gls::tiff_metadata* dng_metadata,
                                                            gls::tiff_metadata* exif_metadata) {
    CanonR6IICalibration calibration;
    auto demosaicParameters = calibration.getDemosaicParameters(inputImage, xyz_rgb, dng_metadata, exif_metadata);

    unpackDNGMetadata(inputImage, dng_metadata, demosaicParameters.get(), xyz_rgb, /*auto_white_balance=*/false, nullptr, false);

    return demosaicParameters;
}


// --- NLFData ---

template<>
const std::array<NoiseModel<5>, 11> CanonR6IICalibration<5>::NLFData = {{
    // ISO 100
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {7.790e-05, 7.240e-05, 6.672e-05, 7.199e-05}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.457e-05, 2.210e-06, 2.594e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.850e-06, 1.235e-06, 1.705e-06}},
            {{9.155e-08, 1.000e-08, 1.000e-08}, {1.144e-06, 3.020e-07, 4.941e-07}},
            {{1.330e-07, 1.000e-08, 1.000e-08}, {5.236e-07, 1.000e-08, 7.363e-08}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.132e-04, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 200
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {9.862e-05, 9.369e-05, 8.910e-05, 9.340e-05}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.022e-05, 3.491e-06, 4.413e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {5.783e-06, 2.108e-06, 2.927e-06}},
            {{7.852e-08, 1.000e-08, 1.000e-08}, {1.530e-06, 5.415e-07, 9.364e-07}},
            {{1.341e-07, 1.000e-08, 1.000e-08}, {6.217e-07, 7.840e-08, 1.612e-07}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.114e-04, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 400
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {1.737e-04, 1.681e-04, 1.639e-04, 1.681e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.635e-05, 6.269e-06, 8.992e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {9.788e-06, 4.077e-06, 5.851e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.651e-06, 1.320e-06, 2.087e-06}},
            {{1.497e-07, 1.000e-08, 1.000e-08}, {8.561e-07, 2.644e-07, 4.432e-07}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {9.356e-05, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 800
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {2.298e-04, 2.225e-04, 2.178e-04, 2.225e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {4.251e-05, 1.033e-05, 1.531e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.477e-05, 6.875e-06, 1.023e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {4.673e-06, 2.498e-06, 3.835e-06}},
            {{1.470e-07, 1.000e-08, 1.000e-08}, {1.222e-06, 5.498e-07, 9.253e-07}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {8.211e-05, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 1600
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {3.042e-04, 2.925e-04, 2.847e-04, 2.920e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.325e-05, 1.782e-05, 2.884e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.031e-05, 1.092e-05, 1.653e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.724e-06, 4.552e-06, 7.157e-06}},
            {{1.181e-07, 1.000e-08, 1.000e-08}, {1.900e-06, 1.111e-06, 1.906e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {4.953e-05, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 3200
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {7.247e-04, 6.950e-04, 6.755e-04, 6.954e-04}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.266e-04, 3.618e-05, 5.743e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.619e-05, 2.073e-05, 3.201e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.256e-05, 8.408e-06, 1.325e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {4.134e-06, 2.409e-06, 4.027e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {5.634e-05, 1.000e-08, 1.000e-08}},
        }}
    },
    // ISO 6400
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {1.457e-03, 1.400e-03, 1.364e-03, 1.401e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.679e-04, 7.325e-05, 1.152e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {6.442e-05, 3.941e-05, 5.709e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.852e-05, 1.429e-05, 2.134e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.338e-06, 4.952e-06, 8.343e-06}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {9.589e-05, 1.040e-06, 1.867e-06}},
        }}
    },
    // ISO 12800
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {3.296e-03, 3.180e-03, 3.106e-03, 3.181e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {4.837e-04, 1.286e-04, 2.314e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.204e-04, 8.102e-05, 1.139e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.611e-05, 2.903e-05, 4.225e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.128e-05, 8.821e-06, 1.319e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.591e-06, 2.454e-06, 4.034e-06}},
        }}
    },
    // ISO 25600
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {5.726e-03, 5.549e-03, 5.450e-03, 5.553e-03}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.286e-04, 2.063e-04, 4.197e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.865e-04, 1.513e-04, 2.367e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.218e-05, 5.190e-05, 7.189e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.956e-05, 1.590e-05, 2.444e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {9.649e-06, 4.480e-06, 7.381e-06}},
        }}
    },
    // ISO 51200
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {1.399e-02, 1.351e-02, 1.320e-02, 1.351e-02}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {2.281e-03, 4.798e-04, 9.065e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {4.912e-04, 2.633e-04, 4.025e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.650e-04, 1.101e-04, 1.617e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {6.124e-05, 3.554e-05, 6.343e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.242e-05, 8.357e-06, 1.385e-05}},
        }}
    },
    // ISO 102400
    {
        {{1.000e-08, 1.000e-08, 1.000e-08, 1.000e-08}, {2.512e-02, 2.438e-02, 2.397e-02, 2.439e-02}},
        {{
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.029e-03, 6.880e-04, 1.520e-03}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {1.314e-03, 5.520e-04, 9.655e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {3.050e-04, 1.953e-04, 2.709e-04}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {9.830e-05, 6.465e-05, 9.921e-05}},
            {{1.000e-08, 1.000e-08, 1.000e-08}, {7.977e-05, 1.696e-05, 2.504e-05}},
        }}
    },
}};
