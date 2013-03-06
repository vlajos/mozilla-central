/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_BUFFERCLIENT_H
#define MOZILLA_GFX_BUFFERCLIENT_H

#include "mozilla/layers/PCompositableChild.h"
#include "mozilla/layers/LayersTypes.h"
#include "Compositor.h"

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


/**
 * IPDL actor used by CompositableClient to match with its corresponding
 * CompositableHost on the compositor side. 
 *
 * CompositableChild is owned by a CompositableClient.
 */
class CompositableChild : public PCompositableChild
{
public:
  CompositableChild()
  : mCompositableClient(nullptr), mID(0)
  {
    MOZ_COUNT_CTOR(CompositableChild);
  }
  ~CompositableChild()
  {
    MOZ_COUNT_DTOR(CompositableChild);
  }

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


/**
 * CompositableClient manages the texture-specific logic for shadow kayers,
 * independently of layers. It is the content side of a ConmpositableClient/CompositableHost
 * pair.
 *
 * CompositableClient's purpose is to send texture data to the compositor side
 * along with extra information about how to render the texture such as buffer
 * rotation, or picture rect.
 * Things like opacity or transformation belong to layer and not compositable.
 *
 * Since Compositables are independent of layers it is possible to create one,
 * connetc it to the compositor side, and start sending images to it. This alone
 * is arguably not very useful, but it means that as long as a shdow layer can
 * do the proper magic to find a reference to the right CompositableHost on the
 * Compositor side, a Compositable client can be used outside of the main
 * shadow layer forwarder machinery that is used on the main thread.
 *
 * The first step is to create a Compositable client and call Connect().
 * Connect() creates the underlyin IPDL actor (see CompositableChild) and the
 * corresponding CompositableHost on the other side.
 *
 * To do in-transaction texture transfer (the default), call
 * ShadowLayerForwarder::Attach(CompositableClient*, ShadowableLayer*). This
 * will let the ShadowLayer on the compositor side now which CompositableHost
 * to use for compositing.
 *
 * To do async texture transfer (like async-video), the CompositableClient
 * should be created with a different CompositableForwarder (like
 * ImageBridgeChild) and attachment is done with
 * CompositableForwarder::AttachAsyncCompositable that takes an identifier
 * instead of a CompositableChild, since the CompositableClient is not managed
 * by this layer forwarder (the matching uses a global map on the compositor side,
 * see CompositableMap in ImageBridgeParent.cpp)
 */
class CompositableClient : public RefCounted<CompositableClient>
{
public:
  CompositableClient(CompositableForwarder* aForwarder)
  : mCompositableChild(nullptr), mForwarder(aForwarder)
  {
    MOZ_COUNT_CTOR(CompositableClient);
  }

  virtual ~CompositableClient();

  virtual CompositableType GetType() const
  {
    NS_WARNING("This method should be overridden");
    return BUFFER_UNKNOWN;
  }

  LayersBackend GetCompositorBackendType() const;

  TemporaryRef<TextureClient> CreateTextureClient(TextureClientType aTextureClientType,
                                                  TextureFlags aFlags);

  /**
   * Establishes the connection with compositor side through IPDL
   */
  virtual bool Connect();

  void Destroy();

  CompositableChild* GetIPDLActor() const;

  // should only be called by a CompositableForwarder
  void SetIPDLActor(CompositableChild* aChild);

  CompositableForwarder* GetForwarder() const
  {
    return mForwarder;
  }

  /**
   * This identifier is what lets us attach async compositables with a shadow
   * layer. It is not used if the compositable is used with the regulat shadow
   * layer forwarder.
   */
  uint64_t GetAsyncID() const
  {
    if (mCompositableChild) {
      return mCompositableChild->GetAsyncID();
    }
    return 0; // zero is always an invalid async ID
  }

protected:
  CompositableChild* mCompositableChild;
  CompositableForwarder* mForwarder;
};

} // namespace
} // namespace

#endif
