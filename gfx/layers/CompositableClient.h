/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_BUFFERCLIENT_H
#define MOZILLA_GFX_BUFFERCLIENT_H

#include "mozilla/layers/PCompositableChild.h"
#include "mozilla/layers/LayersTypes.h"

namespace mozilla {
namespace layers {

typedef uint32_t TextureFlags; // See Layers.h

class CompositableChild;
class CompositableClient;
class TextureClient;
class ShadowLayersChild;
class ImageBridgeChild;
class ShadowableLayer;
class CompositableForwarder;

class CompositableChild : public PCompositableChild
{
public:
  CompositableChild()
  : mCompositableClient(nullptr), mID(0)
  {}

  virtual PTextureChild* AllocPTexture(const TextureInfo& aInfo) MOZ_OVERRIDE;
  virtual bool DeallocPTexture(PTextureChild* aActor) MOZ_OVERRIDE;

  void Destroy();

  void SetClient(CompositableClient* aClient)
  {
    mCompositableClient = aClient;
  }

  CompositableClient* GetCompositableClient() const
  {
    return mCompositableClient;
  }

  void SetAsyncID(uint64_t aID) { mID = aID; }
  uint64_t GetAsyncID() const
  {
    return mID;
  }
private:
  CompositableClient* mCompositableClient;
  uint64_t mID;
};

class CompositableClient : public RefCounted<CompositableClient>
{
public:
  CompositableClient(CompositableForwarder* aForwarder)
  : mCompositableChild(nullptr), mForwarder(aForwarder)
  {}

  virtual ~CompositableClient();

  virtual CompositableType GetType() const
  {
    NS_WARNING("This method should be overridden");
    return BUFFER_UNKNOWN;
  }

  LayersBackend GetCompositorBackendType() const;

  TemporaryRef<TextureClient> CreateTextureClient(TextureHostType aTextureHostType,
                                                  TextureFlags aFlags,
                                                  bool aStrict = false);

  /**
   * Establishes the connection with compositor side through IPDL
   */
  bool Connect();

  void Destroy();

  CompositableChild* GetIPDLActor() const;
  // should only be called by a CompositableForwarder
  void SetIPDLActor(CompositableChild* aChild);

  CompositableForwarder* GetForwarder() const
  {
    return mForwarder;
  }

  uint64_t GetAsyncID() const
  {
    if (mCompositableChild) {
      return mCompositableChild->GetAsyncID();
    }
    return 0;
  }

protected:
  CompositableChild* mCompositableChild;
  CompositableForwarder* mForwarder;
};

} // namespace
} // namespace

#endif
