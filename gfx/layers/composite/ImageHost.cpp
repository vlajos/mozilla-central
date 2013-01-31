/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/ImageContainerParent.h"
#include "ipc/AutoOpenSurface.h"
#include "ImageHost.h"

#include "mozilla/layers/TextureFactoryIdentifier.h" // for TextureInfo
#include "mozilla/layers/Effects.h"

namespace mozilla {

using namespace gfx;

namespace layers {

void
ImageHostSingle::AddTextureHost(TextureHost* aHost) {
  mTextureHost = aHost;
}

SurfaceDescriptor
ImageHostSingle::UpdateImage(const TextureInfo& aTextureInfo,
                             const SurfaceDescriptor& aImage)
{
  if (!mTextureHost) {
    return null_t();
  }

  SurfaceDescriptor result;
  bool success;
  mTextureHost->Update(aImage, &result, &success);
  if (!success) {
    TextureInfo id = aTextureInfo;
    compositor()->FallbackTextureInfo(id);
    id.textureFlags = mTextureHost->GetFlags();
    mTextureHost = compositor()->CreateTextureHost(id.memoryType,
                                                   id.textureFlags,
                                                   SURFACEDESCRIPTOR_UNKNOWN,
                                                   mTextureHost->GetDeAllocator());
    mTextureHost->Update(aImage, &result, &success);
    if (!success) {
      mTextureHost = nullptr;
      NS_ASSERTION(result.type() == SurfaceDescriptor::Tnull_t, "fail should give null result");
    }
  }
  return result;
}

void
ImageHostSingle::Composite(EffectChain& aEffectChain,
                           float aOpacity,
                           const gfx::Matrix4x4& aTransform,
                           const gfx::Point& aOffset,
                           const gfx::Filter& aFilter,
                           const gfx::Rect& aClipRect,
                           const nsIntRegion* aVisibleRegion,
                           TiledLayerProperties* aLayerProperties)
{
  if (!mTextureHost) {
    return;
  }

  mTextureHost->UpdateAsyncTexture();
  RefPtr<Effect> effect;
  switch (mTextureHost->GetFormat()) {
  case FORMAT_B8G8R8A8:
    effect = new EffectBGRA(mTextureHost, true, aFilter);
    break;
  case FORMAT_B8G8R8X8:
    effect = new EffectBGRX(mTextureHost, true, aFilter);
    break;
  case FORMAT_R8G8B8X8:
    effect = new EffectRGBX(mTextureHost, true, aFilter);
    break;
  case FORMAT_R8G8B8A8:
    effect = new EffectRGBA(mTextureHost, true, aFilter);
    break;
  default:
    MOZ_NOT_REACHED("unhandled program type");
  }

  if (!mTextureHost->Lock()) {
    return;
  }
  
  aEffectChain.mEffects[effect->mType] = effect;

  TileIterator* it = mTextureHost->AsTextureSource()->AsTileIterator();
  if (it) {
    it->BeginTileIteration();
    do {
      nsIntRect tileRect = it->GetTileRect();
      gfx::Rect rect(tileRect.x, tileRect.y, tileRect.width, tileRect.height);
      gfx::Rect sourceRect(0, 0, tileRect.width, tileRect.height);
      compositor()->DrawQuad(rect, &sourceRect, nullptr, &aClipRect, aEffectChain,
                            aOpacity, aTransform, aOffset);
    } while (it->NextTile());
  } else {
    gfx::Rect sourceRect(mPictureRect.x, mPictureRect.y,
                         mPictureRect.width, mPictureRect.height);
    gfx::Rect rect(0, 0,
                   mTextureHost->AsTextureSource()->GetSize().width,
                   mTextureHost->AsTextureSource()->GetSize().height);
    compositor()->DrawQuad(rect, &sourceRect, nullptr, &aClipRect, aEffectChain,
                           aOpacity, aTransform, aOffset);
  }

  mTextureHost->Unlock();
}

/*
void
ImageHostSingle::AddTextureHost(const TextureInfo& aTextureInfo, TextureHost* aTextureHost)
{
  NS_ASSERTION((aTextureInfo.imageType == BUFFER_TEXTURE &&
                aTextureInfo.memoryType == TEXTURE_SHMEM) ||
               (aTextureInfo.imageType == BUFFER_SHARED &&
                aTextureInfo.memoryType == TEXTURE_SHARED) ||
               (aTextureInfo.imageType == BUFFER_DIRECT_EXTERNAL &&
                aTextureInfo.memoryType == TEXTURE_SHMEM),
               "BufferType mismatch.");
  mTextureHost = aTextureHost;
}
*/

void
ImageHostBridge::EnsureImageHost()
{
  if (!mImageHost ||
      mImageHost->GetType() != BUFFER_SINGLE) {
    RefPtr<CompositableHost> bufferHost = mManager->CreateCompositableHost(BUFFER_SINGLE);
    mImageHost = static_cast<ImageHost*>(bufferHost.get());

    TextureInfo id;
    id.imageType = BUFFER_SINGLE;
    id.memoryType = TEXTURE_SHMEM|TEXTURE_DIRECT|TEXTURE_EXTERNAL;
    id.textureFlags = NoFlags;
    RefPtr<TextureHost> textureHost = mManager->GetCompositor()->CreateTextureHost(id.memoryType,
                                                                                   id.textureFlags,
                                                                                   SURFACEDESCRIPTOR_UNKNOWN,
                                                                                   nullptr); // TODO[nical] needs a ISurfaceDeallocator
    mImageHost->AddTextureHost(textureHost);
  }
}

SurfaceDescriptor
ImageHostBridge::UpdateImage(const TextureInfo& aTextureInfo,
                             const SurfaceDescriptor& aImage)
{
  // The image data will be queried at render time
  return aImage;
}

void
ImageHostBridge::Composite(EffectChain& aEffectChain,
                           float aOpacity,
                           const gfx::Matrix4x4& aTransform,
                           const gfx::Point& aOffset,
                           const gfx::Filter& aFilter,
                           const gfx::Rect& aClipRect,
                           const nsIntRegion* aVisibleRegion,
                           TiledLayerProperties* aLayerProperties)
{
  ImageContainerParent::SetCompositorIDForImage(mImageContainerID,
                                                compositor()->GetCompositorID());
  uint32_t imgVersion = ImageContainerParent::GetSurfaceDescriptorVersion(mImageContainerID);
  SurfaceDescriptor* img;
  if ((!mImageHost ||
       imgVersion != mImageVersion) &&
      (img = ImageContainerParent::GetSurfaceDescriptor(mImageContainerID))) {
    EnsureImageHost();
    if (mImageHost) {
      TextureInfo textureId;
      textureId.imageType = mImageHost->GetType();
      textureId.memoryType = TEXTURE_SHMEM|TEXTURE_DIRECT|TEXTURE_EXTERNAL;
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
                          aVisibleRegion,
                          aLayerProperties);
  }
}

void
ImageHostBridge::AddTextureHost(TextureHost* aTextureHost)
{
  // nothing to do
}

}
}
