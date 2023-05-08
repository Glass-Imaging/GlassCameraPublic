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

    int p[B + B + 2];
    float g2[B + B + 2][2];

    static inline void setup(int* b0, int* b1, float* r0, float* r1, float val) {
        const int BM = 0xff;
        const int N = 0x1000;

        float t = val + N;
        *b0 = ((int)t) & BM;
        *b1 = (*b0 + 1) & BM;
        *r0 = t - (int)t;
        *r1 = *r0 - 1;
    }

    static inline float at2(float q[2], float rx, float ry) {
        return rx * q[0] + ry * q[1];
    }

    static inline void normalize2(float v[2]) {
        float s;
        s = sqrt(v[0] * v[0] + v[1] * v[1]);
        v[0] = v[0] / s;
        v[1] = v[1] / s;
    }

public:
    float noise(float x, float y) {
        int bx0, bx1, by0, by1;
        float rx0, rx1, ry0, ry1;

        setup(&bx0, &bx1, &rx0, &rx1, x);
        setup(&by0, &by1, &ry0, &ry1, y);

        int i = p[bx0];
        int j = p[bx1];

        int b00 = p[i + by0];
        int b10 = p[j + by0];
        int b01 = p[i + by1];
        int b11 = p[j + by1];

        float sx = s_curve(rx0);
        float sy = s_curve(ry0);

        float u = at2(g2[b00], rx0, ry0);
        float v = at2(g2[b10], rx1, ry0);
        float a = mix(u, v, sx);

        u = at2(g2[b01], rx0, ry1);
        v = at2(g2[b11], rx1, ry1);
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

    Noise2D() {
        for (int i = 0; i < B; i++) {
            p[i] = i;
            for (int j = 0; j < 2; j++) {
                g2[i][j] = (float)((random() % (B + B)) - B) / B;
            }
            normalize2(g2[i]);
        }
        for (int i = B - 1; i > 0; i--) {
            int k = p[i];
            int j = random() % B;
            p[i] = p[j];
            p[j] = k;
        }
        for (int i = 0; i < B + 2; i++) {
            p[B + i] = p[i];
            for (int j = 0; j < 2; j++) {
                g2[B + i][j] = g2[i][j];
            }
        }
    }
};

#endif /* SimplexNoise_hpp */
