/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextureClient.h"
#include "ImageClient.h"
#include "CanvasClient.h"
#include "ContentClient.h"
#include "mozilla/layers/ShadowLayers.h"
#include "SharedTextureImage.h"
#include "GLContext.h"
#include "mozilla/layers/TextureChild.h"
#include "gfxReusableSurfaceWrapper.h"
#include "gfxPlatform.h"
#ifdef XP_WIN
#include "mozilla/layers/TextureD3D11.h"
#endif
 
using namespace mozilla::gl;

namespace mozilla {
namespace layers {


TextureClient::TextureClient(ShadowLayerForwarder* aLayerForwarder,
                             BufferType aBufferType)
  : mLayerForwarder(aLayerForwarder)
{
  mTextureInfo.imageType = aBufferType;
}

TextureClient::~TextureClient()
{}

void AwesomeTextureClient::ReleaseResources() {
  mTextureChild->DestroySharedSurface(&mData);
}

void
TextureClient::Updated(ShadowableLayer* aLayer)
{
  if (mDescriptor.type() != SurfaceDescriptor::T__None) {
    mLayerForwarder->UpdateTexture(mTextureChild, SurfaceDescriptor(mDescriptor));
  }
}

void
TextureClient::Destroyed(ShadowableLayer* aLayer)
{
  mLayerForwarder->DestroyedThebesBuffer(aLayer, mDescriptor);
}

void
TextureClient::SetAsyncContainerID(uint64_t aID)
{
  mLayerForwarder->AttachAsyncTexture(GetTextureChild(), aID);
}

void
TextureClient::UpdatedRegion(const nsIntRegion& aUpdatedRegion,
                             const nsIntRect& aBufferRect,
                             const nsIntPoint& aBufferRotation)
{
  mLayerForwarder->UpdateTextureRegion(this,
                                       ThebesBuffer(mDescriptor, aBufferRect, aBufferRotation),
                                       aUpdatedRegion);
}


TextureClientShmem::TextureClientShmem(ShadowLayerForwarder* aLayerForwarder, BufferType aBufferType)
  : TextureClient(aLayerForwarder, aBufferType)
  , mSurface(nullptr)
  , mSurfaceAsImage(nullptr)
{
  mTextureInfo.memoryType = TEXTURE_SHMEM;
}

TextureClientShmem::~TextureClientShmem()
{
  if (mSurface) {
    mSurface = nullptr;
    ShadowLayerForwarder::CloseDescriptor(mDescriptor);
  }
  if (IsSurfaceDescriptorValid(mDescriptor)) {
    mLayerForwarder->DestroySharedSurface(&mDescriptor);
  }
}

void
TextureClientShmem::EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aContentType)
{
  if (aSize != mSize ||
      aContentType != mContentType ||
      !IsSurfaceDescriptorValid(mDescriptor)) {
    if (IsSurfaceDescriptorValid(mDescriptor)) {
      mLayerForwarder->DestroySharedSurface(&mDescriptor);
    }
    mContentType = aContentType;
    mSize = aSize;

    if (!mLayerForwarder->AllocBuffer(gfxIntSize(mSize.width, mSize.height), mContentType, &mDescriptor)) {
      NS_RUNTIMEABORT("creating SurfaceDescriptor failed!");
    }
  }
}

already_AddRefed<gfxContext>
TextureClientShmem::LockContext()
{
  nsRefPtr<gfxContext> result = new gfxContext(GetSurface());
  return result.forget();
}

gfxASurface*
TextureClientShmem::GetSurface()
{
  if (!mSurface) {
    mSurface = ShadowLayerForwarder::OpenDescriptor(OPEN_READ_WRITE, mDescriptor);
  }
  
  return mSurface.get();
}

void
TextureClientShmem::Unlock()
{
  mSurface = nullptr;
  mSurfaceAsImage = nullptr;

  ShadowLayerForwarder::CloseDescriptor(mDescriptor);
}

gfxImageSurface*
TextureClientShmem::LockImageSurface()
{
  if (!mSurfaceAsImage) {
    mSurfaceAsImage = GetSurface()->GetAsImageSurface();
  }

  return mSurfaceAsImage.get();
}


TextureClientShared::~TextureClientShared()
{
  if (IsSurfaceDescriptorValid(mDescriptor)) {
    mLayerForwarder->DestroySharedSurface(&mDescriptor);
  }
}

TextureClientSharedGL::TextureClientSharedGL(ShadowLayerForwarder* aLayerForwarder,
                                             BufferType aBufferType)
  : TextureClientShared(aLayerForwarder, aBufferType)
{
  mTextureInfo.memoryType = TEXTURE_SHARED_BUFFERED;
}

TextureClientSharedGL::~TextureClientSharedGL()
{
  SharedTextureDescriptor handle = mDescriptor.get_SharedTextureDescriptor();
  if (mGL && handle.handle()) {
    mGL->ReleaseSharedHandle(handle.shareType(), handle.handle());
  }
}


void
TextureClientSharedGL::EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aContentType)
{
  mSize = aSize;
}

SharedTextureHandle
TextureClientSharedGL::LockHandle(GLContext* aGL, gl::GLContext::SharedTextureShareType aFlags)
{
  mGL = aGL;

  SharedTextureHandle handle = 0;
  if (mDescriptor.type() == SurfaceDescriptor::TSharedTextureDescriptor) {
    handle = mDescriptor.get_SharedTextureDescriptor().handle();
  } else {
    handle = mGL->CreateSharedHandle(aFlags);
    if (!handle) {
      return 0;
    }
    mDescriptor = SharedTextureDescriptor(aFlags, handle, nsIntSize(mSize.width, mSize.height), false);
  }

  return handle;
}

void
TextureClientSharedGL::Unlock()
{
  // Move SharedTextureHandle ownership to ShadowLayer
  mDescriptor = SurfaceDescriptor();
}

TextureClientBridge::TextureClientBridge(ShadowLayerForwarder* aLayerForwarder,
                                         BufferType aBufferType)
  : TextureClient(aLayerForwarder, aBufferType)
{
  mTextureInfo.memoryType = TEXTURE_SHMEM;
}

/* static */ BufferType
CompositingFactory::TypeForImage(Image* aImage) {
  if (!aImage) {
    return BUFFER_UNKNOWN;
  }

  if (aImage->GetFormat() == SHARED_TEXTURE) {
    return BUFFER_SHARED;
  }
  return BUFFER_TEXTURE;
}

/* static */ TemporaryRef<ImageClient>
CompositingFactory::CreateImageClient(LayersBackend aParentBackend,
                                      BufferType aCompositableHostType,
                                      ShadowLayerForwarder* aLayerForwarder,
                                      ShadowableLayer* aLayer,
                                      TextureFlags aFlags)
{
  RefPtr<ImageClient> result = nullptr;
  switch (aCompositableHostType) {
  case BUFFER_SHARED:
    if (aParentBackend == LAYERS_OPENGL) {
      result = new ImageClientShared(aLayerForwarder, aLayer, aFlags);
    }
    break;
    // fall through to BUFFER_TEXTURE
  case BUFFER_TEXTURE:
    if (aParentBackend == LAYERS_OPENGL || aParentBackend == LAYERS_D3D11) {
      result = new ImageClientTexture(aLayerForwarder, aLayer, aFlags);
    }
    break;
  case BUFFER_BRIDGE:
    if (aParentBackend == LAYERS_OPENGL) {
      result = new ImageClientBridge(aLayerForwarder, aLayer, aFlags);
    }
    break;
  case BUFFER_UNKNOWN:
    return nullptr;
  default:
    // FIXME [bjacob] unhandled cases were reported as GCC warnings; with this,
    // at least we'll known if we run into them.
    MOZ_NOT_REACHED("unhandled aCompositableHostType");
  }

  NS_ASSERTION(result, "Failed to create ImageClient");

  return result.forget();
}

/* static */ TemporaryRef<CanvasClient>
CompositingFactory::CreateCanvasClient(LayersBackend aParentBackend,
                                       BufferType aCompositableHostType,
                                       ShadowLayerForwarder* aLayerForwarder,
                                       ShadowableLayer* aLayer,
                                       TextureFlags aFlags)
{
  if (aCompositableHostType == BUFFER_DIRECT) {
    return new CanvasClient2D(aLayerForwarder, aLayer, aFlags);
  }
  if (aCompositableHostType == BUFFER_SHARED) {
    if (aParentBackend == LAYERS_OPENGL) {
      return new CanvasClientWebGL(aLayerForwarder, aLayer, aFlags);
    }
    return new CanvasClient2D(aLayerForwarder, aLayer, aFlags);
  }
  return nullptr;
}

/* static */ TemporaryRef<ContentClient>
CompositingFactory::CreateContentClient(LayersBackend aParentBackend,
                                        BufferType aCompositableHostType,
                                        ShadowLayerForwarder* aLayerForwarder,
                                        ShadowableLayer* aLayer,
                                        TextureFlags aFlags)
{
  if (aParentBackend != LAYERS_OPENGL && aParentBackend != LAYERS_D3D11) {
    return nullptr;
  }
  if (aCompositableHostType == BUFFER_CONTENT) {
    return new ContentClientTexture(aLayerForwarder, aLayer, aFlags);
  }
  if (aCompositableHostType == BUFFER_CONTENT_DIRECT) {
    if (ShadowLayerManager::SupportsDirectTexturing()) {
      return new ContentClientDirect(aLayerForwarder, aLayer, aFlags);
    }
    return new ContentClientTexture(aLayerForwarder, aLayer, aFlags);
  }
  if (aCompositableHostType == BUFFER_TILED) {
    NS_RUNTIMEABORT("No CompositableClient for tiled layers");
  }
  return nullptr;
}

/* static */ TemporaryRef<TextureClient>
CompositingFactory::CreateTextureClient(LayersBackend aParentBackend,
                                        TextureHostType aTextureHostType,
                                        BufferType aCompositableHostType,
                                        ShadowLayerForwarder* aLayerForwarder,
                                        bool aStrict /* = false */)
{
  RefPtr<TextureClient> result = nullptr;
  switch (aTextureHostType) {
  case TEXTURE_SHARED_BUFFERED:
    if (aParentBackend == LAYERS_OPENGL) {
      result = new TextureClientSharedGL(aLayerForwarder, aCompositableHostType);
    }
    break;
  case TEXTURE_SHARED:
    if (aParentBackend == LAYERS_OPENGL) {
      result = new TextureClientShared(aLayerForwarder, aCompositableHostType);
    }
    break;
  case TEXTURE_SHMEM:
    if (aParentBackend == LAYERS_OPENGL || aParentBackend == LAYERS_D3D11) {
      result = new TextureClientShmem(aLayerForwarder, aCompositableHostType);
    }
    break;
  case TEXTURE_BRIDGE:
    result = new TextureClientBridge(aLayerForwarder, aCompositableHostType);
    break;
  case TEXTURE_TILED:
    result = new TextureClientTile(aLayerForwarder, aCompositableHostType);
    break;
  case TEXTURE_SHARED_DXGI:
#ifdef XP_WIN
    result = new TextureClientD3D11(aLayerForwarder, aCompositableHostType);
    break;
#endif
  default:
    return result.forget();
  }

  NS_ASSERTION(result, "Failed to create ImageClient");

  return result.forget();
}

void
TextureClientTile::EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aType)
{
  if (!mSurface || mSurface->Format() != gfxPlatform::GetPlatform()->OptimalFormatForContent(aType)) {
    gfxImageSurface* tmpTile = new gfxImageSurface(gfxIntSize(aSize.width, aSize.height),
                                                   gfxPlatform::GetPlatform()->OptimalFormatForContent(aType),
                                                   aType != gfxASurface::CONTENT_COLOR);
    mSurface = new gfxReusableSurfaceWrapper(tmpTile);
  }
}

gfxImageSurface*
TextureClientTile::LockImageSurface()
{
  // Use the gfxReusableSurfaceWrapper, which will reuse the surface
  // if the compositor no longer has a read lock, otherwise the surface
  // will be copied into a new writable surface.
  gfxImageSurface* writableSurface = nullptr;
  mSurface = mSurface->GetWritable(&writableSurface);
  return writableSurface;
}

}
}