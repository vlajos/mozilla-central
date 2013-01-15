/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_COMPOSITOR_H
#define MOZILLA_GFX_COMPOSITOR_H

#include "mozilla/RefPtr.h"
#include "mozilla/gfx/Rect.h"
#include "mozilla/gfx/Matrix.h"
#include "gfxMatrix.h"
#include "nsAutoPtr.h"
#include "nsRegion.h"
#include "LayersTypes.h"
#include "mozilla/layers/TextureFactoryIdentifier.h"

class gfxContext;
class gfxASurface;
class gfxImageSurface;
class nsIWidget;

namespace mozilla {
namespace gfx {
class DrawTarget;
}

namespace layers {

class Compositor;
struct Effect;
struct EffectChain;
class SurfaceDescriptor;
class Image;
class ISurfaceDeallocator;
class CompositableHost;
class TextureHost;
class TextureInfo;
class TextureClient;
class ImageClient;
class CanvasClient;
class ContentClient;
class ShadowLayerForwarder;
class ShadowableLayer;
class PTextureChild;
class TextureSourceOGL;
class TextureParent;
class Matrix4x4;

typedef uint32_t TextureFlags;
const TextureFlags NoFlags            = 0x0;
const TextureFlags UseNearestFilter   = 0x1;
const TextureFlags NeedsYFlip         = 0x2;
const TextureFlags ForceSingleTile    = 0x4;
const TextureFlags UseOpaqueSurface   = 0x8;
const TextureFlags AllowRepeat        = 0x10;

/**
 * A view on a texture host where the texture host is internally represented as tiles
 * (contrast with a tiled buffer, where each texture is a tile). For iteration by
 * the texture's buffer host.
 */
class TileIterator
{
public:
  virtual void BeginTileIteration() = 0;
  virtual nsIntRect GetTileRect() = 0;
  virtual size_t GetTileCount() = 0;
  virtual bool NextTile() = 0;
};

/**
 * TextureSource is the interface for texture objects that can be composited
 * by a given compositor backend. Since the drawing APIs are different
 * between backends, the TextureSource interface is split into different
 * interfaces (TextureSourceOGL, etc.), and TextureSource mostly provide
 * access to these interfaces.
 */
class TextureSource
{
public:
  virtual ~TextureSource() {};
  virtual gfx::IntSize GetSize() const = 0;
  virtual TextureSourceOGL* AsSourceOGL() { return nullptr; }
  virtual TextureSource* GetSubSource(int index) { return nullptr; }
  virtual TileIterator* AsTileIterator() { return nullptr; }
};

/**
 * Interface
 * TextureHost is a thin abstraction over texture data that need to be shared
 * or transfered from the content process to the compositor process.
 * TextureHost only knows how to deserialize generic image data (SurfaceDescriptor)
 * and provide access to one or more TextureSource objects (these provide the
 * necessary APIs for compositor backends to composite the image).
 *
 * Used only on the compositor process.
 */
class TextureHost : public RefCounted<TextureHost>
{
public:
  enum Buffering { NONE, BUFFERED };

  TextureHost(Buffering aBuffering = Buffering::NONE,
              ISurfaceDeallocator* aDeAllocator = nullptr);
  virtual ~TextureHost();

  /**
   * In most case there is one TextureSource per TextureHost, and CompositableHost
   * sometimes need to get a TextureSource from a TextureHost (for example to create
   * a mask effect). This is one of the sketchy pieces of the compositing architecture
   * that I hope to fix asap, maybe by enforcing one TextureSource per TextureHost and
   * replacing this by GetAsTextureSource().
   */
  virtual TextureSource* AsTextureSource() = 0;

  /**
   * Update the texture host from a SurfaceDescriptor, aResult may contain the old
   * content of the texture, a pointer to the new image, or null. The
   * texture client should know what to expect
   * The buffering logic is implemented here rather than in the specialized classes
   */
  void Update(const SurfaceDescriptor& aImage,
              SurfaceDescriptor* aResult = nullptr,
              bool* aIsInitialised = nullptr,
              bool* aNeedsReset = nullptr);

  /**
   * Updates a region of the texture host from aSurface
   */
  void Update(gfxASurface* aSurface, nsIntRegion& aRegion);

  /**
   * Lock the texture host for compositing, returns an effect that should
   * be used to composite this texture.
   */
  virtual Effect* Lock(const gfx::Filter& aFilter) { return nullptr; }

  /**
   * Unlock the texture host after compositing
   */
  virtual void Unlock() {}

  /**
   * Leave the texture host in a sane state if we abandon an update part-way
   * through.
   */
  virtual void Abort() {}

  // TODO[nical] these probably don't belong here (not sure)
  // nrc: do we not need them in order to composite a texture?
  // Or should we pass them in from the CompositableHost? Or store in a TextureSource or something?
  // nical: looks like something that would fit better in CompositableHost or in TextureSource
  void SetFlags(TextureFlags aFlags) { mFlags = aFlags; }
  void AddFlag(TextureFlags aFlag) { mFlags |= aFlag; }
  TextureFlags GetFlags() { return mFlags; }

  void SetDeAllocator(ISurfaceDeallocator* aDeAllocator) {
    mDeAllocator = aDeAllocator;
  }

#ifdef MOZ_DUMP_PAINTING
  virtual already_AddRefed<gfxImageSurface> Dump() { return nullptr; }
#endif

  virtual LayerRenderState GetRenderState() = 0;

  // IPC

  void SetTextureParent(TextureParent* aParent) {
    mTextureParent = aParent;
  }

  TextureParent* GetTextureParent() const {
    return mTextureParent;
  }

  // ImageBridge

  /**
   * \return true if this TextureHost uses ImageBridge
   */
  bool IsAsync() const {
    return mAsyncContainerID != 0;
  }

  void SetAsyncContainerID(uint64_t aID) {
    mAsyncContainerID = aID;
  }

  void SetCompositorID(uint32_t aID) {
    mCompositorID = aID;
  }

  /**
   * If this TextureHost uses ImageBridge, try to fetch the SurfaceDescriptor in
   * the ImageBridge global map and call Update on it.
   * If it does not use ImageBridge, do nothing and return true.
   * Return false if using ImageBridge and failed to fetch the texture.
   * The texture is checked against a version ID to avoid calling Update
   * several times on the same image.
   * Should be called before Lock.
   */
  bool UpdateAsyncTexture();

protected:

  // buffering

  void SetBuffering(Buffering aBuffering,
                    ISurfaceDeallocator* aDeAllocator = nullptr) {
    MOZ_ASSERT (aBuffering == Buffering::NONE || aDeAllocator);
    mBuffering = aBuffering;
    mDeAllocator = aDeAllocator;
  }

  bool IsBuffered() const { return mBuffering == Buffering::BUFFERED; }
  SurfaceDescriptor* GetBuffer() const { return mBuffer; }


  /**
   * Should be implemented by the backend-specific TextureHost classes 
   */
  virtual void UpdateImpl(const SurfaceDescriptor& aImage,
                          bool* aIsInitialised,
                          bool* aNeedsReset)
  {
    NS_RUNTIMEABORT("Should not be reached");
  }

  virtual void UpdateRegionImpl(gfxASurface* aSurface, nsIntRegion& aRegion)
  {
    NS_RUNTIMEABORT("Should not be reached");
  };


  // Texture info
  TextureFlags mFlags;
  Buffering mBuffering;
  SurfaceDescriptor* mBuffer;

  // ImageBridge
  uint64_t mAsyncContainerID;
  uint32_t mAsyncTextureVersion;
  uint32_t mCompositorID;

  TextureParent* mTextureParent;
  ISurfaceDeallocator* mDeAllocator;
};

/**
 * This can be used as an offscreen rendering target by the compositor, and
 * subsequently can be used as a source by the compositor.
 */
class CompositingRenderTarget : public TextureSource, public RefCounted<CompositingRenderTarget>
{
public:
  virtual ~CompositingRenderTarget() {}

#ifdef MOZ_DUMP_PAINTING
  virtual already_AddRefed<gfxImageSurface> Dump(Compositor* aCompositor) { return nullptr; }
#endif
};

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
  {}
  virtual ~Compositor() {}

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
  virtual void SetTarget(gfxContext *aTarget) = 0;

  virtual void MakeCurrent(bool aForce = false) = 0;

  /**
   * Create a new texture host of a kind specified by aIdentifier
   */
  virtual TemporaryRef<TextureHost>
    CreateTextureHost(BufferType aImageType,
                      TextureHostType aMemoryType,
                      uint32_t aTextureFlags,
                      ISurfaceDeallocator* aDeAllocator) = 0;
  /**
   * Create a new buffer host of a kind specified by aType
   */
  virtual TemporaryRef<CompositableHost>
    CreateCompositableHost(BufferType aType) = 0;

  /**
   * modifies the TextureIdentifier if needed in a fallback situation for aId
   */
  virtual void FallbackTextureInfo(TextureInfo& aInfo)
  {
    // nothing to do
  }

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
  virtual void DrawQuad(const gfx::Rect &aRect, const gfx::Rect *aSourceRect,
                        const gfx::Rect *aTextureRect, const gfx::Rect *aClipRect,
                        const EffectChain &aEffectChain,
                        gfx::Float aOpacity, const gfx::Matrix4x4 &aTransform,
                        const gfx::Point &aOffset) = 0;

  /**
   * Start a new frame. If aClipRectIn is null, sets *aClipRectOut to the screen dimensions. 
   */
  virtual void BeginFrame(const gfx::Rect *aClipRectIn, const gfxMatrix& aTransform,
                          gfx::Rect *aClipRectOut = nullptr) = 0;

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

  /* Whether textures created by this compositor can receive partial updates.
   */
  virtual bool SupportsPartialTextureUpdate() = 0;

#ifdef MOZ_DUMP_PAINTING
  virtual const char* Name() const = 0;
#endif // MOZ_DUMP_PAINTING

  // these methods refer to the ID of the Compositor in terms of the Compositor
  // IPDL protocol and CompositorParent
  void SetCompositorID(uint32_t aID)
  {
    NS_ASSERTION(mCompositorID==0, "The compositor ID must be set only once.");
    mCompositorID = aID;
  }
  uint32_t GetCompositorID() const
  {
    return mCompositorID;
  }

  virtual void NotifyShadowTreeTransaction() = 0;

  /**
   * Notify the compositor that composition is being paused/resumed.
   */
  virtual void Pause() {}
  virtual void Resume() {}

  // I expect we will want to move mWidget into this class and implement this
  // method properly.
  virtual nsIWidget* GetWidget() const { return nullptr; }

protected:
  uint32_t mCompositorID;
};

class CompositingFactory
{
public:
  /**
   * The Create*Client methods each create, configure, and return a new buffer
   * or texture client. If necessary, a message will be sent to the compositor
   * to create a corresponding buffer or texture host.
   */
  static TemporaryRef<ImageClient> CreateImageClient(LayersBackend aBackendType,
                                                     BufferType aImageHostType,
                                                     ShadowLayerForwarder* aLayerForwarder,
                                                     ShadowableLayer* aLayer,
                                                     TextureFlags aFlags);
  static TemporaryRef<CanvasClient> CreateCanvasClient(LayersBackend aBackendType,
                                                       BufferType aImageHostType,
                                                       ShadowLayerForwarder* aLayerForwarder,
                                                       ShadowableLayer* aLayer,
                                                       TextureFlags aFlags);
  static TemporaryRef<ContentClient> CreateContentClient(LayersBackend aBackendType,
                                                         BufferType aImageHostType,
                                                         ShadowLayerForwarder* aLayerForwarder,
                                                         ShadowableLayer* aLayer,
                                                         TextureFlags aFlags);
  static TemporaryRef<TextureClient> CreateTextureClient(LayersBackend aBackendType,
                                                         TextureHostType aTextureHostType,
                                                         BufferType aImageHostType,
                                                         ShadowLayerForwarder* aLayerForwarder,
                                                         bool aStrict = false); 

  static BufferType TypeForImage(Image* aImage);
};

}
}

#endif /* MOZILLA_GFX_COMPOSITOR_H */
