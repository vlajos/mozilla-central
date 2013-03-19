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

class ThebesBuffer;
class OptionalThebesBuffer;
struct TexturedEffect;

/**
 * ContentHosts are used for compositing Thebes layers, always matched by a
 * ContentClient of the same type.
 *
 * ContentHosts support only UpdateThebes(), not Update().
 */
class ContentHost : public CompositableHost
{
public:
  // Subclasses should implement this method if they support being used as a
  // tiling.
  virtual TiledLayerComposer* AsTiledLayerComposer() { return nullptr; }

  virtual void UpdateThebes(const ThebesBufferData& aData,
                            const nsIntRegion& aUpdated,
                            const nsIntRegion& aOldValidRegionBack,
                            nsIntRegion* aUpdatedRegionBack) = 0;

#ifdef MOZ_DUMP_PAINTING
  virtual already_AddRefed<gfxImageSurface> Dump() { return nullptr; }
#endif

protected:
  ContentHost(Compositor* aCompositor)
  : CompositableHost(aCompositor) {}
};

/**
 * Base class for non-tiled ContentHosts.
 *
 * Ownership of the SurfaceDescriptor and the resources it represents is passed
 * from the ContentClient to the ContentHost when the TextureClient/Hosts are
 * created, that is recevied here by SetTextureHosts which assigns one or two
 * texture hosts (for single and double buffering) to the ContentHost.
 *
 * It is the responsibility of the ContentHost to destroy its resources when
 * they are recreated or the ContentHost dies.
 */
class ContentHostBase : public ContentHost
{
public:
  typedef ThebesLayerBuffer::ContentType ContentType;
  typedef ThebesLayerBuffer::PaintState PaintState;

  ContentHostBase(Compositor* aCompositor);
  ~ContentHostBase();

  virtual void Composite(EffectChain& aEffectChain,
                         float aOpacity,
                         const gfx::Matrix4x4& aTransform,
                         const gfx::Point& aOffset,
                         const gfx::Filter& aFilter,
                         const gfx::Rect& aClipRect,
                         const nsIntRegion* aVisibleRegion = nullptr,
                         TiledLayerProperties* aLayerProperties = nullptr);

  virtual PaintState BeginPaint(ContentType, uint32_t)
  {
    NS_RUNTIMEABORT("shouldn't BeginPaint for a shadow layer");
    return PaintState();
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

  // Set one or both texture hosts. We do not use AddTextureHost because for
  // double buffering, we need to add two hosts and know which is which.
  virtual void SetTextureHosts(TextureHost* aNewFront,
                               TextureHost* aNewBack = nullptr) = 0;
  // For double buffered ContentHosts we want to set both TextureHosts at
  // once so we ignore this call.
  virtual void AddTextureHost(TextureHost* aTextureHost,
                              ISurfaceAllocator* aAllocator = nullptr) MOZ_OVERRIDE {}
  virtual TextureHost* GetTextureHost() MOZ_OVERRIDE;

  void SetPaintWillResample(bool aResample) { mPaintWillResample = aResample; }
  // The client has destroyed its texture clients and we should destroy our
  // texture hosts and SurfaceDescriptors. Note that we don't immediately
  // destroy our front buffer so that we can continue to composite.
  virtual void DestroyTextures() = 0;

protected:
  virtual nsIntPoint GetOriginOffset() {
    return mBufferRect.TopLeft() - mBufferRotation;
  }

  bool PaintWillResample() { return mPaintWillResample; }

  // Destroy the front buffer's texture host. This should only happen when
  // we have a new front buffer to use or the ContentHost is going to die.
  void DestroyFrontHost();

  nsIntRect mBufferRect;
  nsIntPoint mBufferRotation;
  RefPtr<TextureHost> mTextureHost;
  RefPtr<TextureHost> mTextureHostOnWhite;
  // When we set a new front buffer TextureHost, we don't want to stomp on
  // the old one which might still be used for compositing. So we store it
  // here and move it to mTextureHost once we do the first buffer swap.
  RefPtr<TextureHost> mNewFrontHost;
  bool mPaintWillResample;
  bool mInitialised;
};

/**
 * Double buffering is implemented by swapping the front and back TextureHosts.
 */
class ContentHostDoubleBuffered : public ContentHostBase
{
public:
  ContentHostDoubleBuffered(Compositor* aCompositor)
    : ContentHostBase(aCompositor)
  {}

  ~ContentHostDoubleBuffered();

  virtual CompositableType GetType() { return BUFFER_CONTENT_DIRECT; }

  virtual void UpdateThebes(const ThebesBufferData& aData,
                            const nsIntRegion& aUpdated,
                            const nsIntRegion& aOldValidRegionBack,
                            nsIntRegion* aUpdatedRegionBack);

  // We expect both TextureHosts.
  virtual void SetTextureHosts(TextureHost* aNewFront,
                               TextureHost* aNewBack = nullptr) MOZ_OVERRIDE;
  virtual void DestroyTextures() MOZ_OVERRIDE;

#ifdef MOZ_LAYERS_HAVE_LOG
  virtual void PrintInfo(nsACString& aTo, const char* aPrefix);
#endif
protected:
  nsIntRegion mValidRegionForNextBackBuffer;
  // Texture host for the back buffer. We never read or write this buffer. We
  // only swap it with the front buffer (mTextureHost) when we are told by the
  // content thread.
  RefPtr<TextureHost> mBackHost;
};

/**
 * Single buffered, therefore we must synchronously upload the image from the
 * TextureHost in the layers transaction (i.e., in UpdateThebes).
 */
class ContentHostSingleBuffered : public ContentHostBase
{
public:
  ContentHostSingleBuffered(Compositor* aCompositor)
    : ContentHostBase(aCompositor)
  {}
  virtual ~ContentHostSingleBuffered();

  virtual CompositableType GetType() { return BUFFER_CONTENT; }

  virtual void UpdateThebes(const ThebesBufferData& aData,
                            const nsIntRegion& aUpdated,
                            const nsIntRegion& aOldValidRegionBack,
                            nsIntRegion* aUpdatedRegionBack);

  // We expect only one TextureHost.
  virtual void SetTextureHosts(TextureHost* aNewFront,
                               TextureHost* aNewBack = nullptr) MOZ_OVERRIDE;
  virtual void DestroyTextures() MOZ_OVERRIDE;

#ifdef MOZ_LAYERS_HAVE_LOG
  virtual void PrintInfo(nsACString& aTo, const char* aPrefix);
#endif
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
    if (this == &o)
      return *this;
    mTextureHost = o.mTextureHost;
    return *this;
  }

  void Validate(gfxReusableSurfaceWrapper* aReusableSurface, Compositor* aCompositor, uint16_t aSize);

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

/**
 * ContentHost for tiled Thebes layers. Since tiled layers are special snow
 * flakes, we don't call UpdateThebes or AddTextureHost, etc. We do call Composite
 * in the usual way though.
 *
 * There is no corresponding content client - on the client side we use a
 * BasicTiledLayerBuffer owned by a BasicTiledThebesLayer. On the host side, we
 * just use a regular ThebesLayerComposite, but with a tiled content host.
 *
 * TiledContentHost has a TiledLayerBufferComposite which keeps hold of the tiles.
 * Each tile has a reference to a texture host. During the layers transaction, we
 * receive a copy of the client-side tile buffer (PaintedTiledLayerBuffer). This is
 * copied into the main memory tile buffer and then deleted. Copying copies tiles,
 * but we only copy references to the underlying texture clients.
 *
 * When the content host is composited, we first upload any pending tiles
 * (Process*UploadQueue), then render (RenderLayerBuffer). The former calls Validate
 * on the tile (via ValidateTile and Update), that calls Update on the texture host,
 * which works as for regular texture hosts. Rendering takes us to RenderTile which
 * is similar to Composite for non-tiled ContentHosts.
 */
class TiledContentHost : public ContentHost,
                         public TiledLayerComposer
{
public:
  TiledContentHost(Compositor* aCompositor)
    : ContentHost(aCompositor)
    , mVideoMemoryTiledBuffer(aCompositor)
    , mLowPrecisionVideoMemoryTiledBuffer(aCompositor)
    , mPendingUpload(false)
    , mPendingLowPrecisionUpload(false)
  {}
  ~TiledContentHost();

  virtual LayerRenderState GetRenderState() MOZ_OVERRIDE
  {
    return LayerRenderState();
  }


  virtual void UpdateThebes(const ThebesBufferData& aData,
                            const nsIntRegion& aUpdated,
                            const nsIntRegion& aOldValidRegionBack,
                            nsIntRegion* aUpdatedRegionBack)
  {
    MOZ_ASSERT(false, "N/A for tiled layers");
  }

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
                 const nsIntRegion* aVisibleRegion = nullptr,
                 TiledLayerProperties* aLayerProperties = nullptr);

  virtual CompositableType GetType() { return BUFFER_TILED; }

  virtual TiledLayerComposer* AsTiledLayerComposer() { return this; }

  virtual void AddTextureHost(TextureHost* aTextureHost,
                              ISurfaceAllocator* aAllocator = nullptr)
  {
    MOZ_ASSERT(false, "Does nothing");
  }

#ifdef MOZ_LAYERS_HAVE_LOG
  virtual void PrintInfo(nsACString& aTo, const char* aPrefix);
#endif

private:
  void ProcessUploadQueue(nsIntRegion* aNewValidRegion,
                          TiledLayerProperties* aLayerProperties);
  void ProcessLowPrecisionUploadQueue();

  void RenderLayerBuffer(TiledLayerBufferComposite& aLayerBuffer,
                         const nsIntRegion& aValidRegion,
                         EffectChain& aEffectChain,
                         float aOpacity,
                         const gfx::Point& aOffset,
                         const gfx::Filter& aFilter,
                         const gfx::Rect& aClipRect,
                         const nsIntRegion& aMaskRegion,
                         nsIntRect aVisibleRect,
                         gfx::Matrix4x4 aTransform);

  void EnsureTileStore() {}

  nsIntRegion                  mRegionToUpload;
  nsIntRegion                  mLowPrecisionRegionToUpload;
  BasicTiledLayerBuffer        mMainMemoryTiledBuffer;
  BasicTiledLayerBuffer        mLowPrecisionMainMemoryTiledBuffer;
  TiledLayerBufferComposite    mVideoMemoryTiledBuffer;
  TiledLayerBufferComposite    mLowPrecisionVideoMemoryTiledBuffer;
  bool                         mPendingUpload : 1;
  bool                         mPendingLowPrecisionUpload : 1;
};

}
}

#endif
