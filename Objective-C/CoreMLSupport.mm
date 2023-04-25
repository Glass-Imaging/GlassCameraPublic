//
//  CoreMLSupport.m
//  GlassCamera
//
//  Created by Fabio Riccardi on 4/24/23.
//

#import <Foundation/Foundation.h>

#import <Vision/Vision.h>

#import <CoreImage/CoreImage.h>

//#define USE_FEMN_MODEL true

#ifdef USE_FEMN_MODEL
#import "FMEN.h"
#endif

int test() {
    printf("Thanks for the call.\n");
    return 42;
}

void runModel() {
#ifdef USE_FEMN_MODEL
    MLModel* fmen = [[[FMEN alloc] init] model];

    NSError *error = nil;
    NSArray<NSNumber *> *tile_shape = @[@1, @1, @1024, @1024];
    MLMultiArray *multiarray_tile = [[MLMultiArray alloc] initWithShape:tile_shape
                                                               dataType:MLMultiArrayDataTypeFloat error: &error];
    if (error != nil) {
        // Handle the error.
        return;
    }

    FMENInput *femn_input = [[FMENInput alloc] initWithX_3:multiarray_tile];

    FMENOutput* femn_output = [fmen predictionFromFeatures: femn_input error: &error];

    printf("result is %p\n", femn_output);
#endif
}
