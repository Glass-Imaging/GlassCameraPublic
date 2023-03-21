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

        mtlContext.waitForCompletion();

        const auto scaledRawImageCpu = scaledRawImage.mapImage();
        gls::image<gls::luma_pixel> saveImage(scaledRawImageCpu->width, scaledRawImageCpu->height);
        saveImage.apply([&](gls::luma_pixel* p, int x, int y) {
            p->luma = 255 * (*scaledRawImageCpu)[y][x].luma;
        });
        saveImage.write_png_file("/Users/fabio/scaled.png");

        std::cout << "It all went very well..." << std::endl;
        return 0;
    } else {
        std::cout << "We need a file name..." << std::endl;
        return -1;
    }
}
