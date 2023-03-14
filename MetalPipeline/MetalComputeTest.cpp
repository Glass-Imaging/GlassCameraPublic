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
    NS::SharedPtr<MTL::Buffer> _textureBuffer;
    NS::SharedPtr<MTL::Texture> _texture;
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
    const uint32_t mlta = (uint32_t)_device->minimumLinearTextureAlignmentForPixelFormat(MTL::PixelFormatRGBA8Unorm);
    uint32_t bytesPerRow = mlta * ((4 * kTextureWidth + mlta - 1) / mlta);

    _textureBuffer =
        NS::TransferPtr(_device->newBuffer(bytesPerRow * kTextureHeight, MTL::ResourceStorageModeShared));

    NS::SharedPtr<MTL::TextureDescriptor> textureDesc = NS::TransferPtr(MTL::TextureDescriptor::alloc()->init());
    textureDesc->setWidth(kTextureWidth);
    textureDesc->setHeight(kTextureHeight);
    textureDesc->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
    textureDesc->setTextureType(MTL::TextureType2D);
    textureDesc->setStorageMode(MTL::StorageModeShared);
    textureDesc->setUsage(MTL::ResourceUsageSample | MTL::ResourceUsageRead | MTL::ResourceUsageWrite);

    _texture = NS::TransferPtr(_textureBuffer->newTexture(textureDesc.get(), 0, bytesPerRow));
}

void Renderer::generateMandelbrotTexture() {
    auto commandBuffer = _commandQueue->commandBuffer();
    assert(commandBuffer);

    auto computeEncoder = commandBuffer->computeCommandEncoder();
    computeEncoder->setComputePipelineState(_computePSO.get());
    computeEncoder->setTexture(_texture.get(), 0);

    auto gridSize = MTL::Size(kTextureWidth, kTextureHeight, 1);

    auto threadGroupSize = _computePSO->maxTotalThreadsPerThreadgroup();
    auto threadgroupSize = MTL::Size(threadGroupSize, 1, 1);

    computeEncoder->dispatchThreads(gridSize, threadgroupSize);
    computeEncoder->endEncoding();
    commandBuffer->commit();

    commandBuffer->waitUntilCompleted();

    void* bufferData = _textureBuffer->contents();
    size_t bufferLength = _textureBuffer->length();

    const uint32_t mlta = (uint32_t)_device->minimumLinearTextureAlignmentForPixelFormat(MTL::PixelFormatRGBA8Unorm);
    uint32_t bytesPerRow = mlta * ((4 * kTextureWidth + mlta - 1) / mlta);

    const auto image = gls::image<gls::rgba_pixel>(
        kTextureWidth, kTextureHeight, bytesPerRow / sizeof(gls::rgba_pixel),
        std::span<gls::rgba_pixel>((gls::rgba_pixel*)bufferData, bufferLength / sizeof(gls::rgba_pixel)));

    image.write_png_file(_path + "test.png");
}

extern "C" void runRenderer(const char* path) {
    auto metalDevice = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
    auto renderer = Renderer(metalDevice, path);
}
