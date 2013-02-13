/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_EFFECTS_H
#define MOZILLA_LAYERS_EFFECTS_H

#include "mozilla/gfx/Matrix.h"

namespace mozilla {
namespace layers {

enum EffectTypes
{
  EFFECT_MASK,
  EFFECT_PRIMARY,
  EFFECT_BGRX,
  EFFECT_RGBX,
  EFFECT_BGRA,
  EFFECT_RGBA,
  EFFECT_RGBA_EXTERNAL,
  EFFECT_YCBCR,
  EFFECT_COMPONENT_ALPHA,
  EFFECT_SOLID_COLOR,
  EFFECT_RENDER_TARGET,
  EFFECT_MAX
};

struct Effect : public RefCounted<Effect>
{
  Effect(EffectTypes aType) : mType(aType) {}

  virtual void Composite() = 0;

  EffectTypes mType;
};

struct TexturedEffect : public Effect
{
  TexturedEffect(EffectTypes aType) : Effect(aType),
    mTextureCoords(0, 0, 1.0f, 1.0f) {}

  gfx::Rect mTextureCoords;
};

struct EffectMask : public Effect
{
  EffectMask(TextureSource *aMaskTexture,
             gfx::IntSize aSize,
             const gfx::Matrix4x4 &aMaskTransform)
    : Effect(EFFECT_MASK), mMaskTexture(aMaskTexture)
    , mIs3D(false)
    , mSize(aSize)
    , mMaskTransform(aMaskTransform)
  {}

  virtual void Composite() MOZ_OVERRIDE
  {
    MOZ_ASSERT(false, "Can't composite a mask without a primary effect.");
  }

  TextureSource* mMaskTexture;
  bool mIs3D;
  gfx::IntSize mSize;
  gfx::Matrix4x4 mMaskTransform;
};

struct EffectRenderTarget : public Effect
{
  EffectRenderTarget(CompositingRenderTarget *aRenderTarget)
    : Effect(EFFECT_RENDER_TARGET), mRenderTarget(aRenderTarget)
  {}

  RefPtr<CompositingRenderTarget> mRenderTarget;
};

struct EffectBGRX : public TexturedEffect
{
  EffectBGRX(TextureSource *aBGRXTexture,
             bool aPremultiplied,
             mozilla::gfx::Filter aFilter,
             bool aFlipped = false)
    : TexturedEffect(EFFECT_BGRX), mBGRXTexture(aBGRXTexture)
    , mPremultiplied(aPremultiplied), mFilter(aFilter)
    , mFlipped(aFlipped)
  {}

  TextureSource* mBGRXTexture;
  bool mPremultiplied;
  mozilla::gfx::Filter mFilter;
  bool mFlipped;
};

struct EffectRGBX : public TexturedEffect
{
  EffectRGBX(TextureSource *aRGBXTexture,
             bool aPremultiplied,
             mozilla::gfx::Filter aFilter,
             bool aFlipped = false)
    : TexturedEffect(EFFECT_RGBX), mRGBXTexture(aRGBXTexture)
    , mPremultiplied(aPremultiplied), mFilter(aFilter)
    , mFlipped(aFlipped)
  {}

  TextureSource* mRGBXTexture;
  bool mPremultiplied;
  mozilla::gfx::Filter mFilter;
  bool mFlipped;
};

struct EffectBGRA : public TexturedEffect
{
  EffectBGRA(TextureSource *aBGRATexture,
             bool aPremultiplied,
             mozilla::gfx::Filter aFilter,
             bool aFlipped = false)
    : TexturedEffect(EFFECT_BGRA), mBGRATexture(aBGRATexture)
    , mPremultiplied(aPremultiplied), mFilter(aFilter)
    , mFlipped(aFlipped)
  {}

  TextureSource* mBGRATexture;
  bool mPremultiplied;
  mozilla::gfx::Filter mFilter;
  bool mFlipped;
};

struct EffectRGBA : public TexturedEffect
{
  EffectRGBA(TextureSource *aRGBATexture,
             bool aPremultiplied,
             mozilla::gfx::Filter aFilter,
             bool aFlipped = false)
    : TexturedEffect(EFFECT_RGBA), mRGBATexture(aRGBATexture)
    , mPremultiplied(aPremultiplied), mFilter(aFilter)
    , mFlipped(aFlipped)
  {}

  TextureSource* mRGBATexture;
  bool mPremultiplied;
  mozilla::gfx::Filter mFilter;
  bool mFlipped;
};

struct EffectRGBAExternal : public TexturedEffect
{
  EffectRGBAExternal(TextureSource *aRGBATexture,
                     const gfx::Matrix4x4 &aTextureTransform,
                     bool aPremultiplied,
                     mozilla::gfx::Filter aFilter,
                     bool aFlipped = false)
    : TexturedEffect(EFFECT_RGBA_EXTERNAL), mRGBATexture(aRGBATexture)
    , mTextureTransform(aTextureTransform), mPremultiplied(aPremultiplied)
    , mFilter(aFilter), mFlipped(aFlipped)
  {}

  TextureSource* mRGBATexture;
  gfx::Matrix4x4 mTextureTransform;
  bool mPremultiplied;
  mozilla::gfx::Filter mFilter;
  bool mFlipped;
};

struct EffectYCbCr : public TexturedEffect
{
  EffectYCbCr(TextureSource *aSource, mozilla::gfx::Filter aFilter)
    : TexturedEffect(EFFECT_YCBCR), mYCbCrTexture(aSource)
    , mFilter(aFilter)
  {}

  TextureSource* mYCbCrTexture;
  mozilla::gfx::Filter mFilter;
};

struct EffectComponentAlpha : public TexturedEffect
{
  EffectComponentAlpha(TextureSource *aOnWhite, TextureSource *aOnBlack)
    : TexturedEffect(EFFECT_COMPONENT_ALPHA), mOnWhite(aOnWhite), mOnBlack(aOnBlack)
  {}

  TextureSource* mOnWhite;
  TextureSource* mOnBlack;
};

struct EffectSolidColor : public Effect
{
  EffectSolidColor(const mozilla::gfx::Color &aColor)
    : Effect(EFFECT_SOLID_COLOR), mColor(aColor)
  {}

  mozilla::gfx::Color mColor;
};

struct EffectChain
{
  RefPtr<Effect> mPrimaryEffect;
  RefPtr<Effect> mSecondaryEffects[EFFECT_PRIMARY];
};


} // namespace
} // namespace

#endif
