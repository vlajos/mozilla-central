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

  virtual void SetAllocator(ISurfaceDeallocator* aAllocator);
                           
  virtual void DestroyFrontBuffer();

  virtual void Disconnect();

  virtual void SetValidRegion(const nsIntRegion& aRegion)
  {
    ShadowThebesLayer::SetValidRegion(aRegion);
  }

  void SwapTexture(const ThebesBuffer& aNewFront,
                   const nsIntRegion& aUpdatedRegion,
                   OptionalThebesBuffer* aNewBack,
                   nsIntRegion* aNewBackValidRegion,
                   OptionalThebesBuffer* aReadOnlyFront,
                   nsIntRegion* aFrontUpdatedRegion);

  virtual LayerRenderState GetRenderState() MOZ_OVERRIDE;

  CompositableHost* GetCompositableHost() MOZ_OVERRIDE;
  // LayerComposite impl
  virtual void Destroy();
  virtual Layer* GetLayer();
  virtual TiledLayerComposer* GetTiledLayerComposer();
  virtual bool IsEmpty();
  virtual void RenderLayer(const nsIntPoint& aOffset,
                           const nsIntRect& aClipRect,
                           CompositingRenderTarget* aPreviousTarget = nullptr);
  virtual void CleanupResources();

  virtual void SetCompositableHost(CompositableHost* aHost) MOZ_OVERRIDE;

  //TODO[nrc] why did I remove this method?
  //virtual LayerComposite* AsLayerComposite() MOZ_OVERRIDE { return this; }

  void EnsureBuffer(BufferType aHostType);

private:
  gfxRect GetDisplayPort();
  gfxSize GetEffectiveResolution();
  gfxRect GetCompositionBounds();

  nsRefPtr<AContentHost> mBuffer;
  nsIntRegion mValidRegionForNextBackBuffer;
  bool mRequiresTiledProperties;
};

} /* layers */
} /* mozilla */
#endif /* GFX_ThebesLayerComposite_H */
