/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_LAYERS_ISURFACEDEALLOCATOR
#define GFX_LAYERS_ISURFACEDEALLOCATOR

#include "mozilla/ipc/SharedMemory.h"

class gfxSharedImageSurface;



namespace mozilla {
namespace ipc {
class Shmem;
} // namespace
namespace layers {

class SurfaceDescriptor;

/**
 * SurfaceDeallocator interface
 */
class ISurfaceDeallocator
{
public:
  ISurfaceDeallocator() {}
  virtual void DestroySharedSurface(gfxSharedImageSurface* aSurface) = 0;
  virtual void DestroySharedSurface(SurfaceDescriptor* aSurface) = 0;
  // we should make another interface or change this interface's name
  virtual bool AllocateUnsafe(size_t aSize,
                              ipc::SharedMemory::SharedMemoryType aType,
                              ipc::Shmem* aShmem) = 0;
protected:
  ~ISurfaceDeallocator() {}
};

} // namespace
} // namespace

#endif
