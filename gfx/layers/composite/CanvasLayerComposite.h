/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_CanvasLayerComposite_H
#define GFX_CanvasLayerComposite_H


#include "LayerManagerComposite.h"
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
class CanvasLayerComposite : public ShadowCanvasLayer,
                             public LayerComposite
{
public:
  CanvasLayerComposite(LayerManagerComposite* aManager);
  virtual ~CanvasLayerComposite();

  // CanvasLayer impl
  virtual void Initialize(const Data& aData)
  {
    NS_RUNTIMEABORT("Incompatibe surface type");
  }

  // This isn't meaningful for shadow canvas.
  virtual void Updated(const nsIntRect&) {}

  virtual void SetAllocator(ISurfaceDeallocator* aAllocator);

  virtual LayerRenderState GetRenderState() MOZ_OVERRIDE;

  // ShadowCanvasLayer impl
  virtual void Swap(const SurfaceDescriptor& aNewFront,
                    bool needYFlip,
                    SurfaceDescriptor* aNewBack)
  {
    NS_ERROR("Should never be called");
  }

  virtual void SetCompositableHost(CompositableHost* aHost) MOZ_OVERRIDE;
/*
  virtual void SwapTexture(const TextureInfo& aTextureInfo,
                           const SurfaceDescriptor& aFront,
                           SurfaceDescriptor* aNewBack);
*/
  virtual void Disconnect()
  {
    Destroy();
  }

  // LayerComposite impl
  void Destroy();
  Layer* GetLayer();
  virtual void RenderLayer(const nsIntPoint& aOffset,
                           const nsIntRect& aClipRect,
                           CompositingRenderTarget* aPreviousTarget = nullptr);

  virtual void CleanupResources();

  CompositableHost* GetCompositableHost() MOZ_OVERRIDE;

  virtual LayerComposite* AsLayerComposite() MOZ_OVERRIDE { return this; }

private:
  void EnsureImageHost(CompositableType aHostType);

  RefPtr<ImageHost> mImageHost;
};

} /* layers */
} /* mozilla */
#endif /* GFX_CanvasLayerComposite_H */
