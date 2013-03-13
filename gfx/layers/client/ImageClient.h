/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_IMAGECLIENT_H
#define MOZILLA_GFX_IMAGECLIENT_H

#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/layers/CompositableClient.h"
#include "mozilla/layers/TextureClient.h"
#include "gfxPattern.h"

namespace mozilla {
namespace layers {

class ImageContainer;
class ImageLayer;
class PlanarYCbCrImage;

class SharedImageFactory
{
public:
  virtual already_AddRefed<Image> CreateImage(const uint32_t *aFormats,
                                              uint32_t aNumFormats) = 0;
};

// abstract. Used for image and canvas layers
class ImageClient : public CompositableClient,
                    public SharedImageFactory
{
public:
  /**
   * Creates, configures, and returns a new image client. If necessary, a
   * message will be sent to the compositor to create a corresponding image
   * host.
   */
  static TemporaryRef<ImageClient> CreateImageClient(LayersBackend aBackendType,
                                                     CompositableType aImageHostType,
                                                     CompositableForwarder* aFwd,
                                                     TextureFlags aFlags);
  
  ImageClient(CompositableForwarder* aFwd);
  virtual ~ImageClient() {}

  /**
   * Update this ImageClient from aContainer in aLayer
   * returns false if this is the wrong kind of ImageClient for aContainer.
   * Note that returning true does not necessarily imply success
   */
  virtual bool UpdateImage(ImageContainer* aContainer, uint32_t aContentFlags) = 0;

  /**
   * Notify the compositor that this image client has been updated
   */
  virtual void Updated() = 0;

  virtual void UpdatePictureRect(nsIntRect aPictureRect);

  virtual already_AddRefed<Image> CreateImage(const uint32_t *aFormats,
                                              uint32_t aNumFormats) MOZ_OVERRIDE;

protected:
  gfxPattern::GraphicsFilter mFilter;
  int32_t mLastPaintedImageSerial;
  nsIntRect mPictureRect;
};

class ImageClientTexture : public ImageClient
{
public:
  ImageClientTexture(CompositableForwarder* aFwd,
                     TextureFlags aFlags);

  virtual CompositableType GetType() const MOZ_OVERRIDE
  {
    return BUFFER_IMAGE_SINGLE;
  }

  virtual bool UpdateImage(ImageContainer* aContainer, uint32_t aContentFlags);

  void EnsureTextureClient(TextureClientType aType);

  virtual void Updated();
private:
  RefPtr<TextureClient> mTextureClient;
  TextureFlags mFlags;
};

class ImageClientTextureBuffered : public ImageClientTexture
{
public:
   ImageClientTextureBuffered(CompositableForwarder* aFwd,
                              TextureFlags aFlags)
   : ImageClientTexture(aFwd, aFlags) {}

  virtual CompositableType GetType() const MOZ_OVERRIDE
  {
    return BUFFER_IMAGE_BUFFERED;
  }

};

// we store the ImageBridge id in the TextureClientIdentifier
class ImageClientBridge : public ImageClient
{
public:
  ImageClientBridge(CompositableForwarder* aFwd,
                    TextureFlags aFlags);

  virtual CompositableType GetType() const MOZ_OVERRIDE
  {
    return BUFFER_BRIDGE;
  }

  virtual bool UpdateImage(ImageContainer* aContainer, uint32_t aContentFlags);
  virtual bool Connect() { return false; }
  virtual void Updated() {}
  void SetLayer(ShadowableLayer* aLayer) {
    mLayer = aLayer;
  }

protected:
  uint64_t mAsyncContainerID;
  ShadowableLayer* mLayer;
};

}
}

#endif
