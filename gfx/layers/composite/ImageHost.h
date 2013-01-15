/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_IMAGEHOST_H
#define MOZILLA_GFX_IMAGEHOST_H

#include "CompositableHost.h"
#include "mozilla/layers/ImageContainerParent.h"

namespace mozilla {
namespace layers {

// abstract
class ImageHost : public CompositableHost
{
public:
  virtual SurfaceDescriptor UpdateImage(const TextureInfo& aTextureInfo,
                                        const SurfaceDescriptor& aImage) = 0;

  virtual void SetPictureRect(const nsIntRect& aPictureRect) {}

  TextureHost* GetTextureHost() MOZ_OVERRIDE { return nullptr; }

protected:
  ImageHost(Compositor* aCompositor)
    : mCompositor(aCompositor)
  {
  }

  RefPtr<Compositor> mCompositor;
};

class ImageHostSingle : public ImageHost
{
public:
  ImageHostSingle(Compositor* aCompositor, BufferType aType)
    : ImageHost(aCompositor)
    , mTextureHost(nullptr)
    , mType(aType)
  {}

  virtual BufferType GetType() { return mType; }

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
                         const nsIntRegion* aVisibleRegion = nullptr);

  virtual void SetDeAllocator(ISurfaceDeallocator* aDeAllocator)
  {
    mTextureHost->SetDeAllocator(aDeAllocator);
  }

  virtual LayerRenderState GetRenderState() MOZ_OVERRIDE
  {
    return mTextureHost->GetRenderState();
  }

protected:
  RefPtr<TextureHost> mTextureHost;
  BufferType mType;
};

// a YCbCr buffer which uses a single texture host
class YCbCrImageHost : public ImageHost
{
public:
  YCbCrImageHost(Compositor* aCompositor)
    : ImageHost(aCompositor)
  {}

  virtual BufferType GetType() { return BUFFER_YCBCR; }

  virtual SurfaceDescriptor UpdateImage(const TextureInfo& aTextureInfo,
                                        const SurfaceDescriptor& aImage);

  virtual void Composite(EffectChain& aEffectChain,
                         float aOpacity,
                         const gfx::Matrix4x4& aTransform,
                         const gfx::Point& aOffset,
                         const gfx::Filter& aFilter,
                         const gfx::Rect& aClipRect,
                         const nsIntRegion* aVisibleRegion = nullptr);

  void AddTextureHost(TextureHost* aTextureHost) MOZ_OVERRIDE;
  TextureHost* GetTextureHost() MOZ_OVERRIDE { return mTextureHost; }

  virtual LayerRenderState GetRenderState() MOZ_OVERRIDE
  {
    return LayerRenderState();
  }

protected:
  RefPtr<TextureHost> mTextureHost;
  nsIntRect mPictureRect;
};

class ImageHostBridge : public ImageHost
{
public:
  ImageHostBridge(Compositor* aCompositor)
    : ImageHost(aCompositor)
    , mImageContainerID(0)
    , mImageVersion(0)
  {}

  virtual BufferType GetType() { return BUFFER_BRIDGE; }

  virtual SurfaceDescriptor UpdateImage(const TextureInfo& aTextureInfo,
                                        const SurfaceDescriptor& aImage);

  virtual void Composite(EffectChain& aEffectChain,
                         float aOpacity,
                         const gfx::Matrix4x4& aTransform,
                         const gfx::Point& aOffset,
                         const gfx::Filter& aFilter,
                         const gfx::Rect& aClipRect,
                         const nsIntRegion* aVisibleRegion = nullptr);

  virtual void AddTextureHost(TextureHost* aTextureHost);

  virtual LayerRenderState GetRenderState() MOZ_OVERRIDE
  {
    // Update the associated compositor ID in case Composer2D succeeds,
    // because we won't enter RenderLayer() if so ...
    ImageContainerParent::SetCompositorIDForImage(
      mImageContainerID, mCompositor->GetCompositorID());
    // ... but do *not* try to update the local image version.  We need
    // to retain that information in case we fall back on GL, so that we
    // can upload / attach buffers properly.

    SurfaceDescriptor* img = ImageContainerParent::GetSurfaceDescriptor(mImageContainerID);
    if (img) {
      return LayerRenderState(img);
    }
    return LayerRenderState();
  }

protected:
  void EnsureImageHost(BufferType aType);

  RefPtr<ImageHost> mImageHost;
  uint64_t mImageContainerID;
  uint32_t mImageVersion;
};

}
}
#endif
