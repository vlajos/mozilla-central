/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/ImageContainerParent.h"
#include "mozilla/layers/ImageBridgeParent.h"
#include "mozilla/layers/SharedImageUtils.h"
#include "CompositorParent.h"

namespace mozilla {
namespace layers {

ImageContainerParent::ImageContainerParent(uint32_t aHandle)
: mID(aHandle), mStop(false) {
  MOZ_COUNT_CTOR(ImageContainerParent);
}

bool ImageContainerParent::RecvPublishImage(const SurfaceDescriptor& aImage)
{
  SurfaceDescriptor *copy = new SurfaceDescriptor(aImage);
  SurfaceDescriptor *prevImage = SwapSurfaceDescriptor(mID, copy);

  uint32_t compositorID = GetCompositorIDForImage(mID);
  CompositorParent* compositor = CompositorParent::GetCompositor(compositorID);

  if (compositor) {
    compositor->ScheduleComposition();
  }

  if (prevImage && !mStop) {
    SendReturnImage(*prevImage);
    delete prevImage;
  }
  return true;
}

bool ImageContainerParent::RecvFlush()
{
  SurfaceDescriptor *img = RemoveSurfaceDescriptor(mID);
  if (img) {
    delete img;
  }
  return true;
}

void ImageContainerParent::DoStop()
{
  mStop = true;
}

bool ImageContainerParent::RecvStop()
{
  DoStop();
  return true;
}

bool ImageContainerParent::Recv__delete__()
{
  NS_ABORT_IF_FALSE(mStop, "Should be in a stopped state when __delete__");
  SurfaceDescriptor* removed = RemoveSurfaceDescriptor(mID);
  if (removed) {
    DeallocSurfaceDescriptorData(this, *removed);
    delete removed;
  }

  return true;
}

ImageContainerParent::~ImageContainerParent()
{
  MOZ_COUNT_DTOR(ImageContainerParent);
  // On emergency shutdown, Recv__delete__ won't be invoked, so
  // we need to cleanup the global table here and not worry about
  // deallocating the shmem in the scenario since the emergency 
  // shutdown procedure takes care of that. 
  // On regular shutdown, Recv__delete__ also calls RemoveSurfaceDescriptor
  // but it is not a problem because it is safe to call twice.
  SurfaceDescriptor* removed = RemoveSurfaceDescriptor(mID);
  if (removed) {
    delete removed;
  }
}

struct ImageIDPair {
  ImageIDPair(SurfaceDescriptor* aImage, uint32_t aID)
  : image(aImage), id(aID), compositorID(0), version(1) {}
  SurfaceDescriptor*  image;
  uint64_t      id;
  uint64_t      compositorID;
  uint32_t      version;
};

typedef nsTArray<ImageIDPair> SurfaceDescriptorMap;
SurfaceDescriptorMap *sSurfaceDescriptorMap = nullptr;

static const int SURFACEDESCRIPTORMAP_INVALID_INDEX = -1;

static int IndexOf(uint64_t aID)
{
  for (unsigned int i = 0; i < sSurfaceDescriptorMap->Length(); ++i) {
    if ((*sSurfaceDescriptorMap)[i].id == aID) {
      return i;
    }
  }
  return SURFACEDESCRIPTORMAP_INVALID_INDEX;
}

bool ImageContainerParent::IsExistingID(uint64_t aID)
{
  return IndexOf(aID) != SURFACEDESCRIPTORMAP_INVALID_INDEX;
}

SurfaceDescriptor* ImageContainerParent::SwapSurfaceDescriptor(uint64_t aID, 
                                                   SurfaceDescriptor* aImage)
{
  int idx = IndexOf(aID);
  if (idx == SURFACEDESCRIPTORMAP_INVALID_INDEX) {
    sSurfaceDescriptorMap->AppendElement(ImageIDPair(aImage,aID));
    return nullptr;
  }
  SurfaceDescriptor *prev = (*sSurfaceDescriptorMap)[idx].image;
  (*sSurfaceDescriptorMap)[idx].image = aImage;
  (*sSurfaceDescriptorMap)[idx].version++;
  return prev;
}

uint32_t ImageContainerParent::GetSurfaceDescriptorVersion(uint64_t aID)
{
  int idx = IndexOf(aID);
  if (idx == SURFACEDESCRIPTORMAP_INVALID_INDEX) return 0;
  return (*sSurfaceDescriptorMap)[idx].version;
}

SurfaceDescriptor* ImageContainerParent::RemoveSurfaceDescriptor(uint64_t aID) 
{
  int idx = IndexOf(aID);
  if (idx != SURFACEDESCRIPTORMAP_INVALID_INDEX) {
    SurfaceDescriptor* img = (*sSurfaceDescriptorMap)[idx].image;
    sSurfaceDescriptorMap->RemoveElementAt(idx);
    return img;
  }
  return nullptr;
}

SurfaceDescriptor* ImageContainerParent::GetSurfaceDescriptor(uint64_t aID)
{
  int idx = IndexOf(aID);
  if (idx != SURFACEDESCRIPTORMAP_INVALID_INDEX) {
    return (*sSurfaceDescriptorMap)[idx].image;
  }
  return nullptr;
}

bool ImageContainerParent::SetCompositorIDForImage(uint64_t aImageID, uint64_t aCompositorID)
{
  int idx = IndexOf(aImageID);
  if (idx == SURFACEDESCRIPTORMAP_INVALID_INDEX) {
    return false;
  }
  (*sSurfaceDescriptorMap)[idx].compositorID = aCompositorID;
  return true;
}

uint64_t ImageContainerParent::GetCompositorIDForImage(uint64_t aImageID)
{
  int idx = IndexOf(aImageID);
  if (idx != SURFACEDESCRIPTORMAP_INVALID_INDEX) {
    return (*sSurfaceDescriptorMap)[idx].compositorID;
  }
  return 0;
}

void ImageContainerParent::CreateSurfaceDescriptorMap()
{
  if (sSurfaceDescriptorMap == nullptr) {
    sSurfaceDescriptorMap = new SurfaceDescriptorMap;
  }
}
void ImageContainerParent::DestroySurfaceDescriptorMap()
{
  if (sSurfaceDescriptorMap != nullptr) {
    NS_ABORT_IF_FALSE(sSurfaceDescriptorMap->Length() == 0,
                      "The global shared image map should be empty!");
    delete sSurfaceDescriptorMap;
    sSurfaceDescriptorMap = nullptr;
  }
}

} // namespace
} // namespace
