/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=8 et :
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_COMPOSITABLEFORWARDER
#define MOZILLA_LAYERS_COMPOSITABLEFORWARDER

#include <stdint.h>
#include "gfxASurface.h"
#include "GLDefs.h"
#include "mozilla/layers/ISurfaceAllocator.h"

namespace mozilla {
namespace layers {

class CompositableClient;
class ShadowableLayer;
class TextureFactoryIdentifier;
class SurfaceDescriptor;
class ThebesBufferData;
class TextureClient;

/**
 * A transaction is a set of changes that happenned on the content side, that
 * should be sent to the compositor side.
 * CompositableForwarder is an interface to manage a transaction of
 * compositable objetcs.
 *
 * ShadowLayerForwarder is an example of a CompositableForwarder (that can
 * additionally forward modifications of the Layer tree).
 * ImageBridgeChild is another CompositableForwarder.
 */
class CompositableForwarder : public ISurfaceAllocator
{
  friend class AutoOpenSurface;
  friend class TextureClientShmem;
public:
  CompositableForwarder()
  : mMaxTextureSize(0), mCompositorBackend(mozilla::layers::LAYERS_NONE)
  {}

  /**
   * Setup the IPDL actor for aCompositable to be part of layers
   * transactions.
   */
  virtual void Connect(CompositableClient* aCompositable) = 0;

  /**
   * When using the Thebes layer pattern of swapping or updating
   * TextureClient/Host pairs without sending SurfaceDescriptors,
   * use these messages to assign the single or double buffer
   * (TextureClient/Host pairs) to the CompositableHost.
   * We expect the textures to already have been created.
   * With these messages, the ownership of the SurfaceDescriptor(s)
   * moves to the compositor.
   */
  virtual void CreatedSingleBuffer(CompositableClient* aCompositable,
                                   TextureClient* aBuffer) = 0;
  virtual void CreatedDoubleBuffer(CompositableClient* aCompositable,
                                   TextureClient* aFront,
                                   TextureClient* aBack) = 0;

  /**
   * Tell the compositor that a Compositable is killing its buffer(s),
   * that is TextureClient/Hosts.
   */
  virtual void DestroyThebesBuffer(CompositableClient* aCompositable) = 0;  

  /**
   * Communicate to the compositor that the texture identified by aLayer
   * and aIdentifier has been updated to aImage.
   */
  virtual void UpdateTexture(TextureClient* aTexture,
                             const SurfaceDescriptor& aImage) = 0;

  /**
   * Communicate to the compositor that aRegion in the texture identified by aLayer
   * and aIdentifier has been updated to aThebesBuffer.
   */
  virtual void UpdateTextureRegion(CompositableClient* aCompositable,
                                   const ThebesBufferData& aThebesBufferData,
                                   const nsIntRegion& aUpdatedRegion) = 0;

  /**
   * Communicate the picture rect of a YUV image in aLayer to the compositor
   */
  virtual void UpdatePictureRect(CompositableClient* aCompositable,
                                 const nsIntRect& aRect) = 0;

  /**
   * The specified layer is destroying its buffers.
   * |aBackBufferToDestroy| is deallocated when this transaction is
   * posted to the parent.  During the parent-side transaction, the
   * shadow is told to destroy its front buffer.  This can happen when
   * a new front/back buffer pair have been created because of a layer
   * resize, e.g.
   */
  virtual void DestroyedThebesBuffer(const SurfaceDescriptor& aBackBufferToDestroy) = 0;

  /**
   * Shmem (gfxSharedImageSurface) buffers are available on all
   * platforms, but they may not be optimal.
   *
   * In the absence of platform-specific buffers these fall back to
   * Shmem/gfxSharedImageSurface.
   */
/*
  virtual bool AllocBuffer(const gfxIntSize& aSize,
                           gfxASurface::gfxContentType aContent,
                           SurfaceDescriptor* aBuffer) = 0;

  virtual bool AllocBufferWithCaps(const gfxIntSize& aSize,
                                   gfxASurface::gfxContentType aContent,
                                   uint32_t aCaps,
                                   SurfaceDescriptor* aBuffer) = 0;

  virtual bool AllocateUnsafe(size_t aSize,
                              ipc::SharedMemory::SharedMemoryType aType,
                              ipc::Shmem* aShmem) = 0;
*/
  void IdentifyTextureHost(const TextureFactoryIdentifier& aIdentifier);

  virtual int32_t GetMaxTextureSize() const { return mMaxTextureSize; }

  LayersBackend GetCompositorBackendType() {
    return mCompositorBackend;
  }

protected:
  uint32_t mMaxTextureSize;
  LayersBackend mCompositorBackend;
};

} // namespace
} // namespace

#endif
