/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_IMAGECLIENT_H
#define MOZILLA_GFX_IMAGECLIENT_H

#include "mozilla/layers/LayersSurfaces.h"
#include "CompositableClient.h"
#include "TextureClient.h"

namespace mozilla {
namespace layers {

class ImageContainer;
class ImageLayer;
class PlanarYCbCrImage;

// abstract. Used for image and canvas layers
class ImageClient : public CompositableClient
{
public:
  ImageClient();
  virtual ~ImageClient() {}

  virtual CompositableType GetType() const MOZ_OVERRIDE
  {
    return BUFFER_SINGLE; // TODO[nical] maybe not always true, check
  }

  /**
   * Update this ImageClient from aContainer in aLayer
   * returns false if this is the wrong kind of ImageClient for aContainer.
   * Note that returning true does not necessarily imply success
   */
  virtual bool UpdateImage(ImageContainer* aContainer, ImageLayer* aLayer) = 0;

  /**
   * Set the buffer of a texture client (identified by aTextureInfo) to
   * aBuffer. Intended to be used with a buffer from the compositor
   */
  virtual void SetBuffer(const TextureInfo& aTextureInfo,
                         const SurfaceDescriptor& aBuffer) = 0;

  /**
   * Notify the compositor that this image client has been updated
   */
  virtual void Updated(ShadowableLayer* aLayer) = 0;

  virtual void UpdatePictureRect(nsIntRect aPictureRect);

  /**
   * Compositable will have it's own IPDL protocol but in the mean time we use the
   * layer's communication channel. (TODO)
   */
  void SetCompositableChild(ShadowLayerForwarder* aFwd, ShadowableLayer* aLayer) {
    mForwarder = aFwd;
    mLayer = aLayer;
  }
protected:
  int32_t mLastPaintedImageSerial;
  nsIntRect mPictureRect;
  // TODO[nical]
  // we need to keep this here until Compositable gets its own ipdl protocol
  // in the mean time compositable-specific stuff goes through PLayers
  ShadowLayerForwarder* mForwarder;
  ShadowableLayer* mLayer;
};

class ImageClientTexture : public ImageClient
{
public:
  ImageClientTexture(ShadowLayerForwarder* aLayerForwarder,
                     ShadowableLayer* aLayer,
                     TextureFlags aFlags);

  virtual bool UpdateImage(ImageContainer* aContainer, ImageLayer* aLayer);
  bool UpdateYCbCrImage(PlanarYCbCrImage* aImage, ImageLayer* aLayer);
  bool UpdateRGBImage(Image* aImage, ImageLayer* aLayer, gfxASurface* aSurface);

  virtual void SetBuffer(const TextureInfo& aTextureInfo,
                         const SurfaceDescriptor& aBuffer);

  virtual void Updated(ShadowableLayer* aLayer);
private:
  RefPtr<TextureClient> mTextureClient;
};

class ImageClientShared : public ImageClient
{
public:
  ImageClientShared(ShadowLayerForwarder* aLayerForwarder,
                    ShadowableLayer* aLayer,
                    TextureFlags aFlags);

  virtual bool UpdateImage(ImageContainer* aContainer, ImageLayer* aLayer);

  virtual void SetBuffer(const TextureInfo& aTextureInfo,
                         const SurfaceDescriptor& aBuffer) {}

  virtual void Updated(ShadowableLayer* aLayer);
private:
  RefPtr<TextureClient> mTextureClient;
};

// we store the ImageBridge id in the TextureClientIdentifier
class ImageClientBridge : public ImageClient
{
public:
  ImageClientBridge(ShadowLayerForwarder* aLayerForwarder,
                    ShadowableLayer* aLayer,
                    TextureFlags aFlags);

  virtual bool UpdateImage(ImageContainer* aContainer, ImageLayer* aLayer);
  virtual void SetBuffer(const TextureInfo& aTextureInfo,
                         const SurfaceDescriptor& aBuffer) {}
  virtual void Updated(ShadowableLayer* aLayer);

private:
  RefPtr<TextureClient> mTextureClient;
};

}
}

#endif
