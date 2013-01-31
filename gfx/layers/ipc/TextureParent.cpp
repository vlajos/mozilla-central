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

namespace mozilla {
namespace layers {

TextureParent::TextureParent(const TextureInfo& aInfo)
: mTextureInfo(aInfo), mLastSurfaceType(SurfaceDescriptor::Tnull_t)
{
}

TextureParent::~TextureParent()
{
  mTextureHost = nullptr;
}

void TextureParent::SetTextureHost(TextureHost* aHost)
{
  mTextureHost = aHost;
}

CompositableHost* TextureParent::GetCompositableHost() const
{
  ShadowLayerParent* layerParent
    = static_cast<ShadowLayerParent*>(Manager());
  LayerComposite* layer = layerParent->AsLayer()->AsLayerComposite();
  return layer->GetCompositableHost();
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
