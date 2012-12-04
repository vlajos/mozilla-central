
#ifndef MOZILLA_LAYERS_EFFECTS_H
#define MOZILLA_LAYERS_EFFECTS_H

namespace mozilla {
namespace layers {

enum EffectTypes
{
  EFFECT_BGRX,
  EFFECT_RGBX,
  EFFECT_BGRA,
  EFFECT_RGB,
  EFFECT_RGBA,
  EFFECT_RGBA_EXTERNAL,
  EFFECT_YCBCR,
  EFFECT_COMPONENT_ALPHA,
  EFFECT_SOLID_COLOR,
  EFFECT_MASK,
  EFFECT_RENDER_TARGET,
  EFFECT_MAX
};

struct Effect : public RefCounted<Effect>
{
  Effect(uint32_t aType) : mType(aType) {}

  uint32_t mType;
};

struct EffectMask : public Effect
{
  EffectMask(TextureSource *aMaskTexture,
             const gfx::Matrix4x4 &aMaskTransform)
    : Effect(EFFECT_MASK), mMaskTexture(aMaskTexture)
    , mIs3D(false)
    , mMaskTransform(aMaskTransform)
  {}

  RefPtr<TextureSource> mMaskTexture;
  bool mIs3D;
  gfx::Matrix4x4 mMaskTransform;
};

struct EffectRenderTarget : public Effect
{
  EffectRenderTarget(CompositingRenderTarget *aRenderTarget)
    : Effect(EFFECT_RENDER_TARGET), mRenderTarget(aRenderTarget)
  {}

  RefPtr<CompositingRenderTarget> mRenderTarget;
};

struct EffectBGRX : public Effect
{
  EffectBGRX(TextureSource *aBGRXTexture,
             bool aPremultiplied,
             mozilla::gfx::Filter aFilter,
             bool aFlipped = false)
    : Effect(EFFECT_BGRX), mBGRXTexture(aBGRXTexture)
    , mPremultiplied(aPremultiplied), mFilter(aFilter)
    , mFlipped(aFlipped)
  {}

  RefPtr<TextureSource> mBGRXTexture;
  bool mPremultiplied;
  mozilla::gfx::Filter mFilter;
  bool mFlipped;
};

struct EffectRGBX : public Effect
{
  EffectRGBX(TextureSource *aRGBXTexture,
             bool aPremultiplied,
             mozilla::gfx::Filter aFilter,
             bool aFlipped = false)
    : Effect(EFFECT_RGBX), mRGBXTexture(aRGBXTexture)
    , mPremultiplied(aPremultiplied), mFilter(aFilter)
    , mFlipped(aFlipped)
  {}

  RefPtr<TextureSource> mRGBXTexture;
  bool mPremultiplied;
  mozilla::gfx::Filter mFilter;
  bool mFlipped;
};

struct EffectBGRA : public Effect
{
  EffectBGRA(TextureSource *aBGRATexture,
             bool aPremultiplied,
             mozilla::gfx::Filter aFilter,
             bool aFlipped = false)
    : Effect(EFFECT_BGRA), mBGRATexture(aBGRATexture)
    , mPremultiplied(aPremultiplied), mFilter(aFilter)
    , mFlipped(aFlipped)
  {}

  RefPtr<TextureSource> mBGRATexture;
  bool mPremultiplied;
  mozilla::gfx::Filter mFilter;
  bool mFlipped;
};

struct EffectRGB : public Effect
{
  EffectRGB(TextureSource *aRGBTexture,
            bool aPremultiplied,
            mozilla::gfx::Filter aFilter,
            bool aFlipped = false)
    : Effect(EFFECT_RGB), mRGBTexture(aRGBTexture)
    , mPremultiplied(aPremultiplied), mFilter(aFilter)
    , mFlipped(aFlipped)
  {}

  RefPtr<TextureSource> mRGBTexture;
  bool mPremultiplied;
  mozilla::gfx::Filter mFilter;
  bool mFlipped;
};

struct EffectRGBA : public Effect
{
  EffectRGBA(TextureSource *aRGBATexture,
             bool aPremultiplied,
             mozilla::gfx::Filter aFilter,
             bool aFlipped = false)
    : Effect(EFFECT_RGBA), mRGBATexture(aRGBATexture)
    , mPremultiplied(aPremultiplied), mFilter(aFilter)
    , mFlipped(aFlipped)
  {}

  RefPtr<TextureSource> mRGBATexture;
  bool mPremultiplied;
  mozilla::gfx::Filter mFilter;
  bool mFlipped;
};

struct EffectRGBAExternal : public Effect
{
  EffectRGBAExternal(TextureSource *aRGBATexture,
                     const gfx::Matrix4x4 &aTextureTransform,
                     bool aPremultiplied,
                     mozilla::gfx::Filter aFilter,
                     bool aFlipped = false)
    : Effect(EFFECT_RGBA_EXTERNAL), mRGBATexture(aRGBATexture)
    , mTextureTransform(aTextureTransform), mPremultiplied(aPremultiplied)
    , mFilter(aFilter), mFlipped(aFlipped)
  {}

  RefPtr<TextureSource> mRGBATexture;
  gfx::Matrix4x4 mTextureTransform;
  bool mPremultiplied;
  mozilla::gfx::Filter mFilter;
  bool mFlipped;
};

struct EffectYCbCr : public Effect
{
  EffectYCbCr(TextureSource *aY, TextureSource *aCb, TextureSource *aCr,
              mozilla::gfx::Filter aFilter)
    : Effect(EFFECT_YCBCR), mY(aY), mCb(aCb), mCr(aCr)
    , mFilter(aFilter)
  {}

  RefPtr<TextureSource> mY;
  RefPtr<TextureSource> mCb;
  RefPtr<TextureSource> mCr;
  mozilla::gfx::Filter mFilter;
};

struct EffectComponentAlpha : public Effect
{
  EffectComponentAlpha(TextureSource *aOnWhite, TextureSource *aOnBlack)
    : Effect(EFFECT_COMPONENT_ALPHA), mOnWhite(aOnWhite), mOnBlack(aOnBlack)
  {}

  RefPtr<TextureSource> mOnWhite;
  RefPtr<TextureSource> mOnBlack;
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
  // todo - define valid grammar
  RefPtr<Effect> mEffects[EFFECT_MAX];
};


} // namespace
} // namespace

#endif
