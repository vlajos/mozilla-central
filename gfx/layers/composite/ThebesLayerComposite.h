/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_ThebesLayerComposite_H
#define GFX_ThebesLayerComposite_H

#include "mozilla/layers/PLayers.h"
#include "mozilla/layers/ShadowLayers.h"

#include "Layers.h"
#include "LayerManagerComposite.h"
#include "base/task.h"


namespace mozilla {
namespace layers {

class AContentHost;

class ThebesLayerComposite : public ShadowThebesLayer,
                             public LayerComposite
{
public:
  ThebesLayerComposite(LayerManagerComposite *aManager);
  virtual ~ThebesLayerComposite();

  virtual void SetAllocator(ISurfaceDeAllocator* aAllocator);
                           
  virtual void DestroyFrontBuffer();

  virtual void Disconnect();

  virtual void SetValidRegion(const nsIntRegion& aRegion)
  {
    ShadowThebesLayer::SetValidRegion(aRegion);
  }

  // TODO[nical] remove Swap from layer
  void SwapTexture(const ThebesBuffer& aNewFront,
                   const nsIntRegion& aUpdatedRegion,
                   OptionalThebesBuffer* aNewBack,
                   nsIntRegion* aNewBackValidRegion,
                   OptionalThebesBuffer* aReadOnlyFront,
                   nsIntRegion* aFrontUpdatedRegion);

  virtual LayerRenderState GetRenderState() MOZ_OVERRIDE;

  BufferHost* GetBufferHost() MOZ_OVERRIDE;
  // LayerComposite impl
  void Destroy();
  Layer* GetLayer();
  virtual bool IsEmpty();
  virtual void RenderLayer(const nsIntPoint& aOffset,
                           const nsIntRect& aClipRect,
                           CompositingRenderTarget* aPreviousTarget = nullptr);
  virtual void CleanupResources();

  virtual void AddTextureHost(const TextureInfo& aTextureInfo, TextureHost* aTextureHost);

  virtual LayerComposite* AsLayerComposite() MOZ_OVERRIDE { return this; }

private:
  void EnsureBuffer(BufferType aHostType);

  nsRefPtr<AContentHost> mBuffer;
  nsIntRegion mValidRegionForNextBackBuffer;
};

} /* layers */
} /* mozilla */
#endif /* GFX_ThebesLayerComposite_H */
