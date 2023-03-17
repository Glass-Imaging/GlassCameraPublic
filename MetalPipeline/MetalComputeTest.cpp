//
//  ComputeTest.cpp
//  GlassCamera
//
//  Created by Fabio Riccardi on 3/11/23.
//

#include "MetalComputeTest.hpp"

#include <cassert>
#include <iostream>

#include "gls_image.hpp"
#include "gls_mtl_image.hpp"
#include "gls_mtl.hpp"

static constexpr uint32_t kTextureWidth = 128;
static constexpr uint32_t kTextureHeight = 128;

struct MandelbrotParameters {
    MTL::ResourceID outputTextureResourceID;
    uint32_t channel;
    uint32_t extra;
};

class Pipeline {
    MetalContext* _mtlContext;
    std::array<gls::mtl_image_2d<gls::rgba_pixel>::unique_ptr, 3> _mandelbrot_image;
    std::array<Function<MandelbrotParameters>::Parameters, 3> _parameterBuffers;

public:
    Pipeline(MetalContext* mtlContext) : _mtlContext(mtlContext) {
        for (int i = 0; i < 3; i++) {
            _mandelbrot_image[i] = std::make_unique<gls::mtl_image_2d<gls::rgba_pixel>>(mtlContext->device(), kTextureWidth, kTextureHeight);
            _parameterBuffers[i] = Function<MandelbrotParameters>::Parameters(mtlContext->device(), {
                _mandelbrot_image[i]->resourceID(), (uint32_t) i, 0
            });
        }
    }

    void run(const std::string& path) {
        auto mandelbrot_set = Function<MandelbrotParameters>(_mtlContext, "mandelbrot_set", { kTextureWidth, kTextureHeight, 1 });

        for (int channel = 0; channel < 3; channel++) {
            _mtlContext->scheduleFunction(mandelbrot_set, _parameterBuffers[channel], [&](MTL::ComputeCommandEncoder *encoder){
                encoder->useResource(_mandelbrot_image[channel]->texture(), MTL::ResourceUsageWrite);
            });
        }

        auto commandBuffer = _mtlContext->commandBuffer();

        commandBuffer->addCompletedHandler([&] (MTL::CommandBuffer* commandBuffer) -> void {
            if (commandBuffer->status() == MTL::CommandBufferStatusCompleted) {
                const auto start = commandBuffer->GPUStartTime();
                const auto end = commandBuffer->GPUEndTime();

                std::cout << "Metal execution done, execution time: " << end - start << std::endl;

                for (int i = 0; i < 3; i++) {
                    const auto imageCPU = _mandelbrot_image[i]->mapImage();
                    imageCPU.write_png_file(path + "mandelbrot_" + std::to_string(i) + ".png");
                }
            } else {
                std::cout << "Something wrong with Metal execution: " << commandBuffer->status() << std::endl;
            }
        });

        commandBuffer->commit();

        // Wait here for all handlers to finish execution, otherwise the context disappears...
        commandBuffer->waitUntilCompleted();
    }
};

extern "C" void runPipeline(const char* path) {
    auto metalDevice = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
    auto context = MetalContext(metalDevice);
    auto pipeline = Pipeline(&context);
    pipeline.run(path);
}
