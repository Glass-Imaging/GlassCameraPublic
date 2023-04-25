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

    @autoreleasepool {
        // Allocate FMEN model
        FMEN* fmen = [[FMEN alloc] init];

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

        for (int y = 0; y < rawImage.height; y += fmen_tile_size) {
            for (int x = 0; x < rawImage.width; x += fmen_tile_size) {
                for (int j = 0; j < fmen_tile_size; j++) {
                    for (int i = 0; i < fmen_tile_size; i++) {
                        tile[j][i] = rawImage.getPixel(x + i, y + j) / (float) whiteLevel;
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

                for (int j = 0; j < fmen_tile_size; j++) {
                    for (int i = 0; i < fmen_tile_size; i++) {
                        if (y + j < rawImage.height && x + i < rawImage.width) {
                            (*processedImage)[y + j][x + i] = {
                                (float16_t) resultTileRed[j][i],
                                (float16_t) resultTileGreen[j][i],
                                (float16_t) resultTileBlue[j][i],
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
