/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CompositableHost.h"
#include "ImageHost.h"
#include "ContentHost.h"
#include "mozilla/layers/ImageContainerParent.h"
#include "mozilla/layers/TextureParent.h"
#include "Effects.h"

namespace mozilla {
namespace layers {

void CompositableHost::Update(const SurfaceDescriptor& aImage,
                        SurfaceDescriptor* aResult,
                        bool* aIsInitialised,
                        bool* aNeedsReset) {
  if (!GetTextureHost()) {
    *aResult = aImage;
    return;
  }

  GetTextureHost()->Update(aImage, aResult, aIsInitialised, aNeedsReset);
}

bool CompositableHost::AddMaskEffect(EffectChain& aEffects,
                               const gfx::Matrix4x4& aTransform,
                               bool aIs3D)
{
  TextureSource* source = GetTextureHost()->AsTextureSource();
  EffectMask* effect = new EffectMask(source,
                                      source->GetSize(),
                                      aTransform);
  effect->mIs3D = aIs3D;
  aEffects.mEffects[EFFECT_MASK] = effect;
  return true;
}

// static
TemporaryRef<CompositableHost>
CompositableHost::Create(CompositableType aType, Compositor* aCompositor)
{
  RefPtr<CompositableHost> result;
  switch (aType) {
#if 0 // FIXME [bjacob] reenable that
#ifdef MOZ_WIDGET_GONK
  case BUFFER_DIRECT_EXTERNAL:
#endif
#endif
  case BUFFER_SHARED:
  case BUFFER_DIRECT:
  case BUFFER_SINGLE:
    result = new ImageHostSingle(aCompositor, aType);
    return result;
  case BUFFER_TILED:
    result = new TiledContentHost(aCompositor);
    return result;
  case BUFFER_CONTENT:
    result = new ContentHostTexture(aCompositor);
    return result;
  case BUFFER_CONTENT_DIRECT:
    result = new ContentHostDirect(aCompositor);
    return result;
  default:
    NS_ERROR("Unknown CompositableType");
    return nullptr;
  }
}

PTextureParent*
CompositableParent::AllocPTexture(const TextureInfo& aInfo)
{
  return new TextureParent(aInfo, this);
}

bool
CompositableParent::DeallocPTexture(PTextureParent* aActor)
{
  delete aActor;
  return true;
}


CompositableParent::CompositableParent(CompositableParentManager* aMgr,
                                       CompositableType aType)
: mManager(aMgr)
, mType(aType)
{
  mHost = CompositableHost::Create(aType, aMgr->GetCompositor());
}

} // namespace
} // namespace
