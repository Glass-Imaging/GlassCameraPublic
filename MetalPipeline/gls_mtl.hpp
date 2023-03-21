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
#include <exception>
#include <functional>
#include <map>

#include <Metal/Metal.hpp>

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
    std::vector<MTL::CommandBuffer*> work_in_progress;
    EventWrapper _event;

public:
    MetalContext(NS::SharedPtr<MTL::Device> device) : _device(device), _event(_device.get()) {
        _computeLibrary = NS::TransferPtr(_device->newDefaultLibrary());
        _commandQueue = NS::TransferPtr(_device->newCommandQueue());
    }

    ~MetalContext() {
        waitForCompletion();
    }

    void wait(MTL::CommandBuffer* commandBuffer) {
        _event.wait(commandBuffer);
    }

    void signal(MTL::CommandBuffer* commandBuffer) {
        _event.signal(commandBuffer);
    }

    void waitForCompletion() {
        while (!work_in_progress.empty()) {
            for (auto cb : work_in_progress) {
                cb->waitUntilCompleted();
            }
        }
    }

    MTL::Device* device() const {
        return _device.get();
    }

    MTL::ComputePipelineState* newKernelPipelineState(const std::string& kernelName) const {
        NS::Error* error = nullptr;
        auto kernel =
        NS::TransferPtr(_computeLibrary->newFunction(NS::String::string(kernelName.c_str(), NS::UTF8StringEncoding)));
        auto pso = _device->newComputePipelineState(kernel.get(), &error);
        if (!pso) {
            throw std::runtime_error("Couldn't create pipeline state for kernel " + kernelName + " : " + error->localizedDescription()->utf8String());
        }
        return pso;
    }

    void scheduleOnCommandBuffer(std::function<void(MTL::CommandBuffer*)> task, std::function<void(MTL::CommandBuffer*)> completionHandler) {
        auto commandBuffer = _commandQueue->commandBuffer();

        work_in_progress.push_back(commandBuffer);

        task(commandBuffer);

        commandBuffer->addCompletedHandler([this, completionHandler](MTL::CommandBuffer* commandBuffer) {
            completionHandler(commandBuffer);
            work_in_progress.erase(std::remove(work_in_progress.begin(), work_in_progress.end(), commandBuffer), work_in_progress.end());
        });

        commandBuffer->commit();
    }

    void scheduleOnCommandBuffer(std::function<void(MTL::CommandBuffer*)> task) {
        scheduleOnCommandBuffer(task, [](MTL::CommandBuffer*) {});
    }
};

template <typename T>
class BufferParameters {
    NS::SharedPtr<MTL::Buffer> _buffer;

public:
    BufferParameters() = default;

    BufferParameters(const BufferParameters &) = default;

    BufferParameters(MTL::Device* device, const T& value) :
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

extern std::unique_ptr<std::map<const std::string,
                                NS::SharedPtr<MTL::ComputePipelineState>>> kernelStateMap;

template <typename... Ts>
class Kernel {
    MTL::ComputeCommandEncoder* _encoder;
    NS::SharedPtr<MTL::ComputePipelineState> _pipelineState;

    template <int index, typename T0, typename... T1s>
    void setArgs(MTL::ComputeCommandEncoder* encoder, T0&& t0, T1s&&... t1s) {
        setParameter(encoder, t0, index);
        setArgs<index + 1, T1s...>(encoder, std::forward<T1s>(t1s)...);
    }

    template <int index, typename T0>
    void setArgs(MTL::ComputeCommandEncoder* encoder, T0&& t0) {
        setParameter(encoder, t0, index);
    }

public:
    Kernel(MetalContext* mtlContext, const std::string& name) {
        if (kernelStateMap == nullptr) {
            kernelStateMap = std::make_unique<std::map<const std::string,
                                                       NS::SharedPtr<MTL::ComputePipelineState>>>();
        }

        _pipelineState = (*kernelStateMap)[name];

        if (!_pipelineState) {
            _pipelineState = NS::TransferPtr(mtlContext->newKernelPipelineState(name));
            (*kernelStateMap)[name] = _pipelineState;
        }
    }

    ~Kernel() { }

    MTL::ComputePipelineState* pipelineState() const {
        return _pipelineState.get();
    }

    template <typename T>
    void setParameters(MTL::ComputeCommandEncoder* encoder, const BufferParameters<T>& pb) const {
        encoder->setComputePipelineState(_pipelineState.get());
        encoder->setBuffer(pb.buffer(), 0, 0);
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

    void dispatchThreads(const MTL::Size& gridSize, MTL::ComputeCommandEncoder* encoder) const {
        auto threadGroupSize = _pipelineState->maxTotalThreadsPerThreadgroup();
        auto threadgroupSize = MTL::Size(threadGroupSize, 1, 1);
        encoder->dispatchThreads(gridSize, threadgroupSize);
    }

    void operator()(MTL::ComputeCommandEncoder* encoder, const MTL::Size& gridSize, Ts... ts) {
        assert(encoder);
        encoder->setComputePipelineState(_pipelineState.get());
        setArgs<0>(encoder, std::forward<Ts>(ts)...);
        dispatchThreads(gridSize, encoder);
    }

    void operator()(MTL::CommandBuffer* commandBuffer, const MTL::Size& gridSize, Ts... ts) {
        auto encoder = commandBuffer->computeCommandEncoder();
        if (encoder) {
            operator()(encoder, gridSize, std::forward<Ts>(ts)...);
            encoder->endEncoding();
        }
    }

    void operator()(MetalContext* metalContext, const MTL::Size& gridSize, Ts... ts) {
        metalContext->scheduleOnCommandBuffer([&, this](MTL::CommandBuffer* commandBuffer){
            operator()(commandBuffer, gridSize, std::forward<Ts>(ts)...);
        });
    }
};

#endif /* gls_mtl_hpp */
