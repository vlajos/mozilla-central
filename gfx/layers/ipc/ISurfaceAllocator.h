/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_LAYERS_ISURFACEDEALLOCATOR
#define GFX_LAYERS_ISURFACEDEALLOCATOR

#include "mozilla/ipc/SharedMemory.h"
#include "mozilla/RefPtr.h"
#include "gfxPoint.h"
#include "gfxASurface.h"

class gfxSharedImageSurface;
class gfxASurface;

namespace base {
class Thread;
} // namespace

namespace mozilla {
namespace ipc {
class Shmem;
} // namespace
namespace layers {

class SurfaceDescriptor;

/**
 * SurfaceDeallocator interface
 */
class ISurfaceAllocator
{
public:
ISurfaceAllocator() {}
  virtual bool AllocShmem(size_t aSize,
                          ipc::SharedMemory::SharedMemoryType aType,
                          ipc::Shmem* aShmem) = 0;
  virtual bool AllocUnsafeShmem(size_t aSize,
                                   ipc::SharedMemory::SharedMemoryType aType,
                                   ipc::Shmem* aShmem) = 0;
  virtual void DeallocShmem(ipc::Shmem& aShmem) = 0;

  // AllocBuffer
  virtual bool AllocSharedImageSurface(const gfxIntSize& aSize,
                                       gfxASurface::gfxContentType aContent,
                                       gfxSharedImageSurface** aBuffer);
  virtual bool AllocSurfaceDescriptor(const gfxIntSize& aSize,
                                      gfxASurface::gfxContentType aContent,
                                      SurfaceDescriptor* aBuffer);

  // AllocBufferWithCaps<<
  virtual bool AllocSurfaceDescriptorWithCaps(const gfxIntSize& aSize,
                                              gfxASurface::gfxContentType aContent,
                                              uint32_t aCaps,
                                              SurfaceDescriptor* aBuffer);

  virtual void DestroySharedSurface(gfxSharedImageSurface* aSurface);
  virtual void DestroySharedSurface(SurfaceDescriptor* aSurface);

protected:
  static bool PlatformDestroySharedSurface(SurfaceDescriptor* aSurface);
  virtual bool PlatformAllocSurfaceDescriptor(const gfxIntSize& aSize,
                                              gfxASurface::gfxContentType aContent,
                                              uint32_t aCaps,
                                              SurfaceDescriptor* aBuffer);

  ~ISurfaceAllocator() {}
};


/**
 * Useful to use a ISurfaceAllocator asynchronously from another thread
 */
class SurfaceAllocatorProxy : public ISurfaceAllocator,
                              public RefCounted<SurfaceAllocatorProxy>
{
public:
  SurfaceAllocatorProxy(base::Thread* aThread, ISurfaceAllocator* aAllocator)
  : mThread(aThread), mSurfaceAllocator(aAllocator)
  {}

  base::Thread* GetThread() const
  {
    return mThread;
  }

  /**
   * If you are using a proxy you are probably most likely in the allocator's
   * thread so do not call methods of the returned allocator.
   * This method is there for the rare cases where the return reference must be
   * passed in a messahe to thre allocator's tread.
   */
  ISurfaceAllocator* GetAllocator() const
  {
    return mSurfaceAllocator;
  }

private:
  base::Thread* mThread;
  ISurfaceAllocator* mSurfaceAllocator;
};

} // namespace
} // namespace

#endif
