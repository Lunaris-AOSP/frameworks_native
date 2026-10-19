/*
 * Copyright 2026 The Android Open Source Project
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

#include <compositionengine/impl/ClientCompositionRequestCache.h>
#include <gtest/gtest.h>

namespace android::compositionengine::impl {
namespace {

class ClientCompositionRequestCacheTest : public testing::Test {
protected:
    void SetUp() override {
        BlurRegion region{};
        region.blurRadius = 30;
        region.alpha = 1.0f;
        region.right = 100;
        region.bottom = 100;
        mLayers[0].blurRegions.push_back(region);
        mCache.add(kBufferId, mDisplay, mLayers);
    }

    static constexpr uint64_t kBufferId = 1;
    ClientCompositionRequestCache mCache{2};
    renderengine::DisplaySettings mDisplay;
    std::vector<LayerFE::LayerSettings> mLayers{1};
};

TEST_F(ClientCompositionRequestCacheTest, unchangedBlurRegionsReuseComposition) {
    EXPECT_TRUE(mCache.exists(kBufferId, mDisplay, mLayers));
}

TEST_F(ClientCompositionRequestCacheTest, changingBlurRadiusInvalidatesComposition) {
    mLayers[0].blurRegions[0].blurRadius++;
    EXPECT_FALSE(mCache.exists(kBufferId, mDisplay, mLayers));
}

TEST_F(ClientCompositionRequestCacheTest, movingBlurRegionInvalidatesComposition) {
    mLayers[0].blurRegions[0].left++;
    EXPECT_FALSE(mCache.exists(kBufferId, mDisplay, mLayers));
}

TEST_F(ClientCompositionRequestCacheTest, changingBlurAlphaInvalidatesComposition) {
    mLayers[0].blurRegions[0].alpha = 0.5f;
    EXPECT_FALSE(mCache.exists(kBufferId, mDisplay, mLayers));
}

TEST_F(ClientCompositionRequestCacheTest, changingBlurCornersInvalidatesComposition) {
    mLayers[0].blurRegions[0].cornerRadiusTL = 10.0f;
    EXPECT_FALSE(mCache.exists(kBufferId, mDisplay, mLayers));
}

TEST_F(ClientCompositionRequestCacheTest, removingBlurRegionsInvalidatesComposition) {
    mLayers[0].blurRegions.clear();
    EXPECT_FALSE(mCache.exists(kBufferId, mDisplay, mLayers));
    mCache.add(kBufferId, mDisplay, mLayers);
    EXPECT_TRUE(mCache.exists(kBufferId, mDisplay, mLayers));
}

} // namespace
} // namespace android::compositionengine::impl
