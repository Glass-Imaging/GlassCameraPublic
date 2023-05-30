#include <math.h>
#include <stdio.h>

#include <algorithm>

#include "EigenvalueComputation.h"
#include "PCANoiseLevelEstimator.h"
#include "Param.h"
#include "Vector.h"
#include "framework/CHistogram.cpp"

/* Note: this file has been adapted from the code by Pyatykh, S.,
 * Hesser, J., and Lei Zheng.
 *
 * http://ieeexplore.ieee.org/xpl/login.jsp?tp=&arnumber=6316174&url=http%3A%2F%2Fieeexplore.ieee.org%2Fxpls%2Fabs_all.jsp%3Farnumber%3D6316174
 *
 * Original source code: http://physics.medma.uni-heidelberg.de/cms/projects/132-pcanle
 *
 * It has been adapted in order to estimate signal-dependent noise but
 * the original code is by Pyatykh, S., Hesser, J., and Lei Zheng.
 *
 */

//=============================================================================

/**
 * @brief Rounds a number to the nearest integer
 *
 * @param x : input number
 * @return rounded number
 *
 **/
inline int Round(double x) { return x > 0.0 ? int(x + 0.5) : int(x - 0.5); }

//=============================================================================

/**
 * @brief Clips a number between the minimum and maximum values
 *
 * @param value : input number
 * @param begin_value : minimum value
 * @param end_value : maximum value
 * @return clipped value
 *
 **/
inline int Clamp(int value, int begin_value, int end_value) {
    if (value < begin_value) return begin_value;

    if (value > end_value) return end_value;

    return value;
}

//=============================================================================

struct BlockInfo {
    double variance;
    unsigned offset;
    float mean;
};

/**
 * @brief Variance comparator
 *
 * @param a : block block
 * @param b : second block
 * @return true if the variance of a if less than the variance of b
 *
 **/
inline bool operator<(const BlockInfo& a, const BlockInfo& b) { return a.variance < b.variance; }

//=============================================================================

/**
 * @brief Read all blocks statistics
 *
 * @param image_data : input_image
 * @param image_w : image width
 * @param image_h : image height
 * @param block_info : output array with the block statistics
 * @param mask : equal pixels mask
 * @return number of read blocks
 *
 **/
static unsigned ComputeBlockInfo(const double* image_data, int image_w, int image_h, BlockInfo* block_info, int* mask) {
    unsigned block_count = 0;

    for (int y = 0; y < image_h - M2 + 1; y++) {
        for (int x = 0; x < image_w - M1 + 1; x++) {
            // Only is the pixel is valid
            if (mask == NULL || (mask != NULL && mask[image_w * y + x] == 0)) {
                double sum1 = 0.0;
                double sum2 = 0.0;

                for (int by = y; by < y + M2; by++) {
                    for (int bx = x; bx < x + M1; bx++) {
                        double val = image_data[by * image_w + bx];
                        sum1 += val;
                        sum2 += val * val;
                    }
                }

                BlockInfo bi;
                bi.variance = (sum2 - sum1 * sum1 / M) / M;
                bi.offset = y * image_w + x;
                bi.mean = sum1 / M;

                block_info[block_count] = bi;
                block_count++;
            }
        }
    }
    return block_count;
}

//=============================================================================

/**
 * @brief Computes the block statistics within a bin
 *
 * @param image_data : input_image
 * @param image_w : image width
 * @param image_h : image height
 * @param block_info : output array with the block statistics
 * @param block_count : number of blocks
 * @param sum1 : output sum of of values within the block
 * @param sum2 : output squared sum of of values within the block
 * @param subset_size : number of elements in the bin
 * @return number of elements in the bin
 *
 **/
static unsigned ComputeStatistics(const double* image_data, int image_w, int image_h, const BlockInfo* block_info,
                                  unsigned block_count, Vector* sum1, Matrix* sum2, unsigned* subset_size) {
    unsigned subset_count = 0;

    for (double p = 1.0; p - MinLevel > -1e-6; p -= LevelStep) {
        double q = p - LevelStep - MinLevel > -1e-6 ? p - LevelStep : 0.0;
        unsigned max_index = block_count - 1;
        unsigned beg = Clamp(Round(q * max_index), 0, max_index);
        unsigned end = Clamp(Round(p * max_index), 0, max_index);

        Vector curr_sum1 = {{0}};
        Matrix curr_sum2 = {{0}};

        for (unsigned k = beg; k < end; ++k) {
            unsigned offset = block_info[k].offset;

            double block[M];
            for (int by = 0; by < M2; ++by)
                for (int bx = 0; bx < M1; ++bx) block[by * M1 + bx] = image_data[offset + by * image_w + bx];

            for (unsigned i = 0; i < M; ++i) curr_sum1[i] += block[i];

            for (unsigned i = 0; i < M; ++i)
                for (unsigned j = i; j < M; ++j) curr_sum2[i * M + j] += block[i] * block[j];
        }

        sum1[subset_count] = curr_sum1;
        sum2[subset_count] = curr_sum2;
        subset_size[subset_count] = end - beg;

        ++subset_count;
    }

    for (unsigned i = subset_count - 1; i > 0; --i) {
        sum1[i - 1] += sum1[i];
        sum2[i - 1] += sum2[i];
        subset_size[i - 1] += subset_size[i];
    }

    return subset_count;
}

//=============================================================================

/**
 * @brief Computes the variance upper bound
 *
 * @param block_info : array with the block statistics
 * @param block_count : number of blocks
 * @param block_idx : index of the block for which the upper bound will
                      be computed
 * @return upper bound variance
 *
 **/
static double ComputeUpperBound(const BlockInfo* block_info, unsigned block_count, int* block_idx) {
    unsigned max_index = block_count - 1;
    unsigned index = Clamp(Round(UpperBoundLevel * max_index), 0, max_index);
    *block_idx = index;
    return UpperBoundFactor * block_info[index].variance;
}

//=============================================================================

/**
 * @brief Applied PCA to given data
 *
 * @param sum1 : output sum of of values within the block
 * @param sum2 : output squared sum of of values within the block
 * @param subset_size : number of elements in the bin
 * @return Output eigenvalues
 *
 **/
static Vector ApplyPCA(const Vector& sum1, const Matrix& sum2, unsigned subset_size) {
    double mean[M];
    for (unsigned i = 0; i < M; ++i) mean[i] = sum1[i] / subset_size;

    Matrix cov_matrix;
    for (unsigned i = 0; i < M; ++i) {
        for (unsigned j = i; j < M; ++j) {
            cov_matrix[i * M + j] = sum2[i * M + j] / subset_size - mean[i] * mean[j];
            cov_matrix[j * M + i] = cov_matrix[i * M + j];
        }
    }

    return ComputeEigenvalues(&cov_matrix);
}

//=============================================================================

/**
 * @brief Iterative function to get the next estimate
 *
 * @param sum1 : input sum of the values within the block
 * @param sum2 : input squared sum of of values within the block
 * @param subset_size : number of elements in the bin
 * @param sum_count : number of iterations
 * @return Output eigenvalues
 *
 **/
static double GetNextEstimate(const Vector* sum1, const Matrix* sum2, unsigned* subset_size, unsigned sum_count,
                              double prev_estimate, double upper_bound) {
    double var = 0.0;

    for (unsigned i = 0; i < sum_count; ++i) {
        Vector eigen_value = ApplyPCA(sum1[i], sum2[i], subset_size[i]);

        var = eigen_value[0];

        if (var < 1e-6) break;

        double diff = eigen_value[EigenValueCount - 1] - eigen_value[0];
        double diff_threshold = EigenValueDiffThreshold * prev_estimate / sqrt(subset_size[i]);

        if (diff < diff_threshold && var < upper_bound) break;
    }

    return var < upper_bound ? var : upper_bound;
}

//=============================================================================

/**
 * @brief Signal-dependent adaptation of the PCA method
 *
 * @param image_data : input_image
 * @param image_w : image width
 * @param image_h : image height
 * @param num_bins : number of bins in the noise curve
 * @param out_means : output intensities in the noise curve
 * @param out_stds : output STDs in the noise curve
 * @param mask : equal pixels mask
 *
 **/
void EstimateNoiseVariance(const double* image_data, int image_w, int image_h, int num_bins, float* out_means,
                           float* out_stds, int* mask) {
    BlockInfo* block_info = new BlockInfo[image_w * image_h];
    unsigned block_count = ComputeBlockInfo(image_data, image_w, image_h, block_info, mask);

    // Split the blocks using histogram of means.

    float* means = new float[block_count];
    for (int i = 0; i < block_count; i++) means[i] = block_info[i].mean;

    // Special case: block count is zero
    if (block_count == 0) {
        for (int i = 0; i < num_bins; i++) out_means[i] = out_stds[i] = 0;
        return;
    }

    CHistogram<BlockInfo> histo = CHistogram<BlockInfo>(num_bins, block_info, means, block_count);

    for (int bin = 0; bin < num_bins; bin++) {
        int elems_bin = histo.get_num_elements_bin(bin);
        BlockInfo* blocks_bin = histo.get_data_bin(bin);

        std::sort(blocks_bin, blocks_bin + elems_bin);

        Vector sum1[MaxSubsetCount];
        Matrix sum2[MaxSubsetCount];
        unsigned subset_size[MaxSubsetCount];
        unsigned subset_count =
            ComputeStatistics(image_data, image_w, image_h, blocks_bin, elems_bin, sum1, sum2, subset_size);

        int block_idx;
        double upper_bound = ComputeUpperBound(blocks_bin, elems_bin, &block_idx);
        double prev_var = 0.0;
        double var = upper_bound;

        for (unsigned iter = 0; iter < 10; ++iter) {
            if (fabs(prev_var - var) < 1e-6) break;

            prev_var = var;
            var = GetNextEstimate(sum1, sum2, subset_size, subset_count, var, upper_bound);
        }

        if (var < 0) var = 0;

        // Store pair (mean, std)
        out_means[bin] = blocks_bin[block_idx].mean;
        out_stds[bin] = sqrtf(var);
    }

    // Release memory
    delete[] means;
    delete[] block_info;
}
