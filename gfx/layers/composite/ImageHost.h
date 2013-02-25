/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_IMAGEHOST_H
#define MOZILLA_GFX_IMAGEHOST_H

#include "CompositableHost.h"
#include "LayerManagerComposite.h"

namespace mozilla {
namespace layers {

// abstract
class ImageHost : public CompositableHost
{
public:
  virtual SurfaceDescriptor UpdateImage(const TextureInfo& aTextureInfo,
                                        const SurfaceDescriptor& aImage) = 0;

  TextureHost* GetTextureHost() MOZ_OVERRIDE { return nullptr; }

protected:
  ImageHost(Compositor* aCompositor)
  : CompositableHost(aCompositor)
  {
    MOZ_COUNT_CTOR(ImageHost);
  }

  ~ImageHost()
  {
    MOZ_COUNT_DTOR(ImageHost);
  }
};

class ImageHostSingle : public ImageHost
{
public:
  ImageHostSingle(Compositor* aCompositor, CompositableType aType)
    : ImageHost(aCompositor)
    , mTextureHost(nullptr)
    , mType(aType)
  {}

  virtual CompositableType GetType() { return mType; }

  virtual SurfaceDescriptor UpdateImage(const TextureInfo& aTextureInfo,
                                        const SurfaceDescriptor& aImage);

  void AddTextureHost(TextureHost* aTextureHost) MOZ_OVERRIDE;

  TextureHost* GetTextureHost() MOZ_OVERRIDE { return mTextureHost; }

  virtual void Composite(EffectChain& aEffectChain,
                         float aOpacity,
                         const gfx::Matrix4x4& aTransform,
                         const gfx::Point& aOffset,
                         const gfx::Filter& aFilter,
                         const gfx::Rect& aClipRect,
                         const nsIntRegion* aVisibleRegion = nullptr,
                         TiledLayerProperties* aLayerProperties = nullptr);

  virtual void SetDeAllocator(ISurfaceAllocator* aDeAllocator)
  {
    mTextureHost->SetDeAllocator(aDeAllocator);
  }

  virtual void SetPictureRect(const nsIntRect& aPictureRect) MOZ_OVERRIDE
  {
    mPictureRect = aPictureRect;
  }

  virtual LayerRenderState GetRenderState() MOZ_OVERRIDE
  {
    return mTextureHost->GetRenderState();
  }

  virtual void CleanupResources() MOZ_OVERRIDE
  {
    if (mTextureHost) {
      mTextureHost->CleanupResources();
    }
  }

#ifdef MOZ_LAYERS_HAVE_LOG
  virtual void PrintInfo(nsACString& aTo, const char* aPrefix);
#endif

protected:
  RefPtr<TextureHost> mTextureHost;
  nsIntRect mPictureRect;
  CompositableType mType;
};

class ImageHostDirect : public ImageHostSingle
{
public:
  ImageHostDirect(Compositor* aCompositor, CompositableType aType)
    : ImageHostSingle(aCompositor, aType)
  {}

  virtual bool IsBuffered() MOZ_OVERRIDE { return true; }

};

}
}

#endif
