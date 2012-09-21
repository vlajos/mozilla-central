/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gfxSharedImageSurface.h"
#include "mozilla/layers/ImageContainerParent.h"

#include "ipc/AutoOpenSurface.h"
#include "CompositeImageLayer.h"
#include "ImageHost.h"
#include "gfxImageSurface.h"
#include "gfx2DGlue.h"

#include "Compositor.h"

using namespace mozilla::gfx;

namespace mozilla {
namespace layers {

CompositeImageLayer::CompositeImageLayer(CompositeLayerManager* aManager)
  : ShadowImageLayer(aManager, nullptr)
  , CompositeLayer(aManager)
  , mImageHost(nullptr)
{
  mImplData = static_cast<CompositeLayer*>(this);
}

CompositeImageLayer::~CompositeImageLayer()
{}

void
CompositeImageLayer::AddTextureHost(const TextureIdentifier& aTextureIdentifier, TextureHost* aTextureHost)
{
  EnsureImageHost(aTextureIdentifier.mBufferType);

  mImageHost->AddTextureHost(aTextureIdentifier, aTextureHost);
}

void
CompositeImageLayer::SwapTexture(const TextureIdentifier& aTextureIdentifier,
                                 const SharedImage& aFront,
                                 SharedImage* aNewBack)
{
  if (mDestroyed ||
      !mImageHost) {
    *aNewBack = aFront;
    return;
  }

  mImageHost->UpdateImage(aTextureIdentifier, aFront);
  *aNewBack = aFront;
}

void
CompositeImageLayer::EnsureImageHost(BufferType aHostType)
{
  if (!mImageHost ||
      mImageHost->GetType() != aHostType) {
    RefPtr<BufferHost> bufferHost = mCompositor->CreateBufferHost(aHostType);
    mImageHost = static_cast<ImageHost*>(bufferHost.get());
  }
}

void
CompositeImageLayer::Disconnect()
{
  Destroy();
}

void
CompositeImageLayer::Destroy()
{
  if (!mDestroyed) {
    mDestroyed = true;
    CleanupResources();
  }
}

Layer*
CompositeImageLayer::GetLayer()
{
  return this;
}

void
CompositeImageLayer::RenderLayer(const nsIntPoint& aOffset,
                                 const nsIntRect& aClipRect,
                                 Surface*)
{
  if (!mImageHost) {
    return;
  }

  mCompositor->MakeCurrent();

  EffectChain effectChain;
  effectChain.mEffects[EFFECT_MASK] =
    CompositeLayerManager::MakeMaskEffect(mMaskLayer);

  gfx::Matrix4x4 transform;
  ToMatrix4x4(GetEffectiveTransform(), transform);
  gfx::Rect clipRect(aClipRect.x, aClipRect.y, aClipRect.width, aClipRect.height);

  mImageHost->Composite(effectChain,
                        GetEffectiveOpacity(),
                        transform,
                        gfx::Point(aOffset.x, aOffset.y),
                        gfx::ToFilter(mFilter),
                        clipRect);
}

TemporaryRef<TextureHost> 
CompositeImageLayer::AsTextureHost()
{
  return mImageHost->GetTextureHost();
}

void
CompositeImageLayer::SetPictureRect(const nsIntRect& aPictureRect)
{
  if (mImageHost) {
    mImageHost->SetPictureRect(aPictureRect);
  }
}

void
CompositeImageLayer::CleanupResources()
{
  mImageHost = nullptr;
}

} /* layers */
} /* mozilla */
