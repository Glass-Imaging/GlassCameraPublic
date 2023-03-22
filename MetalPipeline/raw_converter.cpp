// Copyright (c) 2021-2022 Glass Imaging Inc.
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

#include "raw_converter.hpp"

// Alternative ways to run the Metal pipeline

//        auto kernel = Kernel<MTL::Texture*,     // rawImage
//                             MTL::Texture*,     // scaledRawImage
//                             int,               // bayerPattern
//                             simd::float4,      // scaleMul
//                             float              // blackLevel
//                             >(&mtlContext, "scaleRawData");

//        const auto scaleMul = demosaicParameters->scale_mul;

//        kernel(&mtlContext, /*gridSize=*/ { (unsigned) scaledRawImage.width / 2, (unsigned) scaledRawImage.height / 2, 1 },
//               rawImage->texture(), scaledRawImage.texture(), demosaicParameters->bayerPattern,
//               simd::float4 { scaleMul[0], scaleMul[1], scaleMul[2], scaleMul[3] },
//               demosaicParameters->black_level / 0xffff);

//        mtlContext.scheduleOnCommandBuffer([&] (MTL::CommandBuffer* commandBuffer) {
//            for (int channel = 0; channel < 3; channel++) {
//                mtlContext.wait(commandBuffer);
//
//                kernel(commandBuffer, /*gridSize=*/ { (unsigned) scaledRawImage.width / 2, (unsigned) scaledRawImage.height / 2, 1 },
//                       rawImage->texture(), scaledRawImage.texture(), demosaicParameters->bayerPattern,
//                       simd::float4 { scaleMul[0], scaleMul[1], scaleMul[2], scaleMul[3] },
//                       demosaicParameters->black_level / 0xffff);
//
//                mtlContext.signal(commandBuffer);
//            }
//        }, [&] (MTL::CommandBuffer* commandBuffer) {
//            if (commandBuffer->status() == MTL::CommandBufferStatusCompleted) {
//                const auto start = commandBuffer->GPUStartTime();
//                const auto end = commandBuffer->GPUEndTime();
//
//                std::cout << "Metal execution done, execution time: " << end - start << std::endl;
//
//                const auto scaledRawImageCpu = scaledRawImage.mapImage();
//
//                gls::image<gls::luma_pixel> saveImage(scaledRawImageCpu->width, scaledRawImageCpu->height);
//
//                saveImage.apply([&](gls::luma_pixel* p, int x, int y) {
//                    p->luma = 255 * (*scaledRawImageCpu)[y][x].luma;
//                });
//
//                saveImage.write_png_file("/Users/fabio/scaled.png");
//            } else {
//                std::cout << "Something wrong with Metal execution: " << commandBuffer->status() << std::endl;
//            }
//        });
