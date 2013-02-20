#ifndef GFX_LAYERS_SURFACEDEALLOCATORIPC_H
#define GFX_LAYERS_SURFACEDEALLOCATORIPC_H

#include "mozilla/layers/ISurfaceDeallocator.h"
#include "mozilla/layers/LayersSurfaces.h"

namespace mozilla {
namespace layers {

template<typename Self>
class SurfaceDeallocator : public ISurfaceDeallocator {
public:
  void DestroySharedSurface(gfxSharedImageSurface* aSurface) MOZ_OVERRIDE {
    // FIXME [bjacob] this function is empty???
    //Self* self = static_cast<Self*>(this);
    NS_RUNTIMEABORT("TODO: DestroySharedSurface(gfxSharedImageSurface*) not implemented"); 
  }
  void DestroySharedSurface(SurfaceDescriptor* aSurface) MOZ_OVERRIDE {
    Self* self = static_cast<Self*>(this);
    switch (aSurface->type()) {
      case SurfaceDescriptor::TShmem:
        self->DeallocShmem(aSurface->get_Shmem());
        *aSurface = SurfaceDescriptor();
        return;
      case SurfaceDescriptor::TYCbCrImage:
        self->DeallocShmem(aSurface->get_YCbCrImage().data());
        *aSurface = SurfaceDescriptor();
        return;
    default:
      NS_RUNTIMEABORT("unexpected SurfaceDescriptor type!");
      return;
    }
  }
/*
  bool AllocateUnsafe(size_t aSize,
                      ipc::SharedMemory::SharedMemoryType aType,
                      ipc::Shmem* aShmem) MOZ_OVERRIDE {
    Self* self = static_cast<Self*>(this);
    return self->AllocUnsafeShmem(aSize, aType, aShmem);
  }
*/
};

} // namespace
} // namespace

#endif
