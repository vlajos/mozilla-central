/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_COMPOSITECOLORLAYER_H
#define GFX_COMPOSITECOLORLAYER_H

#include "mozilla/layers/PLayers.h"
#include "mozilla/layers/ShadowLayers.h"

#include "CompositeLayerManager.h"

namespace mozilla {
namespace layers {


class CompositeColorLayer : public ShadowColorLayer,
                            public CompositeLayer
{
public:
  CompositeColorLayer(CompositeLayerManager *aManager)
    : ShadowColorLayer(aManager, NULL)
    , CompositeLayer(aManager)
  { 
    mImplData = static_cast<CompositeLayer*>(this);
  }
  ~CompositeColorLayer() { Destroy(); }

  // CompositeLayer Implementation
  virtual Layer* GetLayer() { return this; }

  virtual void Destroy() { mDestroyed = true; }

  virtual void RenderLayer(const nsIntPoint& aOffset,
                           const nsIntRect& aClipRect,
                           Surface* aPreviousSurface = nullptr);
  virtual void CleanupResources() {};
};

} /* layers */
} /* mozilla */
#endif /* GFX_COMPOSITECOLORLAYER_H */
