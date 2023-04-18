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

#include "ThreadPool.hpp"

typedef egn::Map<egn::Matrix<float, egn::Dynamic, egn::Dynamic, egn::RowMajor>> MatrixXf_rm;

template <size_t components, size_t principalComponents>
void build_pca_space(const std::span<std::array<float, components>>& patches,
                     std::array<std::array<float16_t, principalComponents>, components>* pcaSpace) {
    auto t_start = std::chrono::high_resolution_clock::now();

    MatrixXf_rm vectors((float *) patches.data(), patches.size(), components);

    // Compute covariance matrix for the patch data
    egn::MatrixXf centered = vectors.rowwise() - vectors.colwise().mean();
    egn::MatrixXf covariance = (centered.adjoint() * centered) / (vectors.rows() - 1);

    // Note: The eigenvectors are the *columns* of the principal_components matrix and they are already normalized
    egn::SelfAdjointEigenSolver<egn::MatrixXf> solver(covariance);
    egn::VectorXf variances = solver.eigenvalues();
    egn::MatrixXf eigenvectors = solver.eigenvectors();

    // std::cout << std::scientific << variances << std::endl;

//    for (int i = variances.size() - 1; i > 0; i--) {
//        std::cout << variances.size() - i - 1 << " : " << std::scientific << variances[i] << ", delta: " << variances[i] - variances[i-1] << std::endl;
//    }
//    std::cout << variances.size() - 1 << " : " << std::scientific << variances[variances.size() - 1] << std::endl;

    auto t_end = std::chrono::high_resolution_clock::now();
    auto elapsed_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    std::cout << "PCA Execution Time: " << (int)elapsed_time_ms << std::endl;

    // Select the largest eigenvectors in decreasing order
    egn::MatrixXf main_components(components, principalComponents);
    for (int c = 0; c < principalComponents; c++) {
        const auto& pci = eigenvectors.innerVector(components - 1 - c);
        for (int r = 0; r < components; r++) {
            (*pcaSpace)[r][c] = pci[r];
        }
    }
}

template
void build_pca_space(const std::span<std::array<float, 25>>& patches,
                     std::array<std::array<float16_t, 8>, 25>* pca_space);

void pca(const gls::image<gls::rgba_pixel_float>& input,
         const std::span<std::array<float, 25>>& patches,
         const std::span<std::array<float, 25>>& patchesSmall,
         int patch_size, gls::image<std::array<gls::float16_t, 8>>* pca_image) {
    std::cout << "PCA Begin" << std::endl;

    auto t_start = std::chrono::high_resolution_clock::now();

    MatrixXf_rm vectors((float *) patches.data(), input.height * input.width, patch_size * patch_size);
    MatrixXf_rm vectorsSmall((float *) patchesSmall.data(), input.height * input.width / 64, patch_size * patch_size);

    std::cout << "Computing covariance" << std::endl;

    // Compute variance matrix for the patch data
    egn::MatrixXf centered = vectorsSmall.rowwise() - vectorsSmall.colwise().mean();

    // egn::MatrixXf covariance = (centered.transpose() * centered) / (vectors.rows() - 1);

    // (Slightly) Faster way to perform "centered.transpose() * centered", see:
    // https://stackoverflow.com/questions/39606224/does-eigen-have-self-transpose-multiply-optimization-like-h-transposeh
    egn::MatrixXf covariance = egn::MatrixXf::Zero(patch_size * patch_size, patch_size * patch_size);
    covariance.template selfadjointView<egn::Lower>().rankUpdate(centered.transpose());
    covariance /= vectors.rows() - 1;

    std::cout << "Running solver" << std::endl;

    // Note: The eigenvectors are the *columns* of the principal_components matrix and they are already normalized

    egn::SelfAdjointEigenSolver<egn::MatrixXf> diag(covariance);
    egn::VectorXf variances = diag.eigenvalues();
    egn::MatrixXf principal_components = diag.eigenvectors();

    auto t_end = std::chrono::high_resolution_clock::now();
    auto solver_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    std::cout << "Solver Time: " << (int)solver_time_ms << std::endl;

    auto elapsed_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

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

template<typename pixel_type>
void pca(const gls::image<pixel_type>& input, int channel, int patch_size, gls::image<std::array<gls::float16_t, 8>>* pca_image) {
    std::cout << "PCA Begin" << std::endl;

    auto t_start = std::chrono::high_resolution_clock::now();

    // Patch vector for PCA weights computation
    egn::MatrixXf vectors(input.height * input.width, patch_size * patch_size);

    // Pixel subset for PCA computation
    egn::MatrixXf vectorsSmall(input.height * input.width / 64, patch_size * patch_size);

    const int radius = patch_size / 2;

    std::cout << "Assembling patches" << std::endl;

    const int slices = input.height % 8 == 0 ? 8 : input.height % 4 == 0 ? 4 : input.height % 2 == 0 ? 2 : 1;
    const int slice_size = input.height / slices;

    {
        ThreadPool threadPool(slices);

        for (int s = 0; s < slices; s++) {
            threadPool.enqueue([s, slices, slice_size, patch_size, radius, &input, &vectors, &vectorsSmall](){
                for (int y = s * slice_size; y < (s + 1) * slice_size; y++) {
                    for (int x = 0; x < input.width; x++) {
                        const int patch_index = y * input.width + x;
                        const int small_patch_index = y * input.width / 64 + x / 8;
                        for (int j = 0; j < patch_size; j++) {
                            for (int i = 0; i < patch_size; i++) {
                                const auto& p = input.getPixel(x + i - radius, y + j - radius);
                                vectors(patch_index, j * patch_size + i) = p.x;
                                if (x % 8 == 0 && y % 8 == 0) {
                                    vectorsSmall(small_patch_index, j * patch_size + i) = p.x;
                                }
                            }
                        }
                    }
                }
            });
        }
    }

    auto t_patches_end = std::chrono::high_resolution_clock::now();
    double elapsed_time_ms = std::chrono::duration<double, std::milli>(t_patches_end - t_start).count();

    std::cout << "Patches time: " << (int)elapsed_time_ms << std::endl;

    std::cout << "Computing covariance" << std::endl;

    // Compute variance matrix for the patch data
    egn::MatrixXf centered = vectorsSmall.rowwise() - vectorsSmall.colwise().mean();

    // egn::MatrixXf covariance = (centered.transpose() * centered) / (vectors.rows() - 1);

    // (Slightly) Faster way to perform "centered.transpose() * centered", see:
    // https://stackoverflow.com/questions/39606224/does-eigen-have-self-transpose-multiply-optimization-like-h-transposeh
    egn::MatrixXf covariance = egn::MatrixXf::Zero(patch_size * patch_size, patch_size * patch_size);
    covariance.template selfadjointView<egn::Lower>().rankUpdate(centered.transpose());
    covariance /= vectors.rows() - 1;

    std::cout << "Running solver" << std::endl;

    // Note: The eigenvectors are the *columns* of the principal_components matrix and they are already normalized

    egn::SelfAdjointEigenSolver<egn::MatrixXf> diag(covariance);
    egn::VectorXf variances = diag.eigenvalues();
    egn::MatrixXf principal_components = diag.eigenvectors();

    auto t_end = std::chrono::high_resolution_clock::now();
    auto solver_time_ms = std::chrono::duration<double, std::milli>(t_end - t_patches_end).count();

    std::cout << "Solver Time: " << (int)solver_time_ms << std::endl;

    elapsed_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

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

    const int radius = patch_size / 2;

    std::cout << "Assembling patches" << std::endl;

    egn::MatrixXf vectors(4 * input.height * input.width, patch_size * patch_size);
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
