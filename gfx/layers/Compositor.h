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
 * Most of the documentation for layers is directly in the source code in the
 * form of doc comments. An overview can also be found in the the wiki:
 * https://wiki.mozilla.org/Gecko:Overview#Graphics
 *
 *
 * # Main interfaces and abstractions
 *
 *  - Layer, ShadowableLayer and ShadowLayer
 *    (see Layers.h and ipc/ShadowLayers.h)
 *  - CompositableClient and CompositableHost
 *    (client/CompositableClient.h composite/CompositableHost.h)
 *  - TextureClient and TextureHost
 *    (client/TextureClient.h composite/TextureHost.h)
 *  - TextureSource
 *    (composite/TextureHost.h)
 *  - Forwarders
 *    (ipc/CompositableForwarder.h ipc/ShadowLayers.h)
 *  - Compositor
 *    (this file)
 *  - IPDL protocols
 *    (.ipdl files under the gfx/layers/ipc directory)
 *
 * The *Client and Shadowable* classes are always used on the content thread.
 * Forwarders are always used on the content thread.
 * The *Host and Shadow* classes are always used on the compositor thread.
 * Compositors, TextureSource, and Effects are always used on the compositor
 * thread.
 * Most enums and constants are declared in LayersTypes.h and CompositorTypes.h
 *
 *
 * # IPDL
 *
 * If off-main-thread compositing (OMTC) is enabled, compositing is performed
 * in a dedicated thread. In some setups compositing happens in a dedicated
 * process Documentation may refer to either the compositor thread ot the
 * compositor process.
 *
 * The layer tree is managed on the content thread, and shadowed in the compositor
 * thread. The shadow layer tree is only kept in sync with whatever happens in
 * the content thread. To do this we use IPDL protocols. IPDL is a domain
 * specific language that describes how two processes or thread should
 * communicate. C++ code is generated from .ipdl files to implement the message
 * passing, synchronization and serialization logic. To use the generated code
 * we implement classes that inherit the generated IPDL actor. the ipdl actors
 * of a protocol PX are PXChild or PXParent (the generated class), and we
 * conventionally implement XChild and XParent. The Parent side of the protocol
 * is the one that lives on the compositor thread. Think of IPDL actors as
 * endpoints of communication. they are useful to send messages and also to
 * dispatch the message to the right actor on the other side. One nice property
 * of an IPDL actor is that when an actor, say PXChild is sent in a message, the
 * PXParent comes out in the other side. we use this property a lot to dispatch
 * messages to the right layers and compositable, each of which have their own
 * ipdl actor on both side.
 *
 * Most of the synchronization logic happens in layer transactions and
 * compositable transactions.
 * A transaction is a set of changes to the layers and/or the compositables
 * that are sent and applied together to the compositor thread to keep the
 * ShadowLayer in a coherent state.
 * Layer transactions maintain the shape of the shadow layer tree, and
 * synchronize the texture data held by compositables. Layer transactions
 * are always between the content thread and the compositor thread.
 * (See ShadowLayers.h)
 * Compositable transactions are subset of a layer transaction with which only
 * compositables and textures can be manipulated, and does not always originate
 * from the content thread. (See CompositableForwarder.h and ImageBridgeChild.h)
 *
 *
 * # Texture transfer
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
 * 
 * Compositable and Texture clients and hosts are created using factory methods.
 * They should only be created by using their constructor in exceptional
 * circumstances. The factory methods are located:
 *    TextureClient       - CompositableClient::CreateTextureClient
 *    TextureHost         - TextureHost::CreateTextureHost, which calls a
 *                          platform-specific function, e.g., CreateTextureHostOGL
 *    CompositableClient  - in the appropriate subclass, e.g.,
 *                          CanvasClient::CreateCanvasClient
 *    CompositableHost    - CompositableHost::Create
 *
 *
 * # Backend implementations
 *
 * compositor backend like OpenGL or flavours of D3D live in their own directory
 * under gfx/layer/. To add a new backend, implement at least the following
 * interfaces:
 * - Compositor (ex. CompositorOGL)
 * - TextureHost (ex. TextureImageTextureHost)
 * Depending on the type of data that needs to be serialized, you may need to
 * add specific TextureClient implementations.
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
  virtual TemporaryRef<CompositingRenderTarget>
  CreateRenderTarget(const gfx::IntRect &aRect,
                     SurfaceInitMode aInit) = 0;

  /**
   * Creates a Surface that can be used as a rendering target by this
   * compositor, and initializes the surface by copying from aSource.
   * If aSource is null, then the screen frame in progress
   * is used as source.
   */
  virtual TemporaryRef<CompositingRenderTarget>
  CreateRenderTargetFromSource(const gfx::IntRect &aRect,
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
   * This tells the compositor to actually draw a quad. What to do draw and how
   * is specified by aEffectChain. aRect is the quad to draw, in user space.
   * aTransform transforms from user space to screen space. aOffset is the
   * offset of the render target from 0,0 of the screen. If texture coords are
   * required, these will be in the primary effect in the effect chain.
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
