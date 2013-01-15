/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_CANVASCLIENT_H
#define MOZILLA_GFX_CANVASCLIENT_H

#include "TextureClient.h"
#include "BufferClient.h"

namespace mozilla {

namespace layers {

class BasicCanvasLayer;

class CanvasClient : public BufferClient
{
public:
  virtual ~CanvasClient() {}

  virtual void Update(gfx::IntSize aSize, BasicCanvasLayer* aLayer) = 0;

  virtual void SetBuffer(const TextureIdentifier& aTextureIdentifier,
                         const SurfaceDescriptor& aBuffer);
  virtual void Updated(ShadowableLayer* aLayer)
  {
    mTextureClient->Updated(aLayer);
  }
protected:
  RefPtr<TextureClient> mTextureClient;
};

// used for 2D canvases and WebGL canvas on non-GL systems where readback is requried
class CanvasClient2D : public CanvasClient
{
public:
  CanvasClient2D(ShadowLayerForwarder* aLayerForwarder,
                      ShadowableLayer* aLayer,
                      TextureFlags aFlags);

  virtual void Update(gfx::IntSize aSize, BasicCanvasLayer* aLayer);
};

// used for GL canvases where we don't need to do any readback, i.e., with a
// GL backend
class CanvasClientWebGL : public CanvasClient
{
public:
  CanvasClientWebGL(ShadowLayerForwarder* aLayerForwarder,
                    ShadowableLayer* aLayer,
                    TextureFlags aFlags);

  virtual void Update(gfx::IntSize aSize, BasicCanvasLayer* aLayer);
};

}
}

#endif
