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
#include "BasicLayers.h" // for PaintContext
#include "ShmemYCbCrImage.h"
#include "gfxReusableSurfaceWrapper.h"
#include "gfxPlatform.h"
#ifdef XP_WIN
#include "mozilla/layers/TextureD3D11.h"
#endif
 
using namespace mozilla::gl;

namespace mozilla {
namespace layers {


TextureClient::TextureClient(CompositableForwarder* aForwarder,
                             CompositableType aCompositableType)
  : mLayerForwarder(aForwarder)
  , mAllocator(nullptr)
  , mTextureChild(nullptr)
{
  mTextureInfo.compositableType = aCompositableType;
}

TextureClient::~TextureClient()
{}

void
TextureClient::Updated(ShadowableLayer* aLayer)
{
  if (mDescriptor.type() != SurfaceDescriptor::T__None) {
    mLayerForwarder->UpdateTexture(this, SurfaceDescriptor(mDescriptor));
  }
}

void
TextureClient::Destroyed(CompositableClient* aCompositable)
{
  // TODO[nical] why did we pass a layer here?
  // TODO: thebes specific stuff in the base class?
  mLayerForwarder->DestroyedThebesBuffer(nullptr, mDescriptor);
}

void
TextureClient::SetAsyncContainerID(uint64_t aID)
{
  NS_WARNING("TODO[nical] remove this code and implement at compositable level");
  //mLayerForwarder->AttachAsyncTexture(GetTextureChild(), aID);
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

void
TextureClient::SetIPDLActor(PTextureChild* aChild) {
  mTextureChild = aChild;
  mAllocator = static_cast<TextureChild*>(aChild);
}


TextureClientShmem::TextureClientShmem(CompositableForwarder* aForwarder, CompositableType aCompositableType)
  : TextureClient(aForwarder, aCompositableType)
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


// --------- Autolock


bool AutoLockShmemClient::EnsureTextureClient(nsIntSize aSize,
                                              gfxASurface* surface,
                                              gfxASurface::gfxContentType contentType)
{
  if (aSize != surface->GetSize() ||
      contentType != surface->GetContentType() ||
      !IsSurfaceDescriptorValid(*mDescriptor)) {
    if (IsSurfaceDescriptorValid(*mDescriptor)) {
      mTextureClient->GetLayerForwarder()->DestroySharedSurface(mDescriptor);
    }
    if (!mTextureClient->GetLayerForwarder()->AllocBuffer(aSize,
                                                          surface->GetContentType(),
                                                          mDescriptor)) {
      NS_WARNING("creating SurfaceDescriptor failed!");
      return false;
    }
  }
  return true;
}

bool AutoLockShmemClient::Update(Image* aImage, ImageLayer* aLayer, gfxASurface* surface)
{
  CompositableType type = CompositingFactory::TypeForImage(aImage);
  if (type != BUFFER_SINGLE) {
    return type == BUFFER_UNKNOWN;
  }

  nsRefPtr<gfxPattern> pat = new gfxPattern(surface);
  if (!pat)
    return true;

  pat->SetFilter(aLayer->GetFilter());
  gfxMatrix mat = pat->GetMatrix();
  aLayer->ScaleMatrix(surface->GetSize(), mat);
  pat->SetMatrix(mat);

  gfxIntSize size = aImage->GetSize();

  gfxASurface::gfxContentType contentType = gfxASurface::CONTENT_COLOR_ALPHA;
  bool isOpaque = (aLayer->GetContentFlags() & Layer::CONTENT_OPAQUE);
  if (surface) {
    contentType = surface->GetContentType();
  }
  if (contentType != gfxASurface::CONTENT_ALPHA &&
      isOpaque) {
    contentType = gfxASurface::CONTENT_COLOR;
  }
  EnsureTextureClient(size, surface, contentType);

  nsRefPtr<gfxContext> tmpCtx = mTextureClient->LockContext();
  tmpCtx->SetOperator(gfxContext::OPERATOR_SOURCE);
  PaintContext(pat,
               nsIntRegion(nsIntRect(0, 0, size.width, size.height)),
               1.0, tmpCtx, nullptr);

  return true;
}


bool
AutoLockYCbCrClient::Update(PlanarYCbCrImage* aImage)
{
  SurfaceDescriptor* descriptor = mTextureClient->LockSurfaceDescriptor();
  MOZ_ASSERT(descriptor);

  const PlanarYCbCrImage::Data *data = aImage->GetData();
  NS_ASSERTION(data, "Must be able to retrieve yuv data from image!");
  if (!data) {
    return false;
  }

  if (!EnsureTextureClient(aImage)) {
    return false;
  }

  ipc::Shmem& shmem = descriptor->get_YCbCrImage().data();

  ShmemYCbCrImage shmemImage(shmem);
  if (!shmemImage.CopyData(data->mYChannel, data->mCbChannel, data->mCrChannel,
                           data->mYSize, data->mYStride,
                           data->mCbCrSize, data->mCbCrStride,
                           data->mYSkip, data->mCbSkip)) {
    NS_WARNING("Failed to copy image data!");
    return false;
  }
  return true;
}

bool AutoLockYCbCrClient::EnsureTextureClient(PlanarYCbCrImage* aImage) {
  if (!aImage) {
    return false;
  }

  const PlanarYCbCrImage::Data *data = aImage->GetData();
  NS_ASSERTION(data, "Must be able to retrieve yuv data from image!");
  if (!data) {
    return false;
  }

  bool needsAllocation = false;
  if (mDescriptor->type() != SurfaceDescriptor::TYCbCrImage) {
    needsAllocation = true;
  } else {
    ipc::Shmem& shmem = mDescriptor->get_YCbCrImage().data();
    ShmemYCbCrImage shmemImage(shmem);
    if (shmemImage.GetYSize() != data->mYSize ||
        shmemImage.GetCbCrSize() != data->mCbCrSize) {
      needsAllocation = true;
    }
  }

  if (!needsAllocation) {
    return true;
  }

  mTextureClient->ReleaseResources();

  ipc::SharedMemory::SharedMemoryType shmType = OptimalShmemType();
  size_t size = ShmemYCbCrImage::ComputeMinBufferSize(data->mYSize,
                                                      data->mCbCrSize);
  ipc::Shmem shmem;
  if (!mTextureClient->GetSurfaceAllocator()->AllocateUnsafe(size, shmType, &shmem)) {
    return false;
  }

  ShmemYCbCrImage::InitializeBufferInfo(shmem.get<uint8_t>(),
                                        data->mYSize,
                                        data->mCbCrSize);


  *mDescriptor = YCbCrImage(shmem, 0);

  return true;
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

void
TextureClientShmemYCbCr::EnsureTextureClient(gfx::IntSize aSize,
                                             gfxASurface::gfxContentType aType)
{
/*
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
*/
}

TextureClientShared::~TextureClientShared()
{
  if (IsSurfaceDescriptorValid(mDescriptor)) {
    mLayerForwarder->DestroySharedSurface(&mDescriptor);
  }
}

TextureClientSharedGL::TextureClientSharedGL(CompositableForwarder* aForwarder,
                                             CompositableType aCompositableType)
  : TextureClientShared(aForwarder, aCompositableType)
{
  mTextureInfo.memoryType = TEXTURE_SHARED|TEXTURE_BUFFERED;
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

TextureClientBridge::TextureClientBridge(CompositableForwarder* aForwarder,
                                         CompositableType aCompositableType)
  : TextureClient(aForwarder, aCompositableType)
{
  mTextureInfo.memoryType = TEXTURE_SHMEM;
}

/* static */ CompositableType
CompositingFactory::TypeForImage(Image* aImage) {
  if (!aImage) {
    return BUFFER_UNKNOWN;
  }

  if (aImage->GetFormat() == SHARED_TEXTURE) {
    return BUFFER_SHARED;
  }
  return BUFFER_SINGLE;
}

/* static */ TemporaryRef<ImageClient>
CompositingFactory::CreateImageClient(LayersBackend aParentBackend,
                                      CompositableType aCompositableHostType,
                                      CompositableForwarder* aForwarder,
                                      TextureFlags aFlags)
{
  RefPtr<ImageClient> result = nullptr;
  switch (aCompositableHostType) {
  case BUFFER_SHARED:
    if (aParentBackend == LAYERS_OPENGL) {
      result = new ImageClientShared(aForwarder, aFlags);
    }
    // fall through to BUFFER_SINGLE
  case BUFFER_SINGLE:
    if (aParentBackend == LAYERS_OPENGL || aParentBackend == LAYERS_D3D11) {
      result = new ImageClientTexture(aForwarder, aFlags);
    }
    break;
  case BUFFER_BRIDGE:
    if (aParentBackend == LAYERS_OPENGL) {
      result = new ImageClientBridge(aForwarder, aFlags);
    }
    break;
  case BUFFER_UNKNOWN:
    return nullptr;
  default:
    // FIXME [bjacob] unhandled cases were reported as GCC warnings; with this,
    // at least we'll known if we run into them.
    MOZ_NOT_REACHED("unhandled program type");
  }

  NS_ASSERTION(result, "Failed to create ImageClient");

  return result.forget();
}

/* static */ TemporaryRef<CanvasClient>
CompositingFactory::CreateCanvasClient(LayersBackend aParentBackend,
                                       CompositableType aCompositableHostType,
                                       CompositableForwarder* aForwarder,
                                       TextureFlags aFlags)
{
  if (aCompositableHostType == BUFFER_DIRECT) {
    return new CanvasClient2D(aForwarder, aFlags);
  }
  if (aCompositableHostType == BUFFER_SHARED) {
    if (aParentBackend == LAYERS_OPENGL) {
      return new CanvasClientWebGL(aForwarder, aFlags);
    }
    return new CanvasClient2D(aForwarder, aFlags);
  }
  return nullptr;
}

/* static */ TemporaryRef<ContentClient>
CompositingFactory::CreateContentClient(LayersBackend aParentBackend,
                                        CompositableType aCompositableHostType,
                                        CompositableForwarder* aForwarder,
                                        TextureFlags aFlags)
{
  if (aParentBackend != LAYERS_OPENGL && aParentBackend != LAYERS_D3D11) {
    return nullptr;
  }
  if (aCompositableHostType == BUFFER_CONTENT) {
    return new ContentClientTexture(aForwarder, aFlags);
  }
  if (aCompositableHostType == BUFFER_CONTENT_DIRECT) {
    if (ShadowLayerManager::SupportsDirectTexturing()) {
      return new ContentClientDirect(aForwarder, aFlags);
    }
    return new ContentClientTexture(aForwarder, aFlags);
  }
  if (aCompositableHostType == BUFFER_TILED) {
    NS_RUNTIMEABORT("No CompositableClient for tiled layers");
  }
  return nullptr;
}

/* static */ TemporaryRef<TextureClient>
CompositingFactory::CreateTextureClient(LayersBackend aParentBackend,
                                        TextureHostType aTextureHostType,
                                        CompositableType aCompositableHostType,
                                        CompositableForwarder* aForwarder,
                                        bool aStrict /* = false */)
{
  RefPtr<TextureClient> result = nullptr;
  switch (aTextureHostType) {
  case TEXTURE_SHARED|TEXTURE_BUFFERED:
    if (aParentBackend == LAYERS_OPENGL) {
      result = new TextureClientSharedGL(aForwarder, aCompositableHostType);
    }
    break;
  case TEXTURE_SHARED:
    if (aParentBackend == LAYERS_OPENGL) {
      result = new TextureClientShared(aForwarder, aCompositableHostType);
    }
    break;
  case TEXTURE_SHMEM:
    if (aParentBackend == LAYERS_OPENGL || aParentBackend == LAYERS_D3D11) {
      result = new TextureClientShmem(aForwarder, aCompositableHostType);
    }
    break;
  case TEXTURE_ASYNC:
    result = new TextureClientBridge(aForwarder, aCompositableHostType);
    break;
  case TEXTURE_TILE:
    result = new TextureClientTile(aForwarder, aCompositableHostType);
    break;
  case TEXTURE_SHARED|TEXTURE_DXGI:
#ifdef XP_WIN
    result = new TextureClientD3D11(aForwarder, aCompositableHostType);
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