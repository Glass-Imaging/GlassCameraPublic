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

#include <metal_stdlib>
using namespace metal;

//#define half float
//#define half2 float2
//#define half3 float3
//#define half4 float4

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

half4 read_imageh(texture2d<half> image, sampler s, float2 coord) {
    return image.sample(s, coord);
}

void write_imageui(texture2d<uint, access::write> image, int2 coord, uint4 value) {
    image.write(value, static_cast<uint2>(coord));
}

uint4 read_imageui(texture2d<uint> image, int2 coord) {
    return image.read(static_cast<uint2>(coord));
}

uint4 read_imageui(texture2d<uint> image, sampler s, float2 coord) {
    return image.sample(s, coord);
}

template <typename T, access a>
int2 get_image_dim(texture2d<T, a> image) {
    return int2(image.get_width(), image.get_height());
}

half gaussian(half x) {
    return exp(- 2 * x * x);
}

half lensShading(half lensShadingCorrection, half distance_from_center) {
    // New iPhones
    return 0.8 + lensShadingCorrection * distance_from_center * distance_from_center;
    // Old iPhones
    // return 1 + distance_from_center * distance_from_center;
    // return 1;
}

// Work on one Quad (2x2) at a time
kernel void scaleRawData(texture2d<half> rawImage                       [[texture(0)]],
                         texture2d<half, access::write> scaledRawImage  [[texture(1)]],
                         constant int& bayerPattern                     [[buffer(2)]],
                         constant half4& scaleMul                       [[buffer(3)]],
                         constant half& blackLevel                      [[buffer(4)]],
                         constant half& lensShadingCorrection           [[buffer(5)]],
                         uint2 index                                    [[thread_position_in_grid]])
{
    const int2 imageCoordinates = 2 * (int2) index;

    half lens_shading = 1;
    if (lensShadingCorrection > 0) {
        float2 imageCenter = float2(get_image_dim(rawImage) / 2);
        float distance_from_center = length(float2(imageCoordinates) - imageCenter) / length(imageCenter);
        // FIXME: the attenuation is a fuzz factor
        lens_shading = lensShading(lensShadingCorrection, distance_from_center);
    }

    for (int c = 0; c < 4; c++) {
        int2 o = bayerOffsets[bayerPattern][c];
        write_imageh(scaledRawImage, imageCoordinates + o,
                     max(lens_shading * scaleMul[c] * (read_imageh(rawImage, imageCoordinates + o).x - blackLevel) * 0.9 + 0.1, 0.0));
    }
}

half2 sobel(texture2d<half> inputImage, int x, int y) {
    half2 value = 0;
    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            half sample = read_imageh(inputImage, int2(x + i, y + j)).x;
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

    write_imageh(gradientImage, imageCoordinates, half4(gradient, abs(gradient)));
}

float sampledConvolutionLuma(texture2d<float> inputImage,
                             int2 imageCoordinates, float2 inputNorm,
                             int samples, constant float *weights) {
    const float2 inputPos = (float2(imageCoordinates)) * inputNorm;

    constexpr sampler linear_sampler(filter::linear);

    float sum = 0;
    float norm = 0;
    for (int i = 0; i < samples; i++) {
        float w = weights[3 * i];
        float2 off = float2(weights[3 * i + 1], weights[3 * i + 2]);
        sum += w * read_imagef(inputImage, linear_sampler, inputPos + (off + 0.5) * inputNorm).x;
        norm += w;
    }
    return sum / norm;
}

kernel void hfNoiseTransferImage(texture2d<float> inputImage                    [[texture(0)]],
                                 texture2d<float> noisyImage                    [[texture(1)]],
                                 texture2d<float, access::write> outputImage    [[texture(2)]],
                                 constant int& samples                          [[buffer(3)]],
                                 constant float* weights                        [[buffer(4)]],
                                 uint2 index                                    [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;
    const float2 inputNorm = 1.0 / float2(get_image_dim(outputImage));

    float filtered_luma = sampledConvolutionLuma(noisyImage, imageCoordinates, inputNorm, samples, weights);
    float hf_luma_noise = read_imagef(noisyImage, imageCoordinates).x - filtered_luma;
    float3 input = read_imagef(inputImage, imageCoordinates).xyz;

    write_imagef(outputImage, imageCoordinates, float4(input.x + hf_luma_noise, input.yz, 0));
}

float4 sampledConvolution(texture2d<float> inputImage,
                          int2 imageCoordinates, float2 inputNorm,
                          int samples, constant float *weights) {
    const float2 inputPos = (float2(imageCoordinates)) * inputNorm;

    constexpr sampler linear_sampler(filter::linear);

    float4 sum = 0;
    float norm = 0;
    for (int i = 0; i < samples; i++) {
        float w = weights[3 * i];
        float2 off = float2(weights[3 * i + 1], weights[3 * i + 2]);
        sum += w * read_imagef(inputImage, linear_sampler, inputPos + (off + 0.5) * inputNorm);
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

    write_imagef(outputImage, imageCoordinates, float4(copysign(result.zw, result.xy), 0, 0));
}

// Modified Hamilton-Adams green channel interpolation

constant const float kHighNoiseVariance = 1e-3;

#define RAW(i, j) read_imagef(rawImage, imageCoordinates + int2(i, j)).x

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

        write_imagef(greenImage, imageCoordinates, green);
    } else {
        // Green pixel locations
        write_imagef(greenImage, imageCoordinates, read_imagef(rawImage, imageCoordinates).x);
    }
}

/*
    Modified Hamilton-Adams red-blue channels interpolation: Red and Blue locations are interpolate first,
    Green locations are interpolated next as they use the data from the previous step
*/

#define GREEN(i, j) read_imagef(greenImage, imageCoordinates + int2(i, j)).x

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

    float3 output = red_pixel ? float3(c1, green, c2) : float3(c2, green, c1);

    write_imagef(rgbImage, imageCoordinates, float4(output, 0));
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
                 float4(0, read_imagef(greenImage, imageCoordinates + g).x, 0, 0));
    write_imagef(rgbImage, imageCoordinates + g2,
                 float4(0, read_imagef(greenImage, imageCoordinates + g2).x, 0, 0));
}
#undef GREEN

#define RGB(i, j) read_imagef(rgbImageIn, imageCoordinates + int2(i, j)).xyz

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

    const int2 r = bayerOffsets[bayerPattern][raw_red];
    const int2 g = bayerOffsets[bayerPattern][raw_green];
    const int2 g2 = bayerOffsets[bayerPattern][raw_green2];
    const int2 b = bayerOffsets[bayerPattern][raw_blue];

    interpolateRedBlueAtGreenPixel(rgbImageIn, gradientImage, rgbImageOut, redVariance, blueVariance, imageCoordinates + g);
    interpolateRedBlueAtGreenPixel(rgbImageIn, gradientImage, rgbImageOut, redVariance, blueVariance, imageCoordinates + g2);

    write_imagef(rgbImageOut, imageCoordinates + r, read_imagef(rgbImageIn, imageCoordinates + r));
    write_imagef(rgbImageOut, imageCoordinates + b, read_imagef(rgbImageIn, imageCoordinates + b));
}

#define M_SQRT3_F 1.7320508f

constant float3 trans[3] = {
    {         1,          1, 1 },
    { M_SQRT3_F, -M_SQRT3_F, 0 },
    {        -1,         -1, 2 },
};
constant float3 itrans[3] = {
    { 1,  M_SQRT3_F / 2, -0.5 },
    { 1, -M_SQRT3_F / 2, -0.5 },
    { 1,              0,  1   },
};

kernel void blendHighlightsImage(texture2d<float> inputImage                    [[texture(0)]],
                                 constant float& clip                           [[buffer(1)]],
                                 texture2d<float, access::write> outputImage    [[texture(2)]],
                                 uint2 index                                    [[thread_position_in_grid]])
{
    const int2 imageCoordinates = (int2) index;

    float3 pixel = read_imagef(inputImage, imageCoordinates).xyz;
    if (any(pixel > clip)) {
        float3 cam[2] = {pixel, min(pixel, clip)};

        float3 lab[2];
        float sum[2];
        for (int i = 0; i < 2; i++) {
            lab[i] = float3(dot(trans[0], cam[i]),
                            dot(trans[1], cam[i]),
                            dot(trans[2], cam[i]));
            sum[i] = dot(lab[i].yz, lab[i].yz);
        }
        float chratio = sum[0] > 0 ? sqrt(sum[1] / sum[0]) : 1;
        lab[0].yz *= chratio;

        pixel = float3(dot(itrans[0], lab[0]),
                       dot(itrans[1], lab[0]),
                       dot(itrans[2], lab[0])) / 3;
    }

    write_imagef(outputImage, imageCoordinates, float4(pixel, 0.0));
}

float3 gaussianBlur(float radius, texture2d<float> inputImage, int2 imageCoordinates) {
    const int kernelSize = (int) (2 * ceil(2.5 * radius) + 1);

    float3 blurred_pixel = 0;
    float3 kernel_norm = 0;
    for (int y = -kernelSize / 2; y <= kernelSize / 2; y++) {
        for (int x = -kernelSize / 2; x <= kernelSize / 2; x++) {
            float kernelWeight = exp(-((float)(x * x + y * y) / (2 * radius * radius)));
            blurred_pixel += kernelWeight * read_imagef(inputImage, imageCoordinates + (int2){x, y}).xyz;
            kernel_norm += kernelWeight;
        }
    }
    return blurred_pixel / kernel_norm;
}

float3 sharpen(float3 pixel_value, float amount, float radius, texture2d<float> inputImage, int2 imageCoordinates) {
    float3 dx = read_imagef(inputImage, imageCoordinates + (int2){1, 0}).xyz - pixel_value;
    float3 dy = read_imagef(inputImage, imageCoordinates + (int2){0, 1}).xyz - pixel_value;

    // Smart sharpening
    float3 sharpening = amount * smoothstep(0.0, 0.03, length(dx) + length(dy))     // Gradient magnitude thresholding
                               * (1.0 - smoothstep(0.95, 1.0, pixel_value))         // Highlight ringing protection
                               * (0.6 + 0.4 * smoothstep(0.0, 0.1, pixel_value));   // Shadows ringing protection

    float3 blurred_pixel = gaussianBlur(radius, inputImage, imageCoordinates);

    return mix(blurred_pixel, pixel_value, max(sharpening, 1.0));
}

// ---- Tone Curve ----

float3 sigmoid(float3 x, float s) {
    return 0.5 * (tanh(s * x - 0.3 * s) + 1);
}

float sigmoid(float x, float s) {
    return 0.5 * (tanh(s * x - 0.3 * s) + 1);
}

// This tone curve is designed to mostly match the default curve from DNG files

float3 toneCurve(float3 x, float s) {
    return (sigmoid(powr(0.95 * x, 0.5), s) - sigmoid(0.0, s)) / (sigmoid(1.0, s) - sigmoid(0.0, s));
}

float toneCurve(float x, float s) {
    return (sigmoid(powr(0.95 * x, 0.5), s) - sigmoid(0.0, s)) / (sigmoid(1.0, s) - sigmoid(0.0, s));
}

float3 saturationBoost(float3 value, float saturation) {
    // Saturation boost with highlight protection
    const float luma = 0.2126 * value.x + 0.7152 * value.y + 0.0722 * value.z; // BT.709-2 (sRGB) luma primaries
    const float3 clipping = smoothstep(0.75, 2.0, value);
    return mix(luma, value, mix(saturation, 1.0, clipping));
}

float3 desaturateBlacks(float3 value) {
    // Saturation boost with highlight protection
    const float luma = 0.2126 * value.x + 0.7152 * value.y + 0.0722 * value.z; // BT.709-2 (sRGB) luma primaries
    const float desaturate = smoothstep(0.005, 0.04, luma);
    return mix(luma, value, desaturate);
}

float3 contrastBoost(float3 value, float contrast) {
    const float gray = 0.2;
    const float3 clipping = smoothstep(0.9, 2.0, value);
    return mix(gray, value, mix(contrast, 1.0, clipping));
}

// Make sure this struct is in sync with the declaration in demosaic.hpp
typedef struct RGBConversionParameters {
    float contrast;
    float saturation;
    float toneCurveSlope;
    float exposureBias;
    float blacks;
    bool localToneMapping;
} RGBConversionParameters;

typedef struct {
    float3 m[3];
} Matrix3x3;

kernel void transformImage(texture2d<float> inputImage                  [[texture(0)]],
                           texture2d<float, access::write> outputImage  [[texture(1)]],
                           constant Matrix3x3& transform                [[buffer(2)]],
                           uint2 index                                  [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;

    float3 inputValue = read_imagef(inputImage, imageCoordinates).xyz;
    float3 outputPixel = float3(
        dot(transform.m[0], inputValue),
        dot(transform.m[1], inputValue),
        dot(transform.m[2], inputValue)
    );
    write_imagef(outputImage, imageCoordinates, float4(outputPixel, 0.0));
}

half tunnel(half x, half y, half angle, half sigma) {
    half a = x * cos(angle) + y * sin(angle);
    return exp(-(a * a) / sigma);
}

kernel void denoiseImage(texture2d<half> inputImage                     [[texture(0)]],
                         texture2d<half> gradientImage                  [[texture(1)]],
                         constant float3& var_a                         [[buffer(2)]],
                         constant float3& var_b                         [[buffer(3)]],
                         constant float3& thresholdMultipliers          [[buffer(4)]],
                         constant float& chromaBoost                    [[buffer(5)]],
                         constant float& gradientBoost                  [[buffer(6)]],
                         constant float& gradientThreshold              [[buffer(7)]],
                         texture2d<half, access::write> denoisedImage   [[texture(8)]],
                         uint2 index                                    [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;

    const half3 inputYCC = read_imageh(inputImage, imageCoordinates).xyz;

    half3 sigma = half3(sqrt(var_a + var_b * inputYCC.x));
    half3 diffMultiplier = 1 / (half3(thresholdMultipliers) * sigma);

    half2 gradient = read_imageh(gradientImage, imageCoordinates).xy;
    half angle = atan2(gradient.y, gradient.x);
    half magnitude = length(gradient);
    half edge = smoothstep(4, 16, gradientThreshold * magnitude / sigma.x);

    const int size = gradientBoost > 0 ? 4 : 2;

    // Use high precision accumulator
    float3 filtered_pixel = 0;
    float3 kernel_norm = 0;
    for (int y = -size; y <= size; y++) {
        for (int x = -size; x <= size; x++) {
            half3 inputSampleYCC = read_imageh(inputImage, imageCoordinates + (int2){x, y}).xyz;
            half2 gradientSample = read_imageh(gradientImage, imageCoordinates + (int2){x, y}).xy;

            half3 inputDiff = (inputSampleYCC - inputYCC) * diffMultiplier;
            half2 gradientDiff = (gradientSample - gradient) / sigma.x;

            half directionWeight = mix(1, tunnel(x, y, angle, (half) 0.25), edge);
            half gradientWeight = 1 - smoothstep(2, 8, length(gradientDiff));

            half lumaWeight = 1 - step(1 + (half) gradientBoost * edge, abs(inputDiff.x));
            half chromaWeight = 1 - step((half) chromaBoost, length(inputDiff));

            half3 sampleWeight = (half3) {directionWeight * gradientWeight * lumaWeight, chromaWeight, chromaWeight};

            filtered_pixel += float3(sampleWeight * inputSampleYCC);
            kernel_norm += float3(sampleWeight);
        }
    }
    half3 denoisedPixel = half3(filtered_pixel / kernel_norm);

    write_imageh(denoisedImage, imageCoordinates, half4(denoisedPixel, magnitude));
}

struct _half8 {
    union {
        struct {
            half4 hi;
            half4 lo;
        };
        array<half, 8> v;
    };

    _half8(half val) : hi(val), lo(val) { }

    _half8(uint4 uintVal) : hi((half4) uintVal.xy), lo((half4) uintVal.zw) { }

    _half8(half4 _hi, half4 _lo) : hi(_hi), lo(_lo) { }

    _half8 operator - (_half8 other) {
        return _half8(this->hi - other.hi, this->lo - other.lo);
    }

    operator uint4() const { return uint4(uint2(hi), uint2(lo)); }
};

half length(_half8 x) {
    return sqrt(dot(x.hi, x.hi) + dot(x.lo, x.lo));
}

half length(_half8 x, int components) {
    float sum = 0;
    for (int i = 0; i < components; i++) {
        sum += x.v[i] * x.v[i];
    }
    return sqrt(sum);
}

kernel void collectPatches(texture2d<half> inputImage         [[texture(0)]],
                            device array<float, 25>* patches  [[buffer(1)]],
                            uint2 index                       [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;

    const int x = imageCoordinates.x;
    const int y = imageCoordinates.y;
    const int width = inputImage.get_width();

    for (int j = -2; j <= 2; j++) {
        for (int i = -2; i <= 2; i++) {
            const half inputLuma = read_imageh(inputImage, 8 * imageCoordinates + int2(i, j)).x;

            const int patch_index = y * width / 8 + x;
            patches[patch_index][(j + 2) * 5 + (i + 2)] = inputLuma;
        }
    }
}

kernel void pcaProjection(texture2d<half> inputImage                        [[texture(0)]],
                            constant array<array<half, 8>, 25>* pcaSpace    [[buffer(1)]],
                            texture2d<uint, access::write> projectedImage   [[texture(2)]],
                            uint2 index                                     [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;

    _half8 v_result(0);
    thread array<half, 8>* result = (thread array<half, 8>*) &v_result;

    int row = 0;
    for (int j = -2; j <= 2; j++) {
        for (int i = -2; i <= 2; i++) {
            const half val = read_imageh(inputImage, imageCoordinates + int2(i, j)).x;

            for (int c = 0; c < 8; c++) {
                (*result)[c] += (*pcaSpace)[row][c] * val;
            }
            row++;
        }
    }

    write_imageui(projectedImage, imageCoordinates, uint4(v_result));
}

kernel void blockMatchingDenoiseImage(texture2d<half> inputImage                     [[texture(0)]],
                                      texture2d<half> gradientImage                  [[texture(1)]],
                                      texture2d<uint> pcaImage                       [[texture(2)]],
                                      constant float3& var_a                         [[buffer(3)]],
                                      constant float3& var_b                         [[buffer(4)]],
                                      constant float3& thresholdMultipliers          [[buffer(5)]],
                                      constant float& chromaBoost                    [[buffer(6)]],
                                      constant float& gradientBoost                  [[buffer(7)]],
                                      texture2d<half, access::write> denoisedImage   [[texture(8)]],
                                      uint2 index                                    [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;

    const half3 inputYCC = read_imageh(inputImage, imageCoordinates).xyz;
    const _half8 inputPCA = _half8(read_imageui(pcaImage, imageCoordinates));

//    float2 imageCenter = float2(get_image_dim(inputImage) / 2);
//    float distance_from_center = length(float2(imageCoordinates) - imageCenter) / length(imageCenter);

    /* FIXME: add lensShadingCorrection */
    half3 sigma = half3(sqrt(var_a + var_b * inputYCC.x));
    half3 diffMultiplier = 1 / (half3(thresholdMultipliers) * sigma);

    half2 gradient = read_imageh(gradientImage, imageCoordinates).xy;
    half magnitude = length(gradient);
    half edge = smoothstep(2, 8, magnitude / sigma.x);

    const int size = 10;

    // Use high precision accumulator
    float3 filtered_pixel = 0;
    float3 kernel_norm = 0;
    for (int y = -size; y <= size; y++) {
        for (int x = -size; x <= size; x++) {
            half3 inputSampleYCC = read_imageh(inputImage, imageCoordinates + int2(x, y)).xyz;
            _half8 samplePCA = _half8(read_imageui(pcaImage, imageCoordinates + int2(x, y)));

            // TODO: is 6 better than 8?
            half pcaDiff = length(samplePCA - inputPCA, 8);

            half2 inputChromaDiff = (inputSampleYCC.yz - inputYCC.yz) * diffMultiplier.yz;

            // half directionWeight = mix(1, tunnel(x, y, angle, (half) 0.25), edge);

            half pcaMultDiff = pcaDiff * diffMultiplier.x;
            // half lumaWeight = 1 - step(1 + (half) gradientBoost * edge, pcaMultDiff);
            half lumaWeight = gaussian(pcaMultDiff / (1 + (half) gradientBoost * edge));

            // half chromaWeight = 1 - step((half) chromaBoost, length(half3(pcaMultDiff, inputChromaDiff)));
            half chromaWeight = gaussian(length(half3(pcaMultDiff, inputChromaDiff)) / (half) chromaBoost);

            half distanceWeight = gaussian(0.1 * length(float2(x, y)));

            half3 sampleWeight = distanceWeight * half3(/*directionWeight * */ lumaWeight, chromaWeight, chromaWeight);

            filtered_pixel += float3(sampleWeight * inputSampleYCC);
            kernel_norm += float3(sampleWeight);
        }
    }
    half3 denoisedPixel = half3(filtered_pixel / kernel_norm);

    write_imageh(denoisedImage, imageCoordinates, half4(denoisedPixel, (half) kernel_norm.x));
}

kernel void downsampleImageXYZ(texture2d<float> inputImage                  [[texture(0)]],
                               texture2d<float, access::write> outputImage  [[texture(1)]],
                               uint2 index                                  [[thread_position_in_grid]]) {
    const int2 output_pos = (int2) index;
    const float2 input_norm = 1.0 / float2(get_image_dim(outputImage));
    const float2 input_pos = (float2(output_pos) + 0.5) * input_norm;

    constexpr sampler linear_sampler(filter::linear);

    // Sub-Pixel Sampling Location
    const float2 s = 0.5 * input_norm;
    float3 outputPixel = read_imagef(inputImage, linear_sampler, input_pos + float2(-s.x, -s.y)).xyz;
    outputPixel +=       read_imagef(inputImage, linear_sampler, input_pos + float2( s.x, -s.y)).xyz;
    outputPixel +=       read_imagef(inputImage, linear_sampler, input_pos + float2(-s.x,  s.y)).xyz;
    outputPixel +=       read_imagef(inputImage, linear_sampler, input_pos + float2( s.x,  s.y)).xyz;
    write_imagef(outputImage, output_pos, float4(0.25 * outputPixel, 0));
}

kernel void downsampleImageXY(texture2d<float> inputImage                    [[texture(0)]],
                              texture2d<float, access::write> outputImage    [[texture(1)]],
                              uint2 index                                    [[thread_position_in_grid]]) {
    const int2 output_pos = (int2) index;
    const float2 input_norm = 1.0 / float2(get_image_dim(outputImage));
    const float2 input_pos = (float2(output_pos) + 0.5) * input_norm;

    constexpr sampler linear_sampler(filter::linear);

    // Sub-Pixel Sampling Location
    const float2 s = 0.5 * input_norm;
    float2 outputPixel = read_imagef(inputImage, linear_sampler, input_pos + float2(-s.x, -s.y)).xy;
    outputPixel +=       read_imagef(inputImage, linear_sampler, input_pos + float2( s.x, -s.y)).xy;
    outputPixel +=       read_imagef(inputImage, linear_sampler, input_pos + float2(-s.x,  s.y)).xy;
    outputPixel +=       read_imagef(inputImage, linear_sampler, input_pos + float2( s.x,  s.y)).xy;
    write_imagef(outputImage, output_pos, float4(0.25 * outputPixel, 0, 0));
}

kernel void subtractNoiseImage(texture2d<float> inputImage                      [[texture(0)]],
                               texture2d<float> inputImage1                     [[texture(1)]],
                               texture2d<float> inputImageDenoised1             [[texture(2)]],
                               texture2d<float> gradientImage                   [[texture(3)]],
                               constant float& luma_weight                      [[buffer(4)]],
                               constant float& sharpening                       [[buffer(5)]],
                               constant float2& nlf                             [[buffer(6)]],
                               texture2d<float, access::write> outputImage      [[texture(7)]],
                               uint2 index                                      [[thread_position_in_grid]]) {
    const int2 output_pos = (int2) index;
    const float2 inputNorm = 1.0 / float2(get_image_dim(outputImage));
    const float2 input_pos = (float2(output_pos) + 0.5) * inputNorm;

    constexpr sampler linear_sampler(filter::linear);

    float4 inputPixel = read_imagef(inputImage, output_pos);

    float3 inputPixel1 = read_imagef(inputImage1, linear_sampler, input_pos).xyz;
    float3 inputPixelDenoised1 = read_imagef(inputImageDenoised1, linear_sampler, input_pos).xyz;

    float3 denoisedPixel = inputPixel.xyz - float3(luma_weight, 1, 1) * (inputPixel1 - inputPixelDenoised1);

    float alpha = sharpening;
    if (alpha > 1.0) {
        float2 gradient = read_imagef(gradientImage, output_pos).xy;
        float sigma = sqrt(nlf.x + nlf.y * inputPixelDenoised1.x);
        float detail = smoothstep(sigma, 4 * sigma, length(gradient))
                       * (1.0 - smoothstep(0.95, 1.0, denoisedPixel.x))          // Highlights ringing protection
                       * (0.6 + 0.4 * smoothstep(0.0, 0.1, denoisedPixel.x));    // Shadows ringing protection
        alpha = 1 + (alpha - 1) * detail;
    }

    // Sharpen all components
    denoisedPixel = mix(inputPixelDenoised1, denoisedPixel, alpha);
    denoisedPixel.x = max(denoisedPixel.x, 0.0);

    write_imagef(outputImage, output_pos, float4(denoisedPixel, inputPixel.w));
}

kernel void bayerToRawRGBA(texture2d<float> rawImage                    [[texture(0)]],
                           texture2d<float, access::write> rgbaImage    [[texture(1)]],
                           constant int& bayerPattern                   [[buffer(2)]],
                           uint2 index                                  [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;

    const int2 r = bayerOffsets[bayerPattern][raw_red];
    const int2 g = bayerOffsets[bayerPattern][raw_green];
    const int2 b = bayerOffsets[bayerPattern][raw_blue];
    const int2 g2 = bayerOffsets[bayerPattern][raw_green2];

    float red    = read_imagef(rawImage, 2 * imageCoordinates + r).x;
    float green  = read_imagef(rawImage, 2 * imageCoordinates + g).x;
    float blue   = read_imagef(rawImage, 2 * imageCoordinates + b).x;
    float green2 = read_imagef(rawImage, 2 * imageCoordinates + g2).x;

    write_imagef(rgbaImage, imageCoordinates, float4(red, green, blue, green2));
}

kernel void rawRGBAToBayer(texture2d<float> rgbaImage                   [[texture(0)]],
                           texture2d<float, access::write> rawImage     [[texture(1)]],
                           constant int& bayerPattern                   [[buffer(2)]],
                           uint2 index                                  [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;

    float4 rgba = read_imagef(rgbaImage, imageCoordinates);

    const int2 r = bayerOffsets[bayerPattern][raw_red];
    const int2 g = bayerOffsets[bayerPattern][raw_green];
    const int2 b = bayerOffsets[bayerPattern][raw_blue];
    const int2 g2 = bayerOffsets[bayerPattern][raw_green2];

    write_imagef(rawImage, 2 * imageCoordinates + r, rgba.x);
    write_imagef(rawImage, 2 * imageCoordinates + g, rgba.y);
    write_imagef(rawImage, 2 * imageCoordinates + b, rgba.z);
    write_imagef(rawImage, 2 * imageCoordinates + g2, rgba.w);
}

kernel void crossDenoiseRawRGBAImage(texture2d<half> inputImage                    [[texture(0)]],
                                     constant half4& rawVariance                   [[buffer(1)]],
                                     constant half& strength                       [[buffer(2)]],
                                     texture2d<half, access::write> denoisedImage  [[texture(3)]],
                                     uint2 index                                   [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;

    half4 inputPixel = read_imageh(inputImage, imageCoordinates);
    half4 inv_sigma = 1 / (strength * sqrt(rawVariance * (inputPixel + 0.0001)));

    float4 sum = 0;
    float4 sumW = 0;
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            half4 samplePixel = read_imageh(inputImage, imageCoordinates + int2(x, y));

            half4 w = gaussian(length((inputPixel - samplePixel) * inv_sigma));

            sum += float4(w * samplePixel);
            sumW += float4(w);
        }
    }

    write_imageh(denoisedImage, imageCoordinates, half4(sum / sumW));
}

half4 despeckle_3x3x4(texture2d<half> inputImage, half4 rawVariance, half gradient, int2 imageCoordinates) {
    half4 sample = 0, firstMax = 0, secondMax = 0;
    half4 firstMin = (half) HALF_MAX, secondMin = (half) HALF_MAX;

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            half4 v = read_imageh(inputImage, imageCoordinates + (int2){x, y});

            secondMax = select(max(v, secondMax), firstMax, v >= firstMax);
            firstMax = max(v, firstMax);

            secondMin = select(min(v, secondMin), firstMin, v <= firstMin);
            firstMin = min(v, firstMin);

            if (x == 0 && y == 0) {
                sample = v;
            }
        }
    }

    half4 sigma = sqrt(rawVariance * sample);
    half4 texture = 1 - 0.5 * smoothstep(1, 2, gradient / sigma);
    half4 minVal = mix(firstMin, secondMin, texture * (1 - smoothstep(2 * sigma, 8 * sigma, secondMin - firstMin)));
    half4 maxVal = mix(firstMax, secondMax, texture * (1 - smoothstep(sigma, 4 * sigma, firstMax - secondMax)));
    return clamp(sample, minVal, maxVal);
}

half4 despeckle_3x3x4_strong(texture2d<half> inputImage, half4 rawVariance, half gradient, int2 imageCoordinates) {
    half4 sample = 0, firstMax = 0, secondMax = 0, thirdMax = 0;
    half4 firstMin = (half) HALF_MAX, secondMin = (half) HALF_MAX, thirdMin = (half) HALF_MAX;

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            half4 v = read_imageh(inputImage, imageCoordinates + (int2){x, y});

            thirdMax = select(max(v, thirdMax), secondMax, v >= secondMax);
            secondMax = select(max(v, secondMax), firstMax, v >= firstMax);
            firstMax = max(v, firstMax);

            thirdMin = select(min(v, thirdMin), secondMin, v <= secondMin);
            secondMin = select(min(v, secondMin), firstMin, v <= firstMin);
            firstMin = min(v, firstMin);

            if (x == 0 && y == 0) {
                sample = v;
            }
        }
    }

    half4 sigma = sqrt(rawVariance * sample);
    half4 texture = smoothstep(1, 4, gradient / sigma);
    half4 minVal = mix(thirdMin, firstMin, texture * smoothstep(2 * sigma, 8 * sigma, secondMin - firstMin));
    half4 maxVal = mix(thirdMax, firstMax, texture * smoothstep(sigma, 4 * sigma, firstMax - secondMax));
    return clamp(sample, minVal, maxVal);
}

kernel void despeckleRawRGBAImage(texture2d<half> inputImage                    [[texture(0)]],
                                  texture2d<half> gradientImage                 [[texture(1)]],
                                  constant float4& rawVariance                  [[buffer(2)]],
                                  texture2d<half, access::write> denoisedImage  [[texture(3)]],
                                  uint2 index                                   [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;

    const half gradient = length(read_imageh(gradientImage, 2 * imageCoordinates).xy);
    half4 despeckledPixel = despeckle_3x3x4(inputImage, half4(rawVariance), gradient, imageCoordinates);

    write_imageh(denoisedImage, imageCoordinates, despeckledPixel);
}

float4 readRAWQuad(texture2d<float> rawImage,
                   int2 imageCoordinates,
                   constant const int2 *offsets) {
    const int2 r = offsets[raw_red];
    const int2 g = offsets[raw_green];
    const int2 b = offsets[raw_blue];
    const int2 g2 = offsets[raw_green2];

    float red    = read_imagef(rawImage, imageCoordinates + r).x;
    float green  = read_imagef(rawImage, imageCoordinates + g).x;
    float blue   = read_imagef(rawImage, imageCoordinates + b).x;
    float green2 = read_imagef(rawImage, imageCoordinates + g2).x;

    return float4(red, green, blue, green2);
}

kernel void basicRawNoiseStatistics(texture2d<float> rawImage                   [[texture(0)]],
                                    constant int& bayerPattern                  [[buffer(1)]],
                                    texture2d<float, access::write> meanImage   [[texture(2)]],
                                    texture2d<float, access::write> varImage    [[texture(3)]],
                                    uint2 index                                 [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;

    constant const int2* offsets = bayerOffsets[bayerPattern];

    int radius = 4;
    const float count = (2 * radius + 1) * (2 * radius + 1);

    float4 sum = 0;
    float4 sumSq = 0;
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            float4 inputSample = readRAWQuad(rawImage, 2 * imageCoordinates + int2(x, y), offsets);
            sum += inputSample;
            sumSq += inputSample * inputSample;
        }
    }
    float4 mean = sum / count;
    float4 var = (sumSq - (sum * sum) / count) / count;

    write_imagef(meanImage, imageCoordinates, mean);
    write_imagef(varImage, imageCoordinates, var);
}

kernel void basicNoiseStatistics(texture2d<float> inputImage                        [[texture(0)]],
                                 texture2d<float, access::write> statisticsImage    [[texture(1)]],
                                 uint2 index                                        [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;

    int radius = 2;
    int count = (2 * radius + 1) * (2 * radius + 1);

    float3 sum = 0;
    float3 sumSq = 0;
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            float3 inputSample = read_imagef(inputImage, imageCoordinates + int2(x, y)).xyz;
            sum += inputSample;
            sumSq += inputSample * inputSample;
        }
    }
    float3 mean = sum / count;
    float3 var = (sumSq - (sum * sum) / count) / count;

    write_imagef(statisticsImage, imageCoordinates, float4(mean.x, var));
}

/// ---- Median Filter 3x3 ----

#define s(a, b)                         \
  ({ typedef __typeof__ (a) type_of_a;  \
     type_of_a temp = a;                \
     a = min(a, b);                     \
     b = max(temp, b); })

#define minMax6(a0,a1,a2,a3,a4,a5) s(a0,a1);s(a2,a3);s(a4,a5);s(a0,a2);s(a1,a3);s(a0,a4);s(a3,a5);
#define minMax5(a0,a1,a2,a3,a4) s(a0,a1);s(a2,a3);s(a0,a2);s(a1,a3);s(a0,a4);s(a3,a4);
#define minMax4(a0,a1,a2,a3) s(a0,a1);s(a2,a3);s(a0,a2);s(a1,a3);
#define minMax3(a0,a1,a2) s(a0,a1);s(a0,a2);s(a1,a2);

#define fast_median3x3(inputImage, imageCoordinates)               \
({                                                                 \
    medianPixelType a0, a1, a2, a3, a4, a5;                        \
                                                                   \
    a0 = readImage(inputImage, imageCoordinates + int2(0, -1));    \
    a1 = readImage(inputImage, imageCoordinates + int2(1, -1));    \
    a2 = readImage(inputImage, imageCoordinates + int2(0, 0));     \
    a3 = readImage(inputImage, imageCoordinates + int2(1, 0));     \
    a4 = readImage(inputImage, imageCoordinates + int2(0, 1));     \
    a5 = readImage(inputImage, imageCoordinates + int2(1, 1));     \
    minMax6(a0, a1, a2, a3, a4, a5);                               \
    a0 = readImage(inputImage, imageCoordinates + int2(-1, 1));    \
    minMax5(a0, a1, a2, a3, a4);                                   \
    a0 = readImage(inputImage, imageCoordinates + int2(-1, 0));    \
    minMax4(a0, a1, a2, a3);                                       \
    a0 = readImage(inputImage, imageCoordinates + int2(-1, -1));   \
    minMax3(a0, a1, a2);                                           \
    a1;                                                            \
})

#define readImage(image, pos)  read_imageh(image, pos).xy;

kernel void medianFilterImage3x3x2(texture2d<half> inputImage                   [[texture(0)]],
                                   texture2d<half, access::write> denoisedImage [[texture(1)]],
                                   uint2 index                                  [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;

    typedef half2 medianPixelType;

    half2 median = fast_median3x3(inputImage, imageCoordinates);

    write_imageh(denoisedImage, imageCoordinates, half4(median, 0, 0));
}

#undef readImage

#define readImage(image, pos)  read_imageh(image, pos).xyz;

kernel void medianFilterImage3x3x3(texture2d<half> inputImage                   [[texture(0)]],
                                   texture2d<half, access::write> filteredImage [[texture(1)]],
                                   uint2 index                                  [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;

    typedef half3 medianPixelType;

    half3 median = fast_median3x3(inputImage, imageCoordinates);

    write_imageh(filteredImage, imageCoordinates, half4(median, 0));
}

#undef readImage

#define readImage(image, pos)  read_imageh(image, pos);

kernel void medianFilterImage3x3x4(texture2d<half> inputImage                   [[texture(0)]],
                                   texture2d<half, access::write> filteredImage [[texture(1)]],
                                   uint2 index                                  [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;

    typedef half4 medianPixelType;

    half4 median = fast_median3x3(inputImage, imageCoordinates);

    write_imageh(filteredImage, imageCoordinates, median);
}

#undef readImage

#define readImage(image, pos)                              \
    ({                                                     \
        half3 p = read_imageh(image, pos).xyz;             \
                                                           \
        half v = p.x;                                      \
        secMax = v <= firstMax && v > secMax ? v : secMax; \
        secMax = v > firstMax ? firstMax : secMax;         \
        firstMax = v > firstMax ? v : firstMax;            \
                                                           \
        secMin = v >= firstMin && v < secMin ? v : secMin; \
        secMin = v < firstMin ? firstMin : secMin;         \
        firstMin = v < firstMin ? v : firstMin;            \
                                                           \
        if (all(pos == imageCoordinates)) {                \
            sample = v;                                    \
        }                                                  \
                                                           \
        p.yz;                                              \
    })

kernel void despeckleLumaMedianChromaImage(texture2d<half> inputImage                   [[texture(0)]],
                                           constant float3& var_a                       [[buffer(1)]],
                                           constant float3& var_b                       [[buffer(2)]],
                                           texture2d<half, access::write> denoisedImage [[texture(3)]],
                                           uint2 index                                  [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;

    half sample = 0, firstMax = 0, secMax = 0;
    half firstMin = (float) 0xffff, secMin = (float) 0xffff;

    // Median filtering of the chroma
    typedef half2 medianPixelType;
    half2 median = fast_median3x3(inputImage, imageCoordinates);

    half sigma = sqrt(var_a.x + var_b.x * sample);
    half minVal = mix(secMin, firstMin, smoothstep(sigma, 4 * sigma, secMin - firstMin));
    half maxVal = mix(secMax, firstMax, smoothstep(sigma, 4 * sigma, firstMax - secMax));

    sample = clamp(sample, minVal, maxVal);

    write_imageh(denoisedImage, imageCoordinates, half4(sample, median, 0));
}

#undef readImage

#undef minMax6
#undef minMax5
#undef minMax4
#undef minMax3
#undef s

// Local Tone Mapping - guideImage can be a downsampled version of inputImage

kernel void GuidedFilterABImage(texture2d<float> guideImage                 [[texture(0)]],
                                texture2d<float, access::write> abImage     [[texture(1)]],
                                constant float& eps                         [[buffer(2)]],
                                uint2 index                                 [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;
    const float2 inputNorm = 1.0 / float2(get_image_dim(guideImage));
    const float2 pos = float2(imageCoordinates) * inputNorm;

    constexpr sampler linear_sampler(filter::linear);

    float sum = 0;
    float sumSq = 0;
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            float sample = read_imagef(guideImage, linear_sampler, pos + (float2(x, y) + 0.5f) * inputNorm).x;
            sum += sample;
            sumSq += sample * sample;
        }
    }
    float mean = sum / 25;
    float var = (sumSq - sum * sum / 25) / 25;

    float a = var / (var + eps);
    float b = mean * (1 - a);

    write_imagef(abImage, imageCoordinates, float4(a, b, 0, 0));
}

// Fast 5x5 box filtering with linear subsampling
typedef struct ConvolutionParameters {
    float weight;
    float2 offset;
} ConvolutionParameters;

constant ConvolutionParameters boxFilter5x5[9] = {
    { 0.1600, { -1.5000, -1.5000 } },
    { 0.1600, {  0.5000, -1.5000 } },
    { 0.0800, {  2.0000, -1.5000 } },
    { 0.1600, { -1.5000,  0.5000 } },
    { 0.1600, {  0.5000,  0.5000 } },
    { 0.0800, {  2.0000,  0.5000 } },
    { 0.0800, { -1.5000,  2.0000 } },
    { 0.0800, {  0.5000,  2.0000 } },
    { 0.0400, {  2.0000,  2.0000 } },
};

kernel void BoxFilterGFImage(texture2d<float> inputImage                    [[texture(0)]],
                             texture2d<float, access::write> outputImage    [[texture(1)]],
                             uint2 index                                    [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;
    const float2 inputNorm = 1.0 / float2(get_image_dim(inputImage));
    const float2 pos = float2(imageCoordinates) * inputNorm;

    constexpr sampler linear_sampler(filter::linear);
    float2 meanAB = 0;
    for (int i = 0; i < 9; i++) {
        constant ConvolutionParameters* cp = &boxFilter5x5[i];
        meanAB += cp->weight * read_imagef(inputImage, linear_sampler, pos + (cp->offset + 0.5f) * inputNorm).xy;
    }

    write_imagef(outputImage, imageCoordinates, float4(meanAB, 0, 0));
}

constant constexpr float kLtmMidtones = 0.22;

float computeLtmMultiplier(float3 input, float2 gfAb, float eps, float shadows, float highlights, float detail) {
    // The filtered image is an estimate of the illuminance
    const float illuminance = gfAb.x * input.x + gfAb.y;
    const float reflectance = input.x / illuminance;

    // LTM curve computed in Log space
    float adjusted_illuminance =
        // Shadows adjustment
        illuminance <= kLtmMidtones ? kLtmMidtones * pow(illuminance / kLtmMidtones, shadows) :
        // Highlights adjustment
        (1 - kLtmMidtones) * pow((illuminance - kLtmMidtones) / (1 - kLtmMidtones), highlights) + kLtmMidtones;

    return adjusted_illuminance * pow(reflectance, detail) / input.x;
}

typedef struct LTMParameters {
    float eps;
    float shadows;
    float highlights;
    float detail[3];
} LTMParameters;

constant int histogramSize = 0x100;

struct histogram_data {
    array<atomic<uint32_t>, histogramSize> histogram;
    array<uint32_t, 8> bands;
    float black_level;
    float white_level;
    float shadows;
    float highlights;
    float mean;
    float median;
};

kernel void localToneMappingMaskImage(texture2d<float> inputImage                   [[texture(0)]],
                                      texture2d<float> lfAbImage                    [[texture(1)]],
                                      texture2d<float> mfAbImage                    [[texture(2)]],
                                      texture2d<float> hfAbImage                    [[texture(3)]],
                                      texture2d<float, access::write> ltmMaskImage  [[texture(4)]],
                                      constant LTMParameters& ltmParameters         [[buffer(5)]],
                                      constant float2& nlf                          [[buffer(6)]],
                                      constant histogram_data& histogram_data       [[buffer(7)]],
                                      uint2 index                                   [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;
    const float2 inputNorm = 1.0 / float2(get_image_dim(ltmMaskImage));
    const float2 pos = float2(imageCoordinates) * inputNorm;

    constexpr sampler linear_sampler(filter::linear);

    float3 input = read_imagef(inputImage, imageCoordinates).xyz;
    float2 lfAbSample = read_imagef(lfAbImage, linear_sampler, pos + 0.5f * inputNorm).xy;

    float ltmMultiplier = computeLtmMultiplier(input, lfAbSample, ltmParameters.eps,
                                               histogram_data.shadows, histogram_data.highlights,
                                               ltmParameters.detail[0]);

    if (ltmParameters.detail[1] != 1) {
        float2 mfAbSample = read_imagef(mfAbImage, linear_sampler, pos + 0.5f * inputNorm).xy;
        ltmMultiplier *= computeLtmMultiplier(input, mfAbSample, ltmParameters.eps,
                                              histogram_data.shadows, 1, ltmParameters.detail[1]);
    }

    if (ltmParameters.detail[2] != 1) {
        float detail = ltmParameters.detail[2];

        if (detail > 1.0) {
            float dx = (read_imagef(inputImage, imageCoordinates + int2(1, 0)).x -
                        read_imagef(inputImage, imageCoordinates - int2(1, 0)).x) / 2;
            float dy = (read_imagef(inputImage, imageCoordinates + int2(0, 1)).x -
                        read_imagef(inputImage, imageCoordinates - int2(0, 1)).x) / 2;

            float noiseThreshold = sqrt(nlf.x + nlf.y * input.x);
            detail = 1 + (detail - 1) * smoothstep(0.5 * noiseThreshold, 2 * noiseThreshold, length(float2(dx, dy)));
        }

        float2 hfAbSample = read_imagef(hfAbImage, linear_sampler, pos + 0.5f * inputNorm).xy;
        ltmMultiplier *= computeLtmMultiplier(input, hfAbSample, ltmParameters.eps,
                                              histogram_data.shadows, 1, detail);
    }

    write_imagef(ltmMaskImage, imageCoordinates, float4(ltmMultiplier, 0, 0, 0));
}

constant constexpr float kHistogramScale = 2;

kernel void histogramImage(texture2d<float> inputImage          [[texture(0)]],
                           device histogram_data& histogram_data [[buffer(1)]],
                           uint2 index                          [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;

    float3 pixelValue = read_imagef(inputImage, imageCoordinates).xyz;

    int histogram_index = clamp((int) ((histogramSize - 1) * sqrt(pixelValue.x / kHistogramScale)), 0, (histogramSize - 1));

    atomic_fetch_add_explicit(&histogram_data.histogram[histogram_index], 1, memory_order_relaxed);
}

kernel void histogramStatistics(device histogram_data& histogram_data [[buffer(0)]],
                                constant uint2& image_dimensions      [[buffer(1)]],
                                uint2 index                           [[thread_position_in_grid]]) {
    if (index.x == 0 && index.y == 0) {
        device array<uint32_t, histogramSize>* plain_histogram = (device array<uint32_t, histogramSize>*) &histogram_data.histogram;

        const uint32_t image_size = image_dimensions.x * image_dimensions.y;

        float mean = 0;
        bool found_median = false;
        bool found_black = false;
        bool found_white = false;
        int median_index = 0;
        int black_index = 0;
        int white_index = 0;
        uint32_t sum = 0;
        for (int i = 0; i < histogramSize; i++) {
            const uint32_t entry = (*plain_histogram)[i];

            // Compute subsampled (8 bands) histogram
            histogram_data.bands[i / 32] += entry;

            // Compute average image value
            mean += entry * (i + 1) / (float) histogramSize;

            // Compute cumulative function statistics
            sum += entry;
            if (!found_median && (sum >= (image_size + 1) / 2)) {
                median_index = i;
                found_median = true;
            }
            if (!found_black && sum >= 0.001 * image_size) {
                black_index = i;
                found_black = true;
            }
            if (!found_white && sum >= 0.999 * image_size) {
                white_index = i;
                found_white = true;
            }
        }
        mean /= image_size;

        {
            float v = (black_index - 1) / (float) (histogramSize - 1);
            histogram_data.black_level = kHistogramScale * v * v;
        }
        {
            float v = white_index / (float) (histogramSize - 1);
            histogram_data.white_level = kHistogramScale * v * v;
        }
        histogram_data.median = median_index / (float) (histogramSize - 1) - histogram_data.black_level;
        histogram_data.mean = mean - histogram_data.black_level;

//        histogram_data.highlights = 1;
//        histogram_data.shadows = 1;
        histogram_data.highlights = 1 + 2 * smoothstep(0.01, 0.1, (histogram_data.bands[5] +
                                                                   histogram_data.bands[6] +
                                                                   histogram_data.bands[7]) / (float) image_size);

        histogram_data.shadows = 0.8 - 0.5 * smoothstep(0.25, 0.5,
                                                        (histogram_data.bands[0] +
                                                         histogram_data.bands[1]) / (float) image_size);
    }
}

kernel void convertTosRGB(texture2d<float> linearImage                  [[texture(0)]],
                          texture2d<float> ltmMaskImage                 [[texture(1)]],
                          texture2d<float, access::write> rgbImage      [[texture(2)]],
                          constant Matrix3x3& transform                 [[buffer(3)]],
                          constant RGBConversionParameters& parameters  [[buffer(4)]],
                          constant histogram_data& histogram_data       [[buffer(5)]],
                          uint2 index                                   [[thread_position_in_grid]])
{
    const int2 imageCoordinates = (int2) index;

    float black_level = histogram_data.black_level;
    float white_level = 1; // - 0.25 * (1 - smoothstep(0.375, 0.625, histogram_data.white_level));

    float brightening = 1; // + 0.5 * smoothstep(0, 0.1, histogram_data.mean - histogram_data.median);
    if (histogram_data.mean > 0.22) {
        brightening *= 0.22 / histogram_data.mean;
    }

    // FIXME: Compute the black and white levels dynamically
    float3 pixel_value = brightening * max(read_imagef(linearImage, imageCoordinates).xyz - black_level, 0) / (white_level - black_level);

    // Exposure Bias
    pixel_value *= parameters.exposureBias != 0 ? powr(2.0, parameters.exposureBias) : 1;

    // Saturation
    pixel_value = parameters.saturation != 1.0 ? saturationBoost(pixel_value, parameters.saturation) : pixel_value;

    // Contrast
    pixel_value = parameters.contrast != 1.0 ? contrastBoost(pixel_value, parameters.contrast) : pixel_value;

    // Conversion to target color space, ensure definite positiveness
    float3 rgb = max(float3(
        dot(transform.m[0], pixel_value),
        dot(transform.m[1], pixel_value),
        dot(transform.m[2], pixel_value)
    ), 0);

    // Local Tone Mapping
    if (parameters.localToneMapping) {
        float ltmBoost = read_imagef(ltmMaskImage, imageCoordinates).x;

        if (ltmBoost > 1) {
            // Modified Naik and Murthy’s method for preserving hue/saturation under luminance changes
            const float luma = 0.2126 * rgb.x + 0.7152 * rgb.y + 0.0722 * rgb.z; // BT.709-2 (sRGB) luma primaries
            // rgb = mix(1 - (1.0 - rgb) * (1 - ltmBoost * luma) / (1 - luma), rgb * ltmBoost, smoothstep(0.125, 0.25, luma));
            rgb = mix(rgb * ltmBoost,
                      mix(1 - (1.0 - rgb) * (1 - ltmBoost * luma) / (1 - luma), rgb * ltmBoost, smoothstep(0.5, 0.8, luma)),
                      min(pow(luma, 0.5), 1.0));
        } else if (ltmBoost < 1) {
            rgb *= ltmBoost;
        }
    }

    // Tone Curve
    rgb = toneCurve(max(rgb, 0), parameters.toneCurveSlope);

    // Black Level Adjustment
    if (parameters.blacks > 0) {
        rgb = (rgb - parameters.blacks) / (1 - parameters.blacks);
    }

    write_imagef(rgbImage, imageCoordinates, float4(clamp(rgb, 0.0, 1.0), 1.0));
}

static inline float2 s_curve(float2 t) {
    return t * t * (3. - 2. * t);
}

static constant int B = 0x100;

float noise(float2 pos, constant array<int, B + B + 2>& p, constant array<float2, B + B + 2>& g2) {
    // setup
    const int BM = 0xff;
    const int N = 0x1000;

    float2 t = pos + N;
    int2 b0 = int2(t) & BM;
    int2 b1 = (b0 + 1) & BM;
    float2 r0 = fract(t);
    float2 r1 = r0 - 1;

    int i = p[b0.x];
    int j = p[b1.x];

    int b00 = p[i + b0.y];
    int b10 = p[j + b0.y];
    int b01 = p[i + b1.y];
    int b11 = p[j + b1.y];

    float2 s = s_curve(r0);

    float u = dot(g2[b00], r0);
    float v = dot(g2[b10], float2(r1.x, r0.y));
    float a = mix(u, v, s.x);

    u = dot(g2[b01], float2(r0.x, r1.y));
    v = dot(g2[b11], r1);
    float b = mix(u, v, s.x);

    return mix(a, b, s.y);
}

float octaveNoise(float2 pos, int octaves, float persistence, float lacunarity,
                  constant array<int, B + B + 2>& p, constant array<float2, B + B + 2>& g2) {
    float freq = 1.0f;
    float amp = 1.0f;
    float norm = 1.0f;
    float sum = noise(pos, p, g2);
    int i;

    for (i = 1; i < octaves; ++i) {
        freq *= lacunarity;
        amp *= persistence;
        norm += amp;
        sum += noise(pos * freq, p, g2) * amp;
    }
    return sum / norm;
}

kernel void simplex_noise(texture2d<float> inputImage                   [[texture(0)]],
                          constant array<int, B + B + 2>& p             [[buffer(1)]],
                          constant array<float2, B + B + 2>& g2         [[buffer(2)]],
                          constant uint& seed                           [[buffer(3)]],
                          constant float& sigma                         [[buffer(4)]],
                          texture2d<float, access::write> outputImage   [[texture(5)]],
                          uint2 index                                   [[thread_position_in_grid]])
{
    const int2 imageCoordinates = (int2) index;

    float noise = octaveNoise(0.5 * (float2(imageCoordinates) + M_PI_F), 4, 0.5, 0.5, p, g2);

    float4 pixel = read_imagef(inputImage, imageCoordinates);
    pixel.x += 4 * sigma * noise;
    write_imagef(outputImage, imageCoordinates, pixel);
}
