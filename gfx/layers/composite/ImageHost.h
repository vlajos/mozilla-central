/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_IMAGEHOST_H
#define MOZILLA_GFX_IMAGEHOST_H

#include "BufferHost.h"

namespace mozilla {
namespace layers {

class ImageHost : public BufferHost
{
public:
  virtual SharedImage UpdateImage(const TextureInfo& aTextureInfo,
                                  const SharedImage& aImage) = 0;

  virtual void SetPictureRect(const nsIntRect& aPictureRect) {}

  virtual TemporaryRef<TextureHost> GetTextureHost() { return nullptr; }


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

  virtual SharedImage UpdateImage(const TextureInfo& aTextureInfo,
                                  const SharedImage& aImage);

  virtual void AddTextureHost(const TextureInfo& aTextureInfo,
                              TextureHost* aTextureHost);

  virtual TemporaryRef<TextureHost> GetTextureHost() { return mTextureHost; }

  virtual void Composite(EffectChain& aEffectChain,
                         float aOpacity,
                         const gfx::Matrix4x4& aTransform,
                         const gfx::Point& aOffset,
                         const gfx::Filter& aFilter,
                         const gfx::Rect& aClipRect,
                         const nsIntRegion* aVisibleRegion = nullptr);

  virtual void SetDeAllocator(ISurfaceDeAllocator* aDeAllocator)
  {
    mTextureHost->SetDeAllocator(aDeAllocator);
  }

  virtual bool AddMaskEffect(EffectChain& aEffects,
                             const gfx::Matrix4x4& aTransform,
                             bool is3D) MOZ_OVERRIDE;

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

  virtual SharedImage UpdateImage(const TextureInfo& aTextureInfo,
                                  const SharedImage& aImage);

  virtual void Composite(EffectChain& aEffectChain,
                         float aOpacity,
                         const gfx::Matrix4x4& aTransform,
                         const gfx::Point& aOffset,
                         const gfx::Filter& aFilter,
                         const gfx::Rect& aClipRect,
                         const nsIntRegion* aVisibleRegion = nullptr);

  virtual void AddTextureHost(const TextureInfo& aTextureInfo, TextureHost* aTextureHost);

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

  virtual SharedImage UpdateImage(const TextureInfo& aTextureInfo,
                                  const SharedImage& aImage);

  virtual void Composite(EffectChain& aEffectChain,
                         float aOpacity,
                         const gfx::Matrix4x4& aTransform,
                         const gfx::Point& aOffset,
                         const gfx::Filter& aFilter,
                         const gfx::Rect& aClipRect,
                         const nsIntRegion* aVisibleRegion = nullptr);

  virtual void AddTextureHost(const TextureInfo& aTextureInfo, TextureHost* aTextureHost);

protected:
  void EnsureImageHost(BufferType aType);

  uint32_t mImageVersion;
  RefPtr<ImageHost> mImageHost;
  uint64_t mImageContainerID;
};

}
}
#endif
