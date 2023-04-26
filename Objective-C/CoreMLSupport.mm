//
//  CoreMLSupport.m
//  GlassCamera
//
//  Created by Fabio Riccardi on 4/24/23.
//

#import <Foundation/Foundation.h>
#import <CoreImage/CoreImage.h>

#include "float16.hpp"
#include "gls_image.hpp"

//#define USE_FEMN_MODEL true

#ifdef USE_FEMN_MODEL
#import "FMEN.h"
#endif

void fmenApplyToImage(const gls::image<gls::luma_pixel_16>& rawImage, int whiteLevel, gls::image<gls::rgba_pixel_fp16>* processedImage) {
#ifdef USE_FEMN_MODEL
    const int fmen_tile_size = 1024;
    const int fmen_tile_pixels = fmen_tile_size * fmen_tile_size;
    const int image_tile_padding = 32;
    const int image_tile_size = fmen_tile_size - 2 * image_tile_padding;

    @autoreleasepool {
        // Allocate FMEN model
        static FMEN* fmen = [[FMEN alloc] init];

        // Counting out time
        auto t_fmen_start = std::chrono::high_resolution_clock::now();

        NSError *error = nil;
        NSArray<NSNumber *> *tile_shape = @[@1, @1, @1024, @1024];
        MLMultiArray *multiarray_tile = [[MLMultiArray alloc] initWithShape:tile_shape
                                                                   dataType:MLMultiArrayDataTypeFloat error: &error];
        assert(error == nil);
        FMENInput *femn_input = [[FMENInput alloc] initWithX_3:multiarray_tile];

        gls::image<float> tile(fmen_tile_size, fmen_tile_size, fmen_tile_size,
                               std::span((float *) multiarray_tile.dataPointer, fmen_tile_pixels));

        for (int y = 0; y < rawImage.height; y += image_tile_size) {
            for (int x = 0; x < rawImage.width; x += image_tile_size) {
                for (int j = 0, sj = -image_tile_padding; j < fmen_tile_size; j++, sj++) {
                    for (int i = 0, si = -image_tile_padding; i < fmen_tile_size; i++, si++) {
                        tile[j][i] = rawImage.getPixel(x + si, y + sj) / (float) whiteLevel;
                    }
                }

                FMENOutput* femn_output = [fmen predictionFromFeatures: femn_input error: &error];
                assert(error == nil);

                MLMultiArray *result_multiarray = [femn_output var_540];

                // Minimal sanity check
                NSArray<NSNumber *> *resultShape = [result_multiarray shape];
                assert([resultShape[0] longValue] == 1 &&
                       [resultShape[1] longValue] == 3 &&
                       [resultShape[2] longValue] == fmen_tile_size &&
                       [resultShape[3] longValue] == fmen_tile_size);

                gls::image<float> resultTileRed(fmen_tile_size, fmen_tile_size, fmen_tile_size,
                                                std::span((float *) result_multiarray.dataPointer, fmen_tile_pixels));
                gls::image<float> resultTileGreen(fmen_tile_size, fmen_tile_size, fmen_tile_size,
                                                  std::span((float *) result_multiarray.dataPointer + fmen_tile_pixels, fmen_tile_pixels));
                gls::image<float> resultTileBlue(fmen_tile_size, fmen_tile_size, fmen_tile_size,
                                                 std::span((float *) result_multiarray.dataPointer + 2 * fmen_tile_pixels, fmen_tile_pixels));

                for (int j = 0, rj = image_tile_padding; j < image_tile_size; j++, rj++) {
                    for (int i = 0, ri = image_tile_padding; i < image_tile_size; i++, ri++) {
                        if (y + j < rawImage.height && x + i < rawImage.width) {
                            (*processedImage)[y + j][x + i] = {
                                (float16_t) resultTileRed[rj][ri],
                                (float16_t) resultTileGreen[rj][ri],
                                (float16_t) resultTileBlue[rj][ri],
                                (float16_t) 1
                            };
                        }
                    }
                }
            }
        }

        // Measure execution time
        auto t_fmen_end = std::chrono::high_resolution_clock::now();
        auto elapsed_time_ms = std::chrono::duration<double, std::milli>(t_fmen_end - t_fmen_start).count();
        std::cout << "FMEN Pipeline Execution Time: " << (int)elapsed_time_ms << std::endl;
    }
#endif
}
