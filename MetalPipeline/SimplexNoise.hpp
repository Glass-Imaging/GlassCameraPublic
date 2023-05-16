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

#ifndef SimplexNoise_hpp
#define SimplexNoise_hpp

/* 2D coherent noise function - (some of this code copyright Ken Perlin) */

#include <math.h>

static inline float s_curve(float t) {
    return t * t * (3. - 2. * t);
}

static inline float mix(float a, float b, float t) {
    return a + t * (b - a);
}

class Noise2D {
    static constexpr int B = 0x100;

public:
    static constexpr int arraySize = 2 * B + 2;

private:
    std::array<int, arraySize> perm;
    std::array<std::array<float, 2>, arraySize> grad;

    static inline void setup(int* b0, int* b1, float* r0, float* r1, float val) {
        const int BM = 0xff;
        const int N = 0x1000;

        float t = val + N;
        *b0 = ((int)t) & BM;
        *b1 = (*b0 + 1) & BM;
        *r0 = t - (int)t;
        *r1 = *r0 - 1;
    }

    static inline float at2(const std::array<float, 2>& q, const std::array<float, 2>& r) {
        return r[0] * q[0] + r[1] * q[1];
    }

    static inline void normalize(std::array<float, 2>* v) {
        float s;
        s = sqrt((*v)[0] * (*v)[0] + (*v)[1] * (*v)[1]);
        (*v)[0] = (*v)[0] / s;
        (*v)[1] = (*v)[1] / s;
    }

public:
    float noise(float x, float y) {
        int bx0, bx1, by0, by1;
        float rx0, rx1, ry0, ry1;

        setup(&bx0, &bx1, &rx0, &rx1, x);
        setup(&by0, &by1, &ry0, &ry1, y);

        int i = perm[bx0];
        int j = perm[bx1];

        int b00 = perm[i + by0];
        int b10 = perm[j + by0];
        int b01 = perm[i + by1];
        int b11 = perm[j + by1];

        float sx = s_curve(rx0);
        float sy = s_curve(ry0);

        float u = at2(grad[b00], { rx0, ry0 });
        float v = at2(grad[b10], { rx1, ry0 });
        float a = mix(u, v, sx);

        u = at2(grad[b01], { rx0, ry1 });
        v = at2(grad[b11], { rx1, ry1 });
        float b = mix(u, v, sx);

        return mix(a, b, sy);
    }

    float octaveNoise(float x, float y, int octaves, float persistence = 0.5, float lacunarity = 0.5) {
        float freq = 1.0f;
        float amp = 1.0f;
        float max = 1.0f;
        float total = noise(x, y);
        int i;

        for (i = 1; i < octaves; ++i) {
            freq *= lacunarity;
            amp *= persistence;
            max += amp;
            total += noise(x * freq, y * freq) * amp;
        }
        return total / max;
    }

    static void initGradients(std::array<int, arraySize>* perm,
                              std::array<std::array<float, 2>, arraySize>* grad) {
        for (int i = 0; i < B; i++) {
            (*perm)[i] = i;
            for (int j = 0; j < 2; j++) {
                (*grad)[i][j] = (float)((random() % (2 * B)) - B) / B;
            }
            normalize(&(*grad)[i]);
        }
        for (int i = B - 1; i > 0; i--) {
            int k = (*perm)[i];
            int j = random() % B;
            (*perm)[i] = (*perm)[j];
            (*perm)[j] = k;
        }
        for (int i = 0; i < B + 2; i++) {
            (*perm)[B + i] = (*perm)[i];
            for (int j = 0; j < 2; j++) {
                (*grad)[B + i][j] = (*grad)[i][j];
            }
        }
    }

    static void randomSeed(unsigned seed) {
        srandom(seed);
    }

    Noise2D() {
        initGradients(&perm, &grad);
    }

    Noise2D(unsigned seed) {
        randomSeed(seed);
        initGradients(&perm, &grad);
    }
};

#endif /* SimplexNoise_hpp */
