/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ipc/AutoOpenSurface.h"
#include "mozilla/layers/ShadowLayers.h"
#include "mozilla/layers/ContentHost.h"
#include "mozilla/layers/Effects.h"
#include "gfxPlatform.h"
#include "LayersLogging.h"
#include "nsPrintfCString.h"

namespace mozilla {
using namespace gfx;
namespace layers {

ContentHostBase::ContentHostBase(Compositor* aCompositor)
  : ContentHost(aCompositor)
  , mPaintWillResample(false)
  , mInitialised(false)
{}

ContentHostBase::~ContentHostBase()
{}

TextureHost*
ContentHostBase::GetTextureHost()
{
  return mTextureHost;
}

void
ContentHostBase::DestroyFrontHost()
{
  if (mTextureHost) {
    MOZ_ASSERT(mTextureHost->GetDeAllocator(), "We won't be able to destroy our SurfaceDescriptor");
    mTextureHost = nullptr;
  }
}

void
ContentHostBase::Composite(EffectChain& aEffectChain,
                           float aOpacity,
                           const gfx::Matrix4x4& aTransform,
                           const Point& aOffset,
                           const Filter& aFilter,
                           const Rect& aClipRect,
                           const nsIntRegion* aVisibleRegion,
                           TiledLayerProperties* aLayerProperties)
{
  NS_ASSERTION(aVisibleRegion, "Requires a visible region");

  if (!mTextureHost || !mTextureHost->Lock()) {
    return;
  }

  RefPtr<TexturedEffect> effect =
    CreateTexturedEffect(mTextureHost, aFilter);

  aEffectChain.mPrimaryEffect = effect;

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
  TextureSource* source = mTextureHost;
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
    iterOnWhite = mTextureHostOnWhite->AsTileIterator();
    NS_ASSERTION((!tileIter) || tileIter->GetTileCount() == iterOnWhite->GetTileCount(),
                 "Tile count mismatch on component alpha texture");
    if (iterOnWhite) {
      iterOnWhite->BeginTileIteration();
    }
  }

  bool usingTiles = (tileIter && tileIter->GetTileCount() > 1);
  do {
    if (iterOnWhite) {
      NS_ASSERTION(iterOnWhite->GetTileRect() == tileIter->GetTileRect(),
                   "component alpha textures should be the same size.");
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

            effect->mTextureCoords = Rect(Float(tileRegionRect.x) / texRect.width,
                                          Float(tileRegionRect.y) / texRect.height,
                                          Float(tileRegionRect.width) / texRect.width,
                                          Float(tileRegionRect.height) / texRect.height);
            GetCompositor()->DrawQuad(rect, &aClipRect, aEffectChain, aOpacity, aTransform, aOffset);
            GetCompositor()->DrawColoredBorders(gfx::Color(0.0,1.0,0.0,1.0),
                                                rect, &aClipRect, aTransform, aOffset);

        }
      }
    }

    if (iterOnWhite) {
      iterOnWhite->NextTile();
    }
  } while (usingTiles && tileIter->NextTile());

  if (tileIter) {
    tileIter->EndTileIteration();
  }
  if (iterOnWhite) {
    iterOnWhite->EndTileIteration();
  }

  mTextureHost->Unlock();
}

ContentHostSingleBuffered::~ContentHostSingleBuffered()
{
  DestroyTextures();
  DestroyFrontHost();
}

void
ContentHostSingleBuffered::SetTextureHosts(TextureHost* aNewFront,
                                           TextureHost* aNewBack /*=nullptr*/)
{
  MOZ_ASSERT(!aNewBack);
  mNewFrontHost = aNewFront;
}

void
ContentHostSingleBuffered::DestroyTextures()
{
  if (mNewFrontHost) {
    MOZ_ASSERT(mNewFrontHost->GetDeAllocator(),
               "We won't be able to destroy our SurfaceDescriptor");
    mNewFrontHost = nullptr;
  }

  // don't touch mTextureHost, we might need it for compositing
}

void
ContentHostSingleBuffered::UpdateThebes(const ThebesBufferData& aData,
                                        const nsIntRegion& aUpdated,
                                        const nsIntRegion& aOldValidRegionBack,
                                        nsIntRegion* aUpdatedRegionBack)
{
  aUpdatedRegionBack->SetEmpty();

  if (!mTextureHost && !mNewFrontHost) {
    mInitialised = false;
    return;
  }

  if (mNewFrontHost) {
    DestroyFrontHost();
    mTextureHost = mNewFrontHost;
    mNewFrontHost = nullptr;
  }

  MOZ_ASSERT(mTextureHost);

  // updated is in screen coordinates. Convert it to buffer coordinates.
  nsIntRegion destRegion(aUpdated);
  destRegion.MoveBy(-aData.rect().TopLeft());

  // Correct for rotation
  destRegion.MoveBy(aData.rotation());

  gfxIntSize size = aData.rect().Size();
  nsIntRect destBounds = destRegion.GetBounds();
  destRegion.MoveBy((destBounds.x >= size.width) ? -size.width : 0,
                    (destBounds.y >= size.height) ? -size.height : 0);

  // There's code to make sure that updated regions don't cross rotation
  // boundaries, so assert here that this is the case
  NS_ASSERTION(((destBounds.x % size.width) + destBounds.width <= size.width) &&
               ((destBounds.y % size.height) + destBounds.height <= size.height),
               "updated region lies across rotation boundaries!");

  mTextureHost->Update(*mTextureHost->GetBuffer(), &destRegion);
  mInitialised = true;

  mBufferRect = aData.rect();
  mBufferRotation = aData.rotation();
}

ContentHostDoubleBuffered::~ContentHostDoubleBuffered()
{
  DestroyTextures();
  DestroyFrontHost();
}

void
ContentHostDoubleBuffered::SetTextureHosts(TextureHost* aNewFront,
                                           TextureHost* aNewBack /*=nullptr*/)
{
  MOZ_ASSERT(aNewBack);
  // the actual TextureHosts are created in reponse to the PTexture constructor
  // we just match them up here
  mNewFrontHost = aNewFront;
  mBackHost = aNewBack;
  mBufferRect = nsIntRect();
  mBufferRotation = nsIntPoint();
}

void
ContentHostDoubleBuffered::DestroyTextures()
{
  if (mNewFrontHost) {
    MOZ_ASSERT(mNewFrontHost->GetDeAllocator(),
               "We won't be able to destroy our SurfaceDescriptor");
    mNewFrontHost = nullptr;
  }

  if (mBackHost) {
    MOZ_ASSERT(mBackHost->GetDeAllocator(),
               "We won't be able to destroy our SurfaceDescriptor");
    mBackHost = nullptr;
  }

  // don't touch mTextureHost, we might need it for compositing
}

void
ContentHostDoubleBuffered::UpdateThebes(const ThebesBufferData& aData,
                                        const nsIntRegion& aUpdated,
                                        const nsIntRegion& aOldValidRegionBack,
                                        nsIntRegion* aUpdatedRegionBack)
{
  if (!mTextureHost && !mNewFrontHost) {
    mInitialised = false;

    *aUpdatedRegionBack = aUpdated;
    return;
  }

  bool needsReset = false;
  if (mNewFrontHost) {
    DestroyFrontHost();
    mTextureHost = mNewFrontHost;
    needsReset = true;
    mNewFrontHost = nullptr;
  }

  MOZ_ASSERT(mTextureHost);
  MOZ_ASSERT(mBackHost);

  RefPtr<TextureHost> oldFront = mTextureHost;
  mTextureHost = mBackHost;
  mBackHost = oldFront;

  mTextureHost->Update(*mTextureHost->GetBuffer());
  mInitialised = true;

  mBufferRect = aData.rect();
  mBufferRotation = aData.rotation();

  *aUpdatedRegionBack = aUpdated;

  // Save the current valid region of our front buffer, because if
  // we're double buffering, it's going to be the valid region for the
  // next back buffer sent back to the renderer.
  //
  // NB: we rely here on the fact that mValidRegion is initialized to
  // empty, and that the first time Swap() is called we don't have a
  // valid front buffer that we're going to return to content.
  mValidRegionForNextBackBuffer = aOldValidRegionBack;
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

  aTile.Validate(mMainMemoryTiledBuffer->GetTile(aTileOrigin).GetSurface(), mCompositor, GetTileLength());

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
}

void
TiledContentHost::MemoryPressure()
{
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
TiledContentHost::ProcessUploadQueue(nsIntRegion* aNewValidRegion,
                                     TiledLayerProperties* aLayerProperties)
{
  if (!mPendingUpload)
    return;

  // If we coalesce uploads while the layers' valid region is changing we will
  // end up trying to upload area outside of the valid region. (bug 756555)
  mRegionToUpload.And(mRegionToUpload, mMainMemoryTiledBuffer.GetValidRegion());

  mVideoMemoryTiledBuffer.Upload(&mMainMemoryTiledBuffer,
                                 mMainMemoryTiledBuffer.GetValidRegion(),
                                 mRegionToUpload, aLayerProperties->mEffectiveResolution);

  *aNewValidRegion = mVideoMemoryTiledBuffer.GetValidRegion();

  mMainMemoryTiledBuffer.ReadUnlock();
  // Release all the tiles by replacing the tile buffer with an empty
  // tiled buffer. This will prevent us from doing a double unlock when
  // calling  ~TiledThebesLayerComposite.
  // XXX: This wont be needed when we do progressive upload and lock
  // tile by tile.
  mMainMemoryTiledBuffer = BasicTiledLayerBuffer();
  mRegionToUpload = nsIntRegion();
  mPendingUpload = false;
}

void
TiledContentHost::Composite(EffectChain& aEffectChain,
                            float aOpacity,
                            const gfx::Matrix4x4& aTransform,
                            const gfx::Point& aOffset,
                            const gfx::Filter& aFilter,
                            const gfx::Rect& aClipRect,
                            const nsIntRegion* aVisibleRegion /* = nullptr */,
                            TiledLayerProperties* aLayerProperties /* = nullptr */)
{
  MOZ_ASSERT(aLayerProperties, "aLayerProperties required for TiledContentHost");

  // note that ProcessUploadQueue updates the valid region which is then used by
  // the RenderLayerBuffer calls below and then sent back to the layer.
  ProcessUploadQueue(&aLayerProperties->mValidRegion, aLayerProperties);
  ProcessLowPrecisionUploadQueue();

  // Render valid tiles.
  nsIntRect visibleRect = aVisibleRegion->GetBounds();

  RenderLayerBuffer(mLowPrecisionVideoMemoryTiledBuffer,
                    mLowPrecisionVideoMemoryTiledBuffer.GetValidRegion(), aEffectChain, aOpacity,
                    aOffset, aFilter, aClipRect, aLayerProperties->mValidRegion, visibleRect, aTransform);
  RenderLayerBuffer(mVideoMemoryTiledBuffer, aLayerProperties->mValidRegion, aEffectChain, aOpacity, aOffset,
                    aFilter, aClipRect, nsIntRegion(), visibleRect, aTransform);
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

  RefPtr<TexturedEffect> effect =
    CreateTexturedEffect(aTile.mTextureHost, aFilter);
  if (aTile.mTextureHost->Lock()) {
    aEffectChain.mPrimaryEffect = effect;
  } else {
    return;
  }

  nsIntRegionRectIterator it(aScreenRegion);
  for (const nsIntRect* rect = it.Next(); rect != nullptr; rect = it.Next()) {
    Rect graphicsRect(rect->x, rect->y, rect->width, rect->height);
    Rect textureRect(rect->x - aTextureOffset.x, rect->y - aTextureOffset.y,
                     rect->width, rect->height);

    effect->mTextureCoords = Rect(textureRect.x / aTextureBounds.width,
                                  textureRect.y / aTextureBounds.height,
                                  textureRect.width / aTextureBounds.width,
                                  textureRect.height / aTextureBounds.height);
    mCompositor->DrawQuad(graphicsRect, &aClipRect, aEffectChain, aOpacity, aTransform, aOffset);
    mCompositor->DrawColoredBorders(gfx::Color(0.0,0.5,0.0,1.0),
                                    graphicsRect, &aClipRect, aTransform, aOffset);
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
                                    nsIntRect aVisibleRect,
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
    aVisibleRect.ScaleRoundOut(layerScale.width, layerScale.height);
  }
  aTransform.Scale(1/(resolution * layerScale.width),
                   1/(resolution * layerScale.height), 1);

  uint32_t rowCount = 0;
  uint32_t tileX = 0;
  for (int32_t x = aVisibleRect.x; x < aVisibleRect.x + aVisibleRect.width;) {
    rowCount++;
    int32_t tileStartX = aLayerBuffer.GetTileStart(x);
    int32_t w = aLayerBuffer.GetScaledTileLength() - tileStartX;
    if (x + w > aVisibleRect.x + aVisibleRect.width)
      w = aVisibleRect.x + aVisibleRect.width - x;
    int tileY = 0;
    for (int32_t y = aVisibleRect.y; y < aVisibleRect.y + aVisibleRect.height;) {
      int32_t tileStartY = aLayerBuffer.GetTileStart(y);
      int32_t h = aLayerBuffer.GetScaledTileLength() - tileStartY;
      if (y + h > aVisibleRect.y + aVisibleRect.height)
        h = aVisibleRect.y + aVisibleRect.height - y;

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

void
TiledTexture::Validate(gfxReusableSurfaceWrapper* aReusableSurface, Compositor* aCompositor, uint16_t aSize) {
  TextureFlags flags = 0;
  if (!mTextureHost) {
    // convert placeholder tile to a real tile
    mTextureHost = TextureHost::CreateTextureHost(SurfaceDescriptor::Tnull_t,
                                                  TEXTURE_HOST_TILED,
                                                  NoFlags);
    mTextureHost->SetCompositor(aCompositor);
    flags |= NewTile;
  }

  mTextureHost->Update(aReusableSurface, flags, gfx::IntSize(aSize, aSize));
}

#ifdef MOZ_LAYERS_HAVE_LOG
void
ContentHostSingleBuffered::PrintInfo(nsACString& aTo, const char* aPrefix)
{
  aTo += aPrefix;
  aTo += nsPrintfCString("ContentHostSingleBuffered (0x%p)", this);

  AppendToString(aTo, mBufferRect, " [buffer-rect=", "]");
  AppendToString(aTo, mBufferRotation, " [buffer-rotation=", "]");
  if (PaintWillResample()) {
    aTo += " [paint-will-resample]";
  }

  nsAutoCString pfx(aPrefix);
  pfx += "  ";

  if (mTextureHost) {
    aTo += "\n";
    mTextureHost->PrintInfo(aTo, pfx.get());
  }
}

void
ContentHostDoubleBuffered::PrintInfo(nsACString& aTo, const char* aPrefix)
{
  aTo += aPrefix;
  aTo += nsPrintfCString("ContentHostDoubleBuffered (0x%p)", this);

  AppendToString(aTo, mBufferRect, " [buffer-rect=", "]");
  AppendToString(aTo, mBufferRotation, " [buffer-rotation=", "]");
  if (PaintWillResample()) {
    aTo += " [paint-will-resample]";
  }

  nsAutoCString pfx(aPrefix);
  pfx += "  ";

  if (mTextureHost) {
    aTo += "\n";
    mTextureHost->PrintInfo(aTo, pfx.get());
  }

  if (mBackHost) {
    aTo += "\n";
    mBackHost->PrintInfo(aTo, pfx.get());
  }
}

void
TiledContentHost::PrintInfo(nsACString& aTo, const char* aPrefix)
{
  aTo += aPrefix;
  aTo += nsPrintfCString("TiledContentHost (0x%p)", this);

}
#endif


} // namespace
} // namespace
