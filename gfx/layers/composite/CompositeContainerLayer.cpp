/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CompositeContainerLayer.h"
#include "gfxUtils.h"
#include "Compositor.h"
#include "LayersTypes.h"
#include "gfx2DGlue.h"

namespace mozilla {
namespace layers {


CompositeContainerLayer::CompositeContainerLayer(CompositeLayerManager *aManager)
  : ShadowContainerLayer(aManager, NULL)
  , CompositeLayer(aManager)
{
  mImplData = static_cast<CompositeLayer*>(this);
}
 
CompositeContainerLayer::~CompositeContainerLayer()
{
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
CompositeContainerLayer::InsertAfter(Layer* aChild, Layer* aAfter)
{
  ContainerInsertAfter(this, aChild, aAfter);
}

void
CompositeContainerLayer::RemoveChild(Layer *aChild)
{
  ContainerRemoveChild(this, aChild);
}

void
CompositeContainerLayer::Destroy()
{
  ContainerDestroy(this);
}

CompositeLayer*
CompositeContainerLayer::GetFirstChildComposite()
{
  if (!mFirstChild) {
    return nullptr;
   }
  return static_cast<CompositeLayer*>(mFirstChild->ImplData());
}

void
CompositeContainerLayer::RenderLayer(const nsIntPoint& aOffset,
                                     const nsIntRect& aClipRect,
                                     Surface* aPreviousSurface)
{
  ContainerRender(this, aPreviousSurface, aOffset, mCompositeManager, aClipRect);
}

void
CompositeContainerLayer::CleanupResources()
{
  ContainerCleanupResources(this);
}

CompositeRefLayer::CompositeRefLayer(CompositeLayerManager* aManager)
  : ShadowRefLayer(aManager, NULL)
  , CompositeLayer(aManager)
{
  mImplData = static_cast<CompositeLayer*>(this);
}

CompositeRefLayer::~CompositeRefLayer()
{
  Destroy();
}

void
CompositeRefLayer::Destroy()
{
  MOZ_ASSERT(!mFirstChild);
  mDestroyed = true;
}

CompositeLayer*
CompositeRefLayer::GetFirstChildComposite()
{
  if (!mFirstChild) {
    return nullptr;
   }
  return static_cast<CompositeLayer*>(mFirstChild->ImplData());
}

void
CompositeRefLayer::RenderLayer(const nsIntPoint& aOffset,
                               const nsIntRect& aClipRect,
                               Surface* aPreviousSurface)
{
  ContainerRender(this, aPreviousSurface, aOffset, mCompositeManager, aClipRect);
}

void
CompositeRefLayer::CleanupResources()
{
}

} /* layers */
} /* mozilla */
