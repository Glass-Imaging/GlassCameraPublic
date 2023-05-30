// Copyright (c) 1999-2008 Insight Software Consortium All rights reserved. Redistribution and use in source and binary
// forms, with or without modification, are permitted provided that the following conditions are met:
//
//    Redistributions of source code must retain the above copyright notice, this list of conditions and the following
//    disclaimer. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
//    the following disclaimer in the documentation and/or other materials provided with the distribution. Neither the
//    name of the Insight Software Consortium nor the names of its contributors may be used to endorse or promote
//    products derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

//=============================================================================

#include <math.h>

#include "EigenvalueComputation.h"
#include "Param.h"

//=============================================================================

#define m_Order M
#define m_Dimension M
#define vnl_math_abs fabs
#define vnl_math_abs fabs
#define vcl_sqrt sqrt
#define vnl_math_sgn0 Sgn0
#define vnl_math_hypot Hypot

//=============================================================================

inline int Sgn0(double x) { return x >= 0.0 ? 1 : -1; }

//=============================================================================

inline double Hypot(double x, double y) { return sqrt(x * x + y * y); }

//=============================================================================

static void ReduceToTridiagonalMatrix(double* a, double* d, double* e) {
    double d__1;

    /* Local variables */
    double f, g, h;
    int i, j, k, l;
    double scale;

    for (i = 0; i < static_cast<int>(m_Order); ++i) {
        d[i] = a[m_Order - 1 + i * m_Dimension];
        a[m_Order - 1 + i * m_Dimension] = a[i + i * m_Dimension];
    }

    for (i = m_Order - 1; i >= 0; --i) {
        l = i - 1;
        h = 0.;
        scale = 0.;

        /*     .......... scale row (algol tol then not needed) .......... */
        for (k = 0; k <= l; ++k) {
            scale += vnl_math_abs(d[k]);
        }
        if (scale == 0.) {
            for (j = 0; j <= l; ++j) {
                d[j] = a[l + j * m_Dimension];
                a[l + j * m_Dimension] = a[i + j * m_Dimension];
                a[i + j * m_Dimension] = 0.;
            }
            e[i] = 0.;
            // e2[i] = 0.;
            continue;
        }
        for (k = 0; k <= l; ++k) {
            d[k] /= scale;
            h += d[k] * d[k];
        }

        // e2[i] = scale * scale * h;
        f = d[l];
        d__1 = vcl_sqrt(h);
        g = (-1.0) * vnl_math_sgn0(f) * vnl_math_abs(d__1);
        e[i] = scale * g;
        h -= f * g;
        d[l] = f - g;
        if (l != 0) {
            /*     .......... form a*u .......... */
            for (j = 0; j <= l; ++j) {
                e[j] = 0.;
            }

            for (j = 0; j <= l; ++j) {
                f = d[j];
                g = e[j] + a[j + j * m_Dimension] * f;

                for (k = j + 1; k <= l; ++k) {
                    g += a[k + j * m_Dimension] * d[k];
                    e[k] += a[k + j * m_Dimension] * f;
                }
                e[j] = g;
            }

            /*     .......... form p .......... */
            f = 0.;

            for (j = 0; j <= l; ++j) {
                e[j] /= h;
                f += e[j] * d[j];
            }

            h = f / (h + h);
            /*     .......... form q .......... */
            for (j = 0; j <= l; ++j) {
                e[j] -= h * d[j];
            }

            /*     .......... form reduced a .......... */
            for (j = 0; j <= l; ++j) {
                f = d[j];
                g = e[j];

                for (k = j; k <= l; ++k) {
                    a[k + j * m_Dimension] = a[k + j * m_Dimension] - f * e[k] - g * d[k];
                }
            }
        }

        for (j = 0; j <= l; ++j) {
            f = d[j];
            d[j] = a[l + j * m_Dimension];
            a[l + j * m_Dimension] = a[i + j * m_Dimension];
            a[i + j * m_Dimension] = f * scale;
        }
    }
}

//=============================================================================

static unsigned ComputeEigenValuesUsingQL(double* d, double* e) {
    const double c_b10 = 1.0;

    /* Local variables */
    double c, f, g, h;
    unsigned int i, j, l, m;
    double p, r, s, c2, c3 = 0.0;
    double s2 = 0.0;
    double dl1, el1;
    double tst1, tst2;

    unsigned int ierr = 0;
    if (m_Order == 1) {
        return 1;
    }

    for (i = 1; i < m_Order; ++i) {
        e[i - 1] = e[i];
    }

    f = 0.;
    tst1 = 0.;
    e[m_Order - 1] = 0.;

    for (l = 0; l < m_Order; ++l) {
        j = 0;
        h = vnl_math_abs(d[l]) + vnl_math_abs(e[l]);
        if (tst1 < h) {
            tst1 = h;
        }
        /*     .......... look for small sub-diagonal element .......... */
        for (m = l; m < m_Order - 1; ++m) {
            tst2 = tst1 + vnl_math_abs(e[m]);
            if (tst2 == tst1) {
                break;
            }
            /*     .......... e(n) is always zero, so there is no exit */
            /*                through the bottom of the loop .......... */
        }

        if (m != l) {
            do {
                if (j == 30) {
                    /*     .......... set error -- no convergence to an */
                    /*                eigenvalue after 30 iterations .......... */
                    ierr = l + 1;
                    return ierr;
                }
                ++j;
                /*     .......... form shift .......... */
                g = d[l];
                p = (d[l + 1] - g) / (e[l] * 2.);
                r = vnl_math_hypot(p, c_b10);
                d[l] = e[l] / (p + vnl_math_sgn0(p) * vnl_math_abs(r));
                d[l + 1] = e[l] * (p + vnl_math_sgn0(p) * vnl_math_abs(r));
                dl1 = d[l + 1];
                h = g - d[l];

                for (i = l + 2; i < m_Order; ++i) {
                    d[i] -= h;
                }

                f += h;
                /*     .......... ql transformation .......... */
                p = d[m];
                c = 1.;
                c2 = c;
                el1 = e[l + 1];
                s = 0.;
                for (i = m - 1; i >= l; --i) {
                    c3 = c2;
                    c2 = c;
                    s2 = s;
                    g = c * e[i];
                    h = c * p;
                    r = vnl_math_hypot(p, e[i]);
                    e[i + 1] = s * r;
                    s = e[i] / r;
                    c = p / r;
                    p = c * d[i] - s * g;
                    d[i + 1] = h + s * (c * g + s * d[i]);
                    if (i == l) {
                        break;
                    }
                }

                p = -s * s2 * c3 * el1 * e[l] / dl1;
                e[l] = s * p;
                d[l] = c * p;
                tst2 = tst1 + vnl_math_abs(e[l]);
            } while (tst2 > tst1);
        }

        p = d[l] + f;

        // if( m_OrderEigenValues == OrderByValue )
        {
            // Order by value
            for (i = l; i > 0; --i) {
                if (p >= d[i - 1]) break;
                d[i] = d[i - 1];
            }
            d[i] = p;
        }
    }

    return ierr;  // ierr'th eigen value that couldn't be computed
}

//=============================================================================

Vector ComputeEigenvalues(Matrix* matrix) {
    Vector eigenvalue;
    Vector work_area;
    ReduceToTridiagonalMatrix(*matrix, eigenvalue, work_area);
    ComputeEigenValuesUsingQL(eigenvalue, work_area);
    return eigenvalue;
}
