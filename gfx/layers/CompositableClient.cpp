/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CompositableClient.h"
#include "TextureClient.h"
#include "mozilla/layers/TextureChild.h"
#include "mozilla/layers/ShadowLayersChild.h"
#include "mozilla/layers/ShadowLayers.h" // for CompositabeForwarder
#ifdef XP_WIN
#include "mozilla/layers/TextureD3D11.h"
#endif

namespace mozilla {
namespace layers {

CompositableClient::~CompositableClient() {
  Destroy();
}

LayersBackend
CompositableClient::GetCompositorBackendType() const{
  return mForwarder->GetCompositorBackendType();
}

void
CompositableClient::SetIPDLActor(CompositableChild* aChild)
{
  mCompositableChild = aChild;
}

CompositableChild*
CompositableClient::GetIPDLActor() const {
  return mCompositableChild;
}

bool
CompositableClient::Connect()
{
  if (!GetForwarder() || GetIPDLActor()) {
    return false;
  }
  GetForwarder()->Connect(this);
  return true;
}

void
CompositableClient::Destroy()
{
  if (!mCompositableChild) {
    return;
  }
  mCompositableChild->Destroy();
  mCompositableChild = nullptr;
}

void
CompositableChild::Destroy()
{
  int numChildren = ManagedPTextureChild().Length();
  for (int i = numChildren-1; i >= 0; --i) {
    TextureChild* texture =
      static_cast<TextureChild*>(
        ManagedPTextureChild()[i]);
    texture->Destroy();
  }
  Send__delete__(this);
}

PTextureChild*
CompositableChild::AllocPTexture(const TextureInfo& aInfo)
{
  return new TextureChild();
}

bool
CompositableChild::DeallocPTexture(PTextureChild* aActor)
{
    delete aActor;
    return true;
}

TemporaryRef<TextureClient>
CompositableClient::CreateTextureClient(TextureHostType aTextureHostType,
                                        TextureFlags aFlags,
                                        bool aStrict)
{
  MOZ_ASSERT(GetForwarder(), "Can't create a texture client if the compositable is not connected to the compositor.");
  LayersBackend parentBackend = GetForwarder()->GetCompositorBackendType();
  RefPtr<TextureClient> result = nullptr;
  switch (aTextureHostType) {
  case TEXTURE_SHARED|TEXTURE_BUFFERED:
    if (parentBackend == LAYERS_OPENGL) {
      result = new TextureClientSharedGL(GetForwarder(), GetType());
    }
    break;
  case TEXTURE_SHARED:
    if (parentBackend == LAYERS_OPENGL) {
      result = new TextureClientShared(GetForwarder(), GetType());
    }
    break;
  case TEXTURE_SHMEM:
    if (parentBackend == LAYERS_OPENGL || parentBackend == LAYERS_D3D11) {
      result = new TextureClientShmem(GetForwarder(), GetType());
    }
    break;
  case TEXTURE_DIRECT:
    if (parentBackend == LAYERS_OPENGL || parentBackend == LAYERS_D3D11) {
      result = new TextureClientShmem(GetForwarder(), GetType());
    }
    break;
  case TEXTURE_TILE:
    result = new TextureClientTile(GetForwarder(), GetType());
    break;
  case TEXTURE_SHARED|TEXTURE_DXGI:
#ifdef XP_WIN
    result = new TextureClientD3D11(GetForwarder(), GetType());
    break;
#endif
  default:
    MOZ_ASSERT(false, "Unhandled texture host type");
  }

  NS_ASSERTION(result, "Failed to create TextureClient");
  if (result) {
    TextureChild* textureChild
      = static_cast<TextureChild*>(GetIPDLActor()->SendPTextureConstructor(result->GetTextureInfo()));
    result->SetIPDLActor(textureChild);
    textureChild->SetClient(result);
  }
  // debug
  if (aTextureHostType & TEXTURE_SHARED) {
    printf("Shared texture\n");
  }

  return result.forget();
}


} // namespace
} // namespace
