
#ifndef GFX_LAYERS_ISURFACEDEALLOCATOR
#define GFX_LAYERS_ISURFACEDEALLOCATOR

class gfxSharedImageSurface;

namespace mozilla {
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
protected:
  ~ISurfaceDeallocator() {}
};

} // namespace
} // namespace

#endif
