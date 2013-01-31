/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_CONTENTHOST_H
#define GFX_CONTENTHOST_H

#include "ThebesLayerBuffer.h"
#include "CompositableHost.h"
#include "BasicTiledThebesLayer.h" // for BasicTiledLayerBuffer

namespace mozilla {
namespace layers {

// Some properties of a Layer required for tiling
struct TiledLayerProperties
{
  nsIntRegion mVisibleRegion;
  gfxRect mDisplayPort;
  gfxSize mEffectiveResolution;
  gfxRect mCompositionBounds;
  bool mRetainTiles;
};

class ThebesBuffer;
class OptionalThebesBuffer;

// Base class for Thebes layer buffers, used by main thread and off-main thread
// compositing. This is the only place we use *Hosts/*Clients for main thread
// compositing.
class CompositingThebesLayerBuffer
{
  NS_INLINE_DECL_REFCOUNTING(CompositingThebesLayerBuffer)
public:
  typedef ThebesLayerBuffer::ContentType ContentType;
  typedef ThebesLayerBuffer::PaintState PaintState;

  CompositingThebesLayerBuffer(Compositor* aCompositor)
    : mPaintWillResample(false)
    , mInitialised(true)
    , mCompositor(aCompositor)
  {}
  virtual ~CompositingThebesLayerBuffer() {}

  virtual PaintState BeginPaint(ContentType aContentType,
                                uint32_t aFlags) = 0;

  void Composite(EffectChain& aEffectChain,
                 float aOpacity,
                 const gfx::Matrix4x4& aTransform,
                 const gfx::Point& aOffset,
                 const gfx::Filter& aFilter,
                 const gfx::Rect& aClipRect,
                 const nsIntRegion* aVisibleRegion = nullptr);

  void SetPaintWillResample(bool aResample) { mPaintWillResample = aResample; }

protected:
  virtual nsIntPoint GetOriginOffset() = 0;

  bool PaintWillResample() { return mPaintWillResample; }

  bool mPaintWillResample;
  bool mInitialised;
  RefPtr<Compositor> mCompositor;
  RefPtr<TextureHost> mTextureHost;
  RefPtr<TextureHost> mTextureHostOnWhite;
};

// Base class for OMTC Thebes buffers
class AContentHost : public CompositableHost
{
public:
  /**
   * Update the content host.
   * aTextureInfo identifies the texture host which should be updated.
   * aNewBack is the new data
   * aUpdated is the region which should be updated
   * aNewfront may point to the old data in this content host after the call
   * aNewBackResult may point to the updated data in this content host
   * aNewValidRegionFront is the valid region in aNewFront
   * aUpdatedRegionBack is the region in aNewBackResult which has been updated
   */
  virtual void UpdateThebes(const ThebesBuffer& aNewBack,
                            const nsIntRegion& aUpdated,
                            OptionalThebesBuffer* aNewFront,
                            const nsIntRegion& aOldValidRegionFront,
                            const nsIntRegion& aOldValidRegionBack,
                            OptionalThebesBuffer* aNewBackResult,
                            nsIntRegion* aNewValidRegionFront,
                            nsIntRegion* aUpdatedRegionBack,
                            TiledLayerProperties* aLayerProperties = nullptr) = 0;

  // Subclasses should implement this method if they support being used as a tiled buffer
  virtual TiledLayerComposer* AsTiledLayerComposer() { return nullptr; }


#ifdef MOZ_DUMP_PAINTING
  virtual already_AddRefed<gfxImageSurface> Dump() { return nullptr; }
#endif
};

class ContentHost : public AContentHost
{
public:
  typedef ThebesLayerBuffer::ContentType ContentType;
  typedef ThebesLayerBuffer::PaintState PaintState;

  ContentHost(Compositor* aCompositor);
  ~ContentHost();

  void Release() { AContentHost::Release(); }
  void AddRef() { AContentHost::AddRef(); }

  // AContentHost implementation
  virtual void Composite(EffectChain& aEffectChain,
                         float aOpacity,
                         const gfx::Matrix4x4& aTransform,
                         const gfx::Point& aOffset,
                         const gfx::Filter& aFilter,
                         const gfx::Rect& aClipRect,
                         const nsIntRegion* aVisibleRegion = nullptr);

  // CompositingThebesLayerBuffer implementation
  virtual PaintState BeginPaint(ContentType aContentType, uint32_t) {
    NS_RUNTIMEABORT("can't BeginPaint for a shadow layer");
    return PaintState();
  }

  virtual void SetDeAllocator(ISurfaceDeallocator* aDeAllocator)
  {
    mTextureHost->SetDeAllocator(aDeAllocator);
  }

  virtual LayerRenderState GetRenderState() MOZ_OVERRIDE
  {
    LayerRenderState result = mTextureHost->GetRenderState();

    result.mFlags = (mBufferRotation != nsIntPoint()) ?
                    LAYER_RENDER_STATE_BUFFER_ROTATION : 0;
    return result;
  }

#ifdef MOZ_DUMP_PAINTING
  virtual already_AddRefed<gfxImageSurface> Dump()
  {
    return mTextureHost->Dump();
  }
#endif

  void AddTextureHost(TextureHost* aTextureHost) MOZ_OVERRIDE;
  TextureHost* GetTextureHost() MOZ_OVERRIDE;

  void SetPaintWillResample(bool aResample) { mPaintWillResample = aResample; }

protected:
  virtual nsIntPoint GetOriginOffset() {
    return mBufferRect.TopLeft() - mBufferRotation;
  }

  bool PaintWillResample() { return mPaintWillResample; }

  nsIntRect mBufferRect;
  nsIntPoint mBufferRotation;
  bool mPaintWillResample;
  bool mInitialised;
  RefPtr<Compositor> mCompositor;
  RefPtr<TextureHost> mTextureHost;
  RefPtr<TextureHost> mTextureHostOnWhite;
  RefPtr<Effect> mTextureEffect;
};

// We can directly texture the drawn surface.  Use that as our new
// front buffer, and return our previous directly-textured surface
// to the renderer.
class ContentHostDirect : public ContentHost
{
public:
  ContentHostDirect(Compositor* aCompositor)
    : ContentHost(aCompositor)
  {}

  virtual BufferType GetType() { return BUFFER_CONTENT_DIRECT; }

  virtual void UpdateThebes(const ThebesBuffer& aNewBack,
                            const nsIntRegion& aUpdated,
                            OptionalThebesBuffer* aNewFront,
                            const nsIntRegion& aOldValidRegionFront,
                            const nsIntRegion& aOldValidRegionBack,
                            OptionalThebesBuffer* aNewBackResult,
                            nsIntRegion* aNewValidRegionFront,
                            nsIntRegion* aUpdatedRegionBack,
                            TiledLayerProperties* aLayerProperties = nullptr);
};

// We're using resources owned by our texture as the front buffer.
// Upload the changed region and then return the surface back to
// the renderer.
class ContentHostTexture : public ContentHost
{
public:
  ContentHostTexture(Compositor* aCompositor)
    : ContentHost(aCompositor)
  {}

  virtual BufferType GetType() { return BUFFER_CONTENT; }

  virtual void UpdateThebes(const ThebesBuffer& aNewBack,
                            const nsIntRegion& aUpdated,
                            OptionalThebesBuffer* aNewFront,
                            const nsIntRegion& aOldValidRegionFront,
                            const nsIntRegion& aOldValidRegionBack,
                            OptionalThebesBuffer* aNewBackResult,
                            nsIntRegion* aNewValidRegionFront,
                            nsIntRegion* aUpdatedRegionBack,
                            TiledLayerProperties* aLayerProperties = nullptr);
};

class TiledTexture {
public:
  // Constructs a placeholder TiledTexture. See the comments above
  // TiledLayerBuffer for more information on what this is used for;
  // essentially, this is a sentinel used to represent an invalid or blank
  // tile.
  TiledTexture()
    : mTextureHost(nullptr)
  {}

  // Constructs a TiledTexture from a TextureHost.
  TiledTexture(TextureHost* aTextureHost)
    : mTextureHost(aTextureHost)
  {}

  TiledTexture(const TiledTexture& o) {
    mTextureHost = o.mTextureHost;
  }
  TiledTexture& operator=(const TiledTexture& o) {
    if (this == &o) return *this;
    mTextureHost = o.mTextureHost;
    return *this;
  }

  void Validate(gfxReusableSurfaceWrapper* aReusableSurface, Compositor* aCompositor) {
    TextureFlags flags = 0;
    if (!mTextureHost) {
      // convert placeholder tile to a real tile
      mTextureHost = aCompositor->CreateTextureHost(BUFFER_TILED,
                                                    TEXTURE_TILE,
                                                    0,
                                                    SURFACEDESCRIPTOR_UNKNOWN,
                                                    nullptr);
      flags |= NewTile;
    }

    mTextureHost->Update(aReusableSurface, flags);
  }

  bool operator== (const TiledTexture& o) const {
    if (!mTextureHost || !o.mTextureHost) {
      return mTextureHost == o.mTextureHost;
    }
    return *mTextureHost == *o.mTextureHost;
  }
  bool operator!= (const TiledTexture& o) const {
    if (!mTextureHost || !o.mTextureHost) {
      return mTextureHost != o.mTextureHost;
    }
    return *mTextureHost != *o.mTextureHost;
  }

  RefPtr<TextureHost> mTextureHost;
};

class TiledLayerBufferComposite
  : public TiledLayerBuffer<TiledLayerBufferComposite, TiledTexture>
{
  friend class TiledLayerBuffer<TiledLayerBufferComposite, TiledTexture>;

public:
  TiledLayerBufferComposite(Compositor* aCompositor)
    : mCompositor(aCompositor)
  {}

  void Upload(const BasicTiledLayerBuffer* aMainMemoryTiledBuffer,
              const nsIntRegion& aNewValidRegion,
              const nsIntRegion& aInvalidateRegion,
              const gfxSize& aResolution);

  TiledTexture GetPlaceholderTile() const { return TiledTexture(); }

  // Stores the absolute resolution of the containing frame, calculated
  // by the sum of the resolutions of all parent layers' FrameMetrics.
  const gfxSize& GetFrameResolution() { return mFrameResolution; }

protected:
  TiledTexture ValidateTile(TiledTexture aTile,
                            const nsIntPoint& aTileRect,
                            const nsIntRegion& dirtyRect);

  // do nothing, the desctructor in the texture host takes care of releasing resources
  void ReleaseTile(TiledTexture aTile) {}

  void SwapTiles(TiledTexture& aTileA, TiledTexture& aTileB) {
    std::swap(aTileA, aTileB);
  }

private:
  Compositor* mCompositor;
  const BasicTiledLayerBuffer* mMainMemoryTiledBuffer;
  gfxSize mFrameResolution;
};





class TiledThebesLayerComposite;
class ReusableTileStoreComposite;

class TiledContentHost : public AContentHost,
                         public TiledLayerComposer
{
public:
  TiledContentHost(Compositor* aCompositor)
    : mCompositor(aCompositor)
    , mVideoMemoryTiledBuffer(aCompositor)
    , mLowPrecisionVideoMemoryTiledBuffer(aCompositor)
    , mReusableTileStore(nullptr)
    , mPendingUpload(false)
    , mPendingLowPrecisionUpload(false)
  {}
  ~TiledContentHost();

  virtual LayerRenderState GetRenderState() MOZ_OVERRIDE
  {
    return LayerRenderState();
  }


  virtual void UpdateThebes(const ThebesBuffer& aNewBack,
                            const nsIntRegion& aUpdated,
                            OptionalThebesBuffer* aNewFront,
                            const nsIntRegion& aOldValidRegionFront,
                            const nsIntRegion& aOldValidRegionBack,
                            OptionalThebesBuffer* aNewBackResult,
                            nsIntRegion* aNewValidRegionFront,
                            nsIntRegion* aUpdatedRegionBack,
                            TiledLayerProperties* aLayerProperties = nullptr);

  const nsIntRegion& GetValidLowPrecisionRegion() const
  {
    return mLowPrecisionVideoMemoryTiledBuffer.GetValidRegion();
  }

  void MemoryPressure();
  void PaintedTiledLayerBuffer(const BasicTiledLayerBuffer* mTiledBuffer);

  // Renders a single given tile.
  void RenderTile(const TiledTexture& aTile,
                  EffectChain& aEffectChain,
                  float aOpacity,
                  const gfx::Matrix4x4& aTransform,
                  const gfx::Point& aOffset,
                  const gfx::Filter& aFilter,
                  const gfx::Rect& aClipRect,
                  const nsIntRegion& aScreenRegion,
                  const nsIntPoint& aTextureOffset,
                  const nsIntSize& aTextureBounds);

  void Composite(EffectChain& aEffectChain,
                 float aOpacity,
                 const gfx::Matrix4x4& aTransform,
                 const gfx::Point& aOffset,
                 const gfx::Filter& aFilter,
                 const gfx::Rect& aClipRect,
                 const nsIntRegion* aVisibleRegion = nullptr);

  virtual BufferType GetType() { return BUFFER_TILED; }

  virtual TiledLayerComposer* AsTiledLayerComposer() { return this; }

  virtual void AddTextureHost(TextureHost* aTextureHost) { NS_WARNING("Does nothing"); }

private:
  void ProcessUploadQueue(nsIntRegion* aNewValidRegion);
  void ProcessLowPrecisionUploadQueue();

  void RenderLayerBuffer(TiledLayerBufferComposite& aLayerBuffer,
                         const nsIntRegion& aValidRegion,
                         EffectChain& aEffectChain,
                         float aOpacity,
                         const gfx::Point& aOffset,
                         const gfx::Filter& aFilter,
                         const gfx::Rect& aClipRect,
                         const nsIntRegion& aMaskRegion,
                         nsIntRect& visibleRect,
                         gfx::Matrix4x4 aTransform);

  void EnsureTileStore();

  RefPtr<Compositor> mCompositor;

  nsIntRegion                  mValidRegion;
  nsIntRegion                  mRegionToUpload;
  nsIntRegion                  mLowPrecisionRegionToUpload;
  BasicTiledLayerBuffer        mMainMemoryTiledBuffer;
  BasicTiledLayerBuffer        mLowPrecisionMainMemoryTiledBuffer;
  TiledLayerBufferComposite    mVideoMemoryTiledBuffer;
  TiledLayerBufferComposite    mLowPrecisionVideoMemoryTiledBuffer;
  ReusableTileStoreComposite*  mReusableTileStore;
  TiledLayerProperties         mLayerProperties;
  bool                         mPendingUpload : 1;
  bool                         mPendingLowPrecisionUpload : 1;
};

}
}

#endif