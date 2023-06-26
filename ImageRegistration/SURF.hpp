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

#ifndef SURF_hpp
#define SURF_hpp

#include <float.h>

#include "feature2d.hpp"
#include "gls_mtl.hpp"
#include "gls_mtl_image.hpp"
#include "gls_linalg.hpp"

namespace gls {

typedef gls::basic_point<float> Point2f;

class DMatch {
   public:
    DMatch() : queryIdx(-1), trainIdx(-1), distance(FLT_MAX) {}
    DMatch(int _queryIdx, int _trainIdx, float _distance)
        : queryIdx(_queryIdx), trainIdx(_trainIdx), distance(_distance) {}

    int queryIdx;  // query descriptor index
    int trainIdx;  // train descriptor index

    float distance;

    // less is better
    bool operator<(const DMatch& m) const { return distance < m.distance; }
};

class SURF {
   public:
    static std::unique_ptr<SURF> makeInstance(MetalContext* glsContext, int width, int height,
                                              int max_features = -1, int nOctaves = 4, int nOctaveLayers = 2,
                                              float hessianThreshold = 0.02);

    virtual ~SURF() {}

    virtual void integral(const gls::image<float>& img,
                          const std::array<gls::mtl_image_2d<float>::unique_ptr, 4>& sum) const = 0;

    virtual void detect(const std::array<gls::mtl_image_2d<float>::unique_ptr, 4>& integralSum,
                        std::vector<KeyPoint>* keypoints) const = 0;

    virtual void detectAndCompute(const gls::image<float>& img, std::vector<KeyPoint>* keypoints,
                                  gls::image<float>::unique_ptr* _descriptors) const = 0;

    virtual std::vector<DMatch> matchKeyPoints(const gls::image<float>& descriptor1,
                                               const gls::image<float>& descriptor2) const = 0;

    std::vector<std::pair<Point2f, Point2f>> findMatches(const gls::image<float>& descriptors1, const std::vector<KeyPoint>& keypoints1,
                                                         const gls::image<float>& descriptors2, const std::vector<KeyPoint>& keypoints2) const {
        std::vector<gls::DMatch> matchedPoints = matchKeyPoints(descriptors1, descriptors2);

        // Convert to Point2D format
        std::vector<std::pair<Point2f, Point2f>> matches(matchedPoints.size());
        for (int i = 0; i < matchedPoints.size(); i++) {
            matches[i] = std::pair{keypoints1[matchedPoints[i].queryIdx].pt, keypoints2[matchedPoints[i].trainIdx].pt};
        }
        return matches;
    }

    static std::vector<std::pair<Point2f, Point2f>> detection(MetalContext* cLContext,
                                                              const gls::image<float>& image1,
                                                              const gls::image<float>& image2);
};

}  // namespace gls

#endif /* SURF_hpp */
