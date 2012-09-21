/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_LAYERIMPL_H
#define GFX_LAYERIMPL_H

#include "gfxUtils.h"
#include "CompositeLayerManager.h"
#include "LayersTypes.h"
#include "gfx2DGlue.h"

namespace mozilla {
namespace layers {

void
RenderColorLayer(ColorLayer* aLayer, Compositor *aCompositor,
                 const nsIntPoint& aOffset, const nsIntRect& aClipRect)
{
  EffectChain effects;
  gfxRGBA color(aLayer->GetColor());
  RefPtr<EffectSolidColor> effectColor = new EffectSolidColor(gfx::Color(color.r,
                                                                         color.g,
                                                                         color.b,
                                                                         color.a));
  effects.mEffects[EFFECT_SOLID_COLOR] = effectColor;
  nsIntRect visibleRect = aLayer->GetEffectiveVisibleRegion().GetBounds();

  effects.mEffects[EFFECT_MASK] =
    CompositeLayerManager::MakeMaskEffect(aLayer->GetMaskLayer());

  gfx::Rect rect(visibleRect.x, visibleRect.y, visibleRect.width, visibleRect.height);
  float opacity = aLayer->GetEffectiveOpacity();
  gfx::Matrix4x4 transform;
  ToMatrix4x4(aLayer->GetEffectiveTransform(), transform);
  gfx::Rect clipRect(aClipRect.x, aClipRect.y, aClipRect.width, aClipRect.height);
  aCompositor->DrawQuad(rect, nullptr, nullptr, &clipRect, effects, opacity,
                        transform, gfx::Point(aOffset.x, aOffset.y));
}

template<class Container>
void
ContainerInsertAfter(Container* aContainer, Layer* aChild, Layer* aAfter)
{
  aChild->SetParent(aContainer);
  if (!aAfter) {
    Layer *oldFirstChild = aContainer->GetFirstChild();
    aContainer->mFirstChild = aChild;
    aChild->SetNextSibling(oldFirstChild);
    aChild->SetPrevSibling(nullptr);
    if (oldFirstChild) {
      oldFirstChild->SetPrevSibling(aChild);
    } else {
      aContainer->mLastChild = aChild;
    }
    NS_ADDREF(aChild);
    aContainer->DidInsertChild(aChild);
    return;
  }
  for (Layer *child = aContainer->GetFirstChild(); 
       child; child = child->GetNextSibling()) {
    if (aAfter == child) {
      Layer *oldNextSibling = child->GetNextSibling();
      child->SetNextSibling(aChild);
      aChild->SetNextSibling(oldNextSibling);
      if (oldNextSibling) {
        oldNextSibling->SetPrevSibling(aChild);
      } else {
        aContainer->mLastChild = aChild;
      }
      aChild->SetPrevSibling(child);
      NS_ADDREF(aChild);
      aContainer->DidInsertChild(aChild);
      return;
    }
  }
  NS_WARNING("Failed to find aAfter layer!");
}

template<class Container>
void
ContainerRemoveChild(Container* aContainer, Layer* aChild)
{
  if (aContainer->GetFirstChild() == aChild) {
    aContainer->mFirstChild = aContainer->GetFirstChild()->GetNextSibling();
    if (aContainer->mFirstChild) {
      aContainer->mFirstChild->SetPrevSibling(nullptr);
    } else {
      aContainer->mLastChild = nullptr;
    }
    aChild->SetNextSibling(nullptr);
    aChild->SetPrevSibling(nullptr);
    aChild->SetParent(nullptr);
    aContainer->DidRemoveChild(aChild);
    NS_RELEASE(aChild);
    return;
  }
  Layer *lastChild = nullptr;
  for (Layer *child = aContainer->GetFirstChild(); child; 
       child = child->GetNextSibling()) {
    if (child == aChild) {
      // We're sure this is not our first child. So lastChild != NULL.
      lastChild->SetNextSibling(child->GetNextSibling());
      if (child->GetNextSibling()) {
        child->GetNextSibling()->SetPrevSibling(lastChild);
      } else {
        aContainer->mLastChild = lastChild;
      }
      child->SetNextSibling(nullptr);
      child->SetPrevSibling(nullptr);
      child->SetParent(nullptr);
      aContainer->DidRemoveChild(aChild);
      NS_RELEASE(aChild);
      return;
    }
    lastChild = child;
  }
}

template<class LayerT,
         class Container>
void
ContainerDestroy(Container* aContainer)
 {
  if (!aContainer->mDestroyed) {
    while (aContainer->mFirstChild) {
      static_cast<LayerT*>(aContainer->GetFirstChild()->ImplData())->Destroy();
      aContainer->RemoveChild(aContainer->mFirstChild);
    }
    aContainer->mDestroyed = true;
  }
}

template<class Container>
void
ContainerCleanupResources(Container* aContainer)
{
  for (Layer* l = aContainer->GetFirstChild(); l; l = l->GetNextSibling()) {
    CompositeLayer* layerToRender = static_cast<CompositeLayer*>(l->ImplData());
    layerToRender->CleanupResources();
  }
}

template<class LayerT>
LayerT*
GetNextSibling(LayerT* aLayer)
{
   Layer* layer = aLayer->GetLayer()->GetNextSibling();
   return layer ? static_cast<LayerT*>(layer->ImplData())
                : nullptr;
}

bool
HasOpaqueAncestorLayer(Layer* aLayer)
{
  for (Layer* l = aLayer->GetParent(); l; l = l->GetParent()) {
    if (l->GetContentFlags() & Layer::CONTENT_OPAQUE)
      return true;
  }
  return false;
}

template<class ContainerT,
         class LayerT,
         class ManagerT>
void
ContainerRender(ContainerT* aContainer,
                Surface* aPreviousSurface,
                const nsIntPoint& aOffset,
                ManagerT* aManager,
                const nsIntRect& aClipRect)
{
  /**
   * Setup our temporary surface for rendering the contents of this container.
   */
  RefPtr<Surface> surface;

  Compositor* compositor = aManager->GetCompositor();

  nsIntPoint childOffset(aOffset);
  nsIntRect visibleRect = aContainer->GetEffectiveVisibleRegion().GetBounds();

  nsIntRect cachedScissor = aClipRect;
  aContainer->mSupportsComponentAlphaChildren = false;

  float opacity = aContainer->GetEffectiveOpacity();
  bool needsSurface = aContainer->UseIntermediateSurface();
  if (needsSurface) {
    SurfaceInitMode mode = INIT_MODE_CLEAR;
    bool surfaceCopyNeeded = false;
    gfx::IntRect surfaceRect = gfx::IntRect(visibleRect.x, visibleRect.y, visibleRect.width,
                                            visibleRect.height);
    if (aContainer->GetEffectiveVisibleRegion().GetNumRects() == 1 && 
        (aContainer->GetContentFlags() & Layer::CONTENT_OPAQUE))
    {
      // don't need a background, we're going to paint all opaque stuff
      aContainer->mSupportsComponentAlphaChildren = true;
      mode = INIT_MODE_NONE;
    } else {
      const gfx3DMatrix& transform3D = aContainer->GetEffectiveTransform();
      gfxMatrix transform;
      // If we have an opaque ancestor layer, then we can be sure that
      // all the pixels we draw into are either opaque already or will be
      // covered by something opaque. Otherwise copying up the background is
      // not safe.
      if (HasOpaqueAncestorLayer(aContainer) &&
          transform3D.Is2D(&transform) && !transform.HasNonIntegerTranslation()) {
        surfaceCopyNeeded = true;
        surfaceRect.x += transform.x0;
        surfaceRect.y += transform.y0;
        aContainer->mSupportsComponentAlphaChildren = true;
      }
    }

    aManager->SaveViewport();
    surfaceRect -= gfx::IntPoint(childOffset.x, childOffset.y);
    if (surfaceCopyNeeded) {
      surface = compositor->CreateSurfaceFromSurface(surfaceRect, aPreviousSurface);
    } else {
      surface = compositor->CreateSurface(surfaceRect, mode);
    }
    compositor->SetSurfaceTarget(surface);
    childOffset.x = visibleRect.x;
    childOffset.y = visibleRect.y;
  } else {
    surface = aPreviousSurface;
    aContainer->mSupportsComponentAlphaChildren = (aContainer->GetContentFlags() & Layer::CONTENT_OPAQUE) ||
      (aContainer->GetParent() && aContainer->GetParent()->SupportsComponentAlphaChildren());
  }

  nsAutoTArray<Layer*, 12> children;
  aContainer->SortChildrenBy3DZOrder(children);

  /**
   * Render this container's contents.
   */
  for (PRUint32 i = 0; i < children.Length(); i++) {
    LayerT* layerToRender = static_cast<LayerT*>(children.ElementAt(i)->ImplData());

    if (layerToRender->GetLayer()->GetEffectiveVisibleRegion().IsEmpty()) {
      continue;
    }

    nsIntRect scissorRect = layerToRender->GetLayer()->
        CalculateScissorRect(cachedScissor, &aManager->GetWorldTransform());
    if (scissorRect.IsEmpty()) {
      continue;
    }

    layerToRender->RenderLayer(childOffset, scissorRect, surface);
    // invariant: our GL context should be current here, I don't think we can
    // assert it though
  }


  if (needsSurface) {
    // Unbind the current surface and rebind the previous one.
    compositor->SetSurfaceTarget(aPreviousSurface);
#ifdef MOZ_DUMP_PAINTING
    if (gfxUtils::sDumpPainting) {
      nsRefPtr<gfxImageSurface> surf = surface->Dump(aManager->GetCompositor());
      WriteSnapshotToDumpFile(aContainer, surf);
    }
#endif

    aManager->RestoreViewport();

    EffectChain effectChain;
    MaskType maskType = MaskNone;
    if (aContainer->GetMaskLayer()) {
      EffectMask* maskEffect = CompositeLayerManager::MakeMaskEffect(aContainer->GetMaskLayer());
      if (!aContainer->GetTransform().CanDraw2D()) {
        maskEffect->mIs3D = true;
      }
      effectChain.mEffects[EFFECT_MASK] = maskEffect;
    }

    RefPtr<Effect> effect = new EffectSurface(surface);
    effectChain.mEffects[EFFECT_SURFACE] = effect;
    gfx::Matrix4x4 transform;
    ToMatrix4x4(aContainer->GetEffectiveTransform(), transform);

    gfx::Rect rect(visibleRect.x, visibleRect.y, visibleRect.width, visibleRect.height);
    aManager->GetCompositor()->DrawQuad(rect, nullptr, nullptr, nullptr, effectChain, opacity,
                                        transform, gfx::Point(aOffset.x, aOffset.y));

  }
}

}
}
#endif
