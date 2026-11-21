/*
 * Copyright 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <inttypes.h>
#include <iosfwd>
#include <iostream>
#include <string>
#include <memory>
#include <cmath>

#include <math/HashCombine.h>

namespace android {

struct BlurRegion {
    uint32_t blurRadius;
    float cornerRadiusTL;
    float cornerRadiusTR;
    float cornerRadiusBL;
    float cornerRadiusBR;
    float alpha;
    int left;
    int top;
    int right;
    int bottom;
    uint32_t glassMode = 0;
    float glassDensity = 1.0f;
    std::shared_ptr<const std::string> advancedBlurShader;
    uint32_t advancedBlurTint = 0;

    const std::string& getAdvancedBlurShader() const {
        static const std::string empty;
        return advancedBlurShader ? *advancedBlurShader : empty;
    }

    inline bool operator==(const BlurRegion& other) const {
        return blurRadius == other.blurRadius && cornerRadiusTL == other.cornerRadiusTL &&
                cornerRadiusTR == other.cornerRadiusTR && cornerRadiusBL == other.cornerRadiusBL &&
                cornerRadiusBR == other.cornerRadiusBR && alpha == other.alpha &&
                left == other.left && top == other.top && right == other.right &&
                bottom == other.bottom && glassMode == other.glassMode &&
                glassDensity == other.glassDensity && advancedBlurTint == other.advancedBlurTint &&
                (advancedBlurShader == other.advancedBlurShader ||
                 getAdvancedBlurShader() == other.getAdvancedBlurShader());
    }

    inline bool operator!=(const BlurRegion& other) const { return !(*this == other); }
};

static inline void PrintTo(const BlurRegion& blurRegion, ::std::ostream* os) {
    *os << "BlurRegion {";
    *os << "\n    .blurRadius = " << blurRegion.blurRadius;
    *os << "\n    .cornerRadiusTL = " << blurRegion.cornerRadiusTL;
    *os << "\n    .cornerRadiusTR = " << blurRegion.cornerRadiusTR;
    *os << "\n    .cornerRadiusBL = " << blurRegion.cornerRadiusBL;
    *os << "\n    .cornerRadiusBR = " << blurRegion.cornerRadiusBR;
    *os << "\n    .alpha = " << blurRegion.alpha;
    *os << "\n    .left = " << blurRegion.left;
    *os << "\n    .top = " << blurRegion.top;
    *os << "\n    .right = " << blurRegion.right;
    *os << "\n    .bottom = " << blurRegion.bottom;
    *os << "\n    .glassMode = " << blurRegion.glassMode;
    *os << "\n    .glassDensity = " << blurRegion.glassDensity;
    *os << "\n}";
}

// copied from skia/src/core/SkBlurMask.cpp
inline float convertBlurUserRadiusToSigma(float radius) {
    return radius > 0 ? 0.57735f * radius + 0.5f : 0.0f;
}
// copied from skia/src/core/SkBlurEngine.h
inline float convertBlurSigmaToKernelRadius(float sigma) {
    return sigma <= 0.03f ? 0 : std::ceil(3.f * sigma);
}

} // namespace android

namespace std {
template <>
struct hash<android::BlurRegion> {
    size_t operator()(const android::BlurRegion& region) const {
        return android::hashCombine(region.blurRadius, region.cornerRadiusTL, region.cornerRadiusTR,
                                    region.cornerRadiusBL, region.cornerRadiusBR, region.alpha,
                                    region.left, region.top, region.right, region.bottom,
                                    region.glassMode, region.glassDensity, region.getAdvancedBlurShader(),
                                    region.advancedBlurTint);
    }
};
} // namespace std
