/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_REUSABLETILESTORECOMPOSITE_H
#define GFX_REUSABLETILESTORECOMPOSITE_H

#include "nsTArray.h"
#include "nsAutoPtr.h"
#include "ContentHost.h"

namespace mozilla {

namespace gl {
class GLContext;
}

namespace layers {

// A storage class for the information required to render a single tile from
// a TiledLayerBufferComposite.
class ReusableTiledTexture
{
public:
  ReusableTiledTexture(TiledTexture aTexture,
                       const nsIntPoint& aTileOrigin,
                       const nsIntRegion& aTileRegion,
                       uint16_t aTileSize,
                       gfxSize aResolution)
    : mTexture(aTexture)
    , mTileOrigin(aTileOrigin)
    , mTileRegion(aTileRegion)
    , mTileSize(aTileSize)
    , mResolution(aResolution)
  {}

  ~ReusableTiledTexture() {}

  TiledTexture mTexture;
  const nsIntPoint mTileOrigin;
  const nsIntRegion mTileRegion;
  uint16_t mTileSize;
  gfxSize mResolution;
};

// This class will operate on a TiledLayerBufferComposite to harvest tiles that have
// rendered content that is about to become invalid. We do this so that in the
// situation that we need to render an area of a TiledThebesLayerComposite that hasn't
// been updated quickly enough, we can still display something (and hopefully
// it'll be the same as the valid rendered content). While this may end up
// showing invalid data, it should only be momentarily.
class ReusableTileStoreComposite
{
public:
  ReusableTileStoreComposite(float aSizeLimit)
    : mSizeLimit(aSizeLimit)
  {}

  ~ReusableTileStoreComposite();

  // Harvests tiles from a TiledLayerBufferComposite that are about to become
  // invalid. aOldValidRegion and aOldResolution should be the valid region
  // and resolution of the data currently in aVideoMemoryTiledBuffer, and
  // aNewValidRegion and aNewResolution should be the valid region and
  // resolution of the data that is about to update aVideoMemoryTiledBuffer.
  void HarvestTiles(const nsIntRegion& aVisibleRegion,
                    const gfxRect& aDisplayPort,
                    TiledLayerBufferComposite* aVideoMemoryTiledBuffer,
                    const nsIntRegion& aOldValidRegion,
                    const nsIntRegion& aNewValidRegion,
                    const gfxSize& aOldResolution,
                    const gfxSize& aNewResolution);

  // Draws all harvested tiles that don't intersect with the given valid region.
  // Differences in resolution will be reconciled via altering the given
  // transformation.
  void DrawTiles(TiledContentHost* aContentHost,
                 const nsIntRegion& aValidRegion,
                 const gfxSize& aResolution,
                 const gfx::Matrix4x4& aTransform,
                 const gfx::Point& aRenderOffset,
                 EffectChain& aEffectChain,
                 float aOpacity,
                 const gfx::Filter& aFilter,
                 const gfx::Rect& aClipRect,
                 const gfxRect& aBounds);

protected:
  // Invalidates tiles contained within the valid region, or intersecting with
  // the currently rendered region (discovered by looking for a display-port,
  // or failing that, looking at the widget size).
  void InvalidateTiles(const nsIntRegion& aVisibleRegion,
                       const gfxRect& aDisplayPort,
                       const nsIntRegion& aValidRegion,
                       const gfxSize& aResolution);

private:
  // This determines the maximum number of tiles stored in this tile store,
  // as a fraction of the amount of tiles stored in the TiledLayerBufferComposite
  // given to HarvestTiles.
  float mSizeLimit;

  // This stores harvested tiles, in the order in which they were harvested.
  nsTArray< nsAutoPtr<ReusableTiledTexture> > mTiles;
};

} // layers
} // mozilla

#endif // GFX_REUSABLETILESTORECOMPOSITE_H
