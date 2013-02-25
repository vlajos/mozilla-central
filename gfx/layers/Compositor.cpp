/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/Compositor.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/layers/ISurfaceAllocator.h"
//#include "mozilla/layers/ImageContainerParent.h"
#include "LayersLogging.h"
#include "nsPrintfCString.h"

namespace mozilla {
namespace layers {

TextureHost::TextureHost(ISurfaceAllocator* aDeallocator)
  : mFlags(NoFlags)
  , mFormat(gfx::FORMAT_UNKNOWN)
  , mDeAllocator(aDeallocator)
{
  MOZ_COUNT_CTOR(TextureHost);
  mBuffer = new SurfaceDescriptor(null_t());
}

TextureHost::~TextureHost()
{
  if (mBuffer) {
    MOZ_ASSERT(mDeAllocator);
    mDeAllocator->DestroySharedSurface(mBuffer);
    delete mBuffer;
  }
  MOZ_COUNT_DTOR(TextureHost);
}

void TextureHost::Update(const SurfaceDescriptor& aImage,
                         bool* aIsInitialised,
                         bool* aNeedsReset,
                         nsIntRegion* aRegion)
{
  UpdateImpl(aImage, aIsInitialised, aNeedsReset, aRegion);
}

void TextureHost::SwapTextures(const SurfaceDescriptor& aImage,
                               SurfaceDescriptor* aResult,
                               bool* aIsInitialised,
                               bool* aNeedsReset,
                               nsIntRegion* aRegion)
{
  SwapTexturesImpl(aImage, aIsInitialised, aNeedsReset, aRegion);

  if (aResult) {
    *aResult = *mBuffer;
  }
  *mBuffer = aImage;
}

#ifdef MOZ_LAYERS_HAVE_LOG
void
TextureSource::PrintInfo(nsACString& aTo, const char* aPrefix)
{
  aTo += aPrefix;
  aTo += nsPrintfCString("UnknownTextureSource (0x%p)", this);
}

void
TextureHost::PrintInfo(nsACString& aTo, const char* aPrefix)
{
  aTo += aPrefix;
  aTo += nsPrintfCString("%s (0x%p)", Name(), this);
  AppendToString(aTo, GetSize(), " [size=", "]");
  AppendToString(aTo, GetFormat(), " [format=", "]");
  AppendToString(aTo, mFlags, " [flags=", "]");
}
#endif // MOZ_LAYERS_HAVE_LOG

} // namespace
} // namespace
