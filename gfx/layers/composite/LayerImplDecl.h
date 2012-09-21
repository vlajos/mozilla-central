/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_LAYERIMPLDECL_H
#define GFX_LAYERIMPLDECL_H

struct nsIntPoint;
struct nsIntRect;

namespace mozilla {
namespace layers {

class Layer;
class ColorLayer;
class Compositor;
class Surface;

void
RenderColorLayer(ColorLayer* aLayer, Compositor *aCompositor,
                 const nsIntPoint& aOffset, const nsIntRect& aClipRect);

template<class Container>
void ContainerInsertAfter(Container* aContainer, Layer* aChild, Layer* aAfter);

template<class Container>
void ContainerRemoveChild(Container* aContainer, Layer* aChild);

template<class LayerT,
         class Container>
void ContainerDestroy(Container* aContainer);

template<class Container>
void
ContainerCleanupResources(Container* aContainer);

template<class LayerT>
LayerT*
GetNextSibling(LayerT* aLayer);

template<class ContainerT,
         class LayerT,
         class ManagerT>
void ContainerRender(ContainerT* aContainer,
                     Surface* aPreviousSurface,
                     const nsIntPoint& aOffset,
                     ManagerT* aManager,
                     const nsIntRect& aClipRect);

}
}
#endif
