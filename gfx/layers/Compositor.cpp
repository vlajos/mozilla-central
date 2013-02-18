/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/Compositor.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/layers/ISurfaceDeallocator.h"
//#include "mozilla/layers/ImageContainerParent.h"

namespace mozilla {
namespace layers {

TextureHost::TextureHost(BufferMode aBufferMode, ISurfaceDeallocator* aDeallocator)
  : mFlags(NoFlags)
  , mBufferMode(aBufferMode)
  , mDeAllocator(aDeallocator)
  , mAsyncContainerID(0)
  , mAsyncTextureVersion(0)
  , mCompositorID(0)
  , mFormat(gfx::FORMAT_UNKNOWN)

{
  MOZ_COUNT_CTOR(TextureHost);
  if (aBufferMode != BUFFER_NONE) {
    mBuffer = new SurfaceDescriptor(null_t());
  } else {
    mBuffer = nullptr;
  }
}

TextureHost::~TextureHost()
{
  if (IsBuffered() && mBuffer) {
    MOZ_ASSERT(mDeAllocator);
    mDeAllocator->DestroySharedSurface(mBuffer);
    delete mBuffer;
  }
  MOZ_COUNT_DTOR(TextureHost);
}

void TextureHost::Update(const SurfaceDescriptor& aImage,
                         SurfaceDescriptor* aResult,
                         bool* aIsInitialised,
                         bool* aNeedsReset,
                         nsIntRegion* aRegion)
{
  UpdateImpl(aImage, aIsInitialised, aNeedsReset, aRegion);

  // buffering
  if (IsBuffered()) {
    if (aResult) {
      *aResult = *mBuffer;
    }
    *mBuffer = aImage;
  } else {
    if (aResult) {
      *aResult = aImage;
    }
  }
}

bool TextureHost::UpdateAsyncTexture()
{
  if (!IsAsync()) {
    return true;
  }
  /*
  ImageContainerParent::SetCompositorIDForImage(mAsyncContainerID, mCompositorID);
  uint32_t imgVersion = ImageContainerParent::GetSurfaceDescriptorVersion(mAsyncContainerID);
  if (imgVersion != mAsyncTextureVersion) {
    SurfaceDescriptor* img = ImageContainerParent::GetSurfaceDescriptor(mAsyncContainerID);
    if (!img) {
      return false;
    }
    Update(*img, img);
    mAsyncTextureVersion = imgVersion;
  }
  return true;
  */
  return false;
}


} // namespace
} // namespace
