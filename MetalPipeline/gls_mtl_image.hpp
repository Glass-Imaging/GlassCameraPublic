//
//  gls_mtl_image.hpp
//  GlassCamera
//
//  Created by Fabio Riccardi on 3/14/23.
//

#ifndef gls_mtl_image_h
#define gls_mtl_image_h

#include <exception>
#include <map>

#include <Metal/Metal.hpp>

#include "gls_image.hpp"

namespace gls {

template <typename T>
class mtl_image : public basic_image<T> {
public:
    typedef std::unique_ptr<mtl_image<T>> unique_ptr;

    mtl_image(int _width, int _height) : basic_image<T>(_width, _height) {}

    static inline MTL::PixelFormat ImageFormat() {
        static_assert(T::channels == 1 || T::channels == 2 || T::channels == 4);
        static_assert(std::is_same<typename T::value_type, float>::value ||
#if USE_FP16_FLOATS && !(__APPLE__ && __x86_64__)
                      std::is_same<typename T::value_type, gls::float16_t>::value ||
#endif
                      std::is_same<typename T::value_type, uint8_t>::value ||
                      std::is_same<typename T::value_type, uint16_t>::value ||
                      std::is_same<typename T::value_type, uint32_t>::value ||
                      std::is_same<typename T::value_type, int8_t>::value ||
                      std::is_same<typename T::value_type, int16_t>::value ||
                      std::is_same<typename T::value_type, int32_t>::value);

        if (std::is_same<typename T::value_type, float>::value) {
            return T::channels == 1 ? MTL::PixelFormatR32Float :
                   T::channels == 2 ? MTL::PixelFormatRG32Float :
                   MTL::PixelFormatRGBA32Float;
        } else if (std::is_same<typename T::value_type, gls::float16_t>::value) {
            return T::channels == 1 ? MTL::PixelFormatR16Float :
                   T::channels == 2 ? MTL::PixelFormatRG16Float :
                   MTL::PixelFormatRGBA32Float;
        } else if (std::is_same<typename T::value_type, uint8_t>::value) {
            return T::channels == 1 ? MTL::PixelFormatR8Unorm :
                   T::channels == 2 ? MTL::PixelFormatRG8Unorm :
                   MTL::PixelFormatRGBA8Unorm;
        } else if (std::is_same<typename T::value_type, uint16_t>::value) {
            return T::channels == 1 ? MTL::PixelFormatR16Unorm :
                   T::channels == 2 ? MTL::PixelFormatRG16Unorm :
                   MTL::PixelFormatRGBA16Unorm;
        } else if (std::is_same<typename T::value_type, uint32_t>::value) {
            return T::channels == 1 ? MTL::PixelFormatR32Uint :
                   T::channels == 2 ? MTL::PixelFormatRG32Uint :
                   MTL::PixelFormatRGBA32Uint;
        } else if (std::is_same<typename T::value_type, int8_t>::value) {
            return T::channels == 1 ? MTL::PixelFormatR8Snorm :
                   T::channels == 2 ? MTL::PixelFormatRG8Snorm :
                   MTL::PixelFormatRGBA8Snorm;
        } else if (std::is_same<typename T::value_type, int16_t>::value) {
            return T::channels == 1 ? MTL::PixelFormatR16Snorm :
                   T::channels == 2 ? MTL::PixelFormatRG16Snorm :
                   MTL::PixelFormatRGBA16Snorm;
        } else if (std::is_same<typename T::value_type, int32_t>::value) {
            return T::channels == 1 ? MTL::PixelFormatR32Sint :
                   T::channels == 2 ? MTL::PixelFormatRG32Sint :
                   MTL::PixelFormatRGBA32Sint;
        }
    }
};

template <typename T>
class mtl_image_2d : public mtl_image<T> {
protected:
    NS::SharedPtr<MTL::Buffer> _buffer;
    NS::SharedPtr<MTL::Texture> _texture;

public:
    const int stride;
    typedef std::unique_ptr<mtl_image_2d<T>> unique_ptr;

    const MTL::Buffer* getBuffer() const {
        return _buffer.get();
    }

    const MTL::Texture* getTexture() const {
        return _texture.get();
    }

    static uint32_t computeStride(MTL::Device* device, MTL::PixelFormat pixelFormat, int _width) {
        const uint32_t mlta = (uint32_t)device->minimumLinearTextureAlignmentForPixelFormat(pixelFormat);
        uint32_t bytesPerRow = mlta * ((4 * _width + mlta - 1) / mlta);
        return bytesPerRow / sizeof(T);
    }

    mtl_image_2d(MTL::Device* device, int _width, int _height)
    : mtl_image<T>(_width, _height), stride(computeStride(device, mtl_image<T>::ImageFormat(), _width)) {
        uint32_t bytesPerRow = sizeof(T) * stride;

        _buffer = NS::TransferPtr(device->newBuffer(bytesPerRow * _height, MTL::ResourceStorageModeShared));

        auto textureDesc = MTL::TextureDescriptor::texture2DDescriptor(mtl_image<T>::ImageFormat(), _width, _height, /*mipmapped=*/ false);
        textureDesc->setStorageMode(MTL::StorageModeShared);
        textureDesc->setUsage(MTL::ResourceUsageSample | MTL::ResourceUsageRead | MTL::ResourceUsageWrite);

        _texture = NS::TransferPtr(_buffer->newTexture(textureDesc, 0, bytesPerRow));
    }

    mtl_image_2d(MTL::Device* device, const gls::image<T>& other)
        : mtl_image_2d(device, other.width, other.height) {
        copyPixelsFrom(other);
    }

    virtual ~mtl_image_2d() {}

    virtual image<T> mapImage() const {
        void* bufferData = _buffer->contents();
        size_t bufferLength = _buffer->length();

        return gls::image<gls::rgba_pixel>(basic_image<T>::width, basic_image<T>::height, stride,
                                           std::span<gls::rgba_pixel>((gls::rgba_pixel*)bufferData, bufferLength / sizeof(gls::rgba_pixel)));
    }

    virtual void unmapImage(const image<T>& mappedImage) const {
        // TODO: Do we need this?
    }

    inline typename gls::image<T>::unique_ptr toImage() const {
        auto image = std::make_unique<gls::image<T>>(gls::image<T>::width, gls::image<T>::height);
        copyPixelsTo(image.get());
        return image;
    }

    static inline unique_ptr fromImage(MTL::Device device, const gls::image<T>& other) {
        return std::make_unique<mtl_image_2d<T>>(device, other);
    }

    void copyPixelsFrom(const image<T>& other) const {
        assert(other.width == image<T>::width && other.height == image<T>::height);
        auto cpuImage = mapImage();
        copyPixels(&cpuImage, other);
        unmapImage(cpuImage);
    }

    void copyPixelsTo(image<T>* other) const {
        assert(other->width == image<T>::width && other->height == image<T>::height);
        auto cpuImage = mapImage();
        copyPixels(other, cpuImage);
        unmapImage(cpuImage);
    }

    void apply(std::function<void(T* pixel, int x, int y)> process) {
        auto cpu_image = mapImage();
        for (int y = 0; y < basic_image<T>::height; y++) {
            for (int x = 0; x < basic_image<T>::width; x++) {
                process(&cpu_image[y][x], x, y);
            }
        }
        unmapImage(cpu_image);
    }
};

}  // namespace gls


#endif /* gls_mtl_image_h */
