/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ipc/AutoOpenSurface.h"
#include "mozilla/layers/PLayers.h"
#include "TiledLayerBuffer.h"

/* This must occur *after* layers/PLayers.h to avoid typedefs conflicts. */
#include "mozilla/Util.h"

#include "mozilla/layers/ShadowLayers.h"

#include "ThebesLayerBuffer.h"
#include "ThebesLayerComposite.h"
#include "mozilla/layers/ContentHost.h"
#include "gfxUtils.h"
#include "gfx2DGlue.h"

#include "mozilla/layers/TextureFactoryIdentifier.h" // for TextureInfo
#include "mozilla/layers/Effects.h"

namespace mozilla {
namespace layers {

/*void
TiledThebesLayerComposite::AddTextureHost(const TextureInfo& aTextureInfo, TextureHost* aTextureHost)
{
  EnsureBuffer(aTextureInfo.imageType);

  mBuffer->AddTextureHost(aTextureInfo, aTextureHost);
}*/

ThebesLayerComposite::ThebesLayerComposite(LayerManagerComposite *aManager)
  : ShadowThebesLayer(aManager, nullptr)
  , LayerComposite(aManager)
  , mBuffer(nullptr)
{
  mImplData = static_cast<LayerComposite*>(this);
}

ThebesLayerComposite::~ThebesLayerComposite()
{}

void
ThebesLayerComposite::SetCompositableHost(CompositableHost* aHost)
{
  mBuffer= static_cast<ContentHost*>(aHost);
}

void
ThebesLayerComposite::EnsureBuffer(BufferType aHostType)
{
  if (!mBuffer ||
      mBuffer->GetType() != aHostType) {
    RefPtr<CompositableHost> bufferHost = mCompositor->CreateCompositableHost(aHostType);
#ifdef FORCE_BASICTILEDTHEBESLAYER
    NS_ASSERTION(bufferHost->GetType() == BUFFER_TILED, "bad buffer type");
#else
    NS_ASSERTION(bufferHost->GetType() == BUFFER_CONTENT ||
                 bufferHost->GetType() == BUFFER_CONTENT_DIRECT ||
                 bufferHost->GetType() == BUFFER_TILED, "bad buffer type");
#endif
    mBuffer = static_cast<AContentHost*>(bufferHost.get());
    mRequiresTiledProperties = bufferHost->GetType() == BUFFER_TILED;
  }
}

// TODO[nical] remove swap at the layer level and move it to 
// CompositableHost
// This will probably go away with buffer rotation when we
// will use tiling instead.
void
ThebesLayerComposite::SwapTexture(const ThebesBuffer& aNewFront,
                                  const nsIntRegion& aUpdatedRegion,
                                  OptionalThebesBuffer* aNewBack,
                                  nsIntRegion* aNewBackValidRegion,
                                  OptionalThebesBuffer* aReadOnlyFront,
                                  nsIntRegion* aFrontUpdatedRegion)
{
  if (mDestroyed ||
      !mBuffer) {
    // Don't drop buffers on the floor.
    *aNewBack = aNewFront;
    *aNewBackValidRegion = aNewFront.rect();
    return;
  }

  TiledLayerProperties tiledLayerProps;
  if (mRequiresTiledProperties) {
    tiledLayerProps.mVisibleRegion = GetEffectiveVisibleRegion();
    tiledLayerProps.mDisplayPort = GetDisplayPort();
    tiledLayerProps.mEffectiveResolution = GetEffectiveResolution();
    tiledLayerProps.mCompositionBounds = GetCompositionBounds();
    tiledLayerProps.mRetainTiles = !mIsFixedPosition;
  }
  
  mBuffer->UpdateThebes(aNewFront,
                        aUpdatedRegion,
                        aNewBack,
                        mValidRegionForNextBackBuffer,
                        mValidRegion,
                        aReadOnlyFront,
                        aNewBackValidRegion,
                        aFrontUpdatedRegion,
                        mRequiresTiledProperties ? &tiledLayerProps : nullptr);

  // Save the current valid region of our front buffer, because if
  // we're double buffering, it's going to be the valid region for the
  // next back buffer sent back to the renderer.
  //
  // NB: we rely here on the fact that mValidRegion is initialized to
  // empty, and that the first time Swap() is called we don't have a
  // valid front buffer that we're going to return to content.
  mValidRegionForNextBackBuffer = mValidRegion;

  //XXX[nrc] This was for tiled layers only, but I don't think we need it
  //mValidRegion = *aNewBackValidRegion;
}

void
ThebesLayerComposite::Disconnect()
{
  Destroy();
}

void
ThebesLayerComposite::Destroy()
{
  if (!mDestroyed) {
    mBuffer = nullptr;
    mDestroyed = true;
  }
}

Layer*
ThebesLayerComposite::GetLayer()
{
  return this;
}

TiledLayerComposer*
ThebesLayerComposite::GetTiledLayerComposer()
{
  return mBuffer->AsTiledLayerComposer();
}

bool
ThebesLayerComposite::IsEmpty()
{
  return !mBuffer;
}

LayerRenderState
ThebesLayerComposite::GetRenderState()
{
  if (!mBuffer || mDestroyed) {
    return LayerRenderState();
  }
  return mBuffer->GetRenderState();
}

void
ThebesLayerComposite::RenderLayer(const nsIntPoint& aOffset,
                                  const nsIntRect& aClipRect,
                                  CompositingRenderTarget* aPreviousTarget)
{
  if (mCompositeManager->CompositingDisabled()) {
    return;
  }

  if (!mBuffer) {
    return;
  }

  gfx::Matrix4x4 transform;
  ToMatrix4x4(GetEffectiveTransform(), transform);
  gfx::Rect clipRect(aClipRect.x, aClipRect.y, aClipRect.width, aClipRect.height);

#ifdef MOZ_DUMP_PAINTING
  if (gfxUtils::sDumpPainting) {
    nsRefPtr<gfxImageSurface> surf = mBuffer->Dump();
    WriteSnapshotToDumpFile(this, surf);
  }
#endif

  EffectChain effectChain;
  LayerManagerComposite::AddMaskEffect(mMaskLayer, effectChain);

  mBuffer->Composite(effectChain,
                     GetEffectiveOpacity(), 
                     transform,
                     gfx::Point(aOffset.x, aOffset.y),
                     gfx::FILTER_LINEAR,
                     clipRect,
                     &GetEffectiveVisibleRegion());
}

CompositableHost*
ThebesLayerComposite::GetCompositableHost() {
  return mBuffer.get();
}

void
ThebesLayerComposite::DestroyFrontBuffer()
{
  mBuffer = nullptr;
  mValidRegionForNextBackBuffer.SetEmpty();
}

void
ThebesLayerComposite::CleanupResources()
{
  DestroyFrontBuffer();
}

void
ThebesLayerComposite::SetAllocator(ISurfaceDeallocator* aAllocator)
{
  mBuffer->SetDeAllocator(aAllocator);
}

gfxSize
ThebesLayerComposite::GetEffectiveResolution()
{
  // Work out render resolution by multiplying the resolution of our ancestors.
  // Only container layers can have frame metrics, so we start off with a
  // resolution of 1, 1.
  // XXX For large layer trees, it would be faster to do this once from the
  //     root node upwards and store the value on each layer.
  gfxSize resolution(1, 1);
  for (ContainerLayer* parent = GetParent(); parent; parent = parent->GetParent()) {
    const FrameMetrics& metrics = parent->GetFrameMetrics();
    resolution.width *= metrics.mResolution.width;
    resolution.height *= metrics.mResolution.height;
  }

  return resolution;
}

gfxRect
ThebesLayerComposite::GetDisplayPort()
{
  // XXX We use GetTransform instead of GetEffectiveTransform in this function
  //     as we want the transform of the shadowable layers and not that of the
  //     shadow layers, which may have been modified due to async scrolling/
  //     zooming.
  gfx3DMatrix transform = GetTransform();

  // Find out the area of the nearest display-port to invalidate retained
  // tiles.
  gfxRect displayPort;
  gfxSize parentResolution = GetEffectiveResolution();
  for (ContainerLayer* parent = GetParent(); parent; parent = parent->GetParent()) {
    const FrameMetrics& metrics = parent->GetFrameMetrics();
    if (displayPort.IsEmpty()) {
      if (!metrics.mDisplayPort.IsEmpty()) {
          // We use the bounds to cut down on complication/computation time.
          // This will be incorrect when the transform involves rotation, but
          // it'd be quite hard to retain invalid tiles correctly in this
          // situation anyway.
          displayPort = gfxRect(metrics.mDisplayPort.x,
                                metrics.mDisplayPort.y,
                                metrics.mDisplayPort.width,
                                metrics.mDisplayPort.height);
          displayPort.ScaleRoundOut(parentResolution.width, parentResolution.height);
      }
      parentResolution.width /= metrics.mResolution.width;
      parentResolution.height /= metrics.mResolution.height;
    }
    if (parent->UseIntermediateSurface()) {
      transform.PreMultiply(parent->GetTransform());
    }
  }

  // If no display port was found, use the widget size from the layer manager.
  if (displayPort.IsEmpty()) {
    LayerManagerComposite* manager = static_cast<LayerManagerComposite*>(Manager());
    nsIntSize* widgetSize = manager->GetWidgetSize();
    displayPort.width = widgetSize->width;
    displayPort.height = widgetSize->height;
  }

  // Transform the display port into layer space.
  displayPort = transform.Inverse().TransformBounds(displayPort);

  return displayPort;
}

gfxRect
ThebesLayerComposite::GetCompositionBounds()
{
  // Walk up the tree, looking for a display-port - if we find one, we know
  // that this layer represents a content node and we can use its first
  // scrollable child, in conjunction with its content area and viewport offset
  // to establish the screen coordinates to which the content area will be
  // rendered.
  gfxRect compositionBounds;
  ContainerLayer* scrollableLayer = nullptr;
  for (ContainerLayer* parent = GetParent(); parent; parent = parent->GetParent()) {
    const FrameMetrics& parentMetrics = parent->GetFrameMetrics();
    if (parentMetrics.IsScrollable())
      scrollableLayer = parent;
    if (!parentMetrics.mDisplayPort.IsEmpty() && scrollableLayer) {
      // Get the composition bounds, so as not to waste rendering time.
      compositionBounds = gfxRect(parentMetrics.mCompositionBounds);

      // Calculate the scale transform applied to the root layer to determine
      // the content resolution.
      Layer* rootLayer = Manager()->GetRoot();
      const gfx3DMatrix& rootTransform = rootLayer->GetTransform();
      float scaleX = rootTransform.GetXScale();
      float scaleY = rootTransform.GetYScale();

      // Get the content document bounds, in screen-space.
      const FrameMetrics& metrics = scrollableLayer->GetFrameMetrics();
      const nsIntSize& contentSize = metrics.mContentRect.Size();
      gfx::Point scrollOffset =
        gfx::Point((metrics.mScrollOffset.x * metrics.LayersPixelsPerCSSPixel().width) / scaleX,
                   (metrics.mScrollOffset.y * metrics.LayersPixelsPerCSSPixel().height) / scaleY);
      const nsIntPoint& contentOrigin = metrics.mContentRect.TopLeft() -
        nsIntPoint(NS_lround(scrollOffset.x), NS_lround(scrollOffset.y));
      gfxRect contentRect = gfxRect(contentOrigin.x, contentOrigin.y,
                                    contentSize.width, contentSize.height);
      gfxRect contentBounds = scrollableLayer->GetEffectiveTransform().
        TransformBounds(contentRect);

      // Clip the composition bounds to the content bounds
      compositionBounds.IntersectRect(compositionBounds, contentBounds);
      break;
    }
  }

  return compositionBounds;
}


} /* layers */
} /* mozilla */
