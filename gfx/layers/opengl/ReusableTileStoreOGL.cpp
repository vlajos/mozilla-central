/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ReusableTileStoreOGL.h"

#include "GLContext.h"
#include "gfx2DGlue.h"


namespace mozilla {
namespace layers {

ReusableTileStoreComposite::~ReusableTileStoreComposite()
{
  mTiles.Clear();
}

void
ReusableTileStoreComposite::InvalidateTiles(const nsIntRegion& aVisibleRegion,
                                            const gfxRect& aDisplayPort,
                                            const nsIntRegion& aValidRegion,
                                            const gfxSize& aResolution)
{
#ifdef GFX_TILEDLAYER_PREF_WARNINGS
  printf_stderr("Invalidating reused tiles\n");
#endif

  // Iterate over existing harvested tiles and release any that are contained
  // within the new valid region, the display-port or the widget area. The
  // assumption is that anything within this area should be valid, so there's
  // no need to keep invalid tiles there.
  for (uint32_t i = 0; i < mTiles.Length();) {
    ReusableTiledTexture* tile = mTiles[i];

    nsIntRegion tileRegion = tile->mTileRegion;
    if (tile->mResolution != aResolution) {
      tileRegion.ScaleRoundOut(tile->mResolution.width / aResolution.width,
                               tile->mResolution.height / aResolution.height);
    }

    // Check if the tile region is contained within the new valid region.
    nsIntRect tileRect;
    bool release = false;
    bool forceKeep = false;
    if (aValidRegion.Contains(tile->mTileRegion)) {
      release = true;
    } else if (aVisibleRegion.Contains(tile->mTileRegion)) {
      forceKeep = true;
    } else {
      tileRect = tile->mTileRegion.GetBounds();
    }

    // Keep tiles that are within the visible region but outside of the valid
    // region, this signifies area that is in a progressive update and will
    // shortly be refreshed.
    if (forceKeep) {
      i++;
      continue;
    }

    // If the tile region wasn't contained within the valid region, check if
    // it intersects with the currently rendered region.
    if (!release) {
      if (aDisplayPort.Contains(tileRegion.GetBounds())) {
        release = true;
      }
    }

    if (release) {
#if GFX_TILEDLAYER_PREF_WARNINGS
      nsIntRect tileBounds = tile->mTileRegion.GetBounds();
      printf_stderr("Releasing obsolete reused tile at %d,%d, x%f\n",
                    tileBounds.x, tileBounds.y, tile->mResolution.width);
#endif
      mTiles.RemoveElementAt(i);
      continue;
    }

    i++;
  }
}

void
ReusableTileStoreComposite::HarvestTiles(const nsIntRegion& aVisibleRegion,
                                         const gfxRect& aDisplayPort,
                                         TiledLayerBufferComposite* aVideoMemoryTiledBuffer,
                                         const nsIntRegion& aOldValidRegion,
                                         const nsIntRegion& aNewValidRegion,
                                         const gfxSize& aOldResolution,
                                         const gfxSize& aNewResolution)
{
  NS_ASSERTION(aVideoMemoryTiledBuffer->GetResolution() == 1.0f,
               "ReusableTileStoreComposite cannot harvest scaled tiles!");

  gfxSize scaleFactor = gfxSize(aNewResolution.width / aOldResolution.width,
                                aNewResolution.height / aOldResolution.height);

#ifdef GFX_TILEDLAYER_PREF_WARNINGS
  printf_stderr("Seeing if there are any tiles we can reuse\n");
#endif

  // Iterate over the tiles and decide which ones we're going to harvest.
  // We harvest any tile that is entirely outside of the visible region, or
  // any tile that is partially outside of the visible region and whose
  // resolution has changed.
  // XXX Tile iteration needs to be abstracted, or have some utility functions
  //     to make it simpler.
  uint16_t tileSize = aVideoMemoryTiledBuffer->GetTileLength();
  nsIntRect validBounds = aOldValidRegion.GetBounds();
  for (int x = validBounds.x; x < validBounds.XMost();) {
    int w = tileSize - aVideoMemoryTiledBuffer->GetTileStart(x);
    if (x + w > validBounds.x + validBounds.width)
      w = validBounds.x + validBounds.width - x;

    for (int y = validBounds.y; y < validBounds.YMost();) {
      int h = tileSize - aVideoMemoryTiledBuffer->GetTileStart(y);
      if (y + h > validBounds.y + validBounds.height)
        h = validBounds.y + validBounds.height - y;

      // If the new valid region doesn't contain this tile region,
      // harvest the tile.
      nsIntRegion tileRegion;
      tileRegion.And(aOldValidRegion, nsIntRect(x, y, w, h));

      nsIntRegion intersectingRegion;
      bool retainTile = false;
      if (fabs(aNewResolution.width - aOldResolution.width) > 1e-6) {
        // Reconcile resolution changes.
        // If the resolution changes, we know the backing layer will have been
        // invalidated, so retain tiles that are partially encompassed by the
        // new valid area, instead of just tiles that don't intersect at all.
        nsIntRegion transformedTileRegion(tileRegion);
        transformedTileRegion.ScaleRoundOut(scaleFactor.width, scaleFactor.height);
        if (!aVisibleRegion.Contains(transformedTileRegion))
          retainTile = true;
      } else if (intersectingRegion.And(tileRegion, aVisibleRegion).IsEmpty()) {
        retainTile = true;
      }

      if (retainTile) {
        TiledTexture removedTile;
        if (aVideoMemoryTiledBuffer->RemoveTile(nsIntPoint(x, y), removedTile)) {
          ReusableTiledTexture* reusedTile =
            new ReusableTiledTexture(removedTile, nsIntPoint(x, y), tileRegion,
                                        tileSize, aOldResolution);
          mTiles.AppendElement(reusedTile);

#ifdef GFX_TILEDLAYER_PREF_WARNINGS
          bool replacedATile = false;
#endif
          // Remove any tile that is superseded by this new tile.
          // (same resolution, same area)
          for (uint32_t i = 0; i < mTiles.Length() - 1; i++) {
            // XXX Perhaps we should check the region instead of the origin
            //     so a partial tile doesn't replace a full older tile?
            if (aVideoMemoryTiledBuffer->RoundDownToTileEdge(mTiles[i]->mTileOrigin.x) == aVideoMemoryTiledBuffer->RoundDownToTileEdge(x) &&
                aVideoMemoryTiledBuffer->RoundDownToTileEdge(mTiles[i]->mTileOrigin.y) == aVideoMemoryTiledBuffer->RoundDownToTileEdge(y) &&
                abs(mTiles[i]->mResolution.width - aOldResolution.width) < 1e-5) {
              mTiles.RemoveElementAt(i);
#ifdef GFX_TILEDLAYER_PREF_WARNINGS
              replacedATile = true;
#endif
              // There should only be one similar tile
              break;
            }
          }
#ifdef GFX_TILEDLAYER_PREF_WARNINGS
          if (replacedATile) {
            printf_stderr("Replaced tile at %d,%d, x%f for reuse\n", x, y, aOldResolution.width);
          } else {
            printf_stderr("New tile at %d,%d, x%f for reuse\n", x, y, aOldResolution.width);
          }
#endif
        }
#ifdef GFX_TILEDLAYER_PREF_WARNINGS
        else
          printf_stderr("Failed to retain tile for reuse\n");
#endif
      }

      y += h;
    }

    x += w;
  }

  // Make sure we don't hold onto tiles that may cause visible rendering glitches
  InvalidateTiles(aVisibleRegion, aDisplayPort, aNewValidRegion, aNewResolution);

  // Calculate the maximum number of tiles we should have. We base this on the
  // number of tiles it would take to cover the visible region.
  uint32_t maxTiles = 0;
  nsIntRegion visibleRegion = aVisibleRegion;
  while (!visibleRegion.IsEmpty()) {
    nsIntRegionRectIterator it(visibleRegion);
    const nsIntRect* rect = it.Next();
    nsIntRect tileRect;
    tileRect.x = aVideoMemoryTiledBuffer->RoundDownToTileEdge(rect->x);
    tileRect.y = aVideoMemoryTiledBuffer->RoundDownToTileEdge(rect->y);
    tileRect.width = aVideoMemoryTiledBuffer->RoundDownToTileEdge(rect->XMost() + tileSize - 1) - tileRect.x;
    tileRect.height = aVideoMemoryTiledBuffer->RoundDownToTileEdge(rect->YMost() + tileSize - 1) - tileRect.y;
    visibleRegion.Sub(visibleRegion, tileRect);
    maxTiles += (tileRect.width / tileSize) * (tileRect.height / tileSize);
  }
  maxTiles *= mSizeLimit;

  // Now prune our reused tile store of its oldest tiles if it gets too large.
  while (mTiles.Length() > maxTiles) {
#if GFX_TILEDLAYER_PREF_WARNINGS
    nsIntRect tileBounds = mTiles[0]->mTileRegion.GetBounds();
    printf_stderr("Releasing old reused tile at %d,%d, x%f\n",
                  tileBounds.x, tileBounds.y, mTiles[0]->mResolution.width);
#endif
    mTiles.RemoveElementAt(0);
  }

#if GFX_TILEDLAYER_PREF_WARNINGS
  printf_stderr("Retained %d tiles\n", mTiles.Length());
#endif
}

void
ReusableTileStoreComposite::DrawTiles(TiledContentHost* aContentHost,
                                      const nsIntRegion& aValidRegion,
                                      const gfxSize& aResolution,
                                      const gfx::Matrix4x4& aTransform,
                                      const gfx::Point& aRenderOffset,
                                      EffectChain& aEffectChain,
                                      float aOpacity,
                                      const gfx::Filter& aFilter,
                                      const gfx::Rect& aClipRect,
                                      const gfxRect& aBounds)
{
  // Render old tiles to fill in gaps we haven't had the time to render yet.
  // Simultaneously reorder tiles in LRU order.
  nsTArray< nsAutoPtr<ReusableTiledTexture> > reorderedTiles(mTiles.Length());
  for (uint32_t i = 0, lastOldTile = 0; i < mTiles.Length(); i++) {
    ReusableTiledTexture* tile = mTiles[i];

    // Work out the scaling factor in case of resolution differences.
    gfxSize scaleFactor = gfxSize(aResolution.width / tile->mResolution.width,
                                  aResolution.height / tile->mResolution.height);

    // Reconcile the resolution difference by adjusting the transform.
    gfx::Matrix4x4 transform = aTransform;
    if (aResolution != tile->mResolution)
      transform.Scale(scaleFactor.width, scaleFactor.height, 1);
    gfx3DMatrix inverseTransform;
    To3DMatrix(transform, inverseTransform);
    inverseTransform.Invert();

    // Subtract the layer's valid region from the tile region.
    nsIntRegion transformedValidRegion(aValidRegion);
    if (aResolution != tile->mResolution)
      transformedValidRegion.ScaleRoundOut(1.0f/scaleFactor.width,
                                           1.0f/scaleFactor.height);
    nsIntRegion tileRegion;
    tileRegion.Sub(tile->mTileRegion, transformedValidRegion);

    // Intersect the tile region with the composition bounds.
    if (!aBounds.IsEmpty()) {
      // Transform the composition bounds from screen space to layer space.
      gfxRect transformedCompositionBounds = inverseTransform.TransformBounds(aBounds);
      tileRegion.And(tileRegion, nsIntRect(transformedCompositionBounds.x,
                                           transformedCompositionBounds.y,
                                           transformedCompositionBounds.width,
                                           transformedCompositionBounds.height));
    }

    // If the tile region is empty, skip drawing.
    if (tileRegion.IsEmpty()) {
      reorderedTiles.InsertElementAt(lastOldTile++, mTiles[i].forget());
      continue;
    }
    reorderedTiles.AppendElement(mTiles[i].forget());

    // XXX If we have multiple tiles covering the same area, we will
    //     end up with rendering artifacts if the layer isn't opaque.
    int32_t tileStartX;
    int32_t tileStartY;
    if (tile->mTileOrigin.x >= 0) {
      tileStartX = tile->mTileOrigin.x % tile->mTileSize;
    } else {
      tileStartX = (tile->mTileSize - (-tile->mTileOrigin.x % tile->mTileSize)) % tile->mTileSize;
    }
    if (tile->mTileOrigin.y >= 0) {
      tileStartY = tile->mTileOrigin.y % tile->mTileSize;
    } else {
      tileStartY = (tile->mTileSize - (-tile->mTileOrigin.y % tile->mTileSize)) % tile->mTileSize;
    }
    nsIntPoint tileOffset(tile->mTileOrigin.x - tileStartX, tile->mTileOrigin.y - tileStartY);
    nsIntSize textureSize(tile->mTileSize, tile->mTileSize);
    aContentHost->RenderTile(tile->mTexture, aEffectChain, aOpacity, transform, aRenderOffset, aFilter, aClipRect, tileRegion, tileOffset, textureSize);
  }

  mTiles.SwapElements(reorderedTiles);
}

} // mozilla
} // layers
