/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ContainerLayerComposite.h"
#include "gfxUtils.h"
#include "mozilla/layers/Compositor.h"
#include "mozilla/layers/LayersTypes.h"
#include "gfx2DGlue.h"

namespace mozilla {
namespace layers {


ContainerLayerComposite::ContainerLayerComposite(LayerManagerComposite *aManager)
  : ShadowContainerLayer(aManager, nullptr)
  , LayerComposite(aManager)
{
  MOZ_COUNT_CTOR(ContainerLayerComposite);
  mImplData = static_cast<LayerComposite*>(this);
}
 
ContainerLayerComposite::~ContainerLayerComposite()
{
  MOZ_COUNT_DTOR(ContainerLayerComposite);

  // We don't Destroy() on destruction here because this destructor
  // can be called after remote content has crashed, and it may not be
  // safe to free the IPC resources of our children.  Those resources
  // are automatically cleaned up by IPDL-generated code.
  //
  // In the common case of normal shutdown, either
  // LayerManagerOGL::Destroy(), a parent
  // *ContainerLayerOGL::Destroy(), or Disconnect() will trigger
  // cleanup of our resources.
  while (mFirstChild) {
    ContainerRemoveChild(this, mFirstChild);
  }
}

void
ContainerLayerComposite::InsertAfter(Layer* aChild, Layer* aAfter)
{
  ContainerInsertAfter(this, aChild, aAfter);
}

void
ContainerLayerComposite::RemoveChild(Layer *aChild)
{
  ContainerRemoveChild(this, aChild);
}

void
ContainerLayerComposite::Destroy()
{
  ContainerDestroy(this);
}

LayerComposite*
ContainerLayerComposite::GetFirstChildComposite()
{
  if (!mFirstChild) {
    return nullptr;
   }
  return static_cast<LayerComposite*>(mFirstChild->ImplData());
}

void
ContainerLayerComposite::RepositionChild(Layer* aChild, Layer* aAfter)
{
  ContainerRepositionChild(this, aChild, aAfter);
}

void
ContainerLayerComposite::RenderLayer(const nsIntPoint& aOffset,
                                     const nsIntRect& aClipRect,
                                     CompositingRenderTarget* aPreviousTarget)
{
  ContainerRender(this, aPreviousTarget, aOffset, mCompositeManager, aClipRect);
}

void
ContainerLayerComposite::CleanupResources()
{
  ContainerCleanupResources(this);
}

RefLayerComposite::RefLayerComposite(LayerManagerComposite* aManager)
  : ShadowRefLayer(aManager, nullptr)
  , LayerComposite(aManager)
{
  mImplData = static_cast<LayerComposite*>(this);
}

RefLayerComposite::~RefLayerComposite()
{
  Destroy();
}

void
RefLayerComposite::Destroy()
{
  MOZ_ASSERT(!mFirstChild);
  mDestroyed = true;
}

LayerComposite*
RefLayerComposite::GetFirstChildComposite()
{
  if (!mFirstChild) {
    return nullptr;
   }
  return static_cast<LayerComposite*>(mFirstChild->ImplData());
}

void
RefLayerComposite::RenderLayer(const nsIntPoint& aOffset,
                               const nsIntRect& aClipRect,
                               CompositingRenderTarget* aPreviousTarget)
{
  ContainerRender(this, aPreviousTarget, aOffset, mCompositeManager, aClipRect);
}

void
RefLayerComposite::CleanupResources()
{
}

} /* layers */
} /* mozilla */
