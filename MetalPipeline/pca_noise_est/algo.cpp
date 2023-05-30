/*
 *
 * This file is part of the PCA Noise Estimation algorithm.
 *
 * Copyright(c) 2013 Miguel Colom.
 * miguel.colom@cmla.ens-cachan.fr
 *
 * This file may be licensed under the terms of of the
 * GNU General Public License Version 2 (the ``GPL'').
 *
 * Software distributed under the License is distributed
 * on an ``AS IS'' basis, WITHOUT WARRANTY OF ANY KIND, either
 * express or implied. See the GPL for the specific language
 * governing rights and limitations.
 *
 * You should have received a copy of the GPL along with this
 * program. If not, go to http://www.gnu.org/licenses/gpl.html
 * or write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

// #include <omp.h>
//
#include "Param.h"
#include "algo.h"
#include "curve_filter.h"
//
#include "framework/CImage.h"
#include "framework/libparser.h"
#include "framework/operations.cpp"
//
#include "PCANoiseLevelEstimator.h"

/**
 * @brief Build a mask for valide pixel. If mask(i, j) = 1,
 * the pixels will not be used.
 *
 * @param i_im : noisy image;
 * @param o_mask : will contain the mask for all pixel in the
 * image size;
 * @param p_imSize : size of the image;
 * @param p_sizePatch : size of a patch.
 *
 * @return number of valid blocks.
 *
 **/
unsigned buildMask(CImage& i_im, int* o_mask, int Nx, int Ny, int w, int num_channels) {
    unsigned count = 0;

    //#pragma omp parallel for schedule(static) \
          shared(o_mask) reduction(+:count)

    for (unsigned ij = 0; ij < Nx * Ny; ij++) {
        o_mask[ij] = 0;
    }
    return 0;

    // Validate pixels
    int* pixel_mask = new int[Nx * Ny];

    for (unsigned ij = 0; ij < Nx * Ny; ij++) {
        const unsigned j = ij / Nx;
        const unsigned i = ij - j * Nx;

        //! Look if the pixel is not to close to boundaries of the image
        if (i < Nx - w && j < Ny - w) {
            for (unsigned c = 0; c < num_channels; c++) {
                float* u = i_im.get_channel(c);

                //! Look if the square 2x2 of pixels is constant
                int invalid_pixel = (c == 0 ? 1 : pixel_mask[ij]);

                // Try to validate pixel
                if (fabs(u[ij] - u[ij + 1]) > 0.001f) {
                    invalid_pixel = 0;
                } else if (fabs(u[ij + 1] - u[ij + Nx]) > 0.001f) {
                    invalid_pixel = 0;
                } else if (fabs(u[ij + Nx] - u[ij + Nx + 1]) > 0.001f) {
                    invalid_pixel = 0;
                }
                pixel_mask[ij] = invalid_pixel;
            }
        } else {
            pixel_mask[ij] = 1;  // Not valid
        }

        if (pixel_mask[ij] == 0) {
            count++;
        }
    }

    // Validate blocks
    for (unsigned ij = 0; ij < Nx * Ny; ij++) {
        o_mask[ij] = 0;  // Valid
    }

    for (int y = 0; y < Ny - w; y++) {
        for (int x = 0; x < Nx - w; x++) {
            // Now check that the block at [x,y] is valid according to the pixel mask
            bool block_is_valid = true;
            for (int j = 0; j < w && block_is_valid; j++) {
                for (int i = 0; i < w && block_is_valid; i++) {
                    unsigned mask_ptr = (y + j) * Nx + (x + i);
                    block_is_valid = (pixel_mask[mask_ptr] == 0);
                }
            }

            if (!block_is_valid) o_mask[y * Nx + x] = 1;  // Not valid
        }
    }

    delete[] pixel_mask;

    return count;
}

/**
 * @brief Filters the noise curve.
 *
 * @param *vmeans : Array containing the means
 * @param *vstds : Array containing the standard deviations
 * @param  bins : Number of bins
 *
 **/
void filter_curve(vector<float>* vmeans, vector<float>* vstds, int bins) {
    // Take square to stds
    for (int i = 0; i < bins; i++) vstds->at(i) = pow(vstds->at(i), 2);

    for (int i = 1; i < bins - 1; i++) {
        float x0 = vmeans->at(i - 1);
        float x1 = vmeans->at(i + 1);

        if (x1 != x0) {
            float y0 = vstds->at(i - 1);
            float y1 = vstds->at(i + 1);

            float py = y0 + (y1 - y0) * (vmeans->at(i) - x0) / (x1 - x0);

            if (vstds->at(i) > py) vstds->at(i) = py;
        }
    }

    // Take square root of vars
    for (int i = 0; i < bins; i++) vstds->at(i) = sqrtf(vstds->at(i));
}

/**
 * @brief PCA noise estimation main algorithm.
 *
 * @param *framework : Framework
 * @param argc : Number of arguments of the program
 * @param **argv : Arguments of the program
 *
 **/
void algorithm(int argc, char** argv) {
    vector<OptStruct*> options;
    vector<ParStruct*> parameters;

    OptStruct obins = {"b:", 0, "0", NULL, "Number of bins"};
    options.push_back(&obins);
    OptStruct oD = {"D:", 7, "7", NULL, "Filtering distance"};
    options.push_back(&oD);
    OptStruct ore = {"r", 0, NULL, NULL, "Flag to remove equal pixels"};
    options.push_back(&ore);
    OptStruct ofilter = {"g:", 5, "5", NULL, "Filter iterations"};
    options.push_back(&ofilter);

    ParStruct pinput = {"input", NULL, "input file"};
    parameters.push_back(&pinput);

    // Parse program arguments
    if (!parsecmdline("pca", "PCA noise estimation", argc, argv, options, parameters)) {
        printf("\n");
        printf("(c) 2012 Miguel Colom. Under license GNU GPL.\n");
        printf("http://mcolom.perso.math.cnrs.fr/\n");
        printf("\n");
        exit(-1);
    }

    CImage* noisy_img = new CImage();
    noisy_img->load((char*)pinput.value);

    // Get image length
    int Nx = noisy_img->get_width();
    int Ny = noisy_img->get_height();
    int N = Nx * Ny;

    // Get number of channels
    int num_channels = noisy_img->get_num_channels();

    // Get noise curve filter iterations
    int curve_filter_iterations = atoi(ofilter.value);

    // Get the filtering distance
    int D = atoi(oD.value);

    // Get remove equal pixels flag
    bool remove_equal_pixels_blocks = ore.flag;

    // Determine number of bins
    int num_bins = atoi(obins.value);
    if (num_bins <= 0) num_bins = N / 112000;
    if (num_bins <= 0) num_bins = 1;  // Force at least one bin

// OpenMP config
#ifdef _OPENMP
    omp_set_num_threads(omp_get_num_procs());
#endif

    // Build valid pixels mask
    int* mask = NULL;
    if (remove_equal_pixels_blocks) {
        mask = new int[Nx * Ny];
        buildMask(*noisy_img, mask, Nx, Ny, M1, num_channels);
    }

    // Arrays to store the final means and noise estimations
    float* vmeans = new float[num_channels * num_bins];
    float* vstds = new float[num_channels * num_bins];

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int ch = 0; ch < num_channels; ch++) {
        double* data = new double[N];

        // Copy image float data into double array
        float* channel_data = noisy_img->get_channel(ch);
        for (int i = 0; i < N; i++) data[i] = channel_data[i];

        EstimateNoiseVariance(data, noisy_img->get_width(), noisy_img->get_height(), num_bins, &vmeans[ch * num_bins],
                              &vstds[ch * num_bins], mask);

        delete[] data;
    }

    // Filter noise curve
    float* new_std_control = new float[num_bins * num_channels];
    copy_buffer(new_std_control, vstds, num_channels * num_bins);
    //
    for (int ch = 0; ch < num_channels; ch++)
        for (int filt_iter = 0; filt_iter < curve_filter_iterations; filt_iter++) {
            bool allow_up = (filt_iter < 3);
            filter_curve(&vmeans[ch * num_bins], &new_std_control[ch * num_bins], num_bins,
                         &new_std_control[ch * num_bins], D, allow_up);
        }

    // Print results
    for (int bin = 0; bin < num_bins; bin++) {
        // Means
        for (int ch = 0; ch < num_channels; ch++) printf("%f  ", vmeans[ch * num_bins + bin]);

        // Standard deviations
        for (int ch = 0; ch < num_channels; ch++) printf("%f  ", new_std_control[ch * num_bins + bin]);
        //
        printf("\n");
    }

    // Free memory
    delete noisy_img;
    if (mask != NULL) delete[] mask;
    delete[] new_std_control;
    delete[] vstds;
    delete[] vmeans;
}

void algorithm(const gls::image<gls::luma_pixel_float>& image) {
    // Get image length
    int Nx = image.width / 2;
    int Ny = image.height / 2;
    int N = Nx * Ny;

    // Get number of channels
    int num_channels = 1;

    // Get noise curve filter iterations
    int curve_filter_iterations = 5; // atoi(ofilter.value);

    // Get the filtering distance
    int D = 7; // atoi(oD.value);

    // Get remove equal pixels flag
    bool remove_equal_pixels_blocks = false; // ore.flag;

    // Determine number of bins
    int num_bins = 0; // atoi(obins.value);
    if (num_bins <= 0) num_bins = N / 112000;
    if (num_bins <= 0) num_bins = 1;  // Force at least one bin

    // Build valid pixels mask
    int* mask = NULL;
//    if (remove_equal_pixels_blocks) {
//        mask = new int[Nx * Ny];
//        buildMask(*noisy_img, mask, Nx, Ny, M1, num_channels);
//    }

    // Arrays to store the final means and noise estimations
    float* vmeans = new float[num_channels * num_bins];
    float* vstds = new float[num_channels * num_bins];

    for (int ch = 0; ch < num_channels; ch++) {
        double* data = new double[N];

        // Copy image float data into double array
        for (int j = 0; j < image.height; j += 2) {
            for (int i = 0; i < image.width; i += 2) {
                const auto& p = image[j][i];
                data[(j * image.width / 2 + i) / 2] = p.luma;
            }
        }

        EstimateNoiseVariance(data, Nx, Ny, num_bins, &vmeans[ch * num_bins],
                              &vstds[ch * num_bins], mask);

        delete[] data;
    }

    // Filter noise curve
    float* new_std_control = new float[num_bins * num_channels];
    copy_buffer(new_std_control, vstds, num_channels * num_bins);
    //
    for (int ch = 0; ch < num_channels; ch++)
        for (int filt_iter = 0; filt_iter < curve_filter_iterations; filt_iter++) {
            bool allow_up = (filt_iter < 3);
            filter_curve(&vmeans[ch * num_bins], &new_std_control[ch * num_bins], num_bins,
                         &new_std_control[ch * num_bins], D, allow_up);
        }

    // Print results
    for (int bin = 0; bin < num_bins; bin++) {
        // Means
        for (int ch = 0; ch < num_channels; ch++) printf("%f  ", vmeans[ch * num_bins + bin]);

        // Standard deviations
        for (int ch = 0; ch < num_channels; ch++) printf("%e  ", new_std_control[ch * num_bins + bin]);
        //
        printf("\n");
    }

    // Free memory
    if (mask != NULL) delete[] mask;
    delete[] new_std_control;
    delete[] vstds;
    delete[] vmeans;
}
