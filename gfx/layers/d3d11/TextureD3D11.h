/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_TEXTURED3D11_H
#define MOZILLA_GFX_TEXTURED3D11_H

#include "mozilla/layers/Compositor.h"
#include "TextureClient.h"
#include <d3d11.h>

class gfxD2DSurface;

namespace mozilla {
namespace layers {

class TextureSourceD3D11
{
public:
  TextureSourceD3D11()
  { }

  ID3D11Texture2D *GetD3D11Texture() { return mTexture; }
protected:
  virtual gfx::IntSize GetSize() const;

  RefPtr<ID3D11Texture2D> mTexture;
};

class CompositingRenderTargetD3D11 : public CompositingRenderTarget,
                                     public TextureSourceD3D11
{
public:
  CompositingRenderTargetD3D11(ID3D11Texture2D *aTexture);

  virtual TextureSourceD3D11* AsSourceD3D11() { return this; }

  virtual gfx::IntSize GetSize() const;

private:
  friend class CompositorD3D11;

  RefPtr<ID3D11RenderTargetView> mRTView;
};

class TextureClientD3D11 : public TextureClient
{
public:
  TextureClientD3D11(CompositableForwarder* aCompositableForwarder, CompositableType aCompositableType);

  virtual void EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aType);

  virtual gfxASurface* LockSurface();
  virtual void Unlock();

  virtual void SetDescriptor(const SurfaceDescriptor& aDescriptor);

private:
  void EnsureSurface();
  void LockTexture();
  void ReleaseTexture();

  RefPtr<ID3D10Texture2D> mTexture;
  nsRefPtr<gfxD2DSurface> mSurface;
  gfxContentType mContentType;
};

class TextureHostD3D11 : public TextureHost
                       , public TextureSourceD3D11
{
public:
  TextureHostD3D11(BufferMode aBuffering, ISurfaceDeallocator* aDeallocator,
                   ID3D11Device *aDevice)
    : TextureHost(aBuffering, aDeallocator)
    , mDevice(aDevice)
    , mNeedsLock(false)
  {
  }

  virtual TextureSource *AsTextureSource();

  virtual TextureSourceD3D11* AsSourceD3D11() { return this; }

  virtual gfx::IntSize GetSize() const;

  virtual LayerRenderState GetRenderState() { return LayerRenderState(); }

  virtual bool Lock();
  virtual void Unlock();

protected:
  virtual void UpdateImpl(const SurfaceDescriptor& aSurface, bool *aIsInitialised,
                          bool *aNeedsReset, nsIntRegion* aRegion);
private:
  void LockTexture();
  void ReleaseTexture();

  RefPtr<ID3D11Device> mDevice;
  bool mNeedsLock;
};

}
}

#endif /* MOZILLA_GFX_TEXTURED3D11_H */
