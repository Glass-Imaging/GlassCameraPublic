//
//  PCA.hpp
//  GlassPipelineLib
//
//  Created by Fabio Riccardi on 3/1/23.
//

#ifndef PCA_hpp
#define PCA_hpp

#include "gls_image.hpp"

template<typename pixel_type>
void pca(const gls::image<pixel_type>& input, int channel, int patch_size, gls::image<std::array<gls::float16_t, 8>>* pca_image);

void pca(const gls::image<gls::rgba_pixel_float>& input,
         const std::span<std::array<float, 25>>& patches,
         const std::span<std::array<float, 25>>& patchesSmall,
         int patch_size, gls::image<std::array<gls::float16_t, 8>>* pca_image);

template <size_t components, size_t principal_components>
void build_pca_space(const std::span<std::array<float, components>>& patches,
                     std::array<std::array<float16_t, principal_components>, components>* pca_space);

void pca4c(const gls::image<gls::rgba_pixel_float>& input, int patch_size, gls::image<std::array<gls::float16_t, 8>>* pca_image);

#endif /* PCA_hpp */
