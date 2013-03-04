/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageContainer.h"
#include "mozilla/ipc/Shmem.h"
#include "mozilla/ipc/SharedMemory.h"
#include "mozilla/layers/ISurfaceAllocator.h"

#ifndef MOZILLA_LAYERS_SHAREDPLANARYCBCRIMAGE_H
#define MOZILLA_LAYERS_SHAREDPLANARYCBCRIMAGE_H

namespace mozilla {
namespace layers {

class ImageClient;

class SharedPlanarYCbCrImage : public PlanarYCbCrImage
{
public:
  SharedPlanarYCbCrImage(ISurfaceAllocator* aAllocator)
  : PlanarYCbCrImage(nullptr)
  , mSurfaceAllocator(aAllocator), mAllocated(false)
  {
    MOZ_COUNT_CTOR(SharedPlanarYCbCrImage);
  }

  ~SharedPlanarYCbCrImage();

  virtual SharedPlanarYCbCrImage* AsSharedPlanarYCbCrImage() MOZ_OVERRIDE
  {
    return this;
  }

  virtual already_AddRefed<gfxASurface> GetAsSurface() MOZ_OVERRIDE
  {
    if (!mAllocated) {
      NS_WARNING("Can't get as surface");
      return nullptr;
    }
    return PlanarYCbCrImage::GetAsSurface();
  }

  virtual void SetData(const PlanarYCbCrImage::Data& aData) MOZ_OVERRIDE;

  virtual bool Allocate(PlanarYCbCrImage::Data& aData);

  virtual bool IsValid() MOZ_OVERRIDE {
    return mAllocated;
  }

  bool ToSurfaceDescriptor(SurfaceDescriptor& aDesc);

private:
  ipc::Shmem mShmem;
  ISurfaceAllocator* mSurfaceAllocator;
  bool mAllocated;
};

} // namespace
} // namespace

#endif
