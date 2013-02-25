/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_TEXTURED3D11_H
#define MOZILLA_GFX_TEXTURED3D11_H

#include "mozilla/layers/Compositor.h"
#include "TextureClient.h"
#include <d3d11.h>
#include <vector>

class gfxD2DSurface;

namespace mozilla {
namespace layers {

class TextureSourceD3D11
{
public:
  TextureSourceD3D11()
  { }

  virtual ID3D11Texture2D *GetD3D11Texture() { return mTextures[0]; }
  virtual bool IsYCbCrSource() const { return false; }

  struct YCbCrTextures
  {
    ID3D11Texture2D *mY;
    ID3D11Texture2D *mCb;
    ID3D11Texture2D *mCr;
  };
  virtual YCbCrTextures GetYCbCrTextures() {
    YCbCrTextures textures = { mTextures[0], mTextures[1], mTextures[2] };
    return textures;
  }
protected:
  virtual gfx::IntSize GetSize() const { return mSize; }

  gfx::IntSize mSize;
  RefPtr<ID3D11Texture2D> mTextures[3];
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

class TextureHostShmemD3D11 : public TextureHost
                            , public TextureSourceD3D11
                            , public TileIterator
{
public:
  TextureHostShmemD3D11(ISurfaceAllocator* aDeallocator,
                        ID3D11Device *aDevice)
    : TextureHost(aDeallocator)
    , mDevice(aDevice)
    , mIsTiled(false)
    , mCurrentTile(0)
  {
  }

  virtual TextureSource *AsTextureSource();

  virtual TextureSourceD3D11* AsSourceD3D11() { return this; }

  virtual ID3D11Texture2D *GetD3D11Texture() { return mIsTiled ? mTileTextures[mCurrentTile] : TextureSourceD3D11::GetD3D11Texture(); }

  virtual gfx::IntSize GetSize() const;

  virtual LayerRenderState GetRenderState() { return LayerRenderState(); }

  virtual bool Lock() { return true; }

#ifdef MOZ_LAYERS_HAVE_LOG
  virtual const char* Name() { return "TextureHostShmemD3D11"; }
#endif

  virtual void BeginTileIteration() { mCurrentTile = 0; }
  virtual nsIntRect GetTileRect();
  virtual size_t GetTileCount() { return mTileTextures.size(); }
  virtual bool NextTile() { return (++mCurrentTile < mTileTextures.size()); }

  virtual TileIterator* AsTileIterator() { return mIsTiled ? this : nullptr; }
protected:
  virtual void UpdateImpl(const SurfaceDescriptor& aSurface, bool *aIsInitialised,
                          bool *aNeedsReset, nsIntRegion* aRegion);
private:

  gfx::IntRect GetTileRect(uint32_t aID);

  RefPtr<ID3D11Device> mDevice;
  bool mIsTiled;
  std::vector< RefPtr<ID3D11Texture2D> > mTileTextures;
  uint32_t mCurrentTile;
};

class TextureHostDXGID3D11 : public TextureHost
                           , public TextureSourceD3D11
{
public:
  TextureHostDXGID3D11(ISurfaceAllocator* aDeallocator,
                       ID3D11Device *aDevice)
    : TextureHost(aDeallocator)
    , mDevice(aDevice)
  {
  }

  virtual TextureSource *AsTextureSource();

  virtual TextureSourceD3D11* AsSourceD3D11() { return this; }

  virtual gfx::IntSize GetSize() const;

  virtual LayerRenderState GetRenderState() { return LayerRenderState(); }

  virtual bool Lock();
  virtual void Unlock();

#ifdef MOZ_LAYERS_HAVE_LOG
  virtual const char* Name() { return "TextureHostDXGID3D11"; }
#endif

protected:
  virtual void UpdateImpl(const SurfaceDescriptor& aSurface, bool *aIsInitialised,
                          bool *aNeedsReset, nsIntRegion* aRegion);
private:
  void LockTexture();
  void ReleaseTexture();

  gfx::IntRect GetTileRect(uint32_t aID);

  RefPtr<ID3D11Device> mDevice;
};

class TextureHostYCbCrD3D11 : public TextureHost
                            , public TextureSourceD3D11
{
public:
  TextureHostYCbCrD3D11(ISurfaceAllocator* aDeallocator,
                        ID3D11Device *aDevice)
    : TextureHost(aDeallocator)
    , mDevice(aDevice)
  {
    mFormat = gfx::FORMAT_YUV;
  }

  virtual TextureSource *AsTextureSource();

  virtual TextureSourceD3D11* AsSourceD3D11() { return this; }

  virtual gfx::IntSize GetSize() const;

  virtual bool IsYCbCrSource() const { return true; }

#ifdef MOZ_LAYERS_HAVE_LOG
  virtual const char* Name() { return "TextureImageTextureHostD3D11"; }
#endif

protected:
  virtual void UpdateImpl(const SurfaceDescriptor& aSurface, bool *aIsInitialised,
                          bool *aNeedsReset, nsIntRegion* aRegion);

private:
  RefPtr<ID3D11Device> mDevice;
};

inline uint32_t GetMaxTextureSizeForFeatureLevel(D3D_FEATURE_LEVEL aFeatureLevel)
{
  int32_t maxTextureSize;
  switch (aFeatureLevel) {
  case D3D_FEATURE_LEVEL_11_1:
  case D3D_FEATURE_LEVEL_11_0:
    maxTextureSize = 16384;
    break;
  case D3D_FEATURE_LEVEL_10_1:
  case D3D_FEATURE_LEVEL_10_0:
    maxTextureSize = 8192;
    break;
  case D3D_FEATURE_LEVEL_9_3:
    maxTextureSize = 4096;
    break;
  default:
    maxTextureSize = 2048;
  }
  return maxTextureSize;
}

}
}

#endif /* MOZILLA_GFX_TEXTURED3D11_H */
