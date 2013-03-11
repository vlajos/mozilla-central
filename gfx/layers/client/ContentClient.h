/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_CONTENTCLIENT_H
#define MOZILLA_GFX_CONTENTCLIENT_H

#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/layers/CompositableClient.h"
#include "gfxReusableSurfaceWrapper.h"
#include "mozilla/layers/TextureClient.h"
#include "ThebesLayerBuffer.h"
#include "ipc/AutoOpenSurface.h"
#include "ipc/ShadowLayerChild.h"
#include "mozilla/Attributes.h"
#include "TiledLayerBuffer.h"
#include "gfxPlatform.h"

namespace mozilla {
namespace layers {

class BasicLayerManager;

class ContentClient : public CompositableClient
                    , protected ThebesLayerBuffer
{
public:
  ContentClient(CompositableForwarder* aForwarder)
  : CompositableClient(aForwarder)
  , ThebesLayerBuffer(ContainsVisibleBounds)
  {}
  virtual ~ContentClient() {}

  CompositableType GetType() const MOZ_OVERRIDE
  {
    return BUFFER_CONTENT;
  }

  typedef ThebesLayerBuffer::PaintState PaintState;
  typedef ThebesLayerBuffer::ContentType ContentType;

  virtual void Clear() { ThebesLayerBuffer::Clear(); }
  PaintState BeginPaintBuffer(ThebesLayer* aLayer, ContentType aContentType,
                              uint32_t aFlags)
  {
    return ThebesLayerBuffer::BeginPaint(aLayer, aContentType, aFlags);
  }

  virtual void DrawTo(ThebesLayer* aLayer, gfxContext* aTarget, float aOpacity,
                      gfxASurface* aMask, const gfxMatrix* aMaskTransform)
  {
    ThebesLayerBuffer::DrawTo(aLayer, aTarget, aOpacity, aMask, aMaskTransform);
  }

  // Sync front/back buffers content
  // After executing, the new back buffer has the same (interesting) pixels as the
  // new front buffer, and mValidRegion et al. are correct wrt the new
  // back buffer (i.e. as they were for the old back buffer)
  virtual void SyncFrontBufferToBackBuffer() {}

  // Called as part of the layers transation reply. Conveys data about our
  // buffer(s) from the compositor. If appropriate we should swap references
  // to our buffers.
  virtual void SwapBuffers(const ThebesBufferData &aData,
                           const nsIntRegion& aValidRegion,
                           const nsIntRegion& aFrontUpdatedRegion) {}

  // call before and after painting into this content client
  virtual void BeginPaint() {}
  virtual void EndPaint() {}
  virtual const nsIntRegion& ValidRegion() const
  {
    MOZ_ASSERT(false);
    return *(new nsIntRegion());
  }
};

// thin wrapper around BasicThebesLayerBuffer, for on-mtc
class ContentClientBasic : public ContentClient
{
public:
  ContentClientBasic(CompositableForwarder* aForwarder, BasicLayerManager* aManager);

  virtual already_AddRefed<gfxASurface> CreateBuffer(ContentType aType,
                                                     const nsIntSize& aSize,
                                                     uint32_t aFlags);
  virtual CompositableType GetType() const MOZ_OVERRIDE
  {
    MOZ_ASSERT(false, "Should not be called on non-remote ContentClient");
    return BUFFER_UNKNOWN;
  }

private:
  BasicLayerManager* mManager;
};

/**
 * A ContentClient for use with OMTC.
 * When using a ContentClientRemote, SurfaceDescriptors are created on
 * the rendering side and destroyed on the compositing side. They are only
 * passed from one side to the other when the TextureClient/Hosts are created.
 * *Ownership* of the SurfaceDescriptor moves from the rendering side to the
 * comnpositing side with the create message (send from CreateBuffer) which
 * tells the compositor that TextureClients have been created and that the
 * compositor should assign the corresponding TextureHosts to our corresponding
 * ContentHost.
 *
 * If the size or type of our buffer(s) change(s), then we simply destroy and
 * create them.
 */
class ContentClientRemote : public ContentClient
{
  using ThebesLayerBuffer::BufferRect;
  using ThebesLayerBuffer::BufferRotation;
public:
  ContentClientRemote(CompositableForwarder* aForwarder)
    : ContentClient(aForwarder)
    , mTextureClient(nullptr)
    , mIsNewBuffer(false)
    , mFrontAndBackBufferDiffer(false)
    , mContentType(gfxASurface::CONTENT_COLOR_ALPHA)
    , mLockedForCompositor(false)
  {}

  /**
   * Begin/End Paint map a gfxASurface from the texture client
   * into the buffer of ThebesLayerBuffer. The surface is only
   * valid when the texture client is locked, so is mapped out
   * of ThebesLayerBuffer when we are done painting.
   * None of the underlying buffer attributes (rect, rotation)
   * are affected by mapping/unmapping.
   */
  virtual void BeginPaint() MOZ_OVERRIDE;
  virtual void EndPaint() MOZ_OVERRIDE;

  virtual void Updated(const nsIntRegion& aRegionToDraw,
                       const nsIntRegion& aVisibleRegion,
                       bool aDidSelfCopy);

  virtual void SwapBuffers(const ThebesBufferData &aData,
                           const nsIntRegion& aValidRegion,
                           const nsIntRegion& aFrontUpdatedRegion) MOZ_OVERRIDE;

  virtual const nsIntRect& BufferRect() { return ThebesLayerBuffer::BufferRect(); }
  virtual const nsIntPoint& BufferRotation() { return ThebesLayerBuffer::BufferRotation(); }
  virtual const nsIntRegion& ValidRegion() const { return mValidRegion; }

  virtual already_AddRefed<gfxASurface> CreateBuffer(ContentType aType,
                                                     const nsIntSize& aSize,
                                                     uint32_t aFlags);
  void DestroyBuffers();

protected:
  /**
   * Swap out the old backing buffer for |aBuffer| and attributes.
   */
  void SetBackingBuffer(gfxASurface* aBuffer,
                        const nsIntRect& aRect, const nsIntPoint& aRotation);

  virtual nsIntRegion GetUpdatedRegion(const nsIntRegion& aRegionToDraw,
                                       const nsIntRegion& aVisibleRegion,
                                       bool aDidSelfCopy);

  // Create the back buffer for the ContentClient/Host pair if necessary
  // and notify the compositor that we have created the buffer(s).
  virtual void CreateBackBufferAndNotify(uint32_t aFlags) = 0;
  virtual void DestroyBackBuffer() {}

  nsIntRegion mValidRegion;
  nsIntRect mBufferRect;
  nsIntPoint mBufferRotation;

  RefPtr<TextureClient> mTextureClient;
  // keep a record of texture clients we have created and need to keep
  // around, then unlock when we are done painting
  nsTArray<RefPtr<TextureClient>> mOldTextures;

  bool mIsNewBuffer;
  bool mFrontAndBackBufferDiffer;
  ContentType mContentType;
  gfx::IntSize mSize;
#ifdef DEBUG
  // Between posting a message to the compositor that the TextureHost is
  // ready and receiving the reply, the compositor may write to the
  // texture, therefore we must not touch the TextureClient(s). This is
  // a 'soft' lock, since locking is only enforced by our own good
  // behaviour.
  bool mLockedForCompositor;
#endif
};

/**
 * A double buffered ContentClient. mTextureClient is the back buffer, which
 * we draw into. mFrontClient is the front buffer which we may read from, but
 * not write to, when the compositor does not have the 'soft' lock. We can write
 * into mTextureClient at any time.
 *
 * The ContentHost keeps a reference to both corresponding texture hosts, in
 * repsonse to our UpdateTextureRegion message, the compositor swaps its
 * references. In repsonse to the compositor's reply we swap our references
 * (in SwapBuffers).
 */
class ContentClientDirect : public ContentClientRemote
{
public:
  ContentClientDirect(CompositableForwarder* aFwd)
  : ContentClientRemote(aFwd)
  {}
  ~ContentClientDirect();

  CompositableType GetType() const MOZ_OVERRIDE
  {
    return BUFFER_CONTENT_DIRECT;
  }

  virtual void SwapBuffers(const ThebesBufferData &aData,
                           const nsIntRegion& aValidRegion,
                           const nsIntRegion& aFrontUpdatedRegion) MOZ_OVERRIDE;

  virtual void SyncFrontBufferToBackBuffer() MOZ_OVERRIDE;

protected:
  virtual void CreateBackBufferAndNotify(uint32_t aFlags) MOZ_OVERRIDE;
  virtual void DestroyBackBuffer() MOZ_OVERRIDE;

private:
  ContentClientDirect(gfxASurface* aBuffer,
                      const nsIntRect& aRect, const nsIntPoint& aRotation)
    // The size policy doesn't really matter here; this constructor is
    // intended to be used for creating temporaries
    : ContentClientRemote(nullptr)
  {
    SetBuffer(aBuffer, aRect, aRotation);
  }

  void UpdateDestinationFrom(const RotatedBuffer& aSource,
                             const nsIntRegion& aUpdateRegion);

  RefPtr<TextureClient> mFrontClient;
  nsIntRegion mFrontUpdatedRegion;
};

/**
 * A single buffered ContentClient. We have a single TextureClient/Host
 * which we update and then send a message to the compositor that we are
 * done updating. It is not safe for the compositor to use the corresponding
 * TextureHost's memory directly, it most upload it to video memory of some
 * kind. We are free to modify the TextureClient once we receive reply from
 * the compositor.
 */
class ContentClientTexture : public ContentClientRemote
{
public:
  ContentClientTexture(CompositableForwarder* aFwd)
    : ContentClientRemote(aFwd)
  {}
  ~ContentClientTexture();

  virtual CompositableType GetType() const MOZ_OVERRIDE
  {
    return BUFFER_CONTENT;
  }

  virtual void Updated(const nsIntRegion& aRegionToDraw,
                       const nsIntRegion& aVisibleRegion,
                       bool aDidSelfCopy) MOZ_OVERRIDE;

  virtual void SyncFrontBufferToBackBuffer(); 

protected:
  virtual void CreateBackBufferAndNotify(uint32_t aFlags) MOZ_OVERRIDE;
};

/**
 * Represent a single tile in tiled buffer. It's backed
 * by a gfxReusableSurfaceWrapper that implements a
 * copy-on-write mechanism while locked. The tile should be
 * locked before being sent to the compositor and unlocked
 * as soon as it is uploaded to prevent a copy.
 * Ideal place to store per tile debug information.
 */
struct BasicTiledLayerTile {
  RefPtr<TextureClientTile> mTextureClient;
#ifdef GFX_TILEDLAYER_DEBUG_OVERLAY
  TimeStamp        mLastUpdate;
#endif

  // Placeholder
  BasicTiledLayerTile()
    : mTextureClient(nullptr)
  {}

  BasicTiledLayerTile(const BasicTiledLayerTile& o) {
    mTextureClient = o.mTextureClient;
#ifdef GFX_TILEDLAYER_DEBUG_OVERLAY
    mLastUpdate = o.mLastUpdate;
#endif
  }
  BasicTiledLayerTile& operator=(const BasicTiledLayerTile& o) {
    if (this == &o) return *this;
    mTextureClient = o.mTextureClient;
#ifdef GFX_TILEDLAYER_DEBUG_OVERLAY
    mLastUpdate = o.mLastUpdate;
#endif
    return *this;
  }
  bool operator== (const BasicTiledLayerTile& o) const {
    return mTextureClient == o.mTextureClient;
  }
  bool operator!= (const BasicTiledLayerTile& o) const {
    return mTextureClient != o.mTextureClient;
  }

  bool IsPlaceholderTile() { return mTextureClient == nullptr; }

  void ReadUnlock() {
    GetSurface()->ReadUnlock();
  }
  void ReadLock() {
    GetSurface()->ReadLock();
  }

  gfxReusableSurfaceWrapper* GetSurface() {
    return mTextureClient->GetReusableSurfaceWrapper();
  }
};

/**
 * This struct stores all the data necessary to perform a paint so that it
 * doesn't need to be recalculated on every repeated transaction.
 */
struct BasicTiledLayerPaintData {
  gfx::Point mScrollOffset;
  gfx::Point mLastScrollOffset;
  gfx3DMatrix mTransformScreenToLayer;
  nsIntRect mLayerCriticalDisplayPort;
  gfxSize mResolution;
  nsIntRect mCompositionBounds;
  uint16_t mLowPrecisionPaintCount;
  bool mFirstPaint : 1;
  bool mPaintFinished : 1;
};

class BasicTiledThebesLayer;
class BasicShadowLayerManager;

/**
 * Provide an instance of TiledLayerBuffer backed by image surfaces.
 * This buffer provides an implementation to ValidateTile using a
 * thebes callback and can support painting using a single paint buffer
 * which is much faster then painting directly into the tiles.
 */

class BasicTiledLayerBuffer : public TiledLayerBuffer<BasicTiledLayerBuffer, BasicTiledLayerTile>
{
  friend class TiledLayerBuffer<BasicTiledLayerBuffer, BasicTiledLayerTile>;

public:
  BasicTiledLayerBuffer(BasicTiledThebesLayer* aThebesLayer, BasicShadowLayerManager* aManager)
    : mThebesLayer(aThebesLayer)
    , mManager(aManager)
    , mLastPaintOpaque(false)
    {}
 BasicTiledLayerBuffer()
    : mThebesLayer(nullptr)
    , mManager(nullptr)
    , mLastPaintOpaque(false)
    {}

  void PaintThebes(const nsIntRegion& aNewValidRegion,
                   const nsIntRegion& aPaintRegion,
                   LayerManager::DrawThebesLayerCallback aCallback,
                   void* aCallbackData);

  void ReadUnlock() {
    for (size_t i = 0; i < mRetainedTiles.Length(); i++) {
      if (mRetainedTiles[i].IsPlaceholderTile()) continue;
      mRetainedTiles[i].ReadUnlock();
    }
  }

  void ReadLock() {
    for (size_t i = 0; i < mRetainedTiles.Length(); i++) {
      if (mRetainedTiles[i].IsPlaceholderTile()) continue;
      mRetainedTiles[i].ReadLock();
    }
  }

  const gfxSize& GetFrameResolution() { return mFrameResolution; }
  void SetFrameResolution(const gfxSize& aResolution) { mFrameResolution = aResolution; }

  bool HasFormatChanged() const;

  void LockCopyAndWrite();

  /**
   * Performs a progressive update of a given tiled buffer.
   * See ComputeProgressiveUpdateRegion above for parameter documentation.
   */
  bool ProgressiveUpdate(nsIntRegion& aValidRegion,
                         nsIntRegion& aInvalidRegion,
                         const nsIntRegion& aOldValidRegion,
                         BasicTiledLayerPaintData* aPaintData,
                         LayerManager::DrawThebesLayerCallback aCallback,
                         void* aCallbackData);

  /**
   * Copy this buffer duplicating the texture hosts under the tiles
   * XXX This should go. It is a hack because we need to keep the
   * surface wrappers alive whilst they are locked by the compositor.
   * Once we properly implement the texture host/client architecture
   * for tiled layers we shouldn't need this.
   */
  BasicTiledLayerBuffer DeepCopy() const;

protected:
  BasicTiledLayerTile ValidateTile(BasicTiledLayerTile aTile,
                                   const nsIntPoint& aTileRect,
                                   const nsIntRegion& dirtyRect);

  // If this returns true, we perform the paint operation into a single large
  // buffer and copy it out to the tiles instead of calling PaintThebes() on
  // each tile individually. Somewhat surprisingly, this turns out to be faster
  // on Android.
  bool UseSinglePaintBuffer() { return true; }

  void ReleaseTile(BasicTiledLayerTile aTile) { /* No-op. */ }

  void SwapTiles(BasicTiledLayerTile& aTileA, BasicTiledLayerTile& aTileB) {
    std::swap(aTileA, aTileB);
  }

  BasicTiledLayerTile GetPlaceholderTile() const { return BasicTiledLayerTile(); }
private:
  gfxASurface::gfxContentType GetContentType() const;
  BasicTiledThebesLayer* mThebesLayer;
  BasicShadowLayerManager* mManager;
  LayerManager::DrawThebesLayerCallback mCallback;
  void* mCallbackData;
  gfxSize mFrameResolution;
  bool mLastPaintOpaque;

  // The buffer we use when UseSinglePaintBuffer() above is true.
  nsRefPtr<gfxImageSurface>     mSinglePaintBuffer;
  nsIntPoint                    mSinglePaintBufferOffset;

  BasicTiledLayerTile ValidateTileInternal(BasicTiledLayerTile aTile,
                                         const nsIntPoint& aTileOrigin,
                                         const nsIntRect& aDirtyRect);

  /**
   * Calculates the region to update in a single progressive update transaction.
   * This employs some heuristics to update the most 'sensible' region to
   * update at this point in time, and how large an update should be performed
   * at once to maintain visual coherency.
   *
   * aInvalidRegion is the current invalid region.
   * aOldValidRegion is the valid region of mTiledBuffer at the beginning of the
   * current transaction.
   * aRegionToPaint will be filled with the region to update. This may be empty,
   * which indicates that there is no more work to do.
   * aTransform is the transform required to convert from screen-space to
   * layer-space.
   * aScrollOffset is the current scroll offset of the primary scrollable layer.
   * aResolution is the render resolution of the layer.
   * aIsRepeated should be true if this function has already been called during
   * this transaction.
   *
   * Returns true if it should be called again, false otherwise. In the case
   * that aRegionToPaint is empty, this will return aIsRepeated for convenience.
   */
  bool ComputeProgressiveUpdateRegion(const nsIntRegion& aInvalidRegion,
                                      const nsIntRegion& aOldValidRegion,
                                      nsIntRegion& aRegionToPaint,
                                      BasicTiledLayerPaintData* aPaintData,
                                      bool aIsRepeated);
};

}
}

#endif
