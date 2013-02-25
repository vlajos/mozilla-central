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
#include "mozilla/layers/SharedRGBImage.h"
#include "mozilla/layers/SharedPlanarYCbCrImage.h"

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
ImageClientTexture::UpdateImage(ImageContainer* aContainer, uint32_t aContentFlags)
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
      NS_WARNING("failed to update TextureClient (YCbCr)");
      return false;
    }
    UpdatePictureRect(ycbcr->GetData()->GetPictureRect());
  } else {
    AutoLockShmemClient clientLock(mTextureClient);
    if (!clientLock.Update(image, aContentFlags, surface)) {
      NS_WARNING("failed to update TextureClient");
      return false;
    }
    UpdatePictureRect(nsIntRect(0, 0,
                                image->GetSize().width,
                                image->GetSize().height));
  }
  mLastPaintedImageSerial = image->GetSerial();
  return true;
}

/*void
ImageClientTexture::SetBuffer(const TextureInfo& aTextureInfo,
                              const SurfaceDescriptor& aBuffer)
{
  mTextureClient->SetDescriptor(aBuffer);
}*/

void
ImageClientTexture::Updated()
{
  mTextureClient->Updated();
}

ImageClientShared::ImageClientShared(CompositableForwarder* aFwd,
                                     TextureFlags aFlags)
  : ImageClient(aFwd)
{
}

bool
ImageClientShared::UpdateImage(ImageContainer* aContainer, uint32_t aContentFlags)
{
  gfxASurface* dontCare = nullptr;
  AutoLockImage autoLock(aContainer, &dontCare);
  Image *image = autoLock.GetImage();

  if (mLastPaintedImageSerial == image->GetSerial()) {
    return true;
  }

  CompositableType type = CompositingFactory::TypeForImage(autoLock.GetImage());
  if (type != BUFFER_DIRECT_USING_SHAREDTEXTUREIMAGE) {
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
ImageClientShared::Updated()
{
  mTextureClient->Updated();
}

ImageClientBridge::ImageClientBridge(CompositableForwarder* aFwd,
                                     TextureFlags aFlags)
: ImageClient(aFwd)
, mAsyncContainerID(0)
, mLayer(nullptr)
{
}

bool
ImageClientBridge::UpdateImage(ImageContainer* aContainer, uint32_t aContentFlags)
{
  if (!GetForwarder() || !mLayer) {
    return false;
  }
  if (mAsyncContainerID == aContainer->GetAsyncContainerID()) {
    return true;
  }
  mAsyncContainerID = aContainer->GetAsyncContainerID();
  GetForwarder()->AttachAsyncCompositable(mAsyncContainerID, mLayer);
  return true;
}

already_AddRefed<Image>
ImageClient::CreateImage(const uint32_t *aFormats,
                         uint32_t aNumFormats)
{
  nsRefPtr<Image> img;
  for (uint32_t i = 0; i < aNumFormats; i++) {
    switch (aFormats[i]) {
      case PLANAR_YCBCR:
        img = new SharedPlanarYCbCrImage(this);
        return img.forget();
//      case SHARED_RGB:  // TODO[nical]
//        img = new SharedRGBImage(this);
//        return img.forget();
#ifdef MOZ_WIDGET_GONK
      case GONK_IO_SURFACE:
        img = new GonkIOSurfaceImage();
        return img.forget();
      case GRALLOC_PLANAR_YCBCR:
        img = new GrallocPlanarYCbCrImage();
        return img.forget();
#endif
    }
  }
  return nullptr;
}

}
}
