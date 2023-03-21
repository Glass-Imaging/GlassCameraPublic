//
//  main.cpp
//  PipelineTest
//
//  Created by Fabio Riccardi on 3/17/23.
//

#include <filesystem>
#include <iostream>
#include <simd/simd.h>

#include "demosaic_mtl.hpp"

#include "gls_tiff_metadata.hpp"

std::unique_ptr<DemosaicParameters> unpackSonya6400RawImage(const gls::image<gls::luma_pixel_16>& inputImage,
                                                            gls::tiff_metadata* dng_metadata, gls::tiff_metadata* exif_metadata);

template <typename T>
void dumpGradientImage(const gls::mtl_image_2d<T>& image, const std::string& path) {
    gls::image<gls::rgb_pixel> out(image.width, image.height);
    const auto image_cpu = image.mapImage();
    out.apply([&](gls::rgb_pixel* p, int x, int y) {
        const auto& ip = (*image_cpu)[y][x];

        // float direction = (1 + atan2(ip.y, ip.x) / M_PI) / 2;
        // float direction = atan2(abs(ip.y), ip.x) / M_PI;
        float direction = std::atan2(std::abs(ip.y), std::abs(ip.x)) / M_PI_2;
        float magnitude = std::sqrt((float)(ip.x * ip.x + ip.y * ip.y));

        uint8_t val = std::clamp(255 * std::sqrt(magnitude), 0.0f, 255.0f);

        *p = gls::rgb_pixel{
            (uint8_t)(val * std::lerp(1.0f, 0.0f, direction)),
            0,
            (uint8_t)(val * std::lerp(1.0f, 0.0f, 1 - direction)),
        };
    });
    out.write_png_file(path);
}

std::array<gls::Vector<2>, 3> getRawVariance(const RawNLF& rawNLF) {
    const gls::Vector<2> greenVariance = {(rawNLF.first[1] + rawNLF.first[3]) / 2,
                                          (rawNLF.second[1] + rawNLF.second[3]) / 2};
    const gls::Vector<2> redVariance = {rawNLF.first[0], rawNLF.second[0]};
    const gls::Vector<2> blueVariance = {rawNLF.first[2], rawNLF.second[2]};

    return {redVariance, greenVariance, blueVariance};
}

int main(int argc, const char * argv[]) {
    if (argc > 1) {
        auto input_path = std::filesystem::path(argv[1]);

        auto allMetalDevices = NS::TransferPtr(MTL::CopyAllDevices());
        auto metalDevice = NS::RetainPtr(allMetalDevices->object<MTL::Device>(0));

        gls::tiff_metadata dng_metadata, exif_metadata;
        gls::mtl_image_2d<gls::luma_pixel_16>::unique_ptr rawImage;
        const auto rawImageCpu =
        gls::image<gls::luma_pixel_16>::read_dng_file(input_path.string(),
                                                      [&](int width, int height) -> gls::image<gls::luma_pixel_16>::unique_ptr {
            rawImage = std::make_unique<gls::mtl_image_2d<gls::luma_pixel_16>>(metalDevice.get(), width, height);
            return rawImage->mapImage();
        }, &dng_metadata, &exif_metadata);

        auto demosaicParameters = unpackSonya6400RawImage(*rawImageCpu, &dng_metadata, &exif_metadata);

        gls::mtl_image_2d<gls::luma_pixel_float> scaledRawImage(metalDevice.get(), rawImage->width, rawImage->height);

        auto mtlContext = MetalContext(metalDevice);

//        auto kernel = Kernel<MTL::Texture*,     // rawImage
//                             MTL::Texture*,     // scaledRawImage
//                             int,               // bayerPattern
//                             simd::float4,      // scaleMul
//                             float              // blackLevel
//                             >(&mtlContext, "scaleRawData");

//        const auto scaleMul = demosaicParameters->scale_mul;

//        kernel(&mtlContext, /*gridSize=*/ { (unsigned) scaledRawImage.width / 2, (unsigned) scaledRawImage.height / 2, 1 },
//               rawImage->texture(), scaledRawImage.texture(), demosaicParameters->bayerPattern,
//               simd::float4 { scaleMul[0], scaleMul[1], scaleMul[2], scaleMul[3] },
//               demosaicParameters->black_level / 0xffff);

//        mtlContext.scheduleOnCommandBuffer([&] (MTL::CommandBuffer* commandBuffer) {
//            for (int channel = 0; channel < 3; channel++) {
//                mtlContext.wait(commandBuffer);
//
//                kernel(commandBuffer, /*gridSize=*/ { (unsigned) scaledRawImage.width / 2, (unsigned) scaledRawImage.height / 2, 1 },
//                       rawImage->texture(), scaledRawImage.texture(), demosaicParameters->bayerPattern,
//                       simd::float4 { scaleMul[0], scaleMul[1], scaleMul[2], scaleMul[3] },
//                       demosaicParameters->black_level / 0xffff);
//
//                mtlContext.signal(commandBuffer);
//            }
//        }, [&] (MTL::CommandBuffer* commandBuffer) {
//            if (commandBuffer->status() == MTL::CommandBufferStatusCompleted) {
//                const auto start = commandBuffer->GPUStartTime();
//                const auto end = commandBuffer->GPUEndTime();
//
//                std::cout << "Metal execution done, execution time: " << end - start << std::endl;
//
//                const auto scaledRawImageCpu = scaledRawImage.mapImage();
//
//                gls::image<gls::luma_pixel> saveImage(scaledRawImageCpu->width, scaledRawImageCpu->height);
//
//                saveImage.apply([&](gls::luma_pixel* p, int x, int y) {
//                    p->luma = 255 * (*scaledRawImageCpu)[y][x].luma;
//                });
//
//                saveImage.write_png_file("/Users/fabio/scaled.png");
//            } else {
//                std::cout << "Something wrong with Metal execution: " << commandBuffer->status() << std::endl;
//            }
//        });


        scaleRawData(&mtlContext, *rawImage, &scaledRawImage, demosaicParameters->bayerPattern, demosaicParameters->scale_mul, demosaicParameters->black_level / 0xffff);

        gls::mtl_image_2d<gls::rgba_pixel_float> rawSobelImage(metalDevice.get(), rawImage->width, rawImage->height);

        rawImageSobel(&mtlContext, scaledRawImage, &rawSobelImage);

        NoiseModel<5>* noiseModel = &demosaicParameters->noiseModel;
        const auto rawVariance = getRawVariance(noiseModel->rawNlf);

        std::cout << "rawVariance: " << rawVariance[1] << std::endl;

        gls::mtl_image_2d<gls::luma_alpha_pixel_float> rawGradientImage(metalDevice.get(), rawImage->width, rawImage->height);
        gaussianBlurSobelImage(&mtlContext, scaledRawImage, rawSobelImage, rawVariance[1], 1.5, 4.5, &rawGradientImage);

        mtlContext.waitForCompletion();

        const auto scaledRawImageCpu = scaledRawImage.mapImage();
        gls::image<gls::luma_pixel> saveImage(scaledRawImageCpu->width, scaledRawImageCpu->height);
        saveImage.apply([&](gls::luma_pixel* p, int x, int y) {
            p->luma = 255 * (*scaledRawImageCpu)[y][x].luma;
        });
        saveImage.write_png_file("/Users/fabio/scaled.png");

        dumpGradientImage(rawSobelImage, "/Users/fabio/rawSobelImage.png");

        dumpGradientImage(rawGradientImage, "/Users/fabio/rawGradientImage.png");

        std::cout << "It all went very well..." << std::endl;
        return 0;
    } else {
        std::cout << "We need a file name..." << std::endl;
        return -1;
    }
}
