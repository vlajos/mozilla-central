/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ipc/AutoOpenSurface.h"
#include "mozilla/layers/ShadowLayers.h"
#include "mozilla/layers/ContentHost.h"
#include "mozilla/layers/TextureFactoryIdentifier.h" // for TextureInfo
#include "mozilla/layers/Effects.h"
#include "ReusableTileStoreComposite.h"
#include "gfxPlatform.h"

namespace mozilla {
namespace layers {

void
CompositingThebesLayerBuffer::Composite(EffectChain& aEffectChain,
                                        float aOpacity,
                                        const gfx::Matrix4x4& aTransform,
                                        const gfx::Point& aOffset,
                                        const gfx::Filter& aFilter,
                                        const gfx::Rect& aClipRect,
                                        const nsIntRegion* aVisibleRegion)
{
  NS_ASSERTION(aVisibleRegion, "Requires a visible region");

  if (!mTextureHost || !mInitialised) {
    return;
  }

  mTextureHost->UpdateAsyncTexture();
  if (RefPtr<Effect> effect = mTextureHost->Lock(aFilter)) {
    if (mTextureHostOnWhite) {
      mTextureHostOnWhite->UpdateAsyncTexture();
      if (RefPtr<Effect> effectOnWhite = mTextureHostOnWhite->Lock(aFilter)) {
        TextureSource* sourceOnBlack = mTextureHost->AsTextureSource();
        TextureSource* sourceOnWhite = mTextureHostOnWhite->AsTextureSource();
        aEffectChain.mEffects[EFFECT_COMPONENT_ALPHA] =
          new EffectComponentAlpha(sourceOnBlack, sourceOnWhite);
      } else {
        return;
      }
    } else {
        aEffectChain.mEffects[effect->mType] = effect;
    }
  } else {
    return;
  }

  nsIntRegion tmpRegion;
  const nsIntRegion* renderRegion;
  if (PaintWillResample()) {
    // If we're resampling, then the texture image will contain exactly the
    // entire visible region's bounds, and we should draw it all in one quad
    // to avoid unexpected aliasing.
    tmpRegion = aVisibleRegion->GetBounds();
    renderRegion = &tmpRegion;
  } else {
    renderRegion = aVisibleRegion;
  }

  nsIntRegion region(*renderRegion);
  nsIntPoint origin = GetOriginOffset();
  region.MoveBy(-origin);           // translate into TexImage space, buffer origin might not be at texture (0,0)

  // Figure out the intersecting draw region
  TextureSource* source = mTextureHost->AsTextureSource();
  MOZ_ASSERT(source);
  gfx::IntSize texSize = source->GetSize();
  nsIntRect textureRect = nsIntRect(0, 0, texSize.width, texSize.height);
  textureRect.MoveBy(region.GetBounds().TopLeft());
  nsIntRegion subregion;
  subregion.And(region, textureRect);
  if (subregion.IsEmpty()) {
    // Region is empty, nothing to draw
    mTextureHost->Unlock();
    return;
  }

  nsIntRegion screenRects;
  nsIntRegion regionRects;

  // Collect texture/screen coordinates for drawing
  nsIntRegionRectIterator iter(subregion);
  while (const nsIntRect* iterRect = iter.Next()) {
    nsIntRect regionRect = *iterRect;
    nsIntRect screenRect = regionRect;
    screenRect.MoveBy(origin);

    screenRects.Or(screenRects, screenRect);
    regionRects.Or(regionRects, regionRect);
  }

  TileIterator* tileIter = source->AsTileIterator();
  TileIterator* iterOnWhite = nullptr;
  if (tileIter) {
    tileIter->BeginTileIteration();
  }

  if (mTextureHostOnWhite) {
    iterOnWhite = mTextureHostOnWhite->AsTextureSource()->AsTileIterator();
    NS_ASSERTION((!tileIter) || tileIter->GetTileCount() == iterOnWhite->GetTileCount(),
                 "Tile count mismatch on component alpha texture");
    if (iterOnWhite) {
      iterOnWhite->BeginTileIteration();
    }
  }

  bool usingTiles = (tileIter && tileIter->GetTileCount() > 1);
  do {
    if (iterOnWhite) {
      NS_ASSERTION(iterOnWhite->GetTileRect() == tileIter->GetTileRect(), "component alpha textures should be the same size.");
    }

    nsIntRect texRect = tileIter ? tileIter->GetTileRect()
                                 : nsIntRect(0, 0,
                                             texSize.width,
                                             texSize.height);

    // Draw texture. If we're using tiles, we do repeating manually, as texture
    // repeat would cause each individual tile to repeat instead of the
    // compound texture as a whole. This involves drawing at most 4 sections,
    // 2 for each axis that has texture repeat.
    for (int y = 0; y < (usingTiles ? 2 : 1); y++) {
      for (int x = 0; x < (usingTiles ? 2 : 1); x++) {
        nsIntRect currentTileRect(texRect);
        currentTileRect.MoveBy(x * texSize.width, y * texSize.height);

        nsIntRegionRectIterator screenIter(screenRects);
        nsIntRegionRectIterator regionIter(regionRects);

        const nsIntRect* screenRect;
        const nsIntRect* regionRect;
        while ((screenRect = screenIter.Next()) &&
               (regionRect = regionIter.Next())) {
            nsIntRect tileScreenRect(*screenRect);
            nsIntRect tileRegionRect(*regionRect);

            // When we're using tiles, find the intersection between the tile
            // rect and this region rect. Tiling is then handled by the
            // outer for-loops and modifying the tile rect.
            if (usingTiles) {
                tileScreenRect.MoveBy(-origin);
                tileScreenRect = tileScreenRect.Intersect(currentTileRect);
                tileScreenRect.MoveBy(origin);

                if (tileScreenRect.IsEmpty())
                  continue;

                tileRegionRect = regionRect->Intersect(currentTileRect);
                tileRegionRect.MoveBy(-currentTileRect.TopLeft());
            }
            gfx::Rect rect(tileScreenRect.x, tileScreenRect.y,
                           tileScreenRect.width, tileScreenRect.height);
            gfx::Rect sourceRect(tileRegionRect.x, tileRegionRect.y,
                                 tileRegionRect.width, tileRegionRect.height);
            gfx::Rect textureRect(texRect.x, texRect.y,
                                  texRect.width, texRect.height);
            mCompositor->DrawQuad(rect, &sourceRect, &textureRect, &aClipRect, aEffectChain,
                                  aOpacity, aTransform, aOffset);
        }
      }
    }

    if (iterOnWhite)
        iterOnWhite->NextTile();
  } while (usingTiles && tileIter->NextTile());

  mTextureHost->Unlock();
}

void 
ContentHost::AddTextureHost(TextureHost* aTextureHost)
{
  //TODO[nrc] I would like to be able to assert that we can cope with the texture host
  // are we sure we don't have to?
  mTextureHost = aTextureHost;
}

TextureHost*
ContentHost::GetTextureHost()
{
  return mTextureHost.get();
}

void
ContentHostTexture::UpdateThebes(const ThebesBuffer& aNewFront,
                                 const nsIntRegion& aUpdated,
                                 OptionalThebesBuffer* aNewBack,
                                 const nsIntRegion& aOldValidRegionFront,
                                 const nsIntRegion& aOldValidRegionBack,
                                 OptionalThebesBuffer* aNewBackResult,
                                 nsIntRegion* aNewValidRegionFront,
                                 nsIntRegion* aUpdatedRegionBack,
                                 TiledLayerProperties* aLayerProperties)
{
  // updated is in screen coordinates. Convert it to buffer coordinates.
  nsIntRegion destRegion(aUpdated);
  destRegion.MoveBy(-aNewFront.rect().TopLeft());

  // Correct for rotation
  destRegion.MoveBy(aNewFront.rotation());

  gfxIntSize size = aNewFront.rect().Size();
  nsIntRect destBounds = destRegion.GetBounds();
  destRegion.MoveBy((destBounds.x >= size.width) ? -size.width : 0,
                    (destBounds.y >= size.height) ? -size.height : 0);

  // There's code to make sure that updated regions don't cross rotation
  // boundaries, so assert here that this is the case
  NS_ASSERTION(((destBounds.x % size.width) + destBounds.width <= size.width) &&
               ((destBounds.y % size.height) + destBounds.height <= size.height),
               "updated region lies across rotation boundaries!");

  mTextureHost->Update(aNewFront.buffer(), nullptr, nullptr, nullptr, &destRegion);
  mInitialised = true;

  mBufferRect = aNewFront.rect();
  mBufferRotation = aNewFront.rotation();

  *aNewBack = aNewFront;
  *aNewValidRegionFront = aOldValidRegionBack;
  *aNewBackResult = null_t();
  aUpdatedRegionBack->SetEmpty();
}

void
ContentHostDirect::UpdateThebes(const ThebesBuffer& aNewBack,
                                const nsIntRegion& aUpdated,
                                OptionalThebesBuffer* aNewFront,
                                const nsIntRegion& aOldValidRegionFront,
                                const nsIntRegion& aOldValidRegionBack,
                                OptionalThebesBuffer* aNewBackResult,
                                nsIntRegion* aNewValidRegionFront,
                                nsIntRegion* aUpdatedRegionBack,
                                TiledLayerProperties* aLayerProperties)
{
  printf("xxx ContentHostDirect::UpdateThebes\n");
  mBufferRect = aNewBack.rect();
  mBufferRotation = aNewBack.rotation();

  if (!mTextureHost) {
    *aNewFront = null_t();
    mInitialised = false;

    aNewValidRegionFront->SetEmpty();
    *aNewBackResult = null_t();
    *aUpdatedRegionBack = aUpdated;
    return;
  }

  bool needsReset;
  SurfaceDescriptor newFrontBuffer;
  mTextureHost->Update(aNewBack.buffer(), &newFrontBuffer,
                       &mInitialised, &needsReset);
  
  //TODO[nrc] if !mInitialised should we fallback to a different texturehost?
  if (!mInitialised) {
    // try falling back to a (hopefully) more reliable texture host
    mTextureHost = nullptr; //TODO[nrc] hmm, which texture host? Probably basic shmem thing, waiting for nical to finish refactoring texture hosts
    mTextureHost->Update(aNewBack.buffer(), &newFrontBuffer,
                         &mInitialised, &needsReset);
  }

  *aNewFront = ThebesBuffer(newFrontBuffer, mBufferRect, mBufferRotation);

  // We have to invalidate the pixels painted into the new buffer.
  // They might overlap with our old pixels.
  aNewValidRegionFront->Sub(needsReset ? nsIntRegion()
                                       : aOldValidRegionFront, 
                            aUpdated);
  *aNewBackResult = *aNewFront;
  *aUpdatedRegionBack = aUpdated;
}

void
TiledLayerBufferComposite::Upload(const BasicTiledLayerBuffer* aMainMemoryTiledBuffer,
                                  const nsIntRegion& aNewValidRegion,
                                  const nsIntRegion& aInvalidateRegion,
                                  const gfxSize& aResolution)
{
#ifdef GFX_TILEDLAYER_PREF_WARNINGS
  printf_stderr("Upload %i, %i, %i, %i\n", aInvalidateRegion.GetBounds().x, aInvalidateRegion.GetBounds().y, aInvalidateRegion.GetBounds().width, aInvalidateRegion.GetBounds().height);
  long start = PR_IntervalNow();
#endif

  mFrameResolution = aResolution;
  mMainMemoryTiledBuffer = aMainMemoryTiledBuffer;
  Update(aNewValidRegion, aInvalidateRegion);
  mMainMemoryTiledBuffer = nullptr;
#ifdef GFX_TILEDLAYER_PREF_WARNINGS
  if (PR_IntervalNow() - start > 10) {
    printf_stderr("Time to upload %i\n", PR_IntervalNow() - start);
  }
#endif
}

TiledTexture
TiledLayerBufferComposite::ValidateTile(TiledTexture aTile,
                                        const nsIntPoint& aTileOrigin,
                                        const nsIntRegion& aDirtyRect)
{
#ifdef GFX_TILEDLAYER_PREF_WARNINGS
  printf_stderr("Upload tile %i, %i\n", aTileOrigin.x, aTileOrigin.y);
  long start = PR_IntervalNow();
#endif

  aTile.Validate(mMainMemoryTiledBuffer->GetTile(aTileOrigin).GetSurface(), mCompositor);

#ifdef GFX_TILEDLAYER_PREF_WARNINGS
  if (PR_IntervalNow() - start > 1) {
    printf_stderr("Tile Time to upload %i\n", PR_IntervalNow() - start);
  }
#endif
  return aTile;
}

TiledContentHost::~TiledContentHost()
{
  mMainMemoryTiledBuffer.ReadUnlock();
  mLowPrecisionMainMemoryTiledBuffer.ReadUnlock();
  if (mReusableTileStore)
    delete mReusableTileStore;
}

void
TiledContentHost::MemoryPressure()
{
  if (mReusableTileStore) {
    delete mReusableTileStore;
    mReusableTileStore = new ReusableTileStoreComposite(1);
  }
}

void
TiledContentHost::PaintedTiledLayerBuffer(const BasicTiledLayerBuffer* mTiledBuffer)
{
  if (mTiledBuffer->IsLowPrecision()) {
    mLowPrecisionMainMemoryTiledBuffer.ReadUnlock();
    mLowPrecisionMainMemoryTiledBuffer = *mTiledBuffer;
    mLowPrecisionRegionToUpload.Or(mLowPrecisionRegionToUpload,
                                   mLowPrecisionMainMemoryTiledBuffer.GetPaintedRegion());
    mLowPrecisionMainMemoryTiledBuffer.ClearPaintedRegion();
    mPendingLowPrecisionUpload = true;
  } else {
    mMainMemoryTiledBuffer.ReadUnlock();
    mMainMemoryTiledBuffer = *mTiledBuffer;
    mRegionToUpload.Or(mRegionToUpload, mMainMemoryTiledBuffer.GetPaintedRegion());
    mMainMemoryTiledBuffer.ClearPaintedRegion();
    mPendingUpload = true;
  }

  // TODO: Remove me once Bug 747811 lands.
  delete mTiledBuffer;
}

void
TiledContentHost::ProcessLowPrecisionUploadQueue()
{
  if (!mPendingLowPrecisionUpload)
    return;

  mLowPrecisionRegionToUpload.And(mLowPrecisionRegionToUpload,
                                  mLowPrecisionMainMemoryTiledBuffer.GetValidRegion());
  mLowPrecisionVideoMemoryTiledBuffer.SetResolution(
    mLowPrecisionMainMemoryTiledBuffer.GetResolution());
  // XXX It's assumed that the video memory tiled buffer has an up-to-date
  //     frame resolution. As it's always updated first when zooming, this
  //     should always be true.
  mLowPrecisionVideoMemoryTiledBuffer.Upload(&mLowPrecisionMainMemoryTiledBuffer,
                                 mLowPrecisionMainMemoryTiledBuffer.GetValidRegion(),
                                 mLowPrecisionRegionToUpload,
                                 mVideoMemoryTiledBuffer.GetFrameResolution());
  nsIntRegion validRegion = mLowPrecisionVideoMemoryTiledBuffer.GetValidRegion();

  mLowPrecisionMainMemoryTiledBuffer.ReadUnlock();

  mLowPrecisionMainMemoryTiledBuffer = BasicTiledLayerBuffer();
  mLowPrecisionRegionToUpload = nsIntRegion();
  mPendingLowPrecisionUpload = false;
}

void
TiledContentHost::ProcessUploadQueue(nsIntRegion* aNewValidRegion)
{
  if (!mPendingUpload)
    return;

  if (mReusableTileStore) {
    mReusableTileStore->HarvestTiles(mLayerProperties.mVisibleRegion,
                                     mLayerProperties.mDisplayPort,
                                     &mVideoMemoryTiledBuffer,
                                     mVideoMemoryTiledBuffer.GetValidRegion(),
                                     mMainMemoryTiledBuffer.GetValidRegion(),
                                     mVideoMemoryTiledBuffer.GetFrameResolution(),
                                     mLayerProperties.mEffectiveResolution);
  }

  // If we coalesce uploads while the layers' valid region is changing we will
  // end up trying to upload area outside of the valid region. (bug 756555)
  mRegionToUpload.And(mRegionToUpload, mMainMemoryTiledBuffer.GetValidRegion());

  mVideoMemoryTiledBuffer.Upload(&mMainMemoryTiledBuffer,
                                 mMainMemoryTiledBuffer.GetValidRegion(),
                                 mRegionToUpload, mLayerProperties.mEffectiveResolution);

  *aNewValidRegion = mVideoMemoryTiledBuffer.GetValidRegion();

  mMainMemoryTiledBuffer.ReadUnlock();
  // Release all the tiles by replacing the tile buffer with an empty
  // tiled buffer. This will prevent us from doing a double unlock when
  // calling  ~TiledThebesLayerComposite.
  // FIXME: This wont be needed when we do progressive upload and lock
  // tile by tile.
  mMainMemoryTiledBuffer = BasicTiledLayerBuffer();
  mRegionToUpload = nsIntRegion();
  mPendingUpload = false;
}

void
TiledContentHost::EnsureTileStore()
{
  // We should only be retaining old tiles if we're not fixed position.
  // Fixed position layers don't/shouldn't move on the screen, so retaining
  // tiles is not useful and often results in rendering artifacts.
  if (mReusableTileStore && !mLayerProperties.mRetainTiles) {
    delete mReusableTileStore;
    mReusableTileStore = nullptr;
  } else if (gfxPlatform::UseReusableTileStore() &&
             !mReusableTileStore && mLayerProperties.mRetainTiles) {
    // XXX Add a pref for reusable tile store size
    mReusableTileStore = new ReusableTileStoreComposite(1);
  }
}

void
TiledContentHost::UpdateThebes(const ThebesBuffer& aNewBack,
                               const nsIntRegion& aUpdated,
                               OptionalThebesBuffer* aNewFront,
                               const nsIntRegion& aOldValidRegionFront,
                               const nsIntRegion& aOldValidRegionBack,
                               OptionalThebesBuffer* aNewBackResult,
                               nsIntRegion* aNewValidRegionFront,
                               nsIntRegion* aUpdatedRegionBack,
                               TiledLayerProperties* aLayerProperties)
{
  MOZ_ASSERT(aLayerProperties, "aLayerProperties required for TiledContentHost");
  mLayerProperties = *aLayerProperties;

  EnsureTileStore();

  ProcessUploadQueue(aNewValidRegionFront);
  ProcessLowPrecisionUploadQueue();

  mValidRegion = aOldValidRegionBack;
}

void
TiledContentHost::Composite(EffectChain& aEffectChain,
                            float aOpacity,
                            const gfx::Matrix4x4& aTransform,
                            const gfx::Point& aOffset,
                            const gfx::Filter& aFilter,
                            const gfx::Rect& aClipRect,
                            const nsIntRegion* aVisibleRegion /* = nullptr */)
{
  // Render old tiles to fill in gaps we haven't had the time to render yet.
  if (mReusableTileStore) {
    mReusableTileStore->DrawTiles(this,
                                  mVideoMemoryTiledBuffer.GetValidRegion(),
                                  mVideoMemoryTiledBuffer.GetFrameResolution(),
                                  aTransform, aOffset, aEffectChain, aOpacity, aFilter, aClipRect, mLayerProperties.mCompositionBounds);
  }

  // Render valid tiles.
  nsIntRect visibleRect = aVisibleRegion->GetBounds();

  RenderLayerBuffer(mLowPrecisionVideoMemoryTiledBuffer,
                    mLowPrecisionVideoMemoryTiledBuffer.GetValidRegion(), aEffectChain, aOpacity,
                    aOffset, aFilter, aClipRect, mValidRegion, visibleRect, aTransform);
  RenderLayerBuffer(mVideoMemoryTiledBuffer, mValidRegion, aEffectChain, aOpacity, aOffset, aFilter, aClipRect, nsIntRegion(), visibleRect, aTransform);
}


void
TiledContentHost::RenderTile(const TiledTexture& aTile,
                             EffectChain& aEffectChain,
                             float aOpacity,
                             const gfx::Matrix4x4& aTransform,
                             const gfx::Point& aOffset,
                             const gfx::Filter& aFilter,
                             const gfx::Rect& aClipRect,
                             const nsIntRegion& aScreenRegion,
                             const nsIntPoint& aTextureOffset,
                             const nsIntSize& aTextureBounds)
{
  MOZ_ASSERT(aTile.mTextureHost, "Trying to render a placeholder tile?");

  if (Effect* effect = aTile.mTextureHost->Lock(aFilter)) {
    aEffectChain.mEffects[effect->mType] = effect;
  } else {
    return;
  }

  nsIntRegionRectIterator it(aScreenRegion);
  for (const nsIntRect* rect = it.Next(); rect != nullptr; rect = it.Next()) {
    gfx::Rect textureRect(rect->x - aTextureOffset.x, rect->y - aTextureOffset.y,
                          rect->width, rect->height);
    gfx::Rect sourceRect(0, 0, rect->width, rect->height);
    gfx::Rect boundsRect(0, 0, aTextureBounds.width, aTextureBounds.height);
    mCompositor->DrawQuad(textureRect, &sourceRect, &boundsRect,
                          &aClipRect, aEffectChain, aOpacity, aTransform, aOffset);
  }

  aTile.mTextureHost->Unlock();
}

void
TiledContentHost::RenderLayerBuffer(TiledLayerBufferComposite& aLayerBuffer,
                                    const nsIntRegion& aValidRegion,
                                    EffectChain& aEffectChain,
                                    float aOpacity,
                                    const gfx::Point& aOffset,
                                    const gfx::Filter& aFilter,
                                    const gfx::Rect& aClipRect,
                                    const nsIntRegion& aMaskRegion,
                                    nsIntRect& visibleRect,
                                    gfx::Matrix4x4 aTransform)
{
  float resolution = aLayerBuffer.GetResolution();
  gfxSize layerScale(1, 1);
  // We assume that the current frame resolution is the one used in our primary
  // layer buffer. Compensate for a changing frame resolution.
  if (aLayerBuffer.GetFrameResolution() != mVideoMemoryTiledBuffer.GetFrameResolution()) {
    const gfxSize& layerResolution = aLayerBuffer.GetFrameResolution();
    const gfxSize& localResolution = mVideoMemoryTiledBuffer.GetFrameResolution();
    layerScale.width = layerResolution.width / localResolution.width;
    layerScale.height = layerResolution.height / localResolution.height;
    visibleRect.ScaleRoundOut(layerScale.width, layerScale.height);
  }
  aTransform.Scale(1/(resolution * layerScale.width),
                  1/(resolution * layerScale.height), 1);

  uint32_t rowCount = 0;
  uint32_t tileX = 0;
  for (int32_t x = visibleRect.x; x < visibleRect.x + visibleRect.width;) {
    rowCount++;
    int32_t tileStartX = aLayerBuffer.GetTileStart(x);
    int32_t w = aLayerBuffer.GetScaledTileLength() - tileStartX;
    if (x + w > visibleRect.x + visibleRect.width)
      w = visibleRect.x + visibleRect.width - x;
    int tileY = 0;
    for (int32_t y = visibleRect.y; y < visibleRect.y + visibleRect.height;) {
      int32_t tileStartY = aLayerBuffer.GetTileStart(y);
      int32_t h = aLayerBuffer.GetScaledTileLength() - tileStartY;
      if (y + h > visibleRect.y + visibleRect.height)
        h = visibleRect.y + visibleRect.height - y;

      TiledTexture tileTexture = aLayerBuffer.
        GetTile(nsIntPoint(aLayerBuffer.RoundDownToTileEdge(x),
                           aLayerBuffer.RoundDownToTileEdge(y)));
      if (tileTexture != aLayerBuffer.GetPlaceholderTile()) {
        nsIntRegion tileDrawRegion;
        tileDrawRegion.And(aValidRegion,
                           nsIntRect(x * layerScale.width,
                                     y * layerScale.height,
                                     w * layerScale.width,
                                     h * layerScale.height));
        tileDrawRegion.Sub(tileDrawRegion, aMaskRegion);

        if (!tileDrawRegion.IsEmpty()) {
          tileDrawRegion.ScaleRoundOut(resolution / layerScale.width,
                                       resolution / layerScale.height);

          nsIntPoint tileOffset((x - tileStartX) * resolution,
                                (y - tileStartY) * resolution);
          uint32_t tileSize = aLayerBuffer.GetTileLength();
          RenderTile(tileTexture, aEffectChain, aOpacity, aTransform, aOffset, aFilter, aClipRect, tileDrawRegion,
                     tileOffset, nsIntSize(tileSize, tileSize));
        }
      }
      tileY++;
      y += h;
    }
    tileX++;
    x += w;
  }
}


} // namespace
} // namespace
