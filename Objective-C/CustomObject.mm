#import "CustomObject.h"

#include "gls_image.hpp"

@implementation CustomObject

- (int) someMethod {
    gls::image<gls::rgb_pixel_16> the_image(10, 10);

    the_image.apply([] (gls::rgb_pixel_16* p, int x, int y) {
        uint16_t v = x + y;
        *p = { v, v, v };
    });

    NSLog(@"SomeMethod Ran");
    return the_image[9][9].x;
}

@end
