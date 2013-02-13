/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_ColorLayerComposite_H
#define GFX_ColorLayerComposite_H

#include "mozilla/layers/PLayers.h"
#include "mozilla/layers/ShadowLayers.h"

#include "LayerManagerComposite.h"

namespace mozilla {
namespace layers {


class ColorLayerComposite : public ShadowColorLayer,
                            public LayerComposite
{
public:
  ColorLayerComposite(LayerManagerComposite *aManager)
    : ShadowColorLayer(aManager, nullptr)
    , LayerComposite(aManager)
  {
    MOZ_COUNT_CTOR(ColorLayerComposite);
    mImplData = static_cast<LayerComposite*>(this);
  }
  ~ColorLayerComposite()
  {
    MOZ_COUNT_DTOR(ColorLayerComposite);
    Destroy();
  }

  // LayerComposite Implementation
  virtual Layer* GetLayer() { return this; }

  virtual void Destroy() { mDestroyed = true; }

  virtual void RenderLayer(const nsIntPoint& aOffset,
                           const nsIntRect& aClipRect,
                           CompositingRenderTarget* aPreviousTarget = nullptr);
  virtual void CleanupResources() {};

  CompositableHost* GetCompositableHost() MOZ_OVERRIDE { return nullptr; }

  virtual LayerComposite* AsLayerComposite() MOZ_OVERRIDE { return this; }
};

} /* layers */
} /* mozilla */
#endif /* GFX_ColorLayerComposite_H */
