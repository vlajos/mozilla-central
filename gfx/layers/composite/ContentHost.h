/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_CONTENTHOST_H
#define GFX_CONTENTHOST_H

#include "ThebesLayerBuffer.h"
#include "CompositableHost.h"

namespace mozilla {
namespace layers {

class ThebesBuffer;
class OptionalThebesBuffer;

class CompositingThebesLayerBuffer
{
  NS_INLINE_DECL_REFCOUNTING(CompositingThebesLayerBuffer)
public:
  typedef ThebesLayerBuffer::ContentType ContentType;
  typedef ThebesLayerBuffer::PaintState PaintState;

  CompositingThebesLayerBuffer(Compositor* aCompositor)
    : mPaintWillResample(false)
    , mInitialised(true)
    , mCompositor(aCompositor)
  {}
  virtual ~CompositingThebesLayerBuffer() {}

  virtual PaintState BeginPaint(ContentType aContentType,
                                uint32_t aFlags) = 0;

  void Composite(EffectChain& aEffectChain,
                 float aOpacity,
                 const gfx::Matrix4x4& aTransform,
                 const gfx::Point& aOffset,
                 const gfx::Filter& aFilter,
                 const gfx::Rect& aClipRect,
                 const nsIntRegion* aVisibleRegion = nullptr);

  void SetPaintWillResample(bool aResample) { mPaintWillResample = aResample; }

protected:
  virtual nsIntPoint GetOriginOffset() = 0;

  bool PaintWillResample() { return mPaintWillResample; }

  bool mPaintWillResample;
  bool mInitialised;
  RefPtr<Compositor> mCompositor;
  RefPtr<TextureHost> mTextureHost;
  RefPtr<TextureHost> mTextureHostOnWhite;
};

class AContentHost : public CompositableHost
{
public:
  /**
   * Update the content host.
   * aTextureInfo identifies the texture host which should be updated.
   * aNewBack is the new data
   * aUpdated is the region which should be updated
   * aNewfront may point to the old data in this content host after the call
   * aNewBackResult may point to the updated data in this content host
   * aNewValidRegionFront is the valid region in aNewFront
   * aUpdatedRegionBack is the region in aNewBackResult which has been updated
   */
  virtual void UpdateThebes(const ThebesBuffer& aNewBack,
                            const nsIntRegion& aUpdated,
                            OptionalThebesBuffer* aNewFront,
                            const nsIntRegion& aOldValidRegionFront,
                            const nsIntRegion& aOldValidRegionBack,
                            OptionalThebesBuffer* aNewBackResult,
                            nsIntRegion* aNewValidRegionFront,
                            nsIntRegion* aUpdatedRegionBack) = 0;

#ifdef MOZ_DUMP_PAINTING
  virtual already_AddRefed<gfxImageSurface> Dump() { return nullptr; }
#endif
};

class ContentHost : public AContentHost
                  , protected CompositingThebesLayerBuffer
{
public:

  ContentHost(Compositor* aCompositor)
    : CompositingThebesLayerBuffer(aCompositor)
  {
    mInitialised = false;
  }

  void Release() { AContentHost::Release(); }
  void AddRef() { AContentHost::AddRef(); }

  // AContentHost implementation
  virtual void Composite(EffectChain& aEffectChain,
                         float aOpacity,
                         const gfx::Matrix4x4& aTransform,
                         const gfx::Point& aOffset,
                         const gfx::Filter& aFilter,
                         const gfx::Rect& aClipRect,
                         const nsIntRegion* aVisibleRegion = nullptr)
  {
    CompositingThebesLayerBuffer::Composite(aEffectChain,
                                            aOpacity,
                                            aTransform,
                                            aOffset,
                                            aFilter,
                                            aClipRect,
                                            aVisibleRegion);
  }

  // CompositingThebesLayerBuffer implementation
  virtual PaintState BeginPaint(ContentType aContentType, uint32_t) {
    NS_RUNTIMEABORT("can't BeginPaint for a shadow layer");
    return PaintState();
  }

  virtual void SetDeAllocator(ISurfaceDeallocator* aDeAllocator)
  {
    mTextureHost->SetDeAllocator(aDeAllocator);
  }

  virtual LayerRenderState GetRenderState() MOZ_OVERRIDE
  {
    uint32_t flags = (mBufferRotation != nsIntPoint()) ?
                     LAYER_RENDER_STATE_BUFFER_ROTATION : 0;
    //TODO[nrc] I think we need the SurfaceDescriptor from the texture host
    //return LayerRenderState(&mBufferDescriptor, flags);
    return LayerRenderState();
  }

#ifdef MOZ_DUMP_PAINTING
  virtual already_AddRefed<gfxImageSurface> Dump()
  {
    return mTextureHost->Dump();
  }
#endif

  void AddTextureHost(TextureHost* aTextureHost) MOZ_OVERRIDE;
  TextureHost* GetTextureHost() MOZ_OVERRIDE;

protected:
  virtual nsIntPoint GetOriginOffset() {
    return mBufferRect.TopLeft() - mBufferRotation;
  }

  nsIntRect mBufferRect;
  nsIntPoint mBufferRotation;
};

// We can directly texture the drawn surface.  Use that as our new
// front buffer, and return our previous directly-textured surface
// to the renderer.
class ContentHostDirect : public ContentHost
{
public:
  ContentHostDirect(Compositor* aCompositor)
    : ContentHost(aCompositor)
  {}

  virtual BufferType GetType() { return BUFFER_CONTENT_DIRECT; }

  virtual void UpdateThebes(const ThebesBuffer& aNewBack,
                            const nsIntRegion& aUpdated,
                            OptionalThebesBuffer* aNewFront,
                            const nsIntRegion& aOldValidRegionFront,
                            const nsIntRegion& aOldValidRegionBack,
                            OptionalThebesBuffer* aNewBackResult,
                            nsIntRegion* aNewValidRegionFront,
                            nsIntRegion* aUpdatedRegionBack);
};

// We're using resources owned by our texture as the front buffer.
// Upload the changed region and then return the surface back to
// the renderer.
class ContentHostTexture : public ContentHost
{
public:
  ContentHostTexture(Compositor* aCompositor)
    : ContentHost(aCompositor)
  {}

  virtual BufferType GetType() { return BUFFER_CONTENT; }

  virtual void UpdateThebes(const ThebesBuffer& aNewBack,
                            const nsIntRegion& aUpdated,
                            OptionalThebesBuffer* aNewFront,
                            const nsIntRegion& aOldValidRegionFront,
                            const nsIntRegion& aOldValidRegionBack,
                            OptionalThebesBuffer* aNewBackResult,
                            nsIntRegion* aNewValidRegionFront,
                            nsIntRegion* aUpdatedRegionBack);
};

}
}

#endif