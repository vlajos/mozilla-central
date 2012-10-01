/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_COMPOSITECONTAINERLAYER_H
#define GFX_COMPOSITECONTAINERLAYER_H

#include "mozilla/layers/PLayers.h"
#include "mozilla/layers/ShadowLayers.h"

#include "Layers.h"
#include "CompositeLayerManager.h"
#include "LayerImpl.h"

namespace mozilla {
namespace layers {

class CompositeContainerLayer : public ShadowContainerLayer,
                                public CompositeLayer,
                                private ContainerLayerImpl<CompositeContainerLayer,
                                                           CompositeLayer,
                                                           CompositeLayerManager>
{
  template<class ContainerT,
         class LayerT,
         class ManagerT>
  friend class ContainerLayerImpl;

public:
  CompositeContainerLayer(CompositeLayerManager *aManager);
  ~CompositeContainerLayer();

  void InsertAfter(Layer* aChild, Layer* aAfter);

  void RemoveChild(Layer* aChild);

  // CompositeLayer Implementation
  virtual Layer* GetLayer() { return this; }

  virtual void Destroy();

  CompositeLayer* GetFirstChildComposite();

  virtual void RenderLayer(const nsIntPoint& aOffset,
                           const nsIntRect& aClipRect,
                           Surface* aPreviousSurface = nullptr);

  virtual void ComputeEffectiveTransforms(const gfx3DMatrix& aTransformToSurface)
  {
    DefaultComputeEffectiveTransforms(aTransformToSurface);
  }

  virtual void CleanupResources();
};

class CompositeRefLayer : public ShadowRefLayer,
                          public CompositeLayer,
                          protected ContainerLayerImpl<CompositeRefLayer,
                                                       CompositeLayer,
                                                       CompositeLayerManager>
{
  template<class ContainerT,
         class LayerT,
         class ManagerT>
  friend class ContainerLayerImpl;

public:
  CompositeRefLayer(CompositeLayerManager *aManager);
  ~CompositeRefLayer();

  /** LayerOGL implementation */
  Layer* GetLayer() { return this; }

  void Destroy();

  CompositeLayer* GetFirstChildComposite();

  virtual void RenderLayer(const nsIntPoint& aOffset,
                           const nsIntRect& aClipRect,
                           Surface* aPreviousSurface = nullptr);

  virtual void ComputeEffectiveTransforms(const gfx3DMatrix& aTransformToSurface)
  {
    DefaultComputeEffectiveTransforms(aTransformToSurface);
  }

  virtual void CleanupResources();
};

} /* layers */
} /* mozilla */

#endif /* GFX_COMPOSITECONTAINERLAYER_H */
