/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_COMPOSITEIMAGELAYER_H
#define GFX_COMPOSITEIMAGELAYER_H

#include "mozilla/layers/PLayers.h"
#include "mozilla/layers/ShadowLayers.h"

#include "CompositeLayerManager.h"
#include "ImageLayers.h"
//#include "yuv_convert.h"
#include "mozilla/Mutex.h"

namespace mozilla {
namespace layers {

class ImageHost;

class CompositeImageLayer : public ShadowImageLayer,
                            public CompositeLayer
{
  typedef gl::TextureImage TextureImage;

public:
  CompositeImageLayer(CompositeLayerManager* aManager);
  virtual ~CompositeImageLayer();

  virtual void SetAllocator(ISurfaceDeAllocator* aAllocator) {}

  // ShadowImageLayer impl
  virtual void Swap(const SharedImage& aFront,
                    SharedImage* aNewBack)
  {
    NS_ERROR("Not implemented");
  }

  virtual void Disconnect();

  virtual void AddTextureHost(const TextureIdentifier& aTextureIdentifier, TextureHost* aTextureHost);

  virtual void SwapTexture(const TextureIdentifier& aTextureIdentifier,
                           const SharedImage& aFront,
                           SharedImage* aNewBack);

  virtual void SetPictureRect(const nsIntRect& aPictureRect);

  // LayerOGL impl
  virtual void Destroy();

  virtual Layer* GetLayer();

  virtual void RenderLayer(const nsIntPoint& aOffset,
                           const nsIntRect& aClipRect,
                           Surface* aPreviousSurface = nullptr);

  virtual TemporaryRef<TextureHost> AsTextureHost();


  virtual void CleanupResources();

private:
  void EnsureImageHost(BufferType aHostType);

  // A ShadowImageLayer should use only one of the ImageHost
  // or ImageBridge mechanisms at one time
  RefPtr<ImageHost> mImageHost;
};

} /* layers */
} /* mozilla */
#endif /* GFX_COMPOSITEIMAGELAYER_H */
