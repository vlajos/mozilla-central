/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ipc/AutoOpenSurface.h"
#include "mozilla/layers/PLayers.h"
#include "mozilla/layers/ShadowLayers.h"
#include "mozilla/layers/TextureFactoryIdentifier.h" // for TextureInfo
#include "mozilla/layers/Effects.h"

#include "CanvasLayerComposite.h"
#include "ImageHost.h"
#include "gfx2DGlue.h"

using namespace mozilla;
using namespace mozilla::layers;

CanvasLayerComposite::CanvasLayerComposite(LayerManagerComposite* aManager)
  : ShadowCanvasLayer(aManager, nullptr)
  , LayerComposite(aManager)
  , mImageHost(nullptr)
{
  mImplData = static_cast<LayerComposite*>(this);
}

CanvasLayerComposite::~CanvasLayerComposite()
{
}

void
CanvasLayerComposite::EnsureImageHost(BufferType aHostType)
{
  if (!mImageHost ||
      mImageHost->GetType() != aHostType) {
    RefPtr<BufferHost> bufferHost = mCompositor->CreateBufferHost(aHostType);
    mImageHost = static_cast<ImageHost*>(bufferHost.get());
  }
}

void
CanvasLayerComposite::AddTextureHost(const TextureInfo& aTextureInfo,
                                     TextureHost* aTextureHost)
{
  EnsureImageHost(aTextureInfo.imageType);

  if (CanUseOpaqueSurface()) {
    aTextureHost->AddFlag(UseOpaqueSurface);
  }
  mImageHost->AddTextureHost(aTextureInfo, aTextureHost);
}

void
CanvasLayerComposite::SwapTexture(const TextureInfo& aTextureInfo,
                                  const SharedImage& aFront,
                                  SharedImage* aNewBack)
{
  if (mDestroyed ||
      !mImageHost) {
    *aNewBack = aFront;
    return;
  }

  *aNewBack = mImageHost->UpdateImage(aTextureInfo, aFront);
}

void
CanvasLayerComposite::Destroy()
{
  if (!mDestroyed) {
    mDestroyed = true;
  }
}

Layer*
CanvasLayerComposite::GetLayer()
{
  return this;
}

void
CanvasLayerComposite::RenderLayer(const nsIntPoint& aOffset, const nsIntRect& aClipRect, Surface*)
{
  if (mCompositeManager->CompositingDisabled()) {
    return;
  }

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
  effectChain.mEffects[EFFECT_MASK] = LayerManagerComposite::MakeMaskEffect(mMaskLayer);

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
CanvasLayerComposite::CleanupResources()
{
  mImageHost = nullptr;
}

void
CanvasLayerComposite::SetAllocator(ISurfaceDeAllocator* aAllocator)
{
  mImageHost->SetDeAllocator(aAllocator);
}
