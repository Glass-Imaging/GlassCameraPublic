//
//  PCA.cpp
//  GlassPipelineLib
//
//  Created by Fabio Riccardi on 3/1/23.
//

/*
#include <algorithm>

#define EIGEN_NO_DEBUG 1
#include <Eigen/Dense>

#include "PCA.hpp"

namespace egn = Eigen;

#include "gls_image.hpp"

template<typename pixel_type>
void pca(const gls::image<pixel_type>& input, int channel, int patch_size, gls::image<std::array<gls::float16_t, 8>>* pca_image) {
    std::cout << "PCA Begin" << std::endl;

    auto t_start = std::chrono::high_resolution_clock::now();

    egn::MatrixXf vectors(input.height * input.width, patch_size * patch_size);

    const int radius = patch_size / 2;

    std::cout << "Assembling patches" << std::endl;

    for (int y = 0; y < input.height; y++) {
        for (int x = 0; x < input.width; x++) {
            const int patch_index = y * input.width + x;
            for (int j = 0; j < patch_size; j++) {
                for (int i = 0; i < patch_size; i++) {
                    const auto& p = input.getPixel(x + i - radius, y + j - radius);
                    vectors(patch_index, j * patch_size + i) = p[channel];
                }
            }
        }
    }

    std::cout << "Computing covariance" << std::endl;

    // Compute variance matrix for the patch data
    egn::MatrixXf centered = vectors.rowwise() - vectors.colwise().mean();
    egn::MatrixXf covariance = (centered.transpose() * centered) / double(vectors.rows() - 1);

    std::cout << "Running solver" << std::endl;

    // Note: The eigenvectors are the *columns* of the principal_components matrix and they are already normalized

    egn::SelfAdjointEigenSolver<egn::MatrixXf> diag(covariance);
    egn::VectorXf variances = diag.eigenvalues();
    egn::MatrixXf principal_components = diag.eigenvectors();

    auto t_end = std::chrono::high_resolution_clock::now();
    double elapsed_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    std::cout << "PCA Execution Time: " << (int)elapsed_time_ms << std::endl;

    // Reduce principal components to 6 for a smaller patch size
    int components = patch_size == 3 ? 6 : 8;

    // Select eight largest eigenvectors in decreasing order
    egn::MatrixXf main_components(patch_size * patch_size, components);
    for (int i = 0; i < components; i++) {
        main_components.innerVector(i) = principal_components.innerVector(principal_components.cols()-1 - i);
    }

    // project the original patches to the reduced feature space
    egn::MatrixXf projection = vectors * main_components;

//    gls::image<gls::luma_pixel> eigenImage(input.width, input.height);
//    eigenImage.apply([&] (gls::luma_pixel* p, int x, int y) {
//        const int patch_index = y * input.width + x;
//        *p = 255 * std::clamp(projection(patch_index, 0), 0.0f, 1.0f);
//    });
//    static int count = 0;
//    eigenImage.write_png_file("/Users/fabio/eigenImage" + std::to_string(count++) + ".png");

    // Copy the projected features to the result
    pca_image->apply([&] (std::array<gls::float16_t, 8>* p, int x, int y) {
        const int patch_index = y * input.width + x;
        for (int i = 0; i < components; i++) {
            (*p)[i] = projection(patch_index, i);
        }
        for (int i = components; i < 8; i++) {
            (*p)[i] = 0;
        }
    });
}

template
void pca(const gls::image<gls::rgba_pixel_float>& input, int channel, int patch_size, gls::image<std::array<gls::float16_t, 8>>* pca_image);

void pca4c(const gls::image<gls::rgba_pixel_float>& input, int patch_size, gls::image<std::array<gls::float16_t, 8>>* pca_image) {
    std::cout << "PCA Begin" << std::endl;

    auto t_start = std::chrono::high_resolution_clock::now();

    egn::MatrixXf vectors(input.height * input.width, 4 * patch_size * patch_size);

    const int radius = patch_size / 2;

    std::cout << "Assembling patches" << std::endl;

    for (int y = 0; y < input.height; y++) {
        for (int x = 0; x < input.width; x++) {
            const int patch_index = y * input.width + x;
            for (int j = 0; j < patch_size; j++) {
                for (int i = 0; i < patch_size; i++) {
                    const auto& p = input.getPixel(x + i - radius, y + j - radius);
                    vectors(patch_index, 4 * (j * patch_size + i) + 0) = p[0];
                    vectors(patch_index, 4 * (j * patch_size + i) + 1) = p[1];
                    vectors(patch_index, 4 * (j * patch_size + i) + 2) = p[2];
                    vectors(patch_index, 4 * (j * patch_size + i) + 3) = p[3];
                }
            }
        }
    }

    std::cout << "Computing covariance" << std::endl;

    // Compute variance matrix for the patch data
    egn::MatrixXf centered = vectors.rowwise() - vectors.colwise().mean();
    egn::MatrixXf covariance = (centered.transpose() * centered) / double(vectors.rows() - 1);

    std::cout << "Running solver" << std::endl;

    // Note: The eigenvectors are the *columns* of the principal_components matrix and they are already normalized

    egn::SelfAdjointEigenSolver<egn::MatrixXf> diag(covariance);
    egn::VectorXf variances = diag.eigenvalues();
    egn::MatrixXf principal_components = diag.eigenvectors();

    auto t_end = std::chrono::high_resolution_clock::now();
    double elapsed_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    std::cout << "PCA Execution Time: " << (int)elapsed_time_ms << std::endl;

    int components = 8; // patch_size == 3 ? 6 : 8;

    // Select eight largest eigenvectors in decreasing order
    egn::MatrixXf main_components(4 * patch_size * patch_size, components);
    for (int i = 0; i < components; i++) {
        main_components.innerVector(i) = principal_components.innerVector(principal_components.cols()-1 - i);
    }

    // project the original patches to the reduced feature space
    egn::MatrixXf projection = vectors * main_components;

//    gls::image<gls::luma_pixel> eigenImage(input.width, input.height);
//    eigenImage.apply([&] (gls::luma_pixel* p, int x, int y) {
//        const int patch_index = y * input.width + x;
//        *p = 255 * std::clamp(projection(patch_index, 0), 0.0f, 1.0f);
//    });
//    static int count = 0;
//    eigenImage.write_png_file("/Users/fabio/eigenImage" + std::to_string(count++) + ".png");

    // Copy the projected features to the result
    pca_image->apply([&] (std::array<gls::float16_t, 8>* p, int x, int y) {
        const int patch_index = y * input.width + x;
        for (int i = 0; i < components; i++) {
            (*p)[i] = projection(patch_index, i);
        }
    });
}

*/
