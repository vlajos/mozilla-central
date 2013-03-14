/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_COMPOSITOR_H
#define MOZILLA_GFX_COMPOSITOR_H

#include "mozilla/gfx/Rect.h"
#include "mozilla/gfx/Matrix.h"
#include "gfxMatrix.h"
#include "Layers.h"
#include "mozilla/layers/TextureHost.h"


/**
 * Different elements of a web pages are rendered into separate "layers" before
 * they are flattened into the final image that is brought to the screen.
 * See Layers.h for more informations about layers and why we use retained
 * structures.
 *
 * The main interfaces involved in the compositing system are:
 *  - Layer, ShadowableLayer and ShadowLayer
 *    (see Layers.h and ipc/ShadowLayers.h)
 *  - CompositableClient and CompositableHost
 *    (client/CompositableClient.h composite/CompositableHost.h)
 *  - TextureClient and TextureHost
 *    (client/TextureClient.h composite/TextureHost.h)
 *  - TextureSource
      (composite/TextureHost.h)
 *  - Forwarders
 *    (ipc/CompositableForwarder.h ipc/ShadowLayers.h)
 *  - Compositor
      (this file)
 *  - IPDL protocols
 *    (.ipdl files under the gfx/layers/ipc directory)
 *
 * The *Client classes are always used in the content thread, just like the
 * Shadowable* classes.
 * The *Host and Shadow* classes are always used in the compositor threads.
 *
 * If off-main-thread compositing (OMTC) is enabled, compositing is performed
 * into a dedicated thread. In some setups compositing even happens in a dedicated
 * process (only in Firefox OS for now), so documentation may refer to either
 * the compositor thread ot the compositor process alike.
 *
 * The following is only relevent to OMTC:
 * The basic idea is that painted content is handed to ShadowableLayers on the
 * content thread, which have to forward it to ShadowLayers on the compositor
 * thread, where composition happens.
 * Maintaining a shadow layer tree and transfering painted content is done in
 * layer transactions and compositable transactions (See ShadowLayers.h and
 * CompositableForwarder.h).
 *
 * A layer transaction maintains the shape of the shadow layer tree, and
 * synchronize the texture data hel by compositables. Layer transactions
 * are always between the content thread and the compositor thread.
 * A Compositable transaction is a subset of a layer transaction with which only
 * compositables and textures can be manipulated, and does not always originate
 * from the content thread.
 *
 * Most layer classes own a Compositable plus some extra information like
 * transforms and clip rects. They are platform independent.
 * Compositable classes manipulate Texture objects and are reponsible for
 * things like tiling, buffer rotation or double buffering. Compositables
 * are also platform-independent. Examples of compositable classes are:
 *  - ImageClient
 *  - CanvasClient
 *  - ContentHost
 *  - etc.
 * Texture classes (TextureClient and TextureHost) are thin abstractions over
 * platform-dependent texture memory. They are maniplulated by compositables
 * and don't know about buffer rotations and such. The purposes of TextureClient
 * and TextureHost are to synchronize, serialize and deserialize texture data.
 * TextureHosts provide access to TextureSources that are views on the
 * Texture data providing the necessary api for Compositor backend to composite
 * them.
 * See below for more details about the Compositor interface.
 *
 */

class gfxContext;
class nsIWidget;

namespace mozilla {
namespace gfx {
class DrawTarget;
}

namespace layers {

struct Effect;
struct EffectChain;
class Image;
class ISurfaceAllocator;

enum SurfaceInitMode
{
  INIT_MODE_NONE,
  INIT_MODE_CLEAR,
  INIT_MODE_COPY
};

/**
 * Common interface for compositor backends.
 */
class Compositor : public RefCounted<Compositor>
{
public:
  Compositor()
    : mCompositorID(0)
  {
    MOZ_COUNT_CTOR(Compositor);
  }
  virtual ~Compositor() {
    MOZ_COUNT_DTOR(Compositor);
  }

  virtual bool Initialize() = 0;
  virtual void Destroy() = 0;

  /* Request a texture host identifier that may be used for creating textures
   * accross process or thread boundaries that are compatible with this
   * compositor.
   */
  virtual TextureFactoryIdentifier
    GetTextureFactoryIdentifier() = 0;

  /**
   * Properties of the compositor
   */
  virtual bool CanUseCanvasLayerForSize(const gfxIntSize &aSize) = 0;
  virtual int32_t GetMaxTextureSize() const = 0;

  /**
   * Set the target for rendering, intended to be used for the duration of a transaction
   */
  virtual void SetTargetContext(gfxContext *aTarget) = 0;

  /**
   * Make sure that the underlying rendering API selects the right current
   * rendering context.
   */
  virtual void MakeCurrent(bool aForce = false) = 0;

  /**
   * Modifies the TextureIdentifier if needed in a fallback situation for aId
   */
  virtual void FallbackTextureInfo(TextureInfo& aInfo) {}

  /**
   * This creates a Surface that can be used as a rendering target by this
   * compositor.
   */
  virtual TemporaryRef<CompositingRenderTarget> CreateRenderTarget(const gfx::IntRect &aRect,
                                                                              SurfaceInitMode aInit) = 0;

  /**
   * This creates a Surface that can be used as a rendering target by this compositor,
   * and initializes this surface by copying from the given surface. If the given surface
   * is nullptr, the screen frame in progress is used as the source.
   */
  virtual TemporaryRef<CompositingRenderTarget> CreateRenderTargetFromSource(const gfx::IntRect &aRect,
                                                                             const CompositingRenderTarget* aSource) = 0;

  /**
   * Sets the given surface as the target for subsequent calls to DrawQuad.
   * Passing nullptr as aSurface sets the screen as the target.
   */
  virtual void SetRenderTarget(CompositingRenderTarget *aSurface) = 0;

  /**
   * Mostly the compositor will pull the size from a widget and this will
   * be ignored, but compositor implementations are free to use it if they
   * like.
   */
  virtual void SetRenderTargetSize(int aWidth, int aHeight) = 0;

  /**
   * This tells the compositor to actually draw a quad, where the area is
   * specified in userspace, and the source rectangle is the area of the
   * currently set textures to sample from. This area may not refer directly
   * to pixels depending on the effect.
   */
  virtual void DrawQuad(const gfx::Rect &aRect, const gfx::Rect *aClipRect,
                        const EffectChain &aEffectChain,
                        gfx::Float aOpacity, const gfx::Matrix4x4 &aTransform,
                        const gfx::Point &aOffset) = 0;

  /**
   * Start a new frame. If aClipRectIn is null, sets *aClipRectOut to the screen dimensions. 
   */
  virtual void BeginFrame(const gfx::Rect *aClipRectIn, const gfxMatrix& aTransform,
                          const gfx::Rect& aRenderBounds, gfx::Rect *aClipRectOut = nullptr) = 0;

  /**
   * Flush the current frame to the screen.
   */
  virtual void EndFrame(const gfxMatrix& aTransform) = 0;

  /**
   * Post rendering stuff if the rendering is outside of this Compositor
   * e.g., by Composer2D
   */
  virtual void EndFrameForExternalComposition(const gfxMatrix& aTransform) = 0;

  /**
   * Tidy up if BeginFrame has been called, but EndFrame won't be
   */
  virtual void AbortFrame() = 0;

  /**
   * Setup the viewport and projection matrix for rendering
   * to a window of the given dimensions.
   */
  virtual void PrepareViewport(int aWidth, int aHeight, const gfxMatrix& aWorldTransform) = 0;

  // save the current viewport
  virtual void SaveViewport() = 0;
  // resotre the previous viewport and return its bounds
  virtual gfx::IntRect RestoreViewport() = 0;

  /**
   * Whether textures created by this compositor can receive partial updates.
   */
  virtual bool SupportsPartialTextureUpdate() = 0;

#ifdef MOZ_DUMP_PAINTING
  virtual const char* Name() const = 0;
#endif // MOZ_DUMP_PAINTING


  /**
   * Each Compositor has a unique ID.
   * This ID is used to keep references to each Compositor in a map accessed
   * from the compositor thread only, so that async compositables can find
   * the right compositor parent and schedule compositing even if the compositor
   * changed.
   */
  uint32_t GetCompositorID() const
  {
    return mCompositorID;
  }
  void SetCompositorID(uint32_t aID)
  {
    NS_ASSERTION(mCompositorID==0, "The compositor ID must be set only once.");
    mCompositorID = aID;
  }

  virtual void NotifyShadowTreeTransaction() = 0;

  /**
   * Notify the compositor that composition is being paused/resumed.
   */
  virtual void Pause() {}
  /**
   * Returns true if succeeded
   */
  virtual bool Resume() { return true; }

  // I expect we will want to move mWidget into this class and implement this
  // method properly.
  virtual nsIWidget* GetWidget() const { return nullptr; }
  virtual nsIntSize* GetWidgetSize() {
    return nullptr;
  }

  /**
   * We enforce that there can only be one Compositor backend type off the main
   * thread at the same time. The backend type in use can be checked with this
   * static method. We need this for creating texture clients/hosts etc. when we
   * don't have a reference to a Compositor.
   */
  static LayersBackend GetBackend();
protected:
  uint32_t mCompositorID;
  static LayersBackend sBackend;
};


}
}

#endif /* MOZILLA_GFX_COMPOSITOR_H */
