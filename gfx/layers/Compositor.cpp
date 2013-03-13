/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/Compositor.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/layers/ISurfaceAllocator.h"
#include "LayersLogging.h"
#include "nsPrintfCString.h"

namespace mozilla {
namespace layers {

/* static */ LayersBackend Compositor::sBackend = LAYERS_NONE;
/* static */ LayersBackend
Compositor::GetBackend()
{
  return sBackend;
}

// implemented in TextureOGL.cpp
TemporaryRef<TextureHost> CreateTextureHostOGL(SurfaceDescriptorType aDescriptorType,
                                               uint32_t aTextureHostFlags,
                                               uint32_t aTextureFlags);

TemporaryRef<TextureHost> CreateTextureHostD3D9(SurfaceDescriptorType aDescriptorType,
                                                uint32_t aTextureHostFlags,
                                                uint32_t aTextureFlags)
{
  NS_RUNTIMEABORT("not implemented");
  return nullptr;
}

#ifdef MOZ_ENABLE_D3D10_LAYER
TemporaryRef<TextureHost> CreateTextureHostD3D10(SurfaceDescriptorType aDescriptorType,
                                                 uint32_t aTextureHostFlags,
                                                 uint32_t aTextureFlags)
{
  NS_RUNTIMEABORT("not implemented");
  return nullptr;
}

// implemented in TextureD3D11.cpp
TemporaryRef<TextureHost> CreateTextureHostD3D11(SurfaceDescriptorType aDescriptorType,
                                                 uint32_t aTextureHostFlags,
                                                 uint32_t aTextureFlags);
#endif // MOZ_ENABLE_D3D10_LAYER

TemporaryRef<TextureHost> CreateTextureHost(SurfaceDescriptorType aDescriptorType,
                                            uint32_t aTextureHostFlags,
                                            uint32_t aTextureFlags)
{
  switch (Compositor::GetBackend()) {
    case LAYERS_OPENGL : return CreateTextureHostOGL(aDescriptorType,
                                                     aTextureHostFlags,
                                                     aTextureFlags);
    case LAYERS_D3D9 : return CreateTextureHostD3D9(aDescriptorType,
                                                    aTextureHostFlags,
                                                    aTextureFlags);
#ifdef MOZ_ENABLE_D3D10_LAYER
    case LAYERS_D3D10 : return CreateTextureHostD3D10(aDescriptorType,
                                                      aTextureHostFlags,
                                                      aTextureFlags);
    case LAYERS_D3D11 : return CreateTextureHostD3D11(aDescriptorType,
                                                      aTextureHostFlags,
                                                      aTextureFlags);
#endif
    default : return nullptr;
  }
}


} // namespace
} // namespace
