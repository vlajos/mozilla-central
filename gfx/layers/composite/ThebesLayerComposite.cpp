/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ipc/AutoOpenSurface.h"
#include "mozilla/layers/PLayers.h"
#include "TiledLayerBuffer.h"

/* This must occur *after* layers/PLayers.h to avoid typedefs conflicts. */
#include "mozilla/Util.h"

#include "mozilla/layers/ShadowLayers.h"

#include "ThebesLayerBuffer.h"
#include "ThebesLayerComposite.h"
#include "mozilla/layers/ContentHost.h"
#include "gfxUtils.h"
#include "gfx2DGlue.h"

#include "mozilla/layers/TextureFactoryIdentifier.h" // for TextureInfo
#include "mozilla/layers/Effects.h"

namespace mozilla {
namespace layers {

using gl::GLContext;

ThebesLayerComposite::ThebesLayerComposite(LayerManagerComposite *aManager)
  : ShadowThebesLayer(aManager, nullptr)
  , LayerComposite(aManager)
  , mBuffer(nullptr)
{
#ifdef FORCE_BASICTILEDTHEBESLAYER
  NS_ABORT();
#endif
  mImplData = static_cast<LayerComposite*>(this);
}

ThebesLayerComposite::~ThebesLayerComposite()
{}

void
ThebesLayerComposite::AddTextureHost(const TextureInfo& aTextureInfo, TextureHost* aTextureHost)
{
  EnsureBuffer(aTextureInfo.imageType);

  mBuffer->AddTextureHost(aTextureInfo, aTextureHost);
}


void
ThebesLayerComposite::EnsureBuffer(BufferType aHostType)
{
  if (!mBuffer ||
      mBuffer->GetType() != aHostType) {
    RefPtr<BufferHost> bufferHost = mCompositor->CreateBufferHost(aHostType);
    NS_ASSERTION(bufferHost->GetType() == BUFFER_CONTENT ||
                 bufferHost->GetType() == BUFFER_CONTENT_DIRECT, "bad buffer type");
    mBuffer = static_cast<AContentHost*>(bufferHost.get());
  }
}

// TODO[nical] remove swap
void
ThebesLayerComposite::SwapTexture(const ThebesBuffer& aNewFront,
                                  const nsIntRegion& aUpdatedRegion,
                                  OptionalThebesBuffer* aNewBack,
                                  nsIntRegion* aNewBackValidRegion,
                                  OptionalThebesBuffer* aReadOnlyFront,
                                  nsIntRegion* aFrontUpdatedRegion)
{
  if (mDestroyed ||
      !mBuffer) {
    // Don't drop buffers on the floor.
    *aNewBack = aNewFront;
    *aNewBackValidRegion = aNewFront.rect();
    return;
  }
  
  mBuffer->UpdateThebes(aNewFront,
                        aUpdatedRegion,
                        aNewBack,
                        mValidRegionForNextBackBuffer,
                        mValidRegion,
                        aReadOnlyFront,
                        aNewBackValidRegion,
                        aFrontUpdatedRegion);

  // Save the current valid region of our front buffer, because if
  // we're double buffering, it's going to be the valid region for the
  // next back buffer sent back to the renderer.
  //
  // NB: we rely here on the fact that mValidRegion is initialized to
  // empty, and that the first time Swap() is called we don't have a
  // valid front buffer that we're going to return to content.
  mValidRegionForNextBackBuffer = mValidRegion;
}

void
ThebesLayerComposite::Disconnect()
{
  Destroy();
}

void
ThebesLayerComposite::Destroy()
{
  if (!mDestroyed) {
    mBuffer = nullptr;
    mDestroyed = true;
  }
}

Layer*
ThebesLayerComposite::GetLayer()
{
  return this;
}

bool
ThebesLayerComposite::IsEmpty()
{
  return !mBuffer;
}

LayerRenderState
ThebesLayerComposite::GetRenderState()
{
  if (!mBuffer || mDestroyed) {
    return LayerRenderState();
  }
  return mBuffer->GetRenderState();
  //TODO[nrc] need to move this into some ContentHost
  //uint32_t flags = (mBuffer->Rotation() != nsIntPoint()) ?
  //                 LAYER_RENDER_STATE_BUFFER_ROTATION : 0;
  //return LayerRenderState(&mBufferDescriptor, flags);
}

void
ThebesLayerComposite::RenderLayer(const nsIntPoint& aOffset,
                                  const nsIntRect& aClipRect,
                                  CompositingRenderTarget* aPreviousTarget)
{
  if (mCompositeManager->CompositingDisabled()) {
    return;
  }

  if (!mBuffer) {
    return;
  }

  gfx::Matrix4x4 transform;
  ToMatrix4x4(GetEffectiveTransform(), transform);
  gfx::Rect clipRect(aClipRect.x, aClipRect.y, aClipRect.width, aClipRect.height);

#ifdef MOZ_DUMP_PAINTING
  if (gfxUtils::sDumpPainting) {
    nsRefPtr<gfxImageSurface> surf = mBuffer->Dump();
    WriteSnapshotToDumpFile(this, surf);
  }
#endif

  EffectChain effectChain;
  effectChain.mEffects[EFFECT_MASK] = LayerManagerComposite::MakeMaskEffect(mMaskLayer);

  mBuffer->Composite(effectChain,
                     GetEffectiveOpacity(), 
                     transform,
                     gfx::Point(aOffset.x, aOffset.y),
                     gfx::FILTER_LINEAR,
                     clipRect,
                     &GetEffectiveVisibleRegion());
}
void

ThebesLayerComposite::DestroyFrontBuffer()
{
  mBuffer = nullptr;
  mValidRegionForNextBackBuffer.SetEmpty();
}

void
ThebesLayerComposite::CleanupResources()
{
  DestroyFrontBuffer();
}

void
ThebesLayerComposite::SetAllocator(ISurfaceDeAllocator* aAllocator)
{
  mBuffer->SetDeAllocator(aAllocator);
}

} /* layers */
} /* mozilla */
