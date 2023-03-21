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

#include <metal_stdlib>
using namespace metal;

enum BayerPattern {
    grbg = 0,
    gbrg = 1,
    rggb = 2,
    bggr = 3
};

enum { raw_red = 0, raw_green = 1, raw_blue = 2, raw_green2 = 3 };

constant const int2 bayerOffsets[4][4] = {
    { {1, 0}, {0, 0}, {0, 1}, {1, 1} }, // grbg
    { {0, 1}, {0, 0}, {1, 0}, {1, 1} }, // gbrg
    { {0, 0}, {0, 1}, {1, 1}, {1, 0} }, // rggb
    { {1, 1}, {0, 1}, {0, 0}, {1, 0} }  // bggr
};

constant half2 sobelKernel2D[3][3] = {
    { { 1,  1 }, { 0,  2 }, { -1,  1 } },
    { { 2,  0 }, { 0,  0 }, { -2,  0 } },
    { { 1, -1 }, { 0, -2 }, { -1, -1 } },
};

void write_imagef(texture2d<float, access::write> image, int2 coord, float4 value) {
    image.write(value, static_cast<uint2>(coord));
}

float4 read_imagef(texture2d<float> image, int2 coord) {
    return image.read(static_cast<uint2>(coord));
}

float4 read_imagef(texture2d<float> image, sampler s, float2 coord) {
    return image.sample(s, coord);
}

void write_imageh(texture2d<half, access::write> image, int2 coord, half4 value) {
    image.write(value, static_cast<uint2>(coord));
}

half4 read_imageh(texture2d<half> image, int2 coord) {
    return image.read(static_cast<uint2>(coord));
}

template <typename T, access a>
int2 get_image_dim(texture2d<T, a> image) {
    return int2(image.get_width(), image.get_height());
}

// Work on one Quad (2x2) at a time
kernel void scaleRawData(texture2d<half> rawImage                       [[texture(0)]],
                         texture2d<half, access::write> scaledRawImage  [[texture(1)]],
                         constant int& bayerPattern                     [[buffer(2)]],
                         constant half4& scaleMul                       [[buffer(3)]],
                         constant half& blackLevel                      [[buffer(4)]],
                         uint2 index                                    [[thread_position_in_grid]])
{
    const int2 imageCoordinates = 2 * (int2) index;

    for (int c = 0; c < 4; c++) {
        int2 o = bayerOffsets[bayerPattern][c];
        write_imageh(scaledRawImage, imageCoordinates + o,
                     max(scaleMul[c] * (read_imageh(rawImage, imageCoordinates + o).x - blackLevel), 0.0h));
    }
}

half2 sobel(texture2d<half> inputImage, int x, int y) {
    half2 value = 0;
    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            half sample = read_imageh(inputImage, (int2) { x + i, y + j }).x;
            value += sobelKernel2D[j+1][i+1] * sample;
        }
    }
    return value / sqrt(4.5);
}

kernel void rawImageSobel(texture2d<half> inputImage                    [[texture(0)]],
                          texture2d<half, access::write> gradientImage  [[texture(1)]],
                          uint2 index                                   [[thread_position_in_grid]])
{
    const int2 imageCoordinates = (int2) index;
    half2 gradient = sobel(inputImage, imageCoordinates.x, imageCoordinates.y);

    write_imageh(gradientImage, imageCoordinates, (half4) { gradient.x, gradient.y, abs(gradient.x), abs(gradient.y) });
}

float4 sampledConvolution(texture2d<float> inputImage,
                          int2 imageCoordinates, float2 inputNorm,
                          int samples, constant float *weights) {
    const float2 inputPos = ((float2) imageCoordinates) * inputNorm;

    constexpr sampler linear_sampler(min_filter::linear,
                                     mag_filter::linear,
                                     mip_filter::linear);
    float4 sum = 0;
    float norm = 0;
    for (int i = 0; i < samples; i++) {
        float w = weights[3 * i + 0];
        sum += w * read_imagef(inputImage, linear_sampler, inputPos + ((float2) { weights[3 * i + 1], weights[3 * i + 2] } + 0.5) * inputNorm);
        norm += w;
    }
    return sum / norm;
}

kernel void sampledConvolutionSobel(texture2d<float> rawImage                   [[texture(0)]],
                                    texture2d<float> sobelImage                 [[texture(1)]],
                                    constant int& samples1                      [[buffer(2)]],
                                    constant float *weights1                    [[buffer(3)]],
                                    constant int& samples2                      [[buffer(4)]],
                                    constant float *weights2                    [[buffer(5)]],
                                    constant float2& rawVariance                [[buffer(6)]],
                                    texture2d<float, access::write> outputImage [[texture(7)]],
                                    uint2 index                                 [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;

    const float2 inputNorm = 1.0 / float2(get_image_dim(outputImage));
    float4 result = sampledConvolution(sobelImage, imageCoordinates, inputNorm, samples1, weights1);

    float sigma = sqrt(rawVariance.x + rawVariance.y * read_imagef(rawImage, imageCoordinates).x);
    if (length(result.xy) < 4 * sigma) {
        result = sampledConvolution(sobelImage, imageCoordinates, inputNorm, samples2, weights2);
    }

    float2 result2 = copysign(result.zw, result.xy);
    write_imagef(outputImage, imageCoordinates, (float4) { result2.x, result2.y, 0, 0});
}

// Modified Hamilton-Adams green channel interpolation

constant const float kHighNoiseVariance = 1e-3;

#define RAW(i, j) read_imagef(rawImage, imageCoordinates + (int2) {i, j}).x

kernel void interpolateGreen(texture2d<float> rawImage                  [[texture(0)]],
                             texture2d<float> gradientImage             [[texture(1)]],
                             texture2d<float, access::write> greenImage [[texture(2)]],
                             constant int& bayerPattern                 [[buffer(3)]],
                             constant float2& greenVariance             [[buffer(4)]],
                             uint2 index                                [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;

    const int x = imageCoordinates.x;
    const int y = imageCoordinates.y;

    const int2 r = bayerOffsets[bayerPattern][raw_red];
    const int2 b = bayerOffsets[bayerPattern][raw_blue];

    const bool red_pixel = (r.x & 1) == (x & 1) && (r.y & 1) == (y & 1);
    const bool blue_pixel = (b.x & 1) == (x & 1) && (b.y & 1) == (y & 1);

    const float lowNoise = 1 - smoothstep(3.5e-4, 2e-3, greenVariance.y);

    if (red_pixel || blue_pixel) {
        // Red and Blue pixel locations
        float g_left  = RAW(-1, 0);
        float g_right = RAW(1, 0);
        float g_up    = RAW(0, -1);
        float g_down  = RAW(0, 1);

        float c_xy    = RAW(0, 0);

        float c_left  = RAW(-2, 0);
        float c_right = RAW(2, 0);
        float c_up    = RAW(0, -2);
        float c_down  = RAW(0, 2);

        float c2_top_left = RAW(-1, -1);
        float c2_top_right = RAW(1, -1);
        float c2_bottom_left = RAW(-1, 1);
        float c2_bottom_right = RAW(1, 1);
        float c2_ave = (c2_top_left + c2_top_right + c2_bottom_left + c2_bottom_right) / 4;

        // Estimate gradient intensity and direction
        float g_ave = (g_left + g_right + g_up + g_down) / 4;
        float2 gradient = abs(read_imagef(gradientImage, imageCoordinates).xy);

        // Hamilton-Adams second order Laplacian Interpolation
        float2 g_lf = { (g_left + g_right) / 2, (g_up + g_down) / 2 };
        float2 g_hf = { ((c_left + c_right) - 2 * c_xy) / 4, ((c_up + c_down) - 2 * c_xy) / 4 };

        // Minimum gradient threshold wrt the noise model
        float rawStdDev = sqrt(greenVariance.x + greenVariance.y * g_ave);
        float gradient_threshold = smoothstep(rawStdDev, 4 * rawStdDev, length(gradient));
        float low_gradient_threshold = 1 - smoothstep(2 * rawStdDev, 8 * rawStdDev, length(gradient));

        // Sharpen low contrast areas
        float sharpening = (0.5 + 0.5 * lowNoise) * gradient_threshold * (1 + lowNoise * low_gradient_threshold);

        // Edges that are in strong highlights tend to exagerate the gradient
        float highlights_edge = 1 - smoothstep(0.25, 1.0, max(c_xy, max(max(c_left, c_right), max(c_up, c_down))));

        // Gradient direction in [0..1]
        float direction = 2 * atan2(gradient.y, gradient.x) / M_PI_F;

        if (greenVariance.y < kHighNoiseVariance) {
            // Bias result towards vertical and horizontal lines
            direction = direction < 0.5 ? mix(direction, 0, 1 - smoothstep(0.3, 0.45, direction))
                                        : mix(direction, 1, smoothstep((1 - 0.45), (1 - 0.3), direction));
        }

        // TODO: Doesn't seem like a good idea, maybe for high noise images?
        // If the gradient is below threshold interpolate against the grain
        // direction = mix(1 - direction, direction, gradient_threshold);

        // Estimate the degree of correlation between channels to drive the amount of HF extraction
        const float cmin = min(c_xy, min(g_ave, c2_ave));
        const float cmax = max(c_xy, max(g_ave, c2_ave));
        float whiteness = cmin / cmax;

        // Modulate the HF component of the reconstructed green using the whiteness and the gradient magnitude
        float2 g_est = g_lf - highlights_edge * whiteness * sharpening * g_hf;

        // Green pixel estimation
        float green = mix(g_est.y, g_est.x, direction);

        // Limit the range of HF correction to something reasonable
        float max_overshoot = mix(1.0, 1.5, whiteness);
        float min_overshoot = mix(1.0, 0.5, whiteness);

        float gmax = max(max(g_left, g_right), max(g_up, g_down));
        float gmin = min(min(g_left, g_right), min(g_up, g_down));
        green = clamp(green, min_overshoot * gmin, max_overshoot * gmax);

        write_imagef(greenImage, imageCoordinates, clamp(green, 0.0, 1.0));
    } else {
        // Green pixel locations
        write_imagef(greenImage, imageCoordinates, read_imagef(rawImage, imageCoordinates).x);
    }
}

/*
    Modified Hamilton-Adams red-blue channels interpolation: Red and Blue locations are interpolate first,
    Green locations are interpolated next as they use the data from the previous step
*/

#define GREEN(i, j) read_imagef(greenImage, imageCoordinates + (int2){i, j}).x

// Interpolate the other color at Red and Blue RAW locations

void interpolateRedBluePixel(texture2d<float> rawImage,
                             texture2d<float> greenImage,
                             texture2d<float> gradientImage,
                             texture2d<float, access::write> rgbImage,
                             float2 redVariance, float2 blueVariance,
                             bool red_pixel, int2 imageCoordinates) {
    float green = GREEN(0, 0);
    float c1 = RAW(0, 0);

    float g_top_left      = GREEN(-1, -1);
    float g_top_right     = GREEN(1, -1);
    float g_bottom_left   = GREEN(-1, 1);
    float g_bottom_right  = GREEN(1, 1);

    float c2_top_left     = RAW(-1, -1);
    float c2_top_right    = RAW(1, -1);
    float c2_bottom_left  = RAW(-1, 1);
    float c2_bottom_right = RAW(1, 1);
    float c2_ave = (c2_top_left + c2_top_right + c2_bottom_left + c2_bottom_right) / 4;

    float gc_top_left     = g_top_left     - c2_top_left;
    float gc_top_right    = g_top_right    - c2_top_right;
    float gc_bottom_left  = g_bottom_left  - c2_bottom_left;
    float gc_bottom_right = g_bottom_right - c2_bottom_right;

    float g_top_left2      = GREEN(-2, -2);
    float g_top_right2     = GREEN(2, -2);
    float g_bottom_left2   = GREEN(-2, 2);
    float g_bottom_right2  = GREEN(2, 2);

    float c_top_left2     = RAW(-2, -2);
    float c_top_right2    = RAW(2, -2);
    float c_bottom_left2  = RAW(-2, 2);
    float c_bottom_right2 = RAW(2, 2);

    float gc_top_left2     = g_top_left2     - c_top_left2;
    float gc_top_right2    = g_top_right2    - c_top_right2;
    float gc_bottom_left2  = g_bottom_left2  - c_bottom_left2;
    float gc_bottom_right2 = g_bottom_right2 - c_bottom_right2;

    // Estimate the (diagonal) gradient direction taking into account the raw noise model
    float2 variance = red_pixel ? redVariance : blueVariance;
    float rawStdDev = sqrt(variance.x + variance.y * c2_ave);
    float2 gradient = abs(read_imagef(gradientImage, imageCoordinates).xy);
    float direction = 1 - 2 * atan2(gradient.y, gradient.x) / M_PI_F;
    float gradient_threshold = smoothstep(rawStdDev, 4 * rawStdDev, length(gradient));
    // If the gradient is below threshold go flat
    float alpha = mix(0.5, direction, gradient_threshold);

    // Edges that are in strong highlights tend to exagerate the gradient
    float highlights_edge = 1 - smoothstep(0.25, 1.0, max(green, max(max(g_top_right2, g_bottom_left2),
                                                                     max(g_top_left2, g_bottom_right2))));

    float c2 = green - mix((gc_top_right + gc_bottom_left) / 2 + highlights_edge * (gc_top_right2 + gc_bottom_left2 - 2 * (green - c1)) / 8,
                           (gc_top_left + gc_bottom_right) / 2 + highlights_edge * (gc_top_left2 + gc_bottom_right2 - 2 * (green - c1)) / 8, alpha);

    // Limit the range of HF correction to something reasonable
    float c2max = max(max(c2_top_left, c2_top_right), max(c2_bottom_left, c2_bottom_right));
    float c2min = min(min(c2_top_left, c2_top_right), min(c2_bottom_left, c2_bottom_right));
    c2 = clamp(c2, c2min, c2max);

    float3 output = red_pixel ? (float3) { c1, green, c2 } : (float3) { c2, green, c1 };

    // TODO: Verify the float4 constructor
    write_imagef(rgbImage, imageCoordinates, float4(clamp(output, 0.0, 1.0), 0));
}

kernel void interpolateRedBlue(texture2d<float> rawImage                [[texture(0)]],
                               texture2d<float> greenImage              [[texture(1)]],
                               texture2d<float> gradientImage           [[texture(2)]],
                               texture2d<float, access::write> rgbImage [[texture(3)]],
                               constant int& bayerPattern               [[buffer(4)]],
                               constant float2& redVariance             [[buffer(5)]],
                               constant float2& blueVariance            [[buffer(6)]],
                               uint2 index                              [[thread_position_in_grid]]) {
    const int2 imageCoordinates = 2 * (int2) index;

    const int2 r = bayerOffsets[bayerPattern][raw_red];
    const int2 g = bayerOffsets[bayerPattern][raw_green];
    const int2 b = bayerOffsets[bayerPattern][raw_blue];
    const int2 g2 = bayerOffsets[bayerPattern][raw_green2];

    interpolateRedBluePixel(rawImage, greenImage, gradientImage, rgbImage, redVariance, blueVariance, true, imageCoordinates + r);
    interpolateRedBluePixel(rawImage, greenImage, gradientImage, rgbImage, redVariance, blueVariance, false, imageCoordinates + b);

    write_imagef(rgbImage, imageCoordinates + g,
                 (float4) {0, clamp(read_imagef(greenImage, imageCoordinates + g).x, 0.0, 1.0), 0, 0});
    write_imagef(rgbImage, imageCoordinates + g2,
                 (float4) {0, clamp(read_imagef(greenImage, imageCoordinates + g2).x, 0.0, 1.0), 0, 0});
}
#undef GREEN

#define RGB(i, j) read_imagef(rgbImageIn, imageCoordinates + (int2){i, j}).xyz

// Interpolate Red and Blue colors at Green RAW locations

void interpolateRedBlueAtGreenPixel(texture2d<float> rgbImageIn,
                                    texture2d<float> gradientImage,
                                    texture2d<float, access::write> rgbImageOut,
                                    float2 redVariance, float2 blueVariance,
                                    int2 imageCoordinates) {
    float3 rgb = RGB(0, 0);

    // Green pixel locations
    float3 rgb_left     = RGB(-1, 0);
    float3 rgb_right    = RGB(1, 0);
    float3 rgb_up       = RGB(0, -1);
    float3 rgb_down     = RGB(0, 1);

    float red_ave       = (rgb_left.x + rgb_right.x + rgb_up.x + rgb_down.x) / 4;
    float blue_ave      = (rgb_left.z + rgb_right.z + rgb_up.z + rgb_down.z) / 4;

    float gred_left     = rgb_left.y  - rgb_left.x;
    float gred_right    = rgb_right.y - rgb_right.x;
    float gred_up       = rgb_up.y    - rgb_up.x;
    float gred_down     = rgb_down.y  - rgb_down.x;

    float gblue_left    = rgb_left.y  - rgb_left.z;
    float gblue_right   = rgb_right.y - rgb_right.z;
    float gblue_up      = rgb_up.y    - rgb_up.z;
    float gblue_down    = rgb_down.y  - rgb_down.z;

    float3 rgb_left3    = RGB(-3, 0);
    float3 rgb_right3   = RGB(3, 0);
    float3 rgb_up3      = RGB(0, -3);
    float3 rgb_down3    = RGB(0, 3);

    float gred_left3    = rgb_left3.y  - rgb_left3.x;
    float gred_right3   = rgb_right3.y - rgb_right3.x;
    float gred_up3      = rgb_up3.y    - rgb_up3.x;
    float gred_down3    = rgb_down3.y  - rgb_down3.x;

    float gblue_left3   = rgb_left3.y  - rgb_left3.z;
    float gblue_right3  = rgb_right3.y - rgb_right3.z;
    float gblue_up3     = rgb_up3.y    - rgb_up3.z;
    float gblue_down3   = rgb_down3.y  - rgb_down3.z;

    // Gradient direction in [0..1]
    float2 gradient = abs(read_imagef(gradientImage, imageCoordinates).xy);
    float direction = 2 * atan2(gradient.y, gradient.x) / M_PI_F;

    float redStdDev = sqrt(redVariance.x + redVariance.y * red_ave);
    float redGradient_threshold = smoothstep(redStdDev, 4 * redStdDev, length(gradient));

    float blueStdDev = sqrt(blueVariance.x + blueVariance.y * blue_ave);
    float blueGradient_threshold = smoothstep(blueStdDev, 4 * blueStdDev, length(gradient));

    // If the gradient is below threshold go flat
    float redAlpha = mix(0.5, direction, redGradient_threshold);
    float blueAlpha = mix(0.5, direction, blueGradient_threshold);

    // Edges that are in strong highlights tend to exagerate the gradient
    float highlights_edge = 1 - smoothstep(0.25, 1.0, max(rgb.y, max(max(rgb_right.y, rgb_left.y),
                                                                     max(rgb_down.y, rgb_up.y))));

    float red = rgb.y - mix((gred_up + gred_down) / 2 - highlights_edge * ((gred_down3 - gred_up) - (gred_down - gred_up3)) / 8,
                            (gred_left + gred_right) / 2 - highlights_edge * ((gred_right3 - gred_left) - (gred_right - gred_left3)) / 8,
                            redAlpha);

    float blue = rgb.y - mix((gblue_up + gblue_down) / 2 - highlights_edge * ((gblue_down3 - gblue_up) - (gblue_down - gblue_up3)) / 8,
                             (gblue_left + gblue_right) / 2 - highlights_edge * ((gblue_right3 - gblue_left) - (gblue_right - gblue_left3)) / 8,
                             blueAlpha);

    // Limit the range of HF correction to something reasonable
    float red_min = min(min(rgb_left.x, rgb_right.x), min(rgb_up.x, rgb_down.x));
    float red_max = max(max(rgb_left.x, rgb_right.x), max(rgb_up.x, rgb_down.x));
    rgb.x = clamp(red, red_min, red_max);

    float blue_min = min(min(rgb_left.z, rgb_right.z), min(rgb_up.z, rgb_down.z));
    float blue_max = max(max(rgb_left.z, rgb_right.z), max(rgb_up.z, rgb_down.z));
    rgb.z = clamp(blue, blue_min, blue_max);

    write_imagef(rgbImageOut, imageCoordinates, float4(rgb, 0));
}

kernel void interpolateRedBlueAtGreen(texture2d<float> rgbImageIn                   [[texture(0)]],
                                      texture2d<float> gradientImage                [[texture(1)]],
                                      texture2d<float, access::write> rgbImageOut   [[texture(2)]],
                                      constant int& bayerPattern                    [[buffer(3)]],
                                      constant float2& redVariance                  [[buffer(4)]],
                                      constant float2& blueVariance                 [[buffer(5)]],
                                      uint2 index                                   [[thread_position_in_grid]])
{
    const int2 imageCoordinates = 2 * (int2) index;

    const int2 g = bayerOffsets[bayerPattern][raw_green];
    const int2 g2 = bayerOffsets[bayerPattern][raw_green2];

    interpolateRedBlueAtGreenPixel(rgbImageIn, gradientImage, rgbImageOut, redVariance, blueVariance, imageCoordinates + g);
    interpolateRedBlueAtGreenPixel(rgbImageIn, gradientImage, rgbImageOut, redVariance, blueVariance, imageCoordinates + g2);
}
