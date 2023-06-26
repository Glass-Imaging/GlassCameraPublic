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

#ifndef Homography_hpp
#define Homography_hpp

#include <vector>

#include "feature2d.hpp"
#include "gls_linalg.hpp"

namespace gls {

gls::Matrix<3, 3> FindHomography(const std::vector<std::pair<Point2f, Point2f>> matchpoints, float threshold,
                                 int max_iterations, std::vector<int>* inlier_indices = nullptr);

gls::Matrix<3, 3> ScaleHomography(const gls::Matrix<3, 3>& homography, float scale) {
    const auto scaleMatrix = gls::Matrix<3, 3> {
        {2, 0, 0},
        {0, 2, 0},
        {0, 0, 1},
    };

    return scaleMatrix * homography * inverse(scaleMatrix);
}

}  // namespace gls

#endif /* Homography_hpp */
