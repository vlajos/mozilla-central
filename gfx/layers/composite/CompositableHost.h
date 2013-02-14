/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_BUFFERHOST_H
#define MOZILLA_GFX_BUFFERHOST_H

#include "mozilla/layers/Compositor.h"
#include "mozilla/layers/PCompositableParent.h"
#include "mozilla/layers/ISurfaceDeallocator.h"

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
  : mCompositor(aCompositor)
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

  virtual TextureHost* GetTextureHost() { return nullptr; }

  virtual void SetDeAllocator(ISurfaceDeallocator* aDeAllocator) {}

  virtual LayerRenderState GetRenderState() = 0;

  virtual void SetPictureRect(const nsIntRect& aPictureRect) {
    NS_RUNTIMEABORT("If this code is reached it means this method should habe been overridden");
  }

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

  // temporary hack, will be removed when layer won't need to be notfied after texture update
  Layer* GetLayer() const { return mLayer; }
  void SetLayer(Layer* aLayer) { mLayer = aLayer; }
protected:
  Layer* mLayer;
  Compositor* mCompositor;
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

  Compositor* GetCompositor() const;

  CompositableParentManager* GetCompositableManager() const
  {
    return mManager;
  }
private:
  RefPtr<CompositableHost> mHost;
  CompositableParentManager* mManager;
  CompositableType mType;
  uint64_t mID;
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
