/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_ImageLayerComposite_H
#define GFX_ImageLayerComposite_H

#include "mozilla/layers/PLayers.h"
#include "mozilla/layers/ShadowLayers.h"

#include "LayerManagerComposite.h"
#include "ImageLayers.h"
#include "mozilla/Mutex.h"

namespace mozilla {
namespace layers {

class ImageHost;

class ImageLayerComposite : public ShadowImageLayer,
                            public LayerComposite
{
  typedef gl::TextureImage TextureImage;

public:
  ImageLayerComposite(LayerManagerComposite* aManager);
  virtual ~ImageLayerComposite();

  virtual void SetAllocator(ISurfaceDeallocator* aAllocator) {}

  virtual LayerRenderState GetRenderState() MOZ_OVERRIDE;

  // ShadowImageLayer impl
  virtual void Swap(const SurfaceDescriptor& aFront,
                    SurfaceDescriptor* aNewBack)
  {
    NS_ERROR("Not implemented");
  }

  virtual void Disconnect();

  virtual void SetCompositableHost(CompositableHost* aHost) MOZ_OVERRIDE;

  virtual void SetPictureRect(const nsIntRect& aPictureRect);

  // LayerOGL impl
  virtual void Destroy();

  virtual Layer* GetLayer();

  virtual void RenderLayer(const nsIntPoint& aOffset,
                           const nsIntRect& aClipRect,
                           CompositingRenderTarget* aPreviousTarget = nullptr);

  virtual void CleanupResources();

  CompositableHost* GetCompositableHost() MOZ_OVERRIDE;

  virtual LayerComposite* AsLayerComposite() MOZ_OVERRIDE { return this; }

private:
  void EnsureImageHost(CompositableType aHostType);

  // A ShadowImageLayer should use only one of the ImageHost
  // or ImageBridge mechanisms at one time
  RefPtr<ImageHost> mImageHost;
};

} /* layers */
} /* mozilla */
#endif /* GFX_ImageLayerComposite_H */
