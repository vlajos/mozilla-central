/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextureD3D11.h"
#include "CompositorD3D11.h"
#include "gfxContext.h"
#include "gfxImageSurface.h"
#include "Effects.h"

namespace mozilla {

using namespace gfx;

namespace layers {

IntSize
TextureSourceD3D11Base::GetSize() const
{
  D3D11_TEXTURE2D_DESC desc;
  mTexture->GetDesc(&desc);

  return IntSize(desc.Width, desc.Height);
}
CompositingRenderTargetD3D11::CompositingRenderTargetD3D11(ID3D11Texture2D *aTexture)
  : TextureSourceD3D11Base(aTexture)
{
  RefPtr<ID3D11Device> device;
  mTexture->GetDevice(byRef(device));

  HRESULT hr = device->CreateRenderTargetView(mTexture, NULL, byRef(mRTView));

  if (FAILED(hr)) {
    LOGD3D11("Failed to create RenderTargetView.");
  }
}

IntSize
CompositingRenderTargetD3D11::GetSize() const
{
  return TextureSourceD3D11Base::GetSize();
}

IntSize
TextureSourceD3D11::GetSize() const
{
  return TextureSourceD3D11Base::GetSize();
}

IntSize
TextureHostD3D11::GetSize() const
{
  return mTextureSource->GetSize();
}

TextureSource*
TextureHostD3D11::AsTextureSource()
{
  return mTextureSource;
}

Effect*
TextureHostD3D11::Lock(const gfx::Filter& aFilter)
{
  if (mHasAlpha) {
    return new EffectRGBA(mTextureSource, true, FILTER_LINEAR);
  } else {
    return new EffectRGB(mTextureSource, true, FILTER_LINEAR);
  }
}

void
TextureHostD3D11::UpdateImpl(const SurfaceDescriptor& aImage, bool *aIsInitialised,
                             bool *aNeedsReset)
{
}

void
TextureHostD3D11::UpdateRegionImpl(gfxASurface* aSurface, nsIntRegion& aRegion)
{
  gfxIntSize size = aSurface->GetSize();
  
  nsRefPtr<gfxImageSurface> surf = aSurface->GetAsImageSurface();
  D3D11_SUBRESOURCE_DATA initData;
  initData.pSysMem = surf->Data();
  initData.SysMemPitch = surf->Stride();

  CD3D11_TEXTURE2D_DESC desc(DXGI_FORMAT_B8G8R8A8_UNORM, size.width, size.height,
                            1, 1, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_IMMUTABLE);

  mDevice->CreateTexture2D(&desc, &initData, byRef(mTexture));
  mTextureSource = new TextureSourceD3D11(mTexture);

  if (surf->Format() == gfxImageSurface::ImageFormatRGB24) {
    mHasAlpha = false;
  }
}


}
}
