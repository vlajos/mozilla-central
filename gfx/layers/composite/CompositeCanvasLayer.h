/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_COMPOSITECANVASLAYER_H
#define GFX_COMPOSITECANVASLAYER_H


#include "CompositeLayerManager.h"
#include "gfxASurface.h"
//#include "ImageHost.h"
#if defined(MOZ_WIDGET_GTK2) && !defined(MOZ_PLATFORM_MAEMO)
#include "mozilla/X11Util.h"
#endif

namespace mozilla {
namespace layers {

class ImageHost;

// NB: eventually we'll have separate shadow canvas2d and shadow
// canvas3d layers, but currently they look the same from the
// perspective of the compositor process
class CompositeCanvasLayer : public ShadowCanvasLayer,
                             public CompositeLayer
{
public:
  CompositeCanvasLayer(CompositeLayerManager* aManager);
  virtual ~CompositeCanvasLayer();

  // CanvasLayer impl
  virtual void Initialize(const Data& aData)
  {
    NS_RUNTIMEABORT("Incompatibe surface type");
  }

  // This isn't meaningful for shadow canvas.
  virtual void Updated(const nsIntRect&) {}

  virtual void SetAllocator(ISurfaceDeAllocator* aAllocator) {}

  // ShadowCanvasLayer impl
  virtual void Swap(const SharedImage& aNewFront,
                    bool needYFlip,
                    SharedImage* aNewBack)
  {
    NS_ERROR("Should never be called");
  }

  virtual void AddTextureHost(const TextureIdentifier& aTextureIdentifier, TextureHost* aTextureHost);

  virtual void SwapTexture(const TextureIdentifier& aTextureIdentifier,
                           const SharedImage& aFront,
                           SharedImage* aNewBack);

  virtual void Disconnect()
  {
    Destroy();
  }

  // CompositeLayer impl
  void Destroy();
  Layer* GetLayer();
  virtual void RenderLayer(const nsIntPoint& aOffset,
                           const nsIntRect& aClipRect,
                           Surface* aPreviousSurface = nullptr);

  virtual void CleanupResources();

private:
  void EnsureImageHost(BufferType aHostType);

  RefPtr<ImageHost> mImageHost;
};

} /* layers */
} /* mozilla */
#endif /* GFX_COMPOSITECANVASLAYER_H */
