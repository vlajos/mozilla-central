/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_CONTAINERLAYEROGL_H
#define GFX_CONTAINERLAYEROGL_H

#include "mozilla/layers/PLayers.h"
#include "mozilla/layers/ShadowLayers.h"

#include "Layers.h"
#include "LayerManagerOGL.h"

namespace mozilla {
namespace layers {

template<class Container>
static void ContainerInsertAfter(Container* aContainer, Layer* aChild, Layer* aAfter);
template<class Container>
static void ContainerRemoveChild(Container* aContainer, Layer* aChild);
template<class Container>
static void ContainerDestroy(Container* aContainer);
template<class Container>
static void ContainerRender(Container* aContainer,
                            Surface* aPreviousSurface,
                            const nsIntPoint& aOffset,
                            LayerManagerOGL* aManager,
                            const nsIntRect& aClipRect);



class CompositeContainerLayer : public ShadowContainerLayer,
                                public CompositeLayer
{
  template<class Container>
  friend void ContainerInsertAfter(Container* aContainer, Layer* aChild, Layer* aAfter);
  template<class Container>
  friend void ContainerRemoveChild(Container* aContainer, Layer* aChild);
  template<class Container>
  friend void ContainerDestroy(Container* aContainer);
  template<class Container>
  friend void ContainerRender(Container* aContainer,
                              Surface* aPreviousSurface,
                              const nsIntPoint& aOffset,
                              LayerManagerOGL* aManager,
                              const nsIntRect& aClipRect);

public:
  CompositeContainerLayer(CompositeLayerManager *aManager);
  ~CompositeContainerLayer();

  void InsertAfter(Layer* aChild, Layer* aAfter);

  void RemoveChild(Layer* aChild);

  // LayerOGL Implementation
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
                                public CompositeLayer
{
  template<class Container>
  friend void ContainerRender(Container* aContainer,
                              Surface* aPreviousSurface,
                              const nsIntPoint& aOffset,
                              LayerManagerOGL* aManager,
                              const nsIntRect& aClipRect);

public:
  CompositeRefLayer(CompositeLayerManager *aManager);
  ~CompositeRefLayer();

  /** LayerOGL implementation */
  Layer* GetLayer() { return this; }

  void Destroy();

  LayerOGL* GetFirstChildOGL();

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

#endif /* GFX_CONTAINERLAYEROGL_H */
