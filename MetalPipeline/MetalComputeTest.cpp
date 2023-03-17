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

class MetalContext {
    NS::SharedPtr<MTL::Device> _device;
    NS::SharedPtr<MTL::Library> _computeLibrary;
    NS::SharedPtr<MTL::CommandQueue> _commandQueue;
    MTL::CommandBuffer* _commandBuffer = nullptr;

   public:
    MetalContext(NS::SharedPtr<MTL::Device> device) : _device(device) {
        _computeLibrary = NS::TransferPtr(_device->newDefaultLibrary());
        _commandQueue = NS::TransferPtr(_device->newCommandQueue());
        _commandBuffer = _commandQueue->commandBuffer();
    }
    ~MetalContext() {}

    MTL::Device* device() const {
        return _device.get();
    }

    MTL::CommandBuffer* commandBuffer() const {
        return _commandBuffer;
    }

    NS::SharedPtr<MTL::ComputePipelineState> buildFunctionPipelineState(const std::string& functionName) const {
        NS::Error* error = nullptr;
        auto function =
            NS::TransferPtr(_computeLibrary->newFunction(NS::String::string(functionName.c_str(), NS::UTF8StringEncoding)));
        auto pso = NS::TransferPtr(_device->newComputePipelineState(function.get(), &error));
        if (!pso) {
            __builtin_printf("%s", error->localizedDescription()->utf8String());
            assert(false);
        }
        return pso;
    }
};

template <typename T>
class Function {
    MTL::ComputeCommandEncoder* _encoder;
    NS::SharedPtr<MTL::ComputePipelineState> _functionState;
    const MTL::Size _gridSize;

public:
    class Parameters {
        NS::SharedPtr<MTL::Buffer> _buffer;

    public:
        Parameters() = default;

        Parameters(const Parameters &) = default;

        Parameters(MTL::Device* device, const T& value) :
            _buffer(NS::TransferPtr(device->newBuffer(sizeof(T), MTL::ResourceStorageModeShared)))
        {
            T* contents = (T*) _buffer->contents();
            *contents = value;
        }

        const MTL::Buffer* buffer() const {
            return _buffer.get();
        }

        T* data() {
            return (T*) _buffer->contents();
        }
    };

    Function(MetalContext* mtlContext, const std::string& name, MTL::Size gridSize) : _gridSize(gridSize) {
        _functionState = mtlContext->buildFunctionPipelineState(name);
    }

    void setParameters(MTL::ComputeCommandEncoder* encoder, const Parameters& pb) {
        encoder->setComputePipelineState(_functionState.get());
        encoder->setBuffer(pb.buffer(), 0, 0);
    }

    void dispatchThreads(MTL::ComputeCommandEncoder* encoder) {
        auto threadGroupSize = _functionState->maxTotalThreadsPerThreadgroup();
        auto threadgroupSize = MTL::Size(threadGroupSize, 1, 1);
        encoder->dispatchThreads(_gridSize, threadgroupSize);
    }
};

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
    Pipeline(MetalContext* mtlContext, const std::string path) : _mtlContext(mtlContext) {
        for (int i = 0; i < 3; i++) {
            _mandelbrot_image[i] = std::make_unique<gls::mtl_image_2d<gls::rgba_pixel>>(mtlContext->device(), kTextureWidth, kTextureHeight);
            _parameterBuffers[i] = Function<MandelbrotParameters>::Parameters(mtlContext->device(), { _mandelbrot_image[i]->resourceID(), (uint32_t) i, 0 });
        }
    }

    void run(const std::string path) {
        auto mandelbrot_set = Function<MandelbrotParameters>(_mtlContext, "mandelbrot_set", { kTextureWidth, kTextureHeight, 1 });

        auto commandBuffer = _mtlContext->commandBuffer();

        uint64_t signalCounter = 0;
        auto event = NS::TransferPtr(_mtlContext->device()->newEvent());

        for (int channel = 0; channel < 3; channel++) {
            commandBuffer->encodeWait(event.get(), signalCounter);

            auto encoder = commandBuffer->computeCommandEncoder();
            if (encoder) {
                encoder->useResource(_mandelbrot_image[channel]->texture(), MTL::ResourceUsageWrite);

                mandelbrot_set.setParameters(encoder, _parameterBuffers[channel]);
                mandelbrot_set.dispatchThreads(encoder);

                encoder->endEncoding();
            }
            commandBuffer->encodeSignalEvent(event.get(), ++signalCounter);
        }

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

extern "C" void runRenderer(const char* path) {
    auto metalDevice = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
    auto context = MetalContext(metalDevice);
    auto pipeline = Pipeline(&context, path);
    pipeline.run(path);
}
