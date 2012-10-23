/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/ImageContainerParent.h"
#include "ipc/AutoOpenSurface.h"
#include "ImageHost.h"

namespace mozilla {
namespace layers {

ImageHostTexture::ImageHostTexture(Compositor* aCompositor)
  : ImageHost(aCompositor)
  , mTextureHost(nullptr)
{
}

const SharedImage*
ImageHostTexture::UpdateImage(const TextureIdentifier& aTextureIdentifier,
                              const SharedImage& aImage)
{
  return mTextureHost->Update(aImage);
}

void
ImageHostTexture::Composite(EffectChain& aEffectChain,
                            float aOpacity,
                            const gfx::Matrix4x4& aTransform,
                            const gfx::Point& aOffset,
                            const gfx::Filter& aFilter,
                            const gfx::Rect& aClipRect,
                            const nsIntRegion* aVisibleRegion)
{
  if (Effect* effect = mTextureHost->Lock(aFilter)) {
    aEffectChain.mEffects[effect->mType] = effect;
  } else {
    return;
  }

  TileIterator* it = mTextureHost->GetAsTileIterator();
  NS_ASSERTION(it, "Texture host should be iterable as tiles");

  it->BeginTileIteration();
  do {
    nsIntRect tileRect = it->GetTileRect();
    gfx::Rect rect(tileRect.x, tileRect.y, tileRect.width, tileRect.height);
    gfx::Rect sourceRect(0, 0, tileRect.width, tileRect.height);
    mCompositor->DrawQuad(rect, &sourceRect, nullptr, &aClipRect, aEffectChain,
                          aOpacity, aTransform, aOffset);
  } while (it->NextTile());

  mTextureHost->Unlock();
}

void
ImageHostTexture::AddTextureHost(const TextureIdentifier& aTextureIdentifier, TextureHost* aTextureHost)
{
  NS_ASSERTION(aTextureIdentifier.mBufferType == BUFFER_TEXTURE &&
               aTextureIdentifier.mTextureType == TEXTURE_SHMEM,
               "BufferType mismatch.");
  mTextureHost = aTextureHost;
}

ImageHostShared::ImageHostShared(Compositor* aCompositor)
  : ImageHost(aCompositor)
  , mTextureHost(nullptr)
{
}

const SharedImage*
ImageHostShared::UpdateImage(const TextureIdentifier& aTextureIdentifier,
                             const SharedImage& aImage)
{
  return mTextureHost->Update(aImage);
}

void
ImageHostShared::Composite(EffectChain& aEffectChain,
                           float aOpacity,
                           const gfx::Matrix4x4& aTransform,
                           const gfx::Point& aOffset,
                           const gfx::Filter& aFilter,
                           const gfx::Rect& aClipRect,
                           const nsIntRegion* aVisibleRegion)
{
  if (Effect* effect = mTextureHost->Lock(aFilter)) {
    aEffectChain.mEffects[effect->mType] = effect;
  } else {
    return;
  }

  gfx::Rect rect(0, 0, mTextureHost->GetSize().width, mTextureHost->GetSize().height);
  mCompositor->DrawQuad(rect, nullptr, nullptr, &aClipRect, aEffectChain,
                        aOpacity, aTransform, aOffset);

  mTextureHost->Unlock();
}

void
ImageHostShared::AddTextureHost(const TextureIdentifier& aTextureIdentifier, TextureHost* aTextureHost)
{
  NS_ASSERTION((aTextureIdentifier.mBufferType == BUFFER_SHARED &&
                aTextureIdentifier.mTextureType == TEXTURE_SHARED) ||
               (aTextureIdentifier.mBufferType == BUFFER_DIRECT_EXTERNAL &&
                aTextureIdentifier.mTextureType == TEXTURE_SHMEM),
               "BufferType mismatch.");
  mTextureHost = aTextureHost;
}


const SharedImage*
YUVImageHost::UpdateImage(const TextureIdentifier& aTextureIdentifier,
                          const SharedImage& aImage)
{
  NS_ASSERTION(aTextureIdentifier.mBufferType == BUFFER_YUV, "BufferType mismatch.");
  
  if (aImage.type() == SharedImage::TYUVImage) {
    // update all channels at once
    const YUVImage& yuv = aImage.get_YUVImage();

    mTextures[0]->Update(SurfaceDescriptor(yuv.Ydata()));
    mTextures[1]->Update(SurfaceDescriptor(yuv.Udata()));
    mTextures[2]->Update(SurfaceDescriptor(yuv.Vdata()));

    return &aImage;
  }

  // update a single channel
  mTextures[aTextureIdentifier.mDescriptor]->Update(aImage);

  return &aImage;
}

void
YUVImageHost::Composite(EffectChain& aEffectChain,
                        float aOpacity,
                        const gfx::Matrix4x4& aTransform,
                        const gfx::Point& aOffset,
                        const gfx::Filter& aFilter,
                        const gfx::Rect& aClipRect,
                        const nsIntRegion* aVisibleRegion /* = nullptr */)
{
  mTextures[0]->Lock(aFilter);
  mTextures[1]->Lock(aFilter);
  mTextures[2]->Lock(aFilter);

  EffectYCbCr* effect = new EffectYCbCr(mTextures[0]->GetAsTextureSource(),
                                        mTextures[1]->GetAsTextureSource(),
                                        mTextures[2]->GetAsTextureSource(),
                                        aFilter);
  aEffectChain.mEffects[EFFECT_YCBCR] = effect;
  gfx::Rect rect(0, 0, mPictureRect.width, mPictureRect.height);
  gfx::Rect sourceRect(mPictureRect.x, mPictureRect.y,
                       mPictureRect.width, mPictureRect.height);
  mCompositor->DrawQuad(rect, &sourceRect, nullptr, &aClipRect,
                        aEffectChain, aOpacity, aTransform, aOffset);

  mTextures[0]->Unlock();
  mTextures[1]->Unlock();
  mTextures[2]->Unlock();
}

void
YUVImageHost::AddTextureHost(const TextureIdentifier& aTextureIdentifier, TextureHost* aTextureHost)
{
  NS_ASSERTION(aTextureIdentifier.mBufferType == BUFFER_YUV, "BufferType mismatch.");
  mTextures[aTextureIdentifier.mDescriptor] = aTextureHost;
}

const SharedImage*
YCbCrImageHost::UpdateImage(const TextureIdentifier& aTextureIdentifier,
                            const SharedImage& aImage)
{
  NS_ASSERTION(aTextureIdentifier.mBufferType == BUFFER_YCBCR, "BufferType mismatch.");

  mPictureRect = aImage.get_YCbCrImage().picture();;

  return mTextureHost->Update(aImage);
}

void
YCbCrImageHost::Composite(EffectChain& aEffectChain,
                          float aOpacity,
                          const gfx::Matrix4x4& aTransform,
                          const gfx::Point& aOffset,
                          const gfx::Filter& aFilter,
                          const gfx::Rect& aClipRect,
                          const nsIntRegion* aVisibleRegion /* = nullptr */)
{
  if (Effect* effect = mTextureHost->Lock(aFilter)) {
    NS_ASSERTION(effect->mType == EFFECT_YCBCR, "expected YCbCr effect");
    aEffectChain.mEffects[effect->mType] = effect;
  } else {
    return;
  }

  gfx::Rect rect(0, 0, mPictureRect.width, mPictureRect.height);
  gfx::Rect sourceRect(mPictureRect.x, mPictureRect.y, mPictureRect.width, mPictureRect.height);
  mCompositor->DrawQuad(rect, &sourceRect, nullptr, &aClipRect, aEffectChain, aOpacity, aTransform, aOffset);

  mTextureHost->Unlock();
}

void
YCbCrImageHost::AddTextureHost(const TextureIdentifier& aTextureIdentifier,
                               TextureHost* aTextureHost)
{
  NS_ASSERTION(aTextureIdentifier.mBufferType == BUFFER_YCBCR, "BufferType mismatch.");
  mTextureHost = aTextureHost;
}

void
ImageHostBridge::EnsureImageHost(BufferType aType)
{
  if (!mImageHost ||
      mImageHost->GetType() != aType) {
    RefPtr<BufferHost> bufferHost = mCompositor->CreateBufferHost(aType);
    mImageHost = static_cast<ImageHost*>(bufferHost.get());

    if (aType == BUFFER_YUV) {
      TextureIdentifier id;
      id.mBufferType = BUFFER_YUV;
      id.mTextureType = TEXTURE_SHMEM;
      RefPtr<TextureHost> textureHost;
      for (uint32_t i = 0; i < 3; ++i) {
        id.mDescriptor = i;
        textureHost = mCompositor->CreateTextureHost(id, NoFlags);
        mImageHost->AddTextureHost(id, textureHost);
      }
    } else {
      TextureIdentifier id;
      id.mBufferType = mImageHost->GetType();
      id.mTextureType = TEXTURE_SHMEM;
      RefPtr<TextureHost> textureHost = mCompositor->CreateTextureHost(id, NoFlags);
      mImageHost->AddTextureHost(id, textureHost);
    }
  }
}

const SharedImage*
ImageHostBridge::UpdateImage(const TextureIdentifier& aTextureIdentifier,
                             const SharedImage& aImage)
{
  // The image data will be queried at render time
  uint64_t newID = aTextureIdentifier.mDescriptor;
  if (newID != mImageContainerID) {
    mImageContainerID = newID;
    mImageVersion = 0;
  }

  return &aImage;
}

BufferType
BufferTypeForImageBridgeType(SharedImage::Type aType)
{
  switch (aType) {
  case SharedImage::TYUVImage:
    return BUFFER_YUV;
  case SharedImage::TYCbCrImage:
    return BUFFER_YCBCR;
  case SharedImage::TSurfaceDescriptor:
    return BUFFER_DIRECT_EXTERNAL;
  }

  return BUFFER_UNKNOWN;
}

void
ImageHostBridge::Composite(EffectChain& aEffectChain,
                           float aOpacity,
                           const gfx::Matrix4x4& aTransform,
                           const gfx::Point& aOffset,
                           const gfx::Filter& aFilter,
                           const gfx::Rect& aClipRect,
                           const nsIntRegion* aVisibleRegion /* = nullptr */)
{
  ImageContainerParent::SetCompositorIDForImage(mImageContainerID,
                                                mCompositor->GetCompositorID());
  uint32_t imgVersion = ImageContainerParent::GetSharedImageVersion(mImageContainerID);
  SharedImage* img;
  if ((!mImageHost ||
       imgVersion != mImageVersion) &&
      (img = ImageContainerParent::GetSharedImage(mImageContainerID))) {
    EnsureImageHost(BufferTypeForImageBridgeType(img->type()));
    if (mImageHost) {
      TextureIdentifier textureId;
      textureId.mBufferType = mImageHost->GetType();
      textureId.mTextureType = TEXTURE_SHMEM;
      mImageHost->UpdateImage(textureId, *img);
  
      mImageVersion = imgVersion;
    }
  }

  if (mImageHost) {
    mImageHost->Composite(aEffectChain,
                          aOpacity,
                          aTransform,
                          aOffset,
                          aFilter,
                          aClipRect,
                          aVisibleRegion);
  }
}

void
ImageHostBridge::AddTextureHost(const TextureIdentifier& aTextureIdentifier, TextureHost* aTextureHost)
{
  NS_ASSERTION(aTextureIdentifier.mBufferType == BUFFER_BRIDGE, "BufferType mismatch.");
  // nothing to do
}

}
}
