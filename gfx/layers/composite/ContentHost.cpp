/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ipc/AutoOpenSurface.h"
#include "mozilla/layers/ShadowLayers.h"
#include "ContentHost.h"

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
  NS_ASSERTION(mInitialised, "Composite with uninitialised buffer!");

  if (!mTextureHost || !mInitialised)
    return;

  if (RefPtr<Effect> effect = mTextureHost->Lock(aFilter)) {
    if (mTextureHostOnWhite) {
      if (RefPtr<Effect> effectOnWhite = mTextureHostOnWhite->Lock(aFilter)) {
        aEffectChain.mEffects[EFFECT_COMPONENT_ALPHA] = new EffectComponentAlpha(mTextureHostOnWhite, mTextureHost);
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
  gfx::IntSize texSize = mTextureHost->GetSize();
  nsIntRect textureRect = nsIntRect(0, 0, texSize.width, texSize.height);
  textureRect.MoveBy(region.GetBounds().TopLeft());
  nsIntRegion subregion;
  subregion.And(region, textureRect);
  if (subregion.IsEmpty())  // Region is empty, nothing to draw
    return;

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

  TileIterator* tileIter = mTextureHost->GetAsTileIterator();
  TileIterator* iterOnWhite = nullptr;
  tileIter->BeginTileIteration();

  if (mTextureHostOnWhite) {
    iterOnWhite = mTextureHostOnWhite->GetAsTileIterator();
    NS_ASSERTION(tileIter->GetTileCount() == iterOnWhite->GetTileCount(),
                 "Tile count mismatch on component alpha texture");
    iterOnWhite->BeginTileIteration();
  }

  bool usingTiles = (tileIter->GetTileCount() > 1);
  do {
    if (iterOnWhite) {
      NS_ASSERTION(iterOnWhite->GetTileRect() == tileIter->GetTileRect(), "component alpha textures should be the same size.");
    }

    nsIntRect tileRect = tileIter->GetTileRect();

    // Draw texture. If we're using tiles, we do repeating manually, as texture
    // repeat would cause each individual tile to repeat instead of the
    // compound texture as a whole. This involves drawing at most 4 sections,
    // 2 for each axis that has texture repeat.
    for (int y = 0; y < (usingTiles ? 2 : 1); y++) {
      for (int x = 0; x < (usingTiles ? 2 : 1); x++) {
        nsIntRect currentTileRect(tileRect);
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
            gfx::Rect textureRect(tileRect.x, tileRect.y,
                                  tileRect.width, tileRect.height);
            mCompositor->DrawQuad(rect, &sourceRect, &textureRect, &aClipRect, aEffectChain,
                                  aOpacity, aTransform, aOffset);
        }
      }
    }

    if (iterOnWhite)
        iterOnWhite->NextTile();
  } while (tileIter->NextTile());

}


void
ContentHostTexture::UpdateThebes(const TextureIdentifier& aTextureIdentifier,
                                 const ThebesBuffer& aNewFront,
                                 const nsIntRegion& aUpdated,
                                 OptionalThebesBuffer* aNewBack,
                                 const nsIntRegion& aOldValidRegionFront,
                                 const nsIntRegion& aOldValidRegionBack,
                                 OptionalThebesBuffer* aNewBackResult,
                                 nsIntRegion* aNewValidRegionFront,
                                 nsIntRegion* aUpdatedRegionBack)
{
  AutoOpenSurface surface(OPEN_READ_ONLY, aNewFront.buffer());
  gfxASurface* updated = surface.Get();

  // updated is in screen coordinates. Convert it to buffer coordinates.
  nsIntRegion destRegion(aUpdated);
  destRegion.MoveBy(-aNewFront.rect().TopLeft());

  // Correct for rotation
  destRegion.MoveBy(aNewFront.rotation());
  gfxIntSize size = updated->GetSize();
  nsIntRect destBounds = destRegion.GetBounds();
  destRegion.MoveBy((destBounds.x >= size.width) ? -size.width : 0,
                    (destBounds.y >= size.height) ? -size.height : 0);

  // There's code to make sure that updated regions don't cross rotation
  // boundaries, so assert here that this is the case
  NS_ASSERTION(((destBounds.x % size.width) + destBounds.width <= size.width) &&
               ((destBounds.y % size.height) + destBounds.height <= size.height),
               "Updated region lies across rotation boundaries!");

  mTextureHost->Update(updated, destRegion);
  mInitialised = true;

  mBufferRect = aNewFront.rect();
  mBufferRotation = aNewFront.rotation();

  *aNewBack = aNewFront;
  *aNewValidRegionFront = aOldValidRegionBack;
  *aNewBackResult = null_t();
  aUpdatedRegionBack->SetEmpty();
}

void
ContentHostTexture::AddTextureHost(const TextureIdentifier& aTextureIdentifier, TextureHost* aTextureHost)
{
  NS_ASSERTION(aTextureIdentifier.mBufferType == BUFFER_THEBES &&
               aTextureIdentifier.mTextureType == TEXTURE_SHMEM,
               "BufferType mismatch.");
  mTextureHost = aTextureHost;
}

void
ContentHostDirect::UpdateThebes(const TextureIdentifier& aTextureIdentifier,
                                const ThebesBuffer& aNewBack,
                                const nsIntRegion& aUpdated,
                                OptionalThebesBuffer* aNewFront,
                                const nsIntRegion& aOldValidRegionFront,
                                const nsIntRegion& aOldValidRegionBack,
                                OptionalThebesBuffer* aNewBackResult,
                                nsIntRegion* aNewValidRegionFront,
                                nsIntRegion* aUpdatedRegionBack)
{
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

  AutoOpenSurface newBack(OPEN_READ_ONLY, aNewBack.buffer());
  bool needsReset = mTextureHost->GetAsBuffered()->EnsureBuffer(newBack.Size());

  ThebesBuffer newFront;
  mInitialised = mTextureHost->Update(aNewBack.buffer(), &newFront.buffer());
  newFront.rect() = mBufferRect;
  newFront.rotation() = mBufferRotation;
  *aNewFront = newFront;

  // We have to invalidate the pixels painted into the new buffer.
  // They might overlap with our old pixels.
  aNewValidRegionFront->Sub(needsReset ? nsIntRegion() : aOldValidRegionFront, aUpdated);
  *aNewBackResult = newFront;
  *aUpdatedRegionBack = aUpdated;
}

void
ContentHostDirect::AddTextureHost(const TextureIdentifier& aTextureIdentifier, TextureHost* aTextureHost)
{
  NS_ASSERTION(aTextureIdentifier.mBufferType == BUFFER_DIRECT &&
               aTextureIdentifier.mTextureType == TEXTURE_SHMEM,
               "BufferType mismatch.");
  mTextureHost = aTextureHost;
}

}
}