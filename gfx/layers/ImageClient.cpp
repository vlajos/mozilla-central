/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextureClient.h"
#include "ImageClient.h"
#include "BasicLayers.h"
#include "mozilla/layers/ShadowLayers.h"
#include "SharedTextureImage.h"
#include "ImageContainer.h" // For PlanarYCbCrImage

namespace mozilla {
namespace layers {

ImageClient::ImageClient(CompositableForwarder* aFwd)
: CompositableClient(aFwd)
, mLastPaintedImageSerial(0)
{}

void
ImageClient::UpdatePictureRect(nsIntRect aRect)
{
  if (mPictureRect == aRect) {
    return;
  }
  mPictureRect = aRect;
  MOZ_ASSERT(mForwarder);
  GetForwarder()->UpdatePictureRect(this, aRect);
}

ImageClientTexture::ImageClientTexture(CompositableForwarder* aFwd,
                                       TextureFlags aFlags)
: ImageClient(aFwd)
, mFlags(aFlags)
{
}

bool
ImageClientTexture::UpdateImage(ImageContainer* aContainer, ImageLayer* aLayer)
{
  if (!mTextureClient) {
    mTextureClient = CreateTextureClient(TEXTURE_SHMEM, mFlags);
  }

  nsRefPtr<gfxASurface> surface;
  AutoLockImage autoLock(aContainer, getter_AddRefs(surface));
  Image *image = autoLock.GetImage();

  if (mLastPaintedImageSerial == image->GetSerial()) {
    return true;
  }

  if (image->GetFormat() == PLANAR_YCBCR) {
    AutoLockYCbCrClient clientLock(mTextureClient);
    PlanarYCbCrImage* ycbcr = static_cast<PlanarYCbCrImage*>(image);
    if (!clientLock.Update(ycbcr)) {
      return false;
    }
    UpdatePictureRect(ycbcr->GetData()->GetPictureRect());
  } else {
    AutoLockShmemClient clientLock(mTextureClient);
    if (!clientLock.Update(image, aLayer, surface)) {
      return false;
    }
    UpdatePictureRect(nsIntRect(0, 0,
                                image->GetSize().width,
                                image->GetSize().height));
  }
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

ImageClientShared::ImageClientShared(CompositableForwarder* aFwd,
                                     TextureFlags aFlags)
: ImageClient(aFwd)
{
  //mTextureClient = CreateTextureClient(TEXTURE_SHARED, aFlags);
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

  CompositableType type = CompositingFactory::TypeForImage(autoLock.GetImage());
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

ImageClientBridge::ImageClientBridge(CompositableForwarder* aFwd,
                                     TextureFlags aFlags)
: ImageClient(aFwd)
, mAsyncContainerID(0)
{
}

bool
ImageClientBridge::UpdateImage(ImageContainer* aContainer, ImageLayer* aLayer)
{
  if (!GetForwarder()) {
    return false;
  }
  if (mAsyncContainerID == aContainer->GetAsyncContainerID()) {
    return true;
  }
  mAsyncContainerID = aContainer->GetAsyncContainerID();
  GetForwarder()->AttachAsyncCompositable(mAsyncContainerID, aLayer->AsShadowableLayer());
  return true;
}

}
}
