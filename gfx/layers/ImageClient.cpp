/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextureClient.h"
#include "ImageClient.h"
#include "BasicLayers.h"
#include "mozilla/layers/ShadowLayers.h"
#include "SharedTextureImage.h"

namespace mozilla {
namespace layers {

ImageClientTexture::ImageClientTexture(ShadowLayerForwarder* aLayerForwarder,
                                       ShadowableLayer* aLayer,
                                       TextureFlags aFlags)
{
  mTextureClient = aLayerForwarder->CreateTextureClientFor(TEXTURE_SHMEM, BUFFER_TEXTURE, aLayer, aFlags, true);
}

bool
ImageClientTexture::UpdateImage(ImageContainer* aContainer, ImageLayer* aLayer)
{
  if (!mTextureClient) {
    return true;
  }

  nsRefPtr<gfxASurface> surface;
  AutoLockImage autoLock(aContainer, getter_AddRefs(surface));
  Image *image = autoLock.GetImage();

  if (mLastPaintedImageSerial == image->GetSerial()) {
    return true;
  }

  BufferType type = CompositingFactory::TypeForImage(autoLock.GetImage());
  if (type != BUFFER_TEXTURE) {
    return type == BUFFER_UNKNOWN;
  }

  nsRefPtr<gfxPattern> pat = new gfxPattern(surface);
  if (!pat)
    return true;

  pat->SetFilter(aLayer->GetFilter());
  gfxMatrix mat = pat->GetMatrix();
  aLayer->ScaleMatrix(surface->GetSize(), mat);
  pat->SetMatrix(mat);

  gfxIntSize size = autoLock.GetSize();

  gfxASurface::gfxContentType contentType = gfxASurface::CONTENT_COLOR_ALPHA;
  bool isOpaque = (aLayer->GetContentFlags() & Layer::CONTENT_OPAQUE);
  if (surface) {
    contentType = surface->GetContentType();
  }
  if (contentType != gfxASurface::CONTENT_ALPHA &&
      isOpaque) {
    contentType = gfxASurface::CONTENT_COLOR;
  }
  mTextureClient->EnsureTextureClient(gfx::IntSize(size.width, size.height), contentType);

  nsRefPtr<gfxContext> tmpCtx = mTextureClient->LockContext();
  tmpCtx->SetOperator(gfxContext::OPERATOR_SOURCE);
  PaintContext(pat,
               nsIntRegion(nsIntRect(0, 0, size.width, size.height)),
               1.0, tmpCtx, nullptr);

  mTextureClient->Unlock();
  
  mLastPaintedImageSerial = image->GetSerial();
  return true;
}


void
ImageClientTexture::SetBuffer(const TextureInfo& aTextureInfo,
                              const SurfaceDescriptor& aBuffer)
{
  mTextureClient->SetDescriptor(aBuffer);
}

void
ImageClientTexture::Updated(ShadowableLayer* aLayer)
{
  mTextureClient->Updated(aLayer);
}

ImageClientShared::ImageClientShared(ShadowLayerForwarder* aLayerForwarder,
                                     ShadowableLayer* aLayer, 
                                     TextureFlags aFlags)
{
  mTextureClient = aLayerForwarder->CreateTextureClientFor(TEXTURE_SHARED, BUFFER_SHARED, aLayer, true, aFlags);
}

bool
ImageClientShared::UpdateImage(ImageContainer* aContainer, ImageLayer* aLayer)
{
  gfxASurface* dontCare = nullptr;
  AutoLockImage autoLock(aContainer, &dontCare);
  Image *image = autoLock.GetImage();

  if (mLastPaintedImageSerial == image->GetSerial()) {
    return true;
  }

  BufferType type = CompositingFactory::TypeForImage(autoLock.GetImage());
  if (type != BUFFER_SHARED) {
    return type == BUFFER_UNKNOWN;
  }

  SharedTextureImage* sharedImage = static_cast<SharedTextureImage*>(image);
  const SharedTextureImage::Data *data = sharedImage->GetData();

  SharedTextureDescriptor texture(data->mShareType, data->mHandle, data->mSize, data->mInverted);
  mTextureClient->SetDescriptor(SurfaceDescriptor(texture));

  mLastPaintedImageSerial = image->GetSerial();
  return true;
}

void
ImageClientShared::Updated(ShadowableLayer* aLayer)
{
  mTextureClient->Updated(aLayer);
}

ImageClientBridge::ImageClientBridge(ShadowLayerForwarder* aLayerForwarder,
                                     ShadowableLayer* aLayer,
                                     TextureFlags aFlags)
{
  mTextureClient = aLayerForwarder->CreateTextureClientFor(TEXTURE_BRIDGE, BUFFER_BRIDGE, aLayer, aFlags, true);
}

bool
ImageClientBridge::UpdateImage(ImageContainer* aContainer, ImageLayer* aLayer)
{
  mTextureClient->SetAsyncContainerID(aContainer->GetAsyncContainerID());
  return true;
}

void
ImageClientBridge::Updated(ShadowableLayer* aLayer)
{
  //mTextureClient->Updated(aLayer);
}

}
}
