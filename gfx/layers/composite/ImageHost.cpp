/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ipc/AutoOpenSurface.h"
#include "ImageHost.h"

#include "mozilla/layers/TextureFactoryIdentifier.h" // for TextureInfo
#include "mozilla/layers/Effects.h"
#include "LayersLogging.h"
#include "nsPrintfCString.h"

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
    id.textureFlags = mTextureHost->GetFlags();
    GetCompositor()->FallbackTextureInfo(id);
    mTextureHost = GetCompositor()->CreateTextureHost(aImage.type(),
                                                      id.textureHostFlags,
                                                      id.textureFlags,
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
  if (!mTextureHost || !mTextureHost->IsValid()) {
    NS_WARNING("Can't composite an invalid or null TextureHost");
    return;
  }

  RefPtr<TexturedEffect> effect =
    CreateTexturedEffect(mTextureHost, aFilter);

  if (!mTextureHost->Lock()) {
    MOZ_ASSERT(false, "failed to lock texture host");
    return;
  }
  
  aEffectChain.mPrimaryEffect = effect;

  TileIterator* it = mTextureHost->AsTextureSource()->AsTileIterator();
  if (it) {
    it->BeginTileIteration();
    // XXX - TODO - Implement NeedsYFlip?
    do {
      nsIntRect tileRect = it->GetTileRect();
      gfx::Rect rect(tileRect.x, tileRect.y, tileRect.width, tileRect.height);
      GetCompositor()->DrawQuad(rect, &aClipRect, aEffectChain,
                                aOpacity, aTransform, aOffset);
    } while (it->NextTile());
  } else {
    gfx::Rect rect(0, 0,
                   mTextureHost->AsTextureSource()->GetSize().width,
                   mTextureHost->AsTextureSource()->GetSize().height);
    if (mPictureRect.IsEqualInterior(nsIntRect())) {
      effect->mTextureCoords = Rect(0, 0, 1, 1);
    } else {
      effect->mTextureCoords = Rect(Float(mPictureRect.x) / rect.width,
                                    Float(mPictureRect.y) / rect.height,
                                    Float(mPictureRect.width) / rect.width,
                                    Float(mPictureRect.height) / rect.height);
    }

    if (mTextureHost->GetFlags() & NeedsYFlip) {
      effect->mTextureCoords.y = effect->mTextureCoords.YMost();
      effect->mTextureCoords.height = -effect->mTextureCoords.height;
    }

    GetCompositor()->DrawQuad(rect, &aClipRect, aEffectChain,
                              aOpacity, aTransform, aOffset);
  }

  mTextureHost->Unlock();
}

#ifdef MOZ_LAYERS_HAVE_LOG
void
ImageHostSingle::PrintInfo(nsACString& aTo, const char* aPrefix)
{
  aTo += aPrefix;
  aTo += nsPrintfCString("ImageHostSingle (0x%p)", this);

  AppendToString(aTo, mPictureRect, " [picture-rect=", "]");

  if (mTextureHost) {
    nsAutoCString pfx(aPrefix);
    pfx += "  ";
    aTo += "\n";
    mTextureHost->PrintInfo(aTo, pfx.get());
  }
}
#endif

/*
void
ImageHostSingle::AddTextureHost(const TextureInfo& aTextureInfo, TextureHost* aTextureHost)
{
  NS_ASSERTION((aTextureInfo.compositableType == BUFFER_TEXTURE &&
                aTextureInfo.memoryType == TEXTURE_SHMEM) ||
               (aTextureInfo.compositableType == BUFFER_SHARED &&
                aTextureInfo.memoryType == TEXTURE_SHARED) ||
               (aTextureInfo.compositableType == BUFFER_DIRECT_EXTERNAL &&
                aTextureInfo.memoryType == TEXTURE_SHMEM),
               "CompositableType mismatch.");
  mTextureHost = aTextureHost;
}
*/

}
}
