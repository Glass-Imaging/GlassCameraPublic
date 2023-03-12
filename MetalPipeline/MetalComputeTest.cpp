//
//  ComputeTest.cpp
//  GlassCamera
//
//  Created by Fabio Riccardi on 3/11/23.
//

#include "MetalComputeTest.hpp"

#include <iostream>

#include <cassert>

#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define MTK_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#include <Metal/Metal.hpp>

#include "gls_image.hpp"

static constexpr uint32_t kTextureWidth = 128;
static constexpr uint32_t kTextureHeight = 128;

class Renderer
{
    public:
        Renderer( MTL::Device* pDevice, const std::string& path );
        ~Renderer();
        void buildComputePipeline();
        void buildTextures();
        void generateMandelbrotTexture();

    private:
        const std::string _path;
        MTL::Device* _pDevice;
        MTL::CommandQueue* _pCommandQueue;
        MTL::ComputePipelineState* _pComputePSO;
        MTL::Buffer* _pTextureBuffer;
        MTL::Texture* _pTexture;
};

Renderer::Renderer( MTL::Device* pDevice, const std::string& path )
: _pDevice( pDevice->retain() ), _path(path)
{
    std::cout << "Creating Metal Command Queue" << std::endl;
    _pCommandQueue = _pDevice->newCommandQueue();

    std::cout << "Building Compute Pipeline" << std::endl;
    buildComputePipeline();

    std::cout << "Building Metal Textures" << std::endl;
    buildTextures();

    std::cout << "Running Metal Compute Shader" << std::endl;
    generateMandelbrotTexture();
}

Renderer::~Renderer()
{
    _pTexture->release();
    _pTextureBuffer->release();
    _pComputePSO->release();
    _pCommandQueue->release();
    _pDevice->release();
}

void Renderer::buildComputePipeline()
{
    const char* kernelSrc = R"(
        #include <metal_stdlib>
        using namespace metal;
        kernel void mandelbrot_set(texture2d< half, access::write > tex [[texture(0)]],
                                   uint2 index [[thread_position_in_grid]],
                                   uint2 gridSize [[threads_per_grid]])
        {
            // Scale
            float x0 = 2.0 * index.x / gridSize.x - 1.5;
            float y0 = 2.0 * index.y / gridSize.y - 1.0;
            // Implement Mandelbrot set
            float x = 0.0;
            float y = 0.0;
            uint iteration = 0;
            uint max_iteration = 1000;
            float xtmp = 0.0;
            while(x * x + y * y <= 4 && iteration < max_iteration)
            {
                xtmp = x * x - y * y + x0;
                y = 2 * x * y + y0;
                x = xtmp;
                iteration += 1;
            }
            // Convert iteration result to colors
            half color = (0.5 + 0.5 * cos(3.0 + iteration * 0.15));
            tex.write(half4(color, color, color, 1.0), index, 0);
        })";
    NS::Error* pError = nullptr;

    MTL::Library* pComputeLibrary = _pDevice->newLibrary( NS::String::string(kernelSrc, NS::UTF8StringEncoding), nullptr, &pError );
    if ( !pComputeLibrary )
    {
        __builtin_printf( "%s", pError->localizedDescription()->utf8String() );
        assert(false);
    }

    MTL::Function* pMandelbrotFn = pComputeLibrary->newFunction( NS::String::string("mandelbrot_set", NS::UTF8StringEncoding) );
    _pComputePSO = _pDevice->newComputePipelineState( pMandelbrotFn, &pError );
    if ( !_pComputePSO )
    {
        __builtin_printf( "%s", pError->localizedDescription()->utf8String() );
        assert(false);
    }

    pMandelbrotFn->release();
    pComputeLibrary->release();
}

void Renderer::buildTextures()
{
    const uint32_t mlta = (uint32_t) _pDevice->minimumLinearTextureAlignmentForPixelFormat(MTL::PixelFormatRGBA8Unorm);
    uint32_t bytesPerRow = mlta * ((4 * kTextureWidth + mlta - 1) / mlta);

    _pTextureBuffer = _pDevice->newBuffer( bytesPerRow * kTextureHeight, MTL::ResourceStorageModeShared );

    MTL::TextureDescriptor* pTextureDesc = MTL::TextureDescriptor::alloc()->init();
    pTextureDesc->setWidth( kTextureWidth );
    pTextureDesc->setHeight( kTextureHeight );
    pTextureDesc->setPixelFormat( MTL::PixelFormatRGBA8Unorm );
    pTextureDesc->setTextureType( MTL::TextureType2D );
    pTextureDesc->setStorageMode( MTL::StorageModeShared );
    pTextureDesc->setUsage( MTL::ResourceUsageSample | MTL::ResourceUsageRead | MTL::ResourceUsageWrite);

    MTL::Texture *pTexture = _pTextureBuffer->newTexture(pTextureDesc, 0, bytesPerRow);
    _pTexture = pTexture;

    pTextureDesc->release();
}

void Renderer::generateMandelbrotTexture()
{
    MTL::CommandBuffer* pCommandBuffer = _pCommandQueue->commandBuffer();
    assert(pCommandBuffer);

    MTL::ComputeCommandEncoder* pComputeEncoder = pCommandBuffer->computeCommandEncoder();

    pComputeEncoder->setComputePipelineState( _pComputePSO );
    pComputeEncoder->setTexture( _pTexture, 0 );

    MTL::Size gridSize = MTL::Size( kTextureWidth, kTextureHeight, 1 );

    NS::UInteger threadGroupSize = _pComputePSO->maxTotalThreadsPerThreadgroup();
    MTL::Size threadgroupSize( threadGroupSize, 1, 1 );

    pComputeEncoder->dispatchThreads( gridSize, threadgroupSize );

    pComputeEncoder->endEncoding();

    pCommandBuffer->commit();

    pCommandBuffer->waitUntilCompleted();

    void* bufferData = _pTextureBuffer->contents();
    size_t bufferLength = _pTextureBuffer->length();

    std::cout << "bufferData: " << (uint64_t) bufferData << ", bufferLength: " << bufferLength << std::endl;

    const uint32_t mlta = (uint32_t) _pDevice->minimumLinearTextureAlignmentForPixelFormat(MTL::PixelFormatRGBA8Unorm);
    uint32_t bytesPerRow = mlta * ((4 * kTextureWidth + mlta - 1) / mlta);

    const auto image = gls::image<gls::rgba_pixel>(kTextureWidth, kTextureHeight, bytesPerRow / sizeof(gls::rgba_pixel),
                                                   std::span<gls::rgba_pixel>((gls::rgba_pixel*) bufferData,
                                                                              bufferLength / sizeof(gls::rgba_pixel)));

    std::cout << "Writing image to path: " << _path << std::endl;

    image.write_png_file(_path + "test.png");
}

extern "C" void runRenderer(const char* path) {
    std::cout << "runRenderer - path: " << path << std::endl;

    std::cout << "Creating Metal Device" << std::endl;

    auto metalDevice = MTL::CreateSystemDefaultDevice();

    std::cout << "Creating Renderer" << std::endl;

    auto renderer = new Renderer(metalDevice, path);

    std::cout << "Destroying Renderer" << std::endl;

    delete renderer;

    std::cout << "Releasing Metal Device" << std::endl;

    metalDevice->release();

    std::cout << "All done with Metal." << std::endl;
}
