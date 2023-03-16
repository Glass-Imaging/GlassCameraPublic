//
//  ComputeTest.cpp
//  GlassCamera
//
//  Created by Fabio Riccardi on 3/11/23.
//

#include "MetalComputeTest.hpp"

#include <cassert>
#include <iostream>

#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Metal/Metal.hpp>

#include "gls_image.hpp"
#include "gls_mtl_image.hpp"

static constexpr uint32_t kTextureWidth = 128;
static constexpr uint32_t kTextureHeight = 128;

class Renderer {
   public:
    Renderer(NS::SharedPtr<MTL::Device> device, const std::string& path);
    ~Renderer() {}
    void buildComputePipeline();
    void buildTextures();
    void generateMandelbrotTexture();

   private:
    const std::string _path;
    NS::SharedPtr<MTL::Device> _device;
    NS::SharedPtr<MTL::CommandQueue> _commandQueue;
    NS::SharedPtr<MTL::ComputePipelineState> _computePSO;

    gls::mtl_image_2d<gls::rgba_pixel>::unique_ptr _image;
};

Renderer::Renderer(NS::SharedPtr<MTL::Device> device, const std::string& path) : _device(device), _path(path) {
    _commandQueue = NS::TransferPtr(_device->newCommandQueue());

    buildComputePipeline();
    buildTextures();
    generateMandelbrotTexture();
}

void Renderer::buildComputePipeline() {
    auto computeLibrary = NS::TransferPtr(_device->newDefaultLibrary());
    if (!computeLibrary) {
        __builtin_printf("newDefaultLibrary failed.");
        assert(false);
    }

    NS::Error* error = nullptr;
    auto pMandelbrotFn =
        NS::TransferPtr(computeLibrary->newFunction(NS::String::string("mandelbrot_set", NS::UTF8StringEncoding)));
    _computePSO = NS::TransferPtr(_device->newComputePipelineState(pMandelbrotFn.get(), &error));
    if (!_computePSO) {
        __builtin_printf("%s", error->localizedDescription()->utf8String());
        assert(false);
    }
}

void Renderer::buildTextures() {
    _image = std::make_unique<gls::mtl_image_2d<gls::rgba_pixel>>(_device.get(), kTextureWidth, kTextureHeight);
}

void Renderer::generateMandelbrotTexture() {
    auto commandBuffer = _commandQueue->commandBuffer();
    assert(commandBuffer);

    auto computeEncoder = commandBuffer->computeCommandEncoder();
    computeEncoder->setComputePipelineState(_computePSO.get());
    computeEncoder->setTexture(_image->getTexture(), 0);

    auto gridSize = MTL::Size(kTextureWidth, kTextureHeight, 1);

    auto threadGroupSize = _computePSO->maxTotalThreadsPerThreadgroup();
    auto threadgroupSize = MTL::Size(threadGroupSize, 1, 1);
    computeEncoder->dispatchThreads(gridSize, threadgroupSize);
    computeEncoder->endEncoding();

    const bool useCompletedHandler = false;
    if (useCompletedHandler) {
        commandBuffer->addCompletedHandler([&] (MTL::CommandBuffer* commandBuffer) -> void {
            if (commandBuffer->status() == MTL::CommandBufferStatusCompleted) {
                const auto start = commandBuffer->GPUStartTime();
                const auto end = commandBuffer->GPUEndTime();

                std::cout << "Metal execution done, execution time: " << end - start << std::endl;

                const auto imageCPU = _image->mapImage();
                imageCPU.write_png_file(_path + "test.png");
            } else {
                std::cout << "Something wrong with Metal execution: " << commandBuffer->status() << std::endl;
            }
        });
    }
    commandBuffer->commit();

    // Wait here for all handlers to finish execution, otherwise the context disappears...
    commandBuffer->waitUntilCompleted();
    if (!useCompletedHandler) {
        const auto imageCPU = _image->mapImage();
        imageCPU.write_png_file(_path + "test.png");
    }
}

extern "C" void runRenderer(const char* path) {
    auto metalDevice = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
    auto renderer = Renderer(metalDevice, path);
}
