/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ipc/AutoOpenSurface.h"
#include "mozilla/layers/PLayers.h"
#include "mozilla/layers/ShadowLayers.h"

#include "CompositeCanvasLayer.h"
#include "ImageHost.h"
#include "gfx2DGlue.h"

using namespace mozilla;
using namespace mozilla::layers;

CompositeCanvasLayer::CompositeCanvasLayer(CompositeLayerManager* aManager)
  : ShadowCanvasLayer(aManager, nullptr)
  , CompositeLayer(aManager)
  , mImageHost(nullptr)
{
  mImplData = static_cast<CompositeLayer*>(this);
}

CompositeCanvasLayer::~CompositeCanvasLayer()
{
}

void
CompositeCanvasLayer::EnsureImageHost(BufferType aHostType)
{
  if (!mImageHost ||
      mImageHost->GetType() != aHostType) {
    RefPtr<BufferHost> bufferHost = mCompositor->CreateBufferHost(aHostType);
    mImageHost = static_cast<ImageHost*>(bufferHost.get());
  }
}

void
CompositeCanvasLayer::AddTextureHost(const TextureIdentifier& aTextureIdentifier,
                                     TextureHost* aTextureHost)
{
  EnsureImageHost(aTextureIdentifier.mBufferType);

  if (CanUseOpaqueSurface()) {
    aTextureHost->AddFlag(UseOpaqueSurface);
  }
  mImageHost->AddTextureHost(aTextureIdentifier, aTextureHost);
}

void
CompositeCanvasLayer::SwapTexture(const TextureIdentifier& aTextureIdentifier,
                                  const SharedImage& aFront,
                                  SharedImage* aNewBack)
{
  if (mDestroyed ||
      !mImageHost) {
    *aNewBack = aFront;
    return;
  }

  *aNewBack = *mImageHost->UpdateImage(aTextureIdentifier, aFront);
}

void
CompositeCanvasLayer::Destroy()
{
  if (!mDestroyed) {
    mDestroyed = true;
  }
}

Layer*
CompositeCanvasLayer::GetLayer()
{
  return this;
}

void
CompositeCanvasLayer::RenderLayer(const nsIntPoint& aOffset, const nsIntRect& aClipRect, Surface*)
{
  if (!mImageHost) {
    return;
  }

  mCompositor->MakeCurrent();

  gfxPattern::GraphicsFilter filter = mFilter;
#ifdef ANDROID
  // Bug 691354
  // Using the LINEAR filter we get unexplained artifacts.
  // Use NEAREST when no scaling is required.
  gfxMatrix matrix;
  bool is2D = GetEffectiveTransform().Is2D(&matrix);
  if (is2D && !matrix.HasNonTranslationOrFlip()) {
    filter = gfxPattern::FILTER_NEAREST;
  }
#endif

  EffectChain effectChain;
  effectChain.mEffects[EFFECT_MASK] = CompositeLayerManager::MakeMaskEffect(mMaskLayer);

  gfx::Matrix4x4 transform;
  ToMatrix4x4(GetEffectiveTransform(), transform);
  gfx::Rect clipRect(aClipRect.x, aClipRect.y, aClipRect.width, aClipRect.height);

  mImageHost->Composite(effectChain,
                        GetEffectiveOpacity(),
                        transform,
                        gfx::Point(aOffset.x, aOffset.y),
                        gfx::ToFilter(filter),
                        clipRect);
}

void
CompositeCanvasLayer::CleanupResources()
{
  mImageHost = nullptr;
}
