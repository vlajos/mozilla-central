/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BasicThebesLayer.h"
#include "BasicTiledThebesLayer.h"
#include "gfxUtils.h"
#include "nsIWidget.h"
#include "RenderTrace.h"
#include "sampler.h"

#include "prprf.h"

using namespace mozilla::gfx;

namespace mozilla {
namespace layers {

static nsIntRegion
IntersectWithClip(const nsIntRegion& aRegion, gfxContext* aContext)
{
  gfxRect clip = aContext->GetClipExtents();
  clip.RoundOut();
  nsIntRect r(clip.X(), clip.Y(), clip.Width(), clip.Height());
  nsIntRegion result;
  result.And(aRegion, r);
  return result;
}

static void
SetAntialiasingFlags(Layer* aLayer, gfxContext* aTarget)
{
  if (!aTarget->IsCairo()) {
    RefPtr<DrawTarget> dt = aTarget->GetDrawTarget();

    if (dt->GetFormat() != FORMAT_B8G8R8A8) {
      return;
    }

    const nsIntRect& bounds = aLayer->GetVisibleRegion().GetBounds();
    gfx::Rect transformedBounds = dt->GetTransform().TransformBounds(gfx::Rect(Float(bounds.x), Float(bounds.y),
                                                                     Float(bounds.width), Float(bounds.height)));
    transformedBounds.RoundOut();
    IntRect intTransformedBounds;
    transformedBounds.ToIntRect(&intTransformedBounds);
    dt->SetPermitSubpixelAA(!(aLayer->GetContentFlags() & Layer::CONTENT_COMPONENT_ALPHA) ||
                            dt->GetOpaqueRect().Contains(intTransformedBounds));
  } else {
    nsRefPtr<gfxASurface> surface = aTarget->CurrentSurface();
    if (surface->GetContentType() != gfxASurface::CONTENT_COLOR_ALPHA) {
      // Destination doesn't have alpha channel; no need to set any special flags
      return;
    }

    const nsIntRect& bounds = aLayer->GetVisibleRegion().GetBounds();
    surface->SetSubpixelAntialiasingEnabled(
        !(aLayer->GetContentFlags() & Layer::CONTENT_COMPONENT_ALPHA) ||
        surface->GetOpaqueRect().Contains(
          aTarget->UserToDevice(gfxRect(bounds.x, bounds.y, bounds.width, bounds.height))));
  }
}

void
BasicThebesLayer::PaintThebes(gfxContext* aContext,
                              Layer* aMaskLayer,
                              LayerManager::DrawThebesLayerCallback aCallback,
                              void* aCallbackData,
                              ReadbackProcessor* aReadback)
{
  SAMPLE_LABEL("BasicThebesLayer", "PaintThebes");
  NS_ASSERTION(BasicManager()->InDrawing(),
               "Can only draw in drawing phase");
  nsRefPtr<gfxASurface> targetSurface = aContext->CurrentSurface();

  if (!mContentClient) {
    mContentClient = new ContentClientBasic(nullptr,BasicManager()); // TODO[nical] use a forwarder!
  }

  nsTArray<ReadbackProcessor::Update> readbackUpdates;
  if (aReadback && UsedForReadback()) {
    aReadback->GetThebesLayerUpdates(this, &readbackUpdates);
  }
  mContentClient->SyncFrontBufferToBackBuffer();

  bool canUseOpaqueSurface = CanUseOpaqueSurface();
  ContentType contentType =
    canUseOpaqueSurface ? gfxASurface::CONTENT_COLOR :
                          gfxASurface::CONTENT_COLOR_ALPHA;
  float opacity = GetEffectiveOpacity();
  
  if (!BasicManager()->IsRetained()) {
    NS_ASSERTION(readbackUpdates.IsEmpty(), "Can't do readback for non-retained layer");

    mValidRegion.SetEmpty();
    mContentClient->Clear();

    nsIntRegion toDraw = IntersectWithClip(GetEffectiveVisibleRegion(), aContext);

    RenderTraceInvalidateStart(this, "FFFF00", toDraw.GetBounds());

    if (!toDraw.IsEmpty() && !IsHidden()) {
      if (!aCallback) {
        BasicManager()->SetTransactionIncomplete();
        return;
      }

      aContext->Save();

      bool needsClipToVisibleRegion = GetClipToVisibleRegion();
      bool needsGroup =
          opacity != 1.0 || GetOperator() != gfxContext::OPERATOR_OVER || aMaskLayer;
      nsRefPtr<gfxContext> groupContext;
      if (needsGroup) {
        groupContext =
          BasicManager()->PushGroupForLayer(aContext, this, toDraw,
                                            &needsClipToVisibleRegion);
        if (GetOperator() != gfxContext::OPERATOR_OVER) {
          needsClipToVisibleRegion = true;
        }
      } else {
        groupContext = aContext;
      }
      SetAntialiasingFlags(this, groupContext);
      aCallback(this, groupContext, toDraw, nsIntRegion(), aCallbackData);
      if (needsGroup) {
        BasicManager()->PopGroupToSourceWithCachedSurface(aContext, groupContext);
        if (needsClipToVisibleRegion) {
          gfxUtils::ClipToRegion(aContext, toDraw);
        }
        AutoSetOperator setOperator(aContext, GetOperator());
        PaintWithMask(aContext, opacity, aMaskLayer);
      }

      aContext->Restore();
    }

    RenderTraceInvalidateEnd(this, "FFFF00");
    return;
  }

  {
    uint32_t flags = 0;
#ifndef MOZ_WIDGET_ANDROID
    if (BasicManager()->CompositorMightResample()) {
      flags |= ThebesLayerBuffer::PAINT_WILL_RESAMPLE;
    }
    if (!(flags & ThebesLayerBuffer::PAINT_WILL_RESAMPLE)) {
      if (MayResample()) {
        flags |= ThebesLayerBuffer::PAINT_WILL_RESAMPLE;
      }
    }
#endif
    if (mDrawAtomically) {
      flags |= ThebesLayerBuffer::PAINT_NO_ROTATION;
    }
    PaintState state =
      mContentClient->BeginPaintBuffer(this, contentType, flags);
    mValidRegion.Sub(mValidRegion, state.mRegionToInvalidate);

    if (state.mContext) {
      // The area that became invalid and is visible needs to be repainted
      // (this could be the whole visible area if our buffer switched
      // from RGB to RGBA, because we might need to repaint with
      // subpixel AA)
      state.mRegionToInvalidate.And(state.mRegionToInvalidate,
                                    GetEffectiveVisibleRegion());
      nsIntRegion extendedDrawRegion = state.mRegionToDraw;
      SetAntialiasingFlags(this, state.mContext);

      RenderTraceInvalidateStart(this, "FFFF00", state.mRegionToDraw.GetBounds());

      PaintBuffer(state.mContext,
                  state.mRegionToDraw, extendedDrawRegion, state.mRegionToInvalidate,
                  state.mDidSelfCopy,
                  aCallback, aCallbackData);
      Mutated();

      RenderTraceInvalidateEnd(this, "FFFF00");
    } else {
      // It's possible that state.mRegionToInvalidate is nonempty here,
      // if we are shrinking the valid region to nothing. So use mRegionToDraw
      // instead.
      NS_WARN_IF_FALSE(state.mRegionToDraw.IsEmpty(),
                       "No context when we have something to draw, resource exhaustion?");
    }
  }

  if (BasicManager()->IsTransactionIncomplete())
    return;

  gfxRect clipExtents;
  clipExtents = aContext->GetClipExtents();

  // Pull out the mask surface and transform here, because the mask
  // is internal to basic layers
  AutoMaskData mask;
  gfxASurface* maskSurface = nullptr;
  const gfxMatrix* maskTransform = nullptr;
  if (GetMaskData(aMaskLayer, &mask)) {
    maskSurface = mask.GetSurface();
    maskTransform = &mask.GetTransform();
  } 

  if (!IsHidden() && !clipExtents.IsEmpty()) {
    AutoSetOperator setOperator(aContext, GetOperator());
    mContentClient->DrawTo(this, aContext, opacity, maskSurface, maskTransform);
  }

  for (uint32_t i = 0; i < readbackUpdates.Length(); ++i) {
    ReadbackProcessor::Update& update = readbackUpdates[i];
    nsIntPoint offset = update.mLayer->GetBackgroundLayerOffset();
    nsRefPtr<gfxContext> ctx =
      update.mLayer->GetSink()->BeginUpdate(update.mUpdateRect + offset,
                                            update.mSequenceCounter);
    if (ctx) {
      NS_ASSERTION(opacity == 1.0, "Should only read back opaque layers");
      ctx->Translate(gfxPoint(offset.x, offset.y));
      mContentClient->DrawTo(this, ctx, 1.0, maskSurface, maskTransform);
      update.mLayer->GetSink()->EndUpdate(ctx, update.mUpdateRect + offset);
    }
  }
}

//TODO[nrc] just get rid of this
struct AutoPaintClient {
  AutoPaintClient(ContentClient* aContentClient)
    : mContentClient(aContentClient)
  {
    mContentClient->BeginPaint();
  }
  ~AutoPaintClient()
  {
    mContentClient->EndPaint();
  }

private:
  ContentClient* mContentClient;
};

BasicShadowableThebesLayer::~BasicShadowableThebesLayer()
{
  MOZ_COUNT_DTOR(BasicShadowableThebesLayer);
}

void
BasicShadowableThebesLayer::PaintThebes(gfxContext* aContext,
                                        Layer* aMaskLayer,
                                        LayerManager::DrawThebesLayerCallback aCallback,
                                        void* aCallbackData,
                                        ReadbackProcessor* aReadback)
{
  if (!HasShadow()) {
    BasicThebesLayer::PaintThebes(aContext, aMaskLayer, aCallback, aCallbackData, aReadback);
    return;
  }

  if (aMaskLayer) {
    static_cast<BasicImplData*>(aMaskLayer->ImplData())
      ->Paint(aContext, nullptr);
  }
  
  if (!mContentClient) {
    mContentClient = BasicManager()->CreateContentClientFor(BUFFER_CONTENT_DIRECT, this, NoFlags);
    if (!mContentClient) {
      return;
    }
    mContentClient->Connect();
    mContentClient->GetForwarder()->Attach(mContentClient, this);
    MOZ_ASSERT(mContentClient->GetForwarder());
  }
  
  AutoPaintClient autoPaint(mContentClient);
  BasicThebesLayer::PaintThebes(aContext, nullptr, aCallback, aCallbackData, aReadback);
}

void
BasicShadowableThebesLayer::PaintBuffer(gfxContext* aContext,
                                        const nsIntRegion& aRegionToDraw,
                                        const nsIntRegion& aExtendedRegionToDraw,
                                        const nsIntRegion& aRegionToInvalidate,
                                        bool aDidSelfCopy,
                                        LayerManager::DrawThebesLayerCallback aCallback,
                                        void* aCallbackData)
{
  ContentClientRemote* contentClientRemote = static_cast<ContentClientRemote*>(mContentClient.get());
  if (!mContentClient->GetIPDLActor()) {
    mContentClient->Connect();
    mContentClient->GetForwarder()->Attach(mContentClient, this);
    MOZ_ASSERT(mContentClient->GetIPDLActor());
  }
  MOZ_ASSERT(contentClientRemote->GetIPDLActor());
  Base::PaintBuffer(aContext,
                    aRegionToDraw, aExtendedRegionToDraw, aRegionToInvalidate,
                    aDidSelfCopy,
                    aCallback, aCallbackData);
  if (!HasShadow() || BasicManager()->IsTransactionIncomplete()) {
    return;
  }

  // Hold(this) ensures this layer is kept alive through the current transaction
  // The ContentClient assumes this layer is kept alive (e.g., in CreateBuffer),
  // so deleting this Hold for whatever reason will break things.
  contentClientRemote->Updated(BasicManager()->Hold(this),
                               aRegionToDraw,
                               mVisibleRegion,
                               aDidSelfCopy);
}

void
BasicShadowableThebesLayer::Disconnect()
{
  mContentClient = nullptr;
  BasicShadowableLayer::Disconnect();
}

class ShadowThebesLayerBuffer : public ThebesLayerBuffer
{
  typedef ThebesLayerBuffer Base;

public:
  ShadowThebesLayerBuffer()
    : Base(ContainsVisibleBounds)
  {
    MOZ_COUNT_CTOR(ShadowThebesLayerBuffer);
  }

  ~ShadowThebesLayerBuffer()
  {
    MOZ_COUNT_DTOR(ShadowThebesLayerBuffer);
  }

  /**
   * Swap in the old "virtual buffer" (see above) attributes in aNew*
   * and return the old ones in aOld*.
   *
   * Swap() must only be called when the buffer is in its "unmapped"
   * state, that is the underlying gfxASurface is not available.  It
   * is expected that the owner of this buffer holds an unmapped
   * SurfaceDescriptor as the backing storage for this buffer.  That's
   * why no gfxASurface or SurfaceDescriptor parameters appear here.
   */
  void Swap(const nsIntRect& aNewRect, const nsIntPoint& aNewRotation,
            nsIntRect* aOldRect, nsIntPoint* aOldRotation)
  {
    *aOldRect = BufferRect();
    *aOldRotation = BufferRotation();

    nsRefPtr<gfxASurface> oldBuffer;
    oldBuffer = SetBuffer(nullptr, aNewRect, aNewRotation);
    MOZ_ASSERT(!oldBuffer);
  }

  /**
   * When BasicThebesLayerBuffer is used with layers that hold
   * SurfaceDescriptor, this buffer only has a valid gfxASurface in
   * the scope of an AutoOpenSurface for that SurfaceDescriptor.  That
   * is, it's sort of a "virtual buffer" that's only mapped an
   * unmapped within the scope of AutoOpenSurface.  None of the
   * underlying buffer attributes (rect, rotation) are affected by
   * mapping/unmapping.
   *
   * These helpers just exist to provide more descriptive names of the
   * map/unmap process.
   */
  void ProvideBuffer(AutoOpenSurface* aProvider)
  {
    SetBufferProvider(aProvider);
  }
  void RevokeBuffer()
  {
    SetBufferProvider(nullptr);
  }

protected:
  virtual already_AddRefed<gfxASurface>
  CreateBuffer(ContentType, const nsIntSize&, uint32_t)
  {
    NS_RUNTIMEABORT("ShadowThebesLayer can't paint content");
    return nullptr;
  }
};

already_AddRefed<ThebesLayer>
BasicLayerManager::CreateThebesLayer()
{
  NS_ASSERTION(InConstruction(), "Only allowed in construction phase");
  nsRefPtr<ThebesLayer> layer = new BasicThebesLayer(this);
  return layer.forget();
}

already_AddRefed<ThebesLayer>
BasicShadowLayerManager::CreateThebesLayer()
{
  NS_ASSERTION(InConstruction(), "Only allowed in construction phase");
#ifdef FORCE_BASICTILEDTHEBESLAYER
  if (HasShadowManager() && GetParentBackendType() == LAYERS_OPENGL) {
    // BasicTiledThebesLayer doesn't support main
    // thread compositing so only return this layer
    // type if we have a shadow manager.
    nsRefPtr<BasicTiledThebesLayer> layer =
      new BasicTiledThebesLayer(this);
    MAYBE_CREATE_SHADOW(Thebes);
    return layer.forget();
  } else
#endif
  {
    nsRefPtr<BasicShadowableThebesLayer> layer =
      new BasicShadowableThebesLayer(this);
    MAYBE_CREATE_SHADOW(Thebes);
    return layer.forget();
  }
}


}
}
