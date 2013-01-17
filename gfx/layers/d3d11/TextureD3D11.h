/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_TEXTURED3D11_H
#define MOZILLA_GFX_TEXTURED3D11_H

#include "mozilla/layers/Compositor.h"
#include <d3d11.h>

namespace mozilla {
namespace layers {

class TextureSourceD3D11Base
{
public:
  TextureSourceD3D11Base(ID3D11Texture2D *aTexture)
    : mTexture(aTexture)
  { }

  ID3D11Texture2D *GetD3D11Texture() { return mTexture; }
protected:
  virtual gfx::IntSize GetSize() const;

  RefPtr<ID3D11Texture2D> mTexture;
};

class CompositingRenderTargetD3D11 : public CompositingRenderTarget,
                                     public TextureSourceD3D11Base
{
public:
  CompositingRenderTargetD3D11(ID3D11Texture2D *aTexture);

  virtual gfx::IntSize GetSize() const;

private:
  friend class CompositorD3D11;

  RefPtr<ID3D11RenderTargetView> mRTView;
};

class TextureSourceD3D11 : public TextureSource,
                           public TextureSourceD3D11Base
{
public:
  TextureSourceD3D11(ID3D11Texture2D *aTexture)
    : TextureSourceD3D11Base(aTexture)
  { }

  virtual gfx::IntSize GetSize() const;
};                         

class TextureHostD3D11 : public TextureHost
{
public:
  TextureHostD3D11(BufferMode aBuffering, ISurfaceDeallocator* aDeallocator,
                   ID3D11Device *aDevice)
    : TextureHost(aBuffering, aDeallocator)
    , mDevice(aDevice)
    , mHasAlpha(true)
  {
  }

  virtual TextureSource *AsTextureSource();

  virtual gfx::IntSize GetSize() const;

  virtual LayerRenderState GetRenderState() { return LayerRenderState(); }

  virtual Effect *Lock(const gfx::Filter& aFilter);

protected:
  virtual void UpdateImpl(const SurfaceDescriptor& aImage, bool *aIsInitialised,
                          bool *aNeedsReset);

  virtual void UpdateRegionImpl(gfxASurface* aSurface, nsIntRegion& aRegion);
private:
  RefPtr<ID3D11Texture2D> mTexture;
  RefPtr<TextureSourceD3D11> mTextureSource;
  RefPtr<ID3D11Device> mDevice;
  bool mHasAlpha;
};

}
}

#endif /* MOZILLA_GFX_TEXTURED3D11_H */
