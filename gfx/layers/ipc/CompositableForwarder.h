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

namespace mozilla {
namespace layers {

class CompositableClient;
class ShadowableLayer;
class TextureFactoryIdentifier;
class SurfaceDescriptor;
class ThebesBuffer;
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
class CompositableForwarder
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
   * Adds an edit in the transaction in order to attach
   * the corresponding compositable and layer on the compositor side.
   * Connect must have been called on aCompositable beforehand.
   */
  virtual void Attach(CompositableClient* aCompositable,
                      ShadowableLayer* aLayer) = 0;

  virtual void AttachAsyncCompositable(uint64_t aCompositableID,
                                       ShadowableLayer* aLayer) = 0;


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
  virtual void UpdateTextureRegion(TextureClient* aTexture,
                                   const ThebesBuffer& aThebesBuffer,
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
  virtual void DestroyedThebesBuffer(ShadowableLayer* aThebes,
                                     const SurfaceDescriptor& aBackBufferToDestroy) = 0;

  /**
   * Shmem (gfxSharedImageSurface) buffers are available on all
   * platforms, but they may not be optimal.
   *
   * In the absence of platform-specific buffers these fall back to
   * Shmem/gfxSharedImageSurface.
   */
  virtual bool AllocBuffer(const gfxIntSize& aSize,
                           gfxASurface::gfxContentType aContent,
                           SurfaceDescriptor* aBuffer) = 0;

  virtual bool AllocBufferWithCaps(const gfxIntSize& aSize,
                                   gfxASurface::gfxContentType aContent,
                                   uint32_t aCaps,
                                   SurfaceDescriptor* aBuffer) = 0;

  virtual void DestroySharedSurface(SurfaceDescriptor* aSurface) = 0;

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
