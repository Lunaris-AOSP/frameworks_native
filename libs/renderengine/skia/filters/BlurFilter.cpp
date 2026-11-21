/*
 * Copyright 2021 The Android Open Source Project
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

#define ATRACE_TAG ATRACE_TAG_GRAPHICS
#include "BlurFilter.h"
#include <SkBlendMode.h>
#include <SkColorFilter.h>
#include <SkCanvas.h>
#include <SkPaint.h>
#include <SkRRect.h>
#include <SkRuntimeEffect.h>
#include <SkSize.h>
#include <SkString.h>
#include <SkSurface.h>
#include <SkTileMode.h>
#include <common/trace.h>
#include <log/log.h>

#include "RuntimeEffectManager.h"
#include "AdvancedBlurShader.h"
#include <algorithm>
#include <cmath>

namespace android {
namespace renderengine {
namespace skia {

const SkString kEffectSource_BlurFilter_MixEffect(R"(
    uniform shader blurredInput;
    uniform shader originalInput;
    uniform float mixFactor;

    half4 main(float2 xy) {
        return half4(mix(originalInput.eval(xy), blurredInput.eval(xy), mixFactor)).rgb1;
    }
)");

const SkString kEffectSource_BlurFilter_GlassEffect(kAdvancedBlurSource);

static SkMatrix getShaderTransform(const SkCanvas* canvas, const SkRect& blurRect,
                                   const float scale, const float zoomScale) {
    // 1. Apply the blur shader matrix, which scales up the blurred surface to its real size
    auto matrix = SkMatrix::Scale(scale, scale);
    // 2. Since the blurred surface has the size of the layer, we align it with the
    // top left corner of the layer position.
    matrix.postConcat(SkMatrix::Translate(blurRect.fLeft, blurRect.fTop));
    // 3. Apply the "zoom" effect as an extra scale + translate around the center of the blur.
    if (zoomScale != 1.0f) {
        matrix.postScale(zoomScale, zoomScale);
        matrix.postTranslate(
                blurRect.width() * (1 - zoomScale) / 2.0f,
                blurRect.height() * (1 - zoomScale) / 2.0f);
    }
    // 4. Finally, apply the inverse canvas matrix. The snapshot made in the BlurFilter is in the
    // original surface orientation. The inverse matrix has to be applied to align the blur
    // surface with the current orientation/position of the canvas.
    SkMatrix drawInverse;
    if (canvas != nullptr && canvas->getTotalMatrix().invert(&drawInverse)) {
        matrix.postConcat(drawInverse);
    }
    return matrix;
}

BlurFilter::BlurFilter(RuntimeEffectManager& effectManager, const float maxCrossFadeRadius)
      : mMaxCrossFadeRadius(maxCrossFadeRadius),
        mMixEffect(effectManager.mKnownEffects[kBlurFilter_MixEffect]),
        mGlassEffect(effectManager.mKnownEffects[kBlurFilter_GlassEffect]) {}

float BlurFilter::getMaxCrossFadeRadius() const {
    return mMaxCrossFadeRadius;
}

sk_sp<SkShader> BlurFilter::makeBlurShader(const sk_sp<SkImage>& blurredImage,
                                           const SkMatrix& blurMatrix,
                                           const SkRect& /*blurRect*/) const {
    return blurredImage->makeShader(SkTileMode::kMirror, SkTileMode::kMirror,
                                    SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kNone),
                                    &blurMatrix);
}

const BlurFilter::PresetEffect& BlurFilter::presetEffect(const std::string& source) {
    auto found = mPresetEffects.find(source);
    if (found != mPresetEffects.end()) return found->second;
    if (mPresetEffects.size() >= 8) mPresetEffects.erase(mPresetEffects.begin());
    auto result = SkRuntimeEffect::MakeForShader(SkString(source.c_str()));
    PresetEffect entry;
    entry.effect = std::move(result.effect);
    if (entry.effect) {
        const auto* content = entry.effect->findChild("content");
        const auto* sharp = entry.effect->findChild("sharpContent");
        const auto* bounds = entry.effect->findUniform("axBounds");
        const auto* corners = entry.effect->findUniform("axCorners");
        const auto* density = entry.effect->findUniform("axDensity");
        const auto* radius = entry.effect->findUniform("radius");
        const auto* chrome = entry.effect->findUniform("chrome");
        using UniformType = SkRuntimeEffect::Uniform::Type;
        entry.sharpContent = sharp != nullptr;
        entry.radius = radius != nullptr;
        entry.chrome = chrome != nullptr;
        entry.valid = content && content->type == SkRuntimeEffect::ChildType::kShader
                && (!sharp || sharp->type == SkRuntimeEffect::ChildType::kShader)
                && entry.effect->children().size() == (sharp ? 2u : 1u)
                && entry.effect->uniforms().size() == 3u + (radius ? 1u : 0u) + (chrome ? 1u : 0u)
                && entry.effect->uniformSize() <= 64 && bounds && corners && density
                && bounds->type == UniformType::kFloat4 && bounds->count == 1
                && corners->type == UniformType::kFloat4 && corners->count == 1
                && density->type == UniformType::kFloat && density->count == 1
                && (!radius || (radius->type == UniformType::kFloat && radius->count == 1))
                && (!chrome || (chrome->type == UniformType::kFloat4 && chrome->count == 1
                                && chrome->isColor()));
    } else {
        ALOGW("Advanced blur shader compilation failed: %s", result.errorText.c_str());
    }
    return mPresetEffects.emplace(source, std::move(entry)).first->second;
}

void BlurFilter::drawBlurRegion(SkCanvas* canvas, const SkRRect& effectRegion,
                                const uint32_t blurRadius, const float zoomScale,
                                const float blurAlpha,
                                const SkRect& blurRect, sk_sp<SkImage> blurredImage,
                                sk_sp<SkImage> input, uint32_t glassMode, float glassDensity,
                                const std::string& advancedBlurShader, uint32_t advancedBlurTint) {
    SFTRACE_CALL();

    SkPaint paint;
    paint.setAlphaf(blurAlpha);

    auto blurMatrix = getShaderTransform(canvas, blurRect, kInverseInputScale, zoomScale);

    SkSamplingOptions linearSampling(SkFilterMode::kLinear, SkMipmapMode::kNone);
    const auto blurShader = blurRadius > 0 ? makeBlurShader(blurredImage, blurMatrix, blurRect) : nullptr;
    sk_sp<SkShader> sharpShader;
    auto makeSharpShader = [&]() -> sk_sp<SkShader> {
        if (sharpShader || !input) return sharpShader;
        SkMatrix inputMatrix;
        if (!canvas->getTotalMatrix().invert(&inputMatrix)) return nullptr;
        if (zoomScale != 1.0f) {
            inputMatrix.preTranslate(blurRect.width() * (1 - zoomScale) / 2.0f,
                                     blurRect.height() * (1 - zoomScale) / 2.0f);
            inputMatrix.preScale(zoomScale, zoomScale);
        }
        sharpShader = input->makeShader(SkTileMode::kClamp, SkTileMode::kClamp,
                                        linearSampling, inputMatrix);
        return sharpShader;
    };

    if (blurRadius == 0 || blurRadius < mMaxCrossFadeRadius) {
        LOG_ALWAYS_FATAL_IF(!input);

        // For sampling Skia's API expects the inverse of what logically seems appropriate. In this
        // case you might expect the matrix to simply be the canvas matrix.
        SkMatrix inputMatrix;
        if (!canvas->getTotalMatrix().invert(&inputMatrix)) {
            ALOGE("matrix was unable to be inverted");
        }
        if (zoomScale != 1.0f) {
            inputMatrix.preTranslate(
                    blurRect.width() * (1 - zoomScale) / 2.0f,
                    blurRect.height() * (1 - zoomScale) / 2.0f);
            inputMatrix.preScale(zoomScale, zoomScale);
        }

        auto originalShader = input->makeShader(SkTileMode::kMirror, SkTileMode::kMirror,
                                                linearSampling, inputMatrix);
        if (blurRadius == 0) {
            paint.setShader(std::move(originalShader));
        } else {
            SkRuntimeShaderBuilder blurBuilder(mMixEffect);
            blurBuilder.child("blurredInput") = blurShader;
            blurBuilder.child("originalInput") = std::move(originalShader);
            blurBuilder.uniform("mixFactor") = blurRadius / mMaxCrossFadeRadius;
            paint.setShader(blurBuilder.makeShader());
        }
    } else {
        paint.setShader(blurShader);
    }

    if ((glassMode == 3 || glassMode == 4) && !advancedBlurShader.empty()
            && advancedBlurShader.size() <= 32768) {
        const auto& entry = presetEffect(advancedBlurShader);
        bool appliedMaterial = false;
        if (entry.valid && entry.sharpContent == (glassMode == 4)
                && (!entry.sharpContent || makeSharpShader())) {
            const auto& bounds = effectRegion.rect();
            SkRuntimeShaderBuilder preset(entry.effect);
            preset.child("content") = paint.refShader();
            if (entry.sharpContent) preset.child("sharpContent") = sharpShader;
            preset.uniform("axBounds") = SkV4{bounds.left(), bounds.top(),
                                               bounds.right(), bounds.bottom()};
            preset.uniform("axCorners") = SkV4{
                    effectRegion.radii(SkRRect::kUpperLeft_Corner).x(),
                    effectRegion.radii(SkRRect::kUpperRight_Corner).x(),
                    effectRegion.radii(SkRRect::kLowerRight_Corner).x(),
                    effectRegion.radii(SkRRect::kLowerLeft_Corner).x()};
            preset.uniform("axDensity") = std::isfinite(glassDensity) && glassDensity > 0.f
                    ? std::clamp(glassDensity, 0.01f, 16.f) : 1.f;
            if (entry.radius) {
                preset.uniform("radius") = effectRegion.radii(SkRRect::kUpperLeft_Corner).x();
            }
            if (entry.chrome) {
                preset.uniform("chrome") = SkV4{
                        ((advancedBlurTint >> 16) & 255) / 255.f,
                        ((advancedBlurTint >> 8) & 255) / 255.f,
                        (advancedBlurTint & 255) / 255.f,
                        ((advancedBlurTint >> 24) & 255) / 255.f};
            }
            auto shader = preset.makeShader();
            if (shader) {
                paint.setShader(std::move(shader));
                appliedMaterial = true;
            }
        }
        if (!appliedMaterial) {
            paint.setColorFilter(SkColorFilters::Blend(advancedBlurTint, SkBlendMode::kSrcOver));
        }
    }

    if (glassMode == 1 || glassMode == 2) {
        const auto& bounds = effectRegion.rect();
        SkRuntimeShaderBuilder glass(mGlassEffect);
        glass.child("content") = paint.refShader();
        glass.uniform("bounds") = SkV4{bounds.left(), bounds.top(), bounds.right(), bounds.bottom()};
        glass.uniform("cornerRadii") = SkV4{
                effectRegion.radii(SkRRect::kUpperLeft_Corner).x(),
                effectRegion.radii(SkRRect::kUpperRight_Corner).x(),
                effectRegion.radii(SkRRect::kLowerRight_Corner).x(),
                effectRegion.radii(SkRRect::kLowerLeft_Corner).x()};
        glass.uniform("density") = std::isfinite(glassDensity) && glassDensity > 0.f
                ? std::clamp(glassDensity, 0.01f, 16.f) : 1.f;
        glass.uniform("dark") = glassMode == 1 ? 1.f : 0.f;
        paint.setShader(glass.makeShader());
        SkRect drawBounds = bounds;
        drawBounds.outset(1.f, 1.f);
        canvas->drawRect(drawBounds, paint);
        return;
    }

    if (effectRegion.isRect()) {
        if (blurAlpha == 1.0f && glassMode != 3 && glassMode != 4) {
            paint.setBlendMode(SkBlendMode::kSrc);
        }
        canvas->drawRect(effectRegion.rect(), paint);
    } else {
        paint.setAntiAlias(true);
        canvas->drawRRect(effectRegion, paint);
    }
}

} // namespace skia
} // namespace renderengine
} // namespace android
