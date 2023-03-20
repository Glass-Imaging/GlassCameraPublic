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

#ifndef gls_mtl_hpp
#define gls_mtl_hpp

#include <string>
#include <functional>

#include <Metal/Metal.hpp>

class MetalContext;

class Kernel {
    MTL::ComputeCommandEncoder* _encoder;
    NS::SharedPtr<MTL::ComputePipelineState> _kernelState;
    const MTL::Size _gridSize;

public:
    template <typename T>
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

    Kernel(MetalContext* mtlContext, const std::string& name, MTL::Size gridSize);

    template <typename T>
    void setParameters(MTL::ComputeCommandEncoder* encoder, const Parameters<T>& pb) const {
        encoder->setComputePipelineState(_kernelState.get());
        encoder->setBuffer(pb.buffer(), 0, 0);
    }

    void setState(MTL::ComputeCommandEncoder* encoder) const {
        encoder->setComputePipelineState(_kernelState.get());
    }

    template <typename parameter_type>
    void setParameter(MTL::ComputeCommandEncoder* encoder, const parameter_type& parameter, unsigned index) const {
        encoder->setBytes(&parameter, sizeof(parameter_type), index);
    }

    template <>
    void setParameter<MTL::Buffer*>(MTL::ComputeCommandEncoder* encoder, MTL::Buffer* const & parameter, unsigned index) const {
        encoder->setBuffer(parameter, 0, index);
    }

    template <>
    void setParameter<MTL::Texture*>(MTL::ComputeCommandEncoder* encoder, MTL::Texture* const & parameter, unsigned index) const {
        encoder->setTexture(parameter, index);
    }

    void dispatchThreads(MTL::ComputeCommandEncoder* encoder) const {
        auto threadGroupSize = _kernelState->maxTotalThreadsPerThreadgroup();
        auto threadgroupSize = MTL::Size(threadGroupSize, 1, 1);
        encoder->dispatchThreads(_gridSize, threadgroupSize);
    }
};

template <typename... Ts>
class KernelFunctor {
   private:
    const Kernel _kernel;

    template <int index, typename T0, typename... T1s>
    void setArgs(MTL::ComputeCommandEncoder* encoder, T0&& t0, T1s&&... t1s) {
        _kernel.setParameter(encoder, t0, index);
        setArgs<index + 1, T1s...>(encoder, std::forward<T1s>(t1s)...);
    }

    template <int index, typename T0>
    void setArgs(MTL::ComputeCommandEncoder* encoder, T0&& t0) {
        _kernel.setParameter(encoder, t0, index);
    }

    template <int index>
    void setArgs(MTL::ComputeCommandEncoder* encoder) {
    }

   public:
    KernelFunctor(Kernel kernel) : _kernel(kernel) {}

    void operator()(MTL::CommandBuffer* commandBuffer, Ts... ts) {
        auto encoder = commandBuffer->computeCommandEncoder();

        if (encoder) {
            kernel().setState(encoder);

            setArgs<0>(encoder, std::forward<Ts>(ts)...);

            kernel().dispatchThreads(encoder);

            encoder->endEncoding();
        }
    }

    const Kernel& kernel() const { return _kernel; }
};

class EventWrapper {
    NS::SharedPtr<MTL::Event> _event;
    uint64_t _signalCounter = 0;

public:
    EventWrapper(MTL::Device* device) {
        _event = NS::TransferPtr(device->newEvent());
    }

    void signal(MTL::CommandBuffer* commandBuffer) {
        commandBuffer->encodeSignalEvent(_event.get(), ++_signalCounter);
    }

    void wait(MTL::CommandBuffer* commandBuffer) {
        commandBuffer->encodeWait(_event.get(), _signalCounter);
    }
};

class MetalContext {
    NS::SharedPtr<MTL::Device> _device;
    NS::SharedPtr<MTL::Library> _computeLibrary;
    NS::SharedPtr<MTL::CommandQueue> _commandQueue;
    MTL::CommandBuffer* _commandBuffer = nullptr;
    EventWrapper _event;

   public:
    MetalContext(NS::SharedPtr<MTL::Device> device) : _device(device), _event(_device.get()) {
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

    NS::SharedPtr<MTL::ComputePipelineState> buildKernelPipelineState(const std::string& kernelName) const {
        NS::Error* error = nullptr;
        auto kernel =
            NS::TransferPtr(_computeLibrary->newFunction(NS::String::string(kernelName.c_str(), NS::UTF8StringEncoding)));
        auto pso = NS::TransferPtr(_device->newComputePipelineState(kernel.get(), &error));
        if (!pso) {
            __builtin_printf("%s", error->localizedDescription()->utf8String());
            assert(false);
        }
        return pso;
    }

    template <typename T>
    void scheduleKernel(const Kernel& kernel,
                        const typename Kernel::Parameters<T>& parameters,
                        std::function<void(MTL::ComputeCommandEncoder*)> usedResources) {
        _event.wait(_commandBuffer);

        auto encoder = _commandBuffer->computeCommandEncoder();
        if (encoder) {
            usedResources(encoder);

            kernel.setParameters(encoder, parameters);

            kernel.dispatchThreads(encoder);

            encoder->endEncoding();
        }
        _event.signal(_commandBuffer);
    }
};

Kernel::Kernel(MetalContext* mtlContext, const std::string& name, MTL::Size gridSize) : _gridSize(gridSize) {
    _kernelState = mtlContext->buildKernelPipelineState(name);
}

#endif /* gls_mtl_hpp */
