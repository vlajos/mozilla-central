/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/TextureParent.h"
#include "mozilla/layers/Compositor.h"
#include "CompositableHost.h"
#include "mozilla/layers/TextureFactoryIdentifier.h" // for TextureInfo
#include "ShadowLayerParent.h"
#include "LayerManagerComposite.h"
#include "Compositor.h"
#include "mozilla/layers/CompositableTransactionParent.h"
namespace mozilla {
namespace layers {

TextureParent::TextureParent(const TextureInfo& aInfo, CompositableParent* aCompositable)
: mTextureInfo(aInfo), mLastSurfaceType(SurfaceDescriptor::Tnull_t)
{
  MOZ_COUNT_CTOR(TextureParent);
}

TextureParent::~TextureParent()
{
  MOZ_COUNT_DTOR(TextureParent);
  mTextureHost = nullptr;
}

bool
TextureParent::EnsureTextureHost(SurfaceDescriptor::Type aSurfaceType) {
  if (!SurfaceTypeChanged(aSurfaceType)) {
    return false;
  }
  CompositableParent* compParent = static_cast<CompositableParent*>(Manager());
  CompositableHost* compositable = compParent->GetCompositableHost();
  Compositor* compositor = compositable->GetCompositor();

  if (compositor) {
    mLastSurfaceType = aSurfaceType;
    mTextureHost = compositor->CreateTextureHost(mTextureInfo.compositableType,
                                                 mTextureInfo.textureFlags,
                                                 aSurfaceType,
                                                 compParent->GetCompositableManager());
    compositable->AddTextureHost(mTextureHost);
    return true;
  }
  return false;
}


void TextureParent::SetTextureHost(TextureHost* aHost)
{
  mTextureHost = aHost;
}

CompositableHost* TextureParent::GetCompositableHost() const
{
  CompositableParent* actor
    = static_cast<CompositableParent*>(Manager());
  return actor->GetCompositableHost();
}

TextureHost* TextureParent::GetTextureHost() const
{
  return mTextureHost;
}

bool TextureParent::SurfaceTypeChanged(SurfaceDescriptor::Type aNewSurfaceType)
{
  return mLastSurfaceType != aNewSurfaceType;
}

void TextureParent::SetCurrentSurfaceType(SurfaceDescriptor::Type aNewSurfaceType)
{
  mLastSurfaceType = aNewSurfaceType;
}


} // namespace
} // namespace
