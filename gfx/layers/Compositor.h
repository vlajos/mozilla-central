/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_COMPOSITOR_H
#define MOZILLA_GFX_COMPOSITOR_H

#include "mozilla/RefPtr.h"
#include "mozilla/gfx/Rect.h"
#include "mozilla/gfx/Matrix.h"
#include "gfxMatrix.h"
#include "nsAutoPtr.h"
#include "nsRegion.h"
#include "LayersTypes.h"

class gfxContext;
class gfxASurface;
class gfxImageSurface;
class nsIWidget;

namespace mozilla {
namespace gfx {
class DrawTarget;
}

namespace layers {

class Compositor;
struct Effect;
struct EffectChain;
class SharedImage;
class Image;
class ISurfaceDeAllocator;
class BufferHost;
class TextureHost;
class SurfaceDescriptor;

enum TextureFormat
{
  TEXTUREFORMAT_BGRX32,
  TEXTUREFORMAT_BGRA32,
  TEXTUREFORMAT_BGR16,
  TEXTUREFORMAT_Y8
};


enum BufferType
{
  BUFFER_UNKNOWN,
  BUFFER_YUV,
  BUFFER_YCBCR,
  BUFFER_DIRECT_EXTERNAL,
  BUFFER_SHARED,
  BUFFER_TEXTURE,
  BUFFER_BRIDGE,
  BUFFER_CONTENT,
  BUFFER_CONTENT_DIRECT,
  BUFFER_DIRECT
};

enum TextureHostType
{
  TEXTURE_UNKNOWN,
  TEXTURE_SHMEM,
  TEXTURE_SHARED,
  TEXTURE_SHARED_GL,
  TEXTURE_BRIDGE
};

typedef uint32_t TextureFlags;
const TextureFlags NoFlags            = 0x0;
const TextureFlags UseNearestFilter   = 0x1;
const TextureFlags NeedsYFlip         = 0x2;
const TextureFlags ForceSingleTile    = 0x4;
const TextureFlags UseOpaqueSurface   = 0x8;
const TextureFlags AllowRepeat        = 0x10;


/**
 * Sent from the compositor to the drawing LayerManager, includes properties
 * of the compositor and should (in the future) include information (BufferType)
 * about what kinds of buffer and texture clients to create.
 */
struct TextureFactoryIdentifier
{
  LayersBackend mParentBackend;
  int32_t mMaxTextureSize;
};

/**
 * Identifies a texture client/host pair and their type. Sent with updates
 * from a drawing layers to a compositing layer, it should be passed directly
 * to the BufferHost. How the identifier is used depends on the buffer
 * client/host pair.
 */
struct TextureIdentifier
{
  BufferType mBufferType;
  TextureHostType mTextureType;
  uint64_t mDescriptor;
};

static bool operator==(const TextureIdentifier& aLeft, const TextureIdentifier& aRight)
{
  return aLeft.mBufferType == aRight.mBufferType &&
         aLeft.mTextureType == aRight.mTextureType &&
         aLeft.mDescriptor == aRight.mDescriptor;
}

class Texture : public RefCounted<Texture>
{
public:
  /* aRegion is the region of the Texture to upload to. aData is a pointer to the
   * top-left of the bound of the region to be uploaded. If the compositor that
   * created this texture does not support partial texture upload, aRegion must be
   * equal to this Texture's rect.
   */
  virtual void
    UpdateTexture(const nsIntRegion& aRegion, int8_t *aData, uint32_t aStride) = 0;
  virtual ~Texture() {}
};

// a texture or part of texture used for compositing
class TextureSource
{
public:
  virtual void AddRef() = 0;
  virtual void Release() = 0;
  virtual gfx::IntSize GetSize() const = 0;

  virtual ~TextureSource() {};
};

/**
 * A view on a texture host where the texture host is internally represented as tiles
 * (contrast with a tiled buffer, where each texture is a tile). For iteration by
 * the texture's buffer host.
 */
class TileIterator
{
public:
  virtual void BeginTileIteration() = 0;
  virtual nsIntRect GetTileRect() = 0;
  virtual size_t GetTileCount() = 0;
  virtual bool NextTile() = 0;
};

// Interface, used only on the compositor process
class TextureHost
{
public:
  TextureHost() : mFlags(NoFlags), mIsBuffered(false) {}
  virtual ~TextureHost() {}

  virtual void AddRef() = 0;
  virtual void Release() = 0;

  bool IsBuffered() const { return mIsBuffered; }

  /**
   * Update the texture host from a SharedImage, aResult may contain the old
   * content of the texture, a pointer to the new image, or null. The
   * texture client should know what to expect
   */
  void Update(const SharedImage& aImage,
              SharedImage* aResult = nullptr,
              bool* aIsInitialised = nullptr,
              bool* aNeedsReset = nullptr) {
    UpdateImpl(aImage, aResult, aIsInitialised, aNeedsReset);
  }

  virtual void UpdateImpl(const SharedImage& aImage,
                          SharedImage* aResult = nullptr,
                          bool* aIsInitialised = nullptr,
                          bool* aNeedsReset = nullptr) {}

  /**
   * Updates a region of the texture host from aSurface
   */
  void Update(gfxASurface* aSurface, nsIntRegion& aRegion) {
    UpdateImpl(aSurface, aRegion);
  }

  virtual void UpdateImpl(gfxASurface* aSurface, nsIntRegion& aRegion) {}

  /**
   * Lock the texture host for compositing, returns an effect that should
   * be used to composite this texture.
   */
  virtual Effect* Lock(const gfx::Filter& aFilter) { return nullptr; }
  // Unlock the texture host after compositing
  virtual void Unlock() {}

  /**
   * Leave the texture host in a sane state if we abandon an update part-way
   * through.
   */
  virtual void Abort() {}

  void SetFlags(TextureFlags aFlags) { mFlags = aFlags; }
  void AddFlag(TextureFlags aFlag) { mFlags |= aFlag; }
  TextureFlags GetFlags() { return mFlags; }

//  virtual gfx::IntSize GetSize() const = 0;

  virtual void SetDeAllocator(ISurfaceDeAllocator* aDeAllocator) {}

  virtual TileIterator* GetAsTileIterator() { return nullptr; }
  virtual TextureSource* GetAsTextureSource() { return nullptr; }

#ifdef MOZ_DUMP_PAINTING
  virtual already_AddRefed<gfxImageSurface> Dump() { return nullptr; }
#endif
protected:
  TextureFlags mFlags;
  bool mIsBuffered;
  //SharedImage mBuffer;
};

/**
 * This can be used as an offscreen rendering target by the compositor, and
 * subsequently can be used as a source by the compositor.
 */
class Surface : public RefCounted<Surface>
{
public:
  virtual ~Surface() {}

#ifdef MOZ_DUMP_PAINTING
  virtual already_AddRefed<gfxImageSurface> Dump(Compositor* aCompositor) { return nullptr; }
#endif
};

enum SurfaceInitMode
{
  INIT_MODE_NONE,
  INIT_MODE_CLEAR,
  INIT_MODE_COPY
};

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
  EFFECT_SURFACE,
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

struct EffectSurface : public Effect
{
  EffectSurface(Surface *aSurface)
    : Effect(EFFECT_SURFACE), mSurface(aSurface)
  {}

  RefPtr<Surface> mSurface;
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

class Compositor : public RefCounted<Compositor>
{
public:
  Compositor()
    : mCompositorID(0)
  {}
  virtual ~Compositor() {}

  virtual bool Initialize() = 0;
  virtual void Destroy() = 0;

  /* Request a texture host identifier that may be used for creating textures
   * accross process or thread boundaries that are compatible with this
   * compositor.
   */
  virtual TextureFactoryIdentifier
    GetTextureFactoryIdentifier() = 0;

  /**
   * Properties of the compositor
   */
  virtual bool CanUseCanvasLayerForSize(const gfxIntSize &aSize) = 0;
  virtual int32_t GetMaxTextureSize() const = 0;

  /**
   * Set the target for rendering, intended to be used for the duration of a transaction
   */
  virtual void SetTarget(gfxContext *aTarget) = 0;

  virtual void MakeCurrent(bool aForce = false) = 0;

  /* This creates a texture based on an in-memory bitmap.
   */
  virtual TemporaryRef<Texture>
    CreateTextureForData(const gfx::IntSize &aSize, PRInt8 *aData, uint32_t aStride,
                         TextureFormat aFormat) = 0;

  /**
   * Create a new texture host of a kind specified by aIdentifier
   */
  virtual TemporaryRef<TextureHost>
    CreateTextureHost(const TextureIdentifier &aIdentifier, TextureFlags aFlags) = 0;

  /**
   * Create a new buffer host of a kind specified by aType
   */
  virtual TemporaryRef<BufferHost> 
    CreateBufferHost(BufferType aType) = 0;

  /**
   * return a TextureIdentifier to be used in a fallback situation for aId
   */
  virtual TextureIdentifier FallbackIdentifier(const TextureIdentifier& aId)
  {
    return aId;
  }

  /**
   * This creates a Surface that can be used as a rendering target by this
   * compositor.
   */
  virtual TemporaryRef<Surface> CreateSurface(const gfx::IntRect &aRect,
                                              SurfaceInitMode aInit) = 0;

  /**
   * This creates a Surface that can be used as a rendering target by this compositor,
   * and initializes this surface by copying from the given surface. If the given surface
   * is nullptr, the screen frame in progress is used as the source.
   */
  virtual TemporaryRef<Surface> CreateSurfaceFromSurface(const gfx::IntRect &aRect,
                                                         const Surface *aSource) = 0;

  /**
   * Sets the given surface as the target for subsequent calls to DrawQuad.
   * Passing nullptr as aSurface sets the screen as the target.
   */
  virtual void SetSurfaceTarget(Surface *aSurface) = 0;

  /**
   * Mostly the compositor will pull the size from a widget and this will
   * be ignored, but compositor implementations are free to use it if they
   * like.
   */
  virtual void SetSurfaceSize(int aWidth, int aHeight) = 0;

  /**
   * This tells the compositor to actually draw a quad, where the area is
   * specified in userspace, and the source rectangle is the area of the
   * currently set textures to sample from. This area may not refer directly
   * to pixels depending on the effect.
   */
  virtual void DrawQuad(const gfx::Rect &aRect, const gfx::Rect *aSourceRect,
                        const gfx::Rect *aTextureRect, const gfx::Rect *aClipRect,
                        const EffectChain &aEffectChain,
                        gfx::Float aOpacity, const gfx::Matrix4x4 &aTransform,
                        const gfx::Point &aOffset) = 0;

  /**
   * Start a new frame. If aClipRectIn is null, sets *aClipRectOut to the screen dimensions. 
   */
  virtual void BeginFrame(const gfx::Rect *aClipRectIn, const gfxMatrix& aTransform,
                          gfx::Rect *aClipRectOut = nullptr) = 0;

  /**
   * Flush the current frame to the screen.
   */
  virtual void EndFrame(const gfxMatrix& aTransform) = 0;

  /**
   * Tidy up if BeginFrame has been called, but EndFrame won't be
   */
  virtual void AbortFrame() = 0;

  /**
   * Setup the viewport and projection matrix for rendering
   * to a window of the given dimensions.
   */
  virtual void PrepareViewport(int aWidth, int aHeight, const gfxMatrix& aWorldTransform) = 0;

  // save the current viewport
  virtual void SaveViewport() = 0;
  // resotre the previous viewport and return its bounds
  virtual gfx::IntRect RestoreViewport() = 0;

  /* Whether textures created by this compositor can receive partial updates.
   */
  virtual bool SupportsPartialTextureUpdate() = 0;

#ifdef MOZ_DUMP_PAINTING
  virtual const char* Name() const = 0;
#endif // MOZ_DUMP_PAINTING

  // these methods refer to the ID of the Compositor in terms of the Compositor
  // IPDL protocol and CompositorParent
  void SetCompositorID(uint32_t aID)
  {
    NS_ASSERTION(mCompositorID==0, "The compositor ID must be set only once.");
    mCompositorID = aID;
  }
  uint32_t GetCompositorID() const
  {
    return mCompositorID;
  }

  virtual void NotifyShadowTreeTransaction() = 0;

  /**
   * Notify the compositor that composition is being paused/resumed.
   */
  virtual void Pause() {}
  virtual void Resume() {}

protected:
  uint32_t mCompositorID;
};

class TextureClient;
class ImageClient;
class CanvasClient;
class ContentClient;
class ShadowLayerForwarder;
class ShadowableLayer;

class CompositingFactory
{
public:
  /**
   * The Create*Client methods each create, configure, and return a new buffer
   * or texture client. If necessary, a message will be sent to the compositor
   * to create a corresponding buffer or texture host.
   */
  static TemporaryRef<ImageClient> CreateImageClient(LayersBackend aBackendType,
                                                     BufferType aImageHostType,
                                                     ShadowLayerForwarder* aLayerForwarder,
                                                     ShadowableLayer* aLayer,
                                                     TextureFlags aFlags);
  static TemporaryRef<CanvasClient> CreateCanvasClient(LayersBackend aBackendType,
                                                       BufferType aImageHostType,
                                                       ShadowLayerForwarder* aLayerForwarder,
                                                       ShadowableLayer* aLayer,
                                                       TextureFlags aFlags);
  static TemporaryRef<ContentClient> CreateContentClient(LayersBackend aBackendType,
                                                         BufferType aImageHostType,
                                                         ShadowLayerForwarder* aLayerForwarder,
                                                         ShadowableLayer* aLayer,
                                                         TextureFlags aFlags);
  static TemporaryRef<TextureClient> CreateTextureClient(LayersBackend aBackendType,
                                                         TextureHostType aTextureHostType,
                                                         BufferType aImageHostType,
                                                         ShadowLayerForwarder* aLayerForwarder,
                                                         bool aStrict = false); 

  static BufferType TypeForImage(Image* aImage);
};

}
}

#endif /* MOZILLA_GFX_COMPOSITOR_H */
