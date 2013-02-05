/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextureD3D11.h"
#include "CompositorD3D11.h"
#include "gfxContext.h"
#include "gfxImageSurface.h"
#include "Effects.h"
#include "ipc/AutoOpenSurface.h"
#include "gfxWindowsPlatform.h"
#include "gfxD2DSurface.h"

namespace mozilla {

using namespace gfx;

namespace layers {

IntSize
TextureSourceD3D11::GetSize() const
{
  D3D11_TEXTURE2D_DESC desc;
  mTexture->GetDesc(&desc);

  return IntSize(desc.Width, desc.Height);
}

CompositingRenderTargetD3D11::CompositingRenderTargetD3D11(ID3D11Texture2D *aTexture)
{
  mTexture = aTexture;

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
  return TextureSourceD3D11::GetSize();
}

TextureClientD3D11::TextureClientD3D11(ShadowLayerForwarder* aLayerForwarder, BufferType aBufferType)
  : TextureClient(aLayerForwarder, aBufferType)
{
  mTextureInfo.imageType = aBufferType;
  mTextureInfo.memoryType = TEXTURE_DXGI;
}

void
TextureClientD3D11::EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aType)
{
  D3D10_TEXTURE2D_DESC desc;
  if (mTexture) {
    mTexture->GetDesc(&desc);

    if (desc.Width != aSize.width || desc.Height != aSize.height) {
      mTexture = nullptr;
    }

    if (mTexture) {
      return;
    }
  }

  ID3D10Device *device = gfxWindowsPlatform::GetPlatform()->GetD3D10Device();

  CD3D10_TEXTURE2D_DESC newDesc(DXGI_FORMAT_B8G8R8A8_UNORM,
                                aSize.width, aSize.height, 1, 1,
                                D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE);

  newDesc.MiscFlags = D3D10_RESOURCE_MISC_SHARED_KEYEDMUTEX;

  HRESULT hr = device->CreateTexture2D(&newDesc, nullptr, byRef(mTexture));

  if (FAILED(hr)) {
    LOGD3D11("Error creating texture for client!");
    return;
  }

  RefPtr<IDXGIResource> resource;
  mTexture->QueryInterface((IDXGIResource**)byRef(resource));

  HANDLE sharedHandle;
  hr = resource->GetSharedHandle(&sharedHandle);

  if (FAILED(hr)) {
    LOGD3D11("Error getting shared handle for texture.");
  }

  mDescriptor = SurfaceDescriptorD3D10((WindowsHandle)sharedHandle);

  mContentType = aType;
}

gfxASurface*
TextureClientD3D11::LockSurface()
{
  EnsureSurface();

  LockTexture();
  return mSurface.get();
}

void
TextureClientD3D11::Unlock()
{
  ReleaseTexture();
}

void
TextureClientD3D11::SetDescriptor(const SurfaceDescriptor& aDescriptor)
{
  mDescriptor = aDescriptor;

  MOZ_ASSERT(aDescriptor.type() == SurfaceDescriptor::TSurfaceDescriptorD3D10);
  ID3D10Device *device = gfxWindowsPlatform::GetPlatform()->GetD3D10Device();

  device->OpenSharedResource((HANDLE)aDescriptor.get_SurfaceDescriptorD3D10().handle(),
                             __uuidof(ID3D10Texture2D),
                             (void**)(ID3D10Texture2D**)byRef(mTexture));

  mSurface = nullptr;
}

void
TextureClientD3D11::EnsureSurface()
{
  if (mSurface) {
    return;
  }

  LockTexture();
  mSurface = new gfxD2DSurface(mTexture, mContentType);
  ReleaseTexture();
}

void
TextureClientD3D11::LockTexture()
{
  RefPtr<IDXGIKeyedMutex> mutex;
  mTexture->QueryInterface((IDXGIKeyedMutex**)byRef(mutex));

  mutex->AcquireSync(0, INFINITE);
}

void
TextureClientD3D11::ReleaseTexture()
{
  RefPtr<IDXGIKeyedMutex> mutex;
  mTexture->QueryInterface((IDXGIKeyedMutex**)byRef(mutex));

  mutex->ReleaseSync(0);
}

IntSize
TextureHostD3D11::GetSize() const
{
  return TextureSourceD3D11::GetSize();
}

TextureSource*
TextureHostD3D11::AsTextureSource()
{
  return this;
}

bool
TextureHostD3D11::Lock()
{
  LockTexture();
  return true;
}

void
TextureHostD3D11::Unlock()
{
  ReleaseTexture();
}

void
TextureHostD3D11::UpdateImpl(const SurfaceDescriptor& aImage, bool *aIsInitialised,
                             bool *aNeedsReset, nsIntRegion *aRegion)
{

  if (aImage.type() == SurfaceDescriptor::TShmem) {
    AutoOpenSurface openSurf(OPEN_READ_ONLY, aImage);
  
    nsRefPtr<gfxImageSurface> surf = openSurf.GetAsImage();

    gfxIntSize size = surf->GetSize();
    D3D11_SUBRESOURCE_DATA initData;
    initData.pSysMem = surf->Data();
    initData.SysMemPitch = surf->Stride();

    CD3D11_TEXTURE2D_DESC desc(DXGI_FORMAT_B8G8R8A8_UNORM, size.width, size.height,
                              1, 1, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_IMMUTABLE);

    mDevice->CreateTexture2D(&desc, &initData, byRef(mTexture));

    if (surf->Format() == gfxImageSurface::ImageFormatRGB24) {
      mFormat = FORMAT_B8G8R8X8;
    } else {
      mFormat = FORMAT_B8G8R8A8;
    }

    mNeedsLock = false;
  } else if (aImage.type() == SurfaceDescriptor::TSurfaceDescriptorD3D10) {
    mDevice->OpenSharedResource((HANDLE)aImage.get_SurfaceDescriptorD3D10().handle(),
                                __uuidof(ID3D11Texture2D), (void**)(ID3D11Texture2D**)byRef(mTexture));
    mFormat = FORMAT_B8G8R8A8;
    mNeedsLock = true;
  }

}

void
TextureHostD3D11::LockTexture()
{
  if (mNeedsLock) {
    RefPtr<IDXGIKeyedMutex> mutex;
    mTexture->QueryInterface((IDXGIKeyedMutex**)byRef(mutex));

    mutex->AcquireSync(0, INFINITE);
  }
}

void
TextureHostD3D11::ReleaseTexture()
{
  if (mNeedsLock) {
    RefPtr<IDXGIKeyedMutex> mutex;
    mTexture->QueryInterface((IDXGIKeyedMutex**)byRef(mutex));

    mutex->ReleaseSync(0);
  }
}

}
}
