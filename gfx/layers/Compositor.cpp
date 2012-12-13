/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/Compositor.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/layers/ShadowLayers.h" // for ISurfaceDeAllocator

namespace mozilla {
namespace layers {

TextureHost::TextureHost(Buffering aBuffering)
  : mFlags(NoFlags)
  , mBuffering(aBuffering)
{
  if (aBuffering != Buffering::NONE) {
    mBuffer = new SharedImage;
  }
}

TextureHost::~TextureHost()
{
  if (IsBuffered()) {
    MOZ_ASSERT(mDeAllocator);
    mDeAllocator->DestroySharedSurface(mBuffer);
    delete mBuffer;
  }
}

void TextureHost::Update(const SharedImage& aImage,
                         SharedImage* aResult,
                         bool* aIsInitialised,
                         bool* aNeedsReset)
{
  UpdateImpl(aImage, aIsInitialised, aNeedsReset);

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

void TextureHost::Update(gfxASurface* aSurface, nsIntRegion& aRegion) {
  UpdateImpl(aSurface, aRegion);
  //TODO[nical] no buffering?
}


} // namespace
} // namespace