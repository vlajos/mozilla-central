/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_BUFFERHOST_H
#define MOZILLA_GFX_BUFFERHOST_H

#include "mozilla/layers/Compositor.h"
#include "mozilla/layers/PCompositableParent.h"
#include "mozilla/layers/ISurfaceDeallocator.h"
#include "ThebesLayerBuffer.h"
#include "BasicTiledThebesLayer.h" // for BasicTiledLayerBuffer

namespace mozilla {
namespace layers {

// Some properties of a Layer required for tiling
struct TiledLayerProperties
{
  nsIntRegion mVisibleRegion;
  nsIntRegion mValidRegion;
  gfxRect mDisplayPort;
  gfxSize mEffectiveResolution;
  gfxRect mCompositionBounds;
  bool mRetainTiles;
};

class Layer;
class TextureHost;
class SurfaceDescriptor;

class CompositableHost : public RefCounted<CompositableHost>
{
public:
  CompositableHost(Compositor* aCompositor = nullptr)
  : mCompositor(aCompositor), mLayer(nullptr)
  {
    MOZ_COUNT_CTOR(CompositableHost);
  }

  virtual ~CompositableHost()
  {
    MOZ_COUNT_DTOR(CompositableHost);
  }

  static TemporaryRef<CompositableHost> Create(CompositableType aType,
                                               Compositor* aCompositor);

  virtual CompositableType GetType() = 0;

  virtual void CleanupResources() {}

  virtual void SetCompositor(Compositor* aCompositor)
  {
    if (mCompositor == aCompositor) {
      return;
    }
    CleanupResources();
    mCompositor = aCompositor;
  }

  // composite the contents of this buffer host to the compositor's surface
  virtual void Composite(EffectChain& aEffectChain,
                         float aOpacity,
                         const gfx::Matrix4x4& aTransform,
                         const gfx::Point& aOffset,
                         const gfx::Filter& aFilter,
                         const gfx::Rect& aClipRect,
                         const nsIntRegion* aVisibleRegion = nullptr,
                         TiledLayerProperties* aLayerProperties = nullptr) = 0;

  virtual void AddTextureHost(TextureHost* aTextureHost) = 0;

  void Update(const SurfaceDescriptor& aImage,
              SurfaceDescriptor* aResult = nullptr,
              bool* aIsInitialised = nullptr,
              bool* aNeedsReset = nullptr);
  
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
                            const nsIntRegion& aOldValidRegionBack,
                            OptionalThebesBuffer* aNewBackResult,
                            nsIntRegion* aNewValidRegionFront,
                            nsIntRegion* aUpdatedRegionBack)
  {
    NS_RUNTIMEABORT("should be implemented or not used");
  }

  virtual TextureHost* GetTextureHost() { return nullptr; }

  virtual void SetDeAllocator(ISurfaceDeallocator* aDeAllocator) {}

  virtual LayerRenderState GetRenderState() = 0;

  virtual void SetPictureRect(const nsIntRect& aPictureRect) {
    NS_RUNTIMEABORT("If this code is reached it means this method should habe been overridden");
  }

  virtual bool IsBuffered() { return false; }

  /**
   * Adds a mask effect using this texture as the mask, if possible.
   * \return true if the effect was added, false otherwise.
   */
  bool AddMaskEffect(EffectChain& aEffects,
                     const gfx::Matrix4x4& aTransform,
                     bool aIs3D = false);

  Compositor* GetCompositor() const
  {
    return mCompositor;
  }

#ifdef MOZ_LAYERS_HAVE_LOG
  virtual void PrintInfo(nsACString& aTo, const char* aPrefix) { }
#endif

  // temporary hack, will be removed when layer won't need to be notfied after texture update
  Layer* GetLayer() const { return mLayer; }
  void SetLayer(Layer* aLayer) { mLayer = aLayer; }
protected:
  Compositor* mCompositor;
  Layer* mLayer;
};

class CompositableParentManager;

class CompositableParent : public PCompositableParent
{
public:
  CompositableParent(CompositableParentManager* aMgr, CompositableType aType, uint64_t aID = 0);
  ~CompositableParent();
  PTextureParent* AllocPTexture(const TextureInfo& aInfo) MOZ_OVERRIDE;
  bool DeallocPTexture(PTextureParent* aActor) MOZ_OVERRIDE;

  CompositableHost* GetCompositableHost() const {
    return mHost;
  }
  void SetCompositableHost(CompositableHost* aHost) {
    mHost = aHost;
  }

  CompositableType GetType() const
  {
    return mType;
  }

  CompositableParentManager* GetCompositableManager() const
  {
    return mManager;
  }

  void SetCompositorID(uint64_t aCompositorID)
  {
    mCompositorID = aCompositorID;
  }

  uint64_t GetCompositorID() const
  {
    return mCompositorID;
  }
private:
  RefPtr<CompositableHost> mHost;
  CompositableParentManager* mManager;
  CompositableType mType;
  uint64_t mID;
  uint64_t mCompositorID;
};


/**
 * Global CompositableMap, to use in the compositor thread only.
 */
namespace CompositableMap {
  void Create();
  void Destroy();
  CompositableParent* Get(uint64_t aID);
  void Set(uint64_t aID, CompositableParent* aParent);
  void Erase(uint64_t aID);
  void Clear();
} // CompositableMap


} // namespace
} // namespace

#endif
