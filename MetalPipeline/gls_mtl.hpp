// Copyright (c) 2021-2023 Glass Imaging Inc.
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

#include <exception>
#include <functional>
#include <map>
#include <vector>
#include <string>
#include <mutex>

#include <Metal/Metal.hpp>

// Metal execution context implementing a simple sequential pipeline

class MetalContext {
    NS::SharedPtr<MTL::Device> _device;
    NS::SharedPtr<MTL::Library> _computeLibrary;
    NS::SharedPtr<MTL::CommandQueue> _commandQueue;
    std::vector<MTL::CommandBuffer*> work_in_progress;
    std::mutex work_in_progress_mutex;

public:
    MetalContext(NS::SharedPtr<MTL::Device> device) : _device(device) {
        _computeLibrary = NS::TransferPtr(_device->newDefaultLibrary());
        _commandQueue = NS::TransferPtr(_device->newCommandQueue());
    }

    ~MetalContext() {
        waitForCompletion();
    }

    void waitForCompletion() {
        while (true) {
            MTL::CommandBuffer* commandBuffer = nullptr;
            {
                std::lock_guard<std::mutex> guard(work_in_progress_mutex);

                if (!work_in_progress.empty()) {
                    commandBuffer = work_in_progress[work_in_progress.size() - 1];
                } else {
                    break;
                }
            }
            if (commandBuffer) {
                commandBuffer->waitUntilCompleted();
            }
        };
    }

    MTL::Device* device() const {
        return _device.get();
    }

    MTL::ComputePipelineState* newKernelPipelineState(const std::string& kernelName) const {
        NS::Error* error = nullptr;
        auto kernel = NS::TransferPtr(_computeLibrary->newFunction(NS::String::string(kernelName.c_str(), NS::UTF8StringEncoding)));
        auto pso = _device->newComputePipelineState(kernel.get(), &error);
        if (!pso) {
            throw std::runtime_error("Couldn't create pipeline state for kernel " + kernelName + " : " + error->localizedDescription()->utf8String());
        }
        return pso;
    }

    void enqueue(std::function<void(MTL::CommandBuffer*)> task, std::function<void(MTL::CommandBuffer*)> completionHandler) {
        auto commandBuffer = _commandQueue->commandBuffer();

        // Add commandBuffer from work_in_progress
        {
            std::lock_guard<std::mutex> guard(work_in_progress_mutex);
            work_in_progress.push_back(commandBuffer);
        }

        // Schedule task on commandBuffer
        task(commandBuffer);

        commandBuffer->addCompletedHandler((MTL::HandlerFunction) [this, completionHandler](MTL::CommandBuffer* commandBuffer) {
            completionHandler(commandBuffer);

            // Remove completed commandBuffer from work_in_progress
            {
                std::lock_guard<std::mutex> guard(work_in_progress_mutex);
                work_in_progress.erase(std::remove(work_in_progress.begin(), work_in_progress.end(), commandBuffer), work_in_progress.end());
            }
        });

        commandBuffer->commit();
    }

    void enqueue(std::function<void(MTL::CommandBuffer*)> task) {
        enqueue(task, [](MTL::CommandBuffer*) {});
    }

    void enqueue(std::function<void(MTL::ComputeCommandEncoder*)> task, std::function<void(MTL::CommandBuffer*)> completionHandler) {
        enqueue([&] (MTL::CommandBuffer *commandBuffer) {
            auto encoder = commandBuffer->computeCommandEncoder();
            if (encoder) {
                task(encoder);

                encoder->endEncoding();
            }
        }, completionHandler);
    }

    void enqueue(std::function<void(MTL::ComputeCommandEncoder*)> task) {
        enqueue(task, [](MTL::CommandBuffer*) {});
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
    NS::SharedPtr<MTL::ComputePipelineState> _pipelineState;

    template <int index, typename T0, typename... T1s>
    void setArgs(MTL::ComputeCommandEncoder* encoder, T0&& t0, T1s&&... t1s) const {
        setParameter(encoder, t0, index);
        setArgs<index + 1, T1s...>(encoder, std::forward<T1s>(t1s)...);
    }

    template <int index, typename T0>
    void setArgs(MTL::ComputeCommandEncoder* encoder, T0&& t0) const {
        setParameter(encoder, t0, index);
    }

public:
    Kernel(MetalContext* context, const std::string& name) {
        if (kernelStateMap == nullptr) {
            kernelStateMap = std::make_unique<std::map<const std::string,
                                                       NS::SharedPtr<MTL::ComputePipelineState>>>();
        }

        _pipelineState = (*kernelStateMap)[name];

        if (!_pipelineState) {
            _pipelineState = NS::TransferPtr(context->newKernelPipelineState(name));
            (*kernelStateMap)[name] = _pipelineState;
        }
    }

    ~Kernel() { }

    MTL::ComputePipelineState* pipelineState() const {
        return _pipelineState.get();
    }

    template <typename parameter_type>
    void setParameter(MTL::ComputeCommandEncoder* encoder, const parameter_type& parameter, unsigned index) const {
        encoder->setBytes(&parameter, sizeof(parameter_type), index);
    }

    template <>
    void setParameter<MTL::Buffer*>(MTL::ComputeCommandEncoder* encoder, MTL::Buffer* const & buffer, unsigned index) const {
        encoder->setBuffer(buffer, /*offset=*/ 0, index);
    }

    template <>
    void setParameter<MTL::Texture*>(MTL::ComputeCommandEncoder* encoder, MTL::Texture* const & texture, unsigned index) const {
        encoder->setTexture(texture, index);
    }

    void dispatchThreads(const MTL::Size& gridSize, MTL::ComputeCommandEncoder* encoder) const {
        auto threadGroupSize = _pipelineState->maxTotalThreadsPerThreadgroup();
        auto threadgroupSize = MTL::Size(threadGroupSize, 1, 1);
        encoder->dispatchThreads(gridSize, threadgroupSize);
    }

    void operator()(MTL::ComputeCommandEncoder* encoder, const MTL::Size& gridSize, Ts... ts) const {
        assert(encoder);
        encoder->setComputePipelineState(_pipelineState.get());
        setArgs<0>(encoder, std::forward<Ts>(ts)...);
        dispatchThreads(gridSize, encoder);
    }

    void operator()(MTL::CommandBuffer* commandBuffer, const MTL::Size& gridSize, Ts... ts) const {
        auto encoder = commandBuffer->computeCommandEncoder();
        if (encoder) {
            operator()(encoder, gridSize, std::forward<Ts>(ts)...);
            encoder->endEncoding();
        }
    }

    void operator()(MetalContext* metalContext, const MTL::Size& gridSize, Ts... ts) const {
        metalContext->enqueue([&, this](MTL::CommandBuffer* commandBuffer){
            operator()(commandBuffer, gridSize, std::forward<Ts>(ts)...);
        });
    }
};

#endif /* gls_mtl_hpp */
