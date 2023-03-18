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

    Function(MetalContext* mtlContext, const std::string& name, MTL::Size gridSize);

    void setParameters(MTL::ComputeCommandEncoder* encoder, const Parameters& pb) const {
        encoder->setComputePipelineState(_functionState.get());
        encoder->setBuffer(pb.buffer(), 0, 0);
    }

    void dispatchThreads(MTL::ComputeCommandEncoder* encoder) const {
        auto threadGroupSize = _functionState->maxTotalThreadsPerThreadgroup();
        auto threadgroupSize = MTL::Size(threadGroupSize, 1, 1);
        encoder->dispatchThreads(_gridSize, threadgroupSize);
    }
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

    template <typename T>
    void scheduleFunction(const Function<T>& function,
                          const typename Function<T>::Parameters& parameters,
                          std::function<void(MTL::ComputeCommandEncoder*)> usedResources) {
        _event.wait(_commandBuffer);

        auto encoder = _commandBuffer->computeCommandEncoder();
        if (encoder) {
            usedResources(encoder);

            function.setParameters(encoder, parameters);
            function.dispatchThreads(encoder);

            encoder->endEncoding();
        }
        _event.signal(_commandBuffer);
    }
};

template <typename T>
Function<T>::Function(MetalContext* mtlContext, const std::string& name, MTL::Size gridSize) : _gridSize(gridSize) {
    _functionState = mtlContext->buildFunctionPipelineState(name);
}

#endif /* gls_mtl_hpp */
