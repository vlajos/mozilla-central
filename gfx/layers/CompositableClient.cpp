/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CompositableClient.h"
#include "TextureClient.h"
#include "mozilla/layers/TextureChild.h"
#include "mozilla/layers/ShadowLayersChild.h"
#include "mozilla/layers/CompositableForwarder.h"
#ifdef XP_WIN
#include "mozilla/layers/TextureD3D11.h"
#endif

namespace mozilla {
namespace layers {

CompositableClient::~CompositableClient() {
  MOZ_COUNT_DTOR(CompositableClient);
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
CompositableClient::CreateTextureClient(TextureClientType aTextureClientType,
                                        TextureFlags aFlags)
{
  MOZ_ASSERT(GetForwarder(), "Can't create a texture client if the compositable is not connected to the compositor.");
  LayersBackend parentBackend = GetForwarder()->GetCompositorBackendType();
  RefPtr<TextureClient> result = nullptr;

  switch (aTextureClientType) {
  case TEXTURE_SHARED_GL:
     if (parentBackend == LAYERS_OPENGL) {
       result = new TextureClientSharedGL(GetForwarder(), GetType());
     }
     break;
  case TEXTURE_CONTENT:
 #ifdef XP_WIN
     if (GetForwarder()->GetCompositorBackendType() == LAYERS_D3D11) {
       result = new TextureClientD3D11(GetForwarder(), GetType());
       break;
     }
 #endif
     // fall through to TEXTURE_SHMEM
   case TEXTURE_SHMEM:
     if (parentBackend == LAYERS_OPENGL || parentBackend == LAYERS_D3D11) {
       result = new TextureClientShmem(GetForwarder(), GetType());
     }
     break;
   default:
    MOZ_ASSERT(false, "Unhandled texture client type");
   }

  MOZ_ASSERT(result, "Failed to create TextureClient");
  if (result) {
    result->SetFlags(aFlags);
    TextureChild* textureChild
      = static_cast<TextureChild*>(GetIPDLActor()->SendPTextureConstructor(result->GetTextureInfo()));
    result->SetIPDLActor(textureChild);
    textureChild->SetClient(result);
  }

  return result.forget();
}

} // namespace
} // namespace
