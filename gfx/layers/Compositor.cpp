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
  , mBuffer(nullptr)
  , mFormat(gfx::FORMAT_UNKNOWN)
  , mDeAllocator(aDeallocator)
{
  MOZ_COUNT_CTOR(TextureHost);
  if (aDeallocator) {
    mBuffer = new SurfaceDescriptor(null_t());
  }
}

TextureHost::~TextureHost()
{
  if (mBuffer) {
    if (mDeAllocator) {
      mDeAllocator->DestroySharedSurface(mBuffer);
    } else {
      MOZ_ASSERT(mBuffer->type() == SurfaceDescriptor::Tnull_t);
    }
    delete mBuffer;
  }
  MOZ_COUNT_DTOR(TextureHost);
}

void
TextureHost::Update(const SurfaceDescriptor& aImage,
                    bool* aIsInitialised,
                    bool* aNeedsReset,
                    nsIntRegion* aRegion)
{
  UpdateImpl(aImage, aIsInitialised, aNeedsReset, aRegion);
}

void
TextureHost::SwapTextures(const SurfaceDescriptor& aImage,
                          SurfaceDescriptor* aResult,
                          bool* aIsInitialised,
                          bool* aNeedsReset,
                          nsIntRegion* aRegion)
{
  SwapTexturesImpl(aImage, aIsInitialised, aNeedsReset, aRegion);

  MOZ_ASSERT(mBuffer, "trying to swap a non-buffered texture host?");
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

LayersBackend Compositor::sBackend = LAYERS_NONE;
LayersBackend
Compositor::GetBackend()
{
  return sBackend;
}

// implemented in TextureOGL.cpp
TemporaryRef<TextureHost> CreateTextureHostOGL(SurfaceDescriptorType aDescriptorType,
                                               uint32_t aTextureHostFlags,
                                               uint32_t aTextureFlags,
                                               bool aBuffered,
                                               ISurfaceAllocator* aDeAllocator);

TemporaryRef<TextureHost> CreateTextureHostD3D9(SurfaceDescriptorType aDescriptorType,
                                                uint32_t aTextureHostFlags,
                                                uint32_t aTextureFlags,
                                                bool aBuffered,
                                                ISurfaceAllocator* aDeAllocator)
{
  NS_RUNTIMEABORT("not implemented");
  return nullptr;
}

#ifdef MOZ_ENABLE_D3D10_LAYER
TemporaryRef<TextureHost> CreateTextureHostD3D10(SurfaceDescriptorType aDescriptorType,
                                                 uint32_t aTextureHostFlags,
                                                 uint32_t aTextureFlags,
                                                 bool aBuffered,
                                                 ISurfaceAllocator* aDeAllocator)
{
  NS_RUNTIMEABORT("not implemented");
  return nullptr;
}

// implemented in TextureD3D11.cpp
TemporaryRef<TextureHost> CreateTextureHostD3D11(SurfaceDescriptorType aDescriptorType,
                                                 uint32_t aTextureHostFlags,
                                                 uint32_t aTextureFlags,
                                                 bool aBuffered,
                                                 ISurfaceAllocator* aDeAllocator)
#endif // MOZ_ENABLE_D3D10_LAYER

TemporaryRef<TextureHost> CreateTextureHost(SurfaceDescriptorType aDescriptorType,
                                            uint32_t aTextureHostFlags,
                                            uint32_t aTextureFlags,
                                            bool aBuffered,
                                            ISurfaceAllocator* aDeAllocator)
{
  switch (Compositor::GetBackend()) {
    case LAYERS_OPENGL : return CreateTextureHostOGL(aDescriptorType,
                                                     aTextureHostFlags,
                                                     aTextureFlags,
                                                     aBuffered,
                                                     aDeAllocator);
    case LAYERS_D3D9 : return CreateTextureHostD3D9(aDescriptorType,
                                                    aTextureHostFlags,
                                                    aTextureFlags,
                                                    aBuffered,
                                                    aDeAllocator);
#ifdef MOZ_ENABLE_D3D10_LAYER
    case LAYERS_D3D10 : return CreateTextureHostD3D10(aDescriptorType,
                                                      aTextureHostFlags,
                                                      aTextureFlags,
                                                      aBuffered,
                                                      aDeAllocator);
    case LAYERS_D3D11 : return CreateTextureHostD3D11(aDescriptorType,
                                                      aTextureHostFlags,
                                                      aTextureFlags,
                                                      aBuffered,
                                                      aDeAllocator);
#endif
    default : return nullptr;
  }
}


} // namespace
} // namespace
