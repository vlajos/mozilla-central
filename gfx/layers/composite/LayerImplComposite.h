/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_LAYERIMPL_H
#define GFX_LAYERIMPL_H

#include "gfxUtils.h"
#include "LayerManagerComposite.h"
#include "LayersTypes.h"
#include "gfx2DGlue.h"
#include "mozilla/layers/Effects.h"

namespace mozilla {
namespace layers {

void
RenderColorLayer(ColorLayer* aLayer, Compositor *aCompositor,
                 const nsIntPoint& aOffset, const nsIntRect& aClipRect);

template<class ContainerT,
         class LayerT,
         class ManagerT>
class ContainerLayerImpl
{
protected:
  ContainerLayerImpl() {}

  void
  ContainerInsertAfter(ContainerT* aContainer, Layer* aChild, Layer* aAfter)
  {
    NS_ASSERTION(aChild->Manager() == aContainer->Manager(),
                 "Child has wrong manager");
    NS_ASSERTION(!aChild->GetParent(),
                 "aChild already in the tree");
    NS_ASSERTION(!aChild->GetNextSibling() && !aChild->GetPrevSibling(),
                 "aChild already has siblings?");
    NS_ASSERTION(!aAfter ||
                 (aAfter->Manager() == aContainer->Manager() &&
                  aAfter->GetParent() == aContainer),
                 "aAfter is not our child");

    aChild->SetParent(aContainer);
    if (aAfter == aContainer->mLastChild) {
      aContainer->mLastChild = aChild;
    }
    if (!aAfter) {
      aChild->SetNextSibling(aContainer->mFirstChild);
      if (aContainer->mFirstChild) {
        aContainer->mFirstChild->SetPrevSibling(aChild);
      }
      aContainer->mFirstChild = aChild;
      NS_ADDREF(aChild);
      aContainer->DidInsertChild(aChild);
      return;
    }

    Layer* next = aAfter->GetNextSibling();
    aChild->SetNextSibling(next);
    aChild->SetPrevSibling(aAfter);
    if (next) {
      next->SetPrevSibling(aChild);
    }
    aAfter->SetNextSibling(aChild);
    NS_ADDREF(aChild);
    aContainer->DidInsertChild(aChild);
  }

  void
  ContainerRemoveChild(ContainerT* aContainer, Layer* aChild)
  {
    NS_ASSERTION(aChild->Manager() == aContainer->Manager(),
                 "Child has wrong manager");
    NS_ASSERTION(aChild->GetParent() == aContainer,
                 "aChild not our child");

    Layer* prev = aChild->GetPrevSibling();
    Layer* next = aChild->GetNextSibling();
    if (prev) {
      prev->SetNextSibling(next);
    } else {
      aContainer->mFirstChild = next;
    }
    if (next) {
      next->SetPrevSibling(prev);
    } else {
      aContainer->mLastChild = prev;
    }

    aChild->SetNextSibling(nullptr);
    aChild->SetPrevSibling(nullptr);
    aChild->SetParent(nullptr);

    aContainer->DidRemoveChild(aChild);
    NS_RELEASE(aChild);
  }

  void
  ContainerRepositionChild(ContainerT* aContainer, Layer* aChild, Layer* aAfter)
  {
    NS_ASSERTION(aChild->Manager() == aContainer->Manager(),
                 "Child has wrong manager");
    NS_ASSERTION(aChild->GetParent() == aContainer,
                 "aChild not our child");
    NS_ASSERTION(!aAfter ||
                 (aAfter->Manager() == aContainer->Manager() &&
                  aAfter->GetParent() == aContainer),
                 "aAfter is not our child");

    Layer* prev = aChild->GetPrevSibling();
    Layer* next = aChild->GetNextSibling();
    if (prev == aAfter) {
      // aChild is already in the correct position, nothing to do.
      return;
    }
    if (prev) {
      prev->SetNextSibling(next);
    }
    if (next) {
      next->SetPrevSibling(prev);
    }
    if (!aAfter) {
      aChild->SetPrevSibling(nullptr);
      aChild->SetNextSibling(aContainer->mFirstChild);
      if (aContainer->mFirstChild) {
        aContainer->mFirstChild->SetPrevSibling(aChild);
      }
      aContainer->mFirstChild = aChild;
      return;
    }

    Layer* afterNext = aAfter->GetNextSibling();
    if (afterNext) {
      afterNext->SetPrevSibling(aChild);
    } else {
      aContainer->mLastChild = aChild;
    }
    aAfter->SetNextSibling(aChild);
    aChild->SetPrevSibling(aAfter);
    aChild->SetNextSibling(afterNext);
  }

  void
  ContainerDestroy(ContainerT* aContainer)
   {
    if (!aContainer->mDestroyed) {
      while (aContainer->mFirstChild) {
        static_cast<LayerT*>(aContainer->GetFirstChild()->ImplData())->Destroy();
        aContainer->RemoveChild(aContainer->mFirstChild);
      }
      aContainer->mDestroyed = true;
    }
  }

  void
  ContainerCleanupResources(ContainerT* aContainer)
  {
    for (Layer* l = aContainer->GetFirstChild(); l; l = l->GetNextSibling()) {
      LayerT* layerToRender = static_cast<LayerT*>(l->ImplData());
      layerToRender->CleanupResources();
    }
  }

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

  void
  ContainerRender(ContainerT* aContainer,
                  CompositingRenderTarget* aPreviousTarget,
                  const nsIntPoint& aOffset,
                  ManagerT* aManager,
                  const nsIntRect& aClipRect)
  {
    /**
     * Setup our temporary surface for rendering the contents of this container.
     */
    RefPtr<CompositingRenderTarget> surface;

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
      // we're about to create a framebuffer backed by textures to use as an intermediate
      // surface. What to do if its size (as given by framebufferRect) would exceed the
      // maximum texture size supported by the GL? The present code chooses the compromise
      // of just clamping the framebuffer's size to the max supported size.
      // This gives us a lower resolution rendering of the intermediate surface (children layers).
      // See bug 827170 for a discussion.
      int32_t maxTextureSize = compositor->GetMaxTextureSize();
      surfaceRect.width = std::min(maxTextureSize, surfaceRect.width);
      surfaceRect.height = std::min(maxTextureSize, surfaceRect.height);
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
          mode = gfxPlatform::GetPlatform()->UsesSubpixelAATextRendering() ?
                                              INIT_MODE_COPY : INIT_MODE_CLEAR;
          surfaceCopyNeeded = (mode == INIT_MODE_COPY);
          surfaceRect.x += transform.x0;
          surfaceRect.y += transform.y0;
          aContainer->mSupportsComponentAlphaChildren
            = gfxPlatform::GetPlatform()->UsesSubpixelAATextRendering();
        }
      }

      aManager->SaveViewport();
      surfaceRect -= gfx::IntPoint(childOffset.x, childOffset.y);
      if (!aManager->CompositingDisabled()) {
        if (surfaceCopyNeeded) {
          surface = compositor->CreateRenderTargetFromSource(surfaceRect, aPreviousTarget);
        } else {
          surface = compositor->CreateRenderTarget(surfaceRect, mode);
        }
        compositor->SetRenderTarget(surface);
      }
      childOffset.x = visibleRect.x;
      childOffset.y = visibleRect.y;
    } else {
      surface = aPreviousTarget;
      aContainer->mSupportsComponentAlphaChildren = (aContainer->GetContentFlags() & Layer::CONTENT_OPAQUE) ||
        (aContainer->GetParent() && aContainer->GetParent()->SupportsComponentAlphaChildren());
    }

    nsAutoTArray<Layer*, 12> children;
    aContainer->SortChildrenBy3DZOrder(children);

    /**
     * Render this container's contents.
     */
    for (uint32_t i = 0; i < children.Length(); i++) {
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
      compositor->SetRenderTarget(aPreviousTarget);
#ifdef MOZ_DUMP_PAINTING
      if (gfxUtils::sDumpPainting) {
        nsRefPtr<gfxImageSurface> surf = surface->Dump(aManager->GetCompositor());
        WriteSnapshotToDumpFile(aContainer, surf);
      }
#endif

      aManager->RestoreViewport();
      if (!aManager->CompositingDisabled()) {
        EffectChain effectChain;
        if (aContainer->GetMaskLayer()) {
          bool is3D = !aContainer->GetTransform().CanDraw2D();
          LayerManagerComposite::AddMaskEffect(aContainer->GetMaskLayer(), effectChain, is3D);
        }

        RefPtr<Effect> effect = new EffectRenderTarget(surface);
        effectChain.mEffects[EFFECT_RENDER_TARGET] = effect;
        gfx::Matrix4x4 transform;
        ToMatrix4x4(aContainer->GetEffectiveTransform(), transform);

        gfx::Rect rect(visibleRect.x, visibleRect.y, visibleRect.width, visibleRect.height);
        aManager->GetCompositor()->DrawQuad(rect, nullptr, nullptr, nullptr, effectChain, opacity,
                                            transform, gfx::Point(aOffset.x, aOffset.y));
      }
    }
  }
};

}
}
#endif
