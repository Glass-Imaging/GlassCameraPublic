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

#include "MetalComputeTest.hpp"

#include <cassert>
#include <iostream>

#include "gls_image.hpp"
#include "gls_mtl_image.hpp"
#include "gls_mtl.hpp"

static constexpr uint32_t kTextureWidth = 128;
static constexpr uint32_t kTextureHeight = 128;

class Pipeline {
    MetalContext* _mtlContext;
    std::array<gls::mtl_image_2d<gls::rgba_pixel>::unique_ptr, 3> _mandelbrot_image;

public:
    Pipeline(MetalContext* mtlContext) : _mtlContext(mtlContext) {
        for (int i = 0; i < 3; i++) {
            _mandelbrot_image[i] = std::make_unique<gls::mtl_image_2d<gls::rgba_pixel>>(mtlContext->device(), kTextureWidth, kTextureHeight);
        }
    }

    ~Pipeline() {
        _mtlContext->waitForCompletion();
    }

    void run(const std::string& path) {
        auto mandelbrot_set = Kernel<MTL::Texture*,
                                     uint32_t
                                     >(_mtlContext, "mandelbrot_set");

        _mtlContext->scheduleOnCommandBuffer([this, &mandelbrot_set] (MTL::CommandBuffer* commandBuffer) {
            for (int channel = 0; channel < 3; channel++) {
                _mtlContext->wait(commandBuffer);

                mandelbrot_set(commandBuffer, /*gridSize=*/ { kTextureWidth, kTextureHeight, 1 },
                                      _mandelbrot_image[channel]->texture(), channel);

                _mtlContext->signal(commandBuffer);
            }
        }, [this, &path] (MTL::CommandBuffer* commandBuffer) {
            if (commandBuffer->status() == MTL::CommandBufferStatusCompleted) {
                const auto start = commandBuffer->GPUStartTime();
                const auto end = commandBuffer->GPUEndTime();

                std::cout << "Metal execution done, execution time: " << end - start << std::endl;

                for (int i = 0; i < 3; i++) {
                    const auto imageCPU = _mandelbrot_image[i]->mapImage();
                    imageCPU->write_png_file(path + "mandelbrot_" + std::to_string(i) + ".png");
                }
            } else {
                std::cout << "Something wrong with Metal execution: " << commandBuffer->status() << std::endl;
            }
        });

        _mtlContext->waitForCompletion();
    }
};

void runPipelineCommon(NS::SharedPtr<MTL::Device> metalDevice, const char* path) {
    auto context = MetalContext(metalDevice);
    auto pipeline = Pipeline(&context);
    pipeline.run(path);
}

extern "C" void runPipelineCLI(const char* path) {
    auto allMetalDevices = NS::TransferPtr(MTL::CopyAllDevices());

    if (allMetalDevices->count() >= 1) {
        auto metalDevice = NS::RetainPtr(allMetalDevices->object<MTL::Device>(0));
        runPipelineCommon(metalDevice, path);
    } else {
        std::cout << "Couldn't access Metal Device" << std::endl;
    }
}

extern "C" void runPipeline(const char* path) {
    auto metalDevice = NS::TransferPtr(MTL::CreateSystemDefaultDevice());

    runPipelineCommon(metalDevice, path);
}
