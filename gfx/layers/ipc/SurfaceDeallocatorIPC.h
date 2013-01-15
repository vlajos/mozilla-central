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
    Self* self = static_cast<Self*>(this);

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
};

} // namespace
} // namespace

#endif
