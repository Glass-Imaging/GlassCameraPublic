//
//  MandelbrotSet.metal
//  GlassCamera
//
//  Created by Fabio Riccardi on 3/13/23.
//

#include <metal_stdlib>
using namespace metal;

struct MandelbrotParameters {
    texture2d<half, access::write> outputTexture;
    uint32_t channel;
    uint32_t extra;
};

kernel void mandelbrot_set(constant MandelbrotParameters& parameters [[buffer(0)]],
                           uint2 index [[thread_position_in_grid]],
                           uint2 gridSize [[threads_per_grid]])
{
    // Scale
    float x0 = 2.0 * index.x / gridSize.x - 1.5;
    float y0 = 2.0 * index.y / gridSize.y - 1.0;
    // Implement Mandelbrot set
    float x = 0.0;
    float y = 0.0;
    uint iteration = 0;
    uint max_iteration = 1000;
    float xtmp = 0.0;
    while (x * x + y * y <= 4 && iteration < max_iteration) {
        xtmp = x * x - y * y + x0;
        y = 2 * x * y + y0;
        x = xtmp;
        iteration += 1;
    }
    // Convert iteration result to colors
    half color = (0.5 + 0.5 * cos(3.0 + iteration * 0.15));

    half3 result = 0;
    result[parameters.channel] = color;

    parameters.outputTexture.write(half4(result, 1.0), index, 0);
}
