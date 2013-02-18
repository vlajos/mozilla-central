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
#include "mozilla/layers/LayersTypes.h"
#include "mozilla/layers/TextureFactoryIdentifier.h"

class gfxContext;
class gfxASurface;
class gfxImageSurface;
class nsIWidget;
class gfxReusableSurfaceWrapper;

typedef int32_t SurfaceDescriptorType;
static const int32_t SURFACEDESCRIPTOR_UNKNOWN = 0;

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
class CompositableForwarder;
class ShadowableLayer;
class PTextureChild;
class TextureSourceOGL;
class TextureSourceD3D11;
class TextureParent;
class Matrix4x4;
struct TexturedEffect;

typedef uint32_t TextureFlags;
const TextureFlags NoFlags            = 0x0;
const TextureFlags UseNearestFilter   = 0x1;
const TextureFlags NeedsYFlip         = 0x2;
const TextureFlags ForceSingleTile    = 0x4;
const TextureFlags UseOpaqueSurface   = 0x8;
const TextureFlags AllowRepeat        = 0x10;
const TextureFlags NewTile            = 0x20;


/**
 * A view on a TextureHost where the texture is internally represented as tiles
 * (contrast with a tiled buffer, where each texture is a tile). For iteration by
 * the texture's buffer host.
 * This is only useful when the underlying surface is too big to fit in one
 * device texture, which forces us to split it in smaller parts.
 * Tiled Compositable is a different thing.
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
 *
 * This class is used on the compositor side.
 */
class TextureSource : public RefCounted<TextureSource>
{
public:
  TextureSource() {
    MOZ_COUNT_CTOR(TextureSource);
  };
  virtual ~TextureSource() {
    MOZ_COUNT_DTOR(TextureSource);
  };

  virtual gfx::IntSize GetSize() const = 0;
  /**
   * Cast to an TextureSource for the OpenGL backend.
   */
  virtual TextureSourceOGL* AsSourceOGL() { return nullptr; }
  /**
   * Cast to an TextureSource for the D3D11 backend.
   */
  virtual TextureSourceD3D11* AsSourceD3D11() { return nullptr; }
  /**
   * In some rare cases we currently need to consider a group of textures as one
   * TextureSource, that can be split in sub-TextureSources. 
   */
  virtual TextureSource* GetSubSource(int index) { return nullptr; }
  /**
   * Overload this if the TextureSource supports big textures that don't fit in
   * one device texture and must be tiled internally.
   */
  virtual TileIterator* AsTileIterator() { return nullptr; }
};

/**
 * Interface
 *
 * TextureHost is a thin abstraction over texture data that need to be shared
 * or transfered from the content process to the compositor process.
 * TextureHost only knows how to deserialize or synchronize generic image data
 * (SurfaceDescriptor) and provide access to one or more TextureSource objects
 * (these provide the necessary APIs for compositor backends to composite the
 * image).
 *
 * A TextureHost should mostly correspond to one or several SurfaceDescriptor
 * types. This means that for YCbCr planes, even though they are represented as
 * 3 textures internally, use 1 TextureHost and not 3, because the 3 planes
 * arrive in the same IPC message.
 *
 * The Lock/Unlock mecanism here mirrors Lock/Unlock in TextureClient. These two
 * methods don't always have to use blocking locks, unless a resource is shared
 * between the two sides (like shared texture handles). For instance, in some cases
 * the data received in Update(...) is a copy in shared memory of the data owned 
 * by the content process, in which case no blocking lock is required.
 *
 * All TextureHosts should provide access to at least one TextureSource
 * (see AsTextureSource()).
 *
 * The TextureHost class handles buffering and the necessary code for async
 * texture updates, and the internals of this should not be exposed to the
 * the different implementations of TextureHost (other than selecting the
 * right strategy at construction time).
 *
 * TextureHosts can be changed at any time, for example if we receive a
 * SurfaceDescriptor type that was not expected. This should be an incentive
 * to keep the ownership model simple (especially on the OpenGL case, where
 * we have additionnal constraints).
 *
 * The class TextureImageAsTextureHostOGL is a good example of a TextureHost
 * implementation.
 *
 * This class is used only on the compositor side.
 */
class TextureHost : public TextureSource
{
public:
  TextureHost(BufferMode aBufferMode = BUFFER_NONE,
              ISurfaceDeallocator* aDeAllocator = nullptr);
  virtual ~TextureHost();

  /**
   TODO[nical] comment
   * In most case there is one TextureSource per TextureHost, and CompositableHost
   * sometimes need to get a TextureSource from a TextureHost (for example to create
   * a mask effect). This is one of the sketchy pieces of the compositing architecture
   * that I hope to fix asap, maybe by enforcing one TextureSource per TextureHost and
   * replacing this by GetAsTextureSource().
   */
  virtual TextureSource* AsTextureSource() { return this; };

  virtual gfx::SurfaceFormat GetFormat() const { return mFormat; }

  virtual bool IsValid() const { return true; }

  /**
   * Update the texture host from a SurfaceDescriptor, aResult may contain the old
   * content of the texture, a pointer to the new image, or null. The
   * texture client should know what to expect
   * The BufferMode logic is implemented here rather than in the specialized classes
   */
  void Update(const SurfaceDescriptor& aImage,
              SurfaceDescriptor* aResult = nullptr,
              bool* aIsInitialised = nullptr,
              bool* aNeedsReset = nullptr,
              nsIntRegion *aRegion = nullptr);

  /**
   * Update for tiled texture hosts could probably have a better signature, but we
   * will replace it with PTexture stuff anyway, so nm.
   */
  virtual void Update(gfxReusableSurfaceWrapper* aReusableSurface, TextureFlags aFlags, const gfx::IntSize& aSize) {}

  /**
   * Lock the texture host for compositing, returns an effect that should
   * be used to composite this texture.
   */
  virtual bool Lock() { return true; }

  /**
   * Unlock the texture host after compositing
   */
  virtual void Unlock() {}

  /**
   * Leave the texture host in a sane state if we abandon an update part-way
   * through.
   */
  virtual void Abort() {}

  void SetFlags(TextureFlags aFlags) { mFlags = aFlags; }
  void AddFlag(TextureFlags aFlag) { mFlags |= aFlag; }
  TextureFlags GetFlags() { return mFlags; }

  virtual void CleanupResources() {}

  void SetDeAllocator(ISurfaceDeallocator* aDeAllocator)
  {
    mDeAllocator = aDeAllocator;
  }
  ISurfaceDeallocator* GetDeAllocator()
  {
    return mDeAllocator;
  }

#ifdef MOZ_DUMP_PAINTING
  virtual already_AddRefed<gfxImageSurface> Dump() { return nullptr; }
#endif

  bool operator== (const TextureHost& o) const
  {
    return GetIdentifier() == o.GetIdentifier();
  }
  bool operator!= (const TextureHost& o) const
  {
    return GetIdentifier() != o.GetIdentifier();
  }

  LayerRenderState GetRenderState()
  {
    return LayerRenderState(mBuffer,
                            mFlags & NeedsYFlip ? LAYER_RENDER_STATE_Y_FLIPPED : 0);
  }

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

  // BufferMode

  void SetBufferMode(BufferMode aBufferMode,
                     ISurfaceDeallocator* aDeAllocator = nullptr) {
    MOZ_ASSERT (aBufferMode == BUFFER_NONE || aDeAllocator);
    mBufferMode = aBufferMode;
    mDeAllocator = aDeAllocator;
  }

  bool IsBuffered() const { return mBufferMode == BUFFER_BUFFERED; }
  SurfaceDescriptor* GetBuffer() const { return mBuffer; }


  /**
   * Should be implemented by the backend-specific TextureHost classes 
   */
  virtual void UpdateImpl(const SurfaceDescriptor& aImage,
                          bool* aIsInitialised,
                          bool* aNeedsReset,
                          nsIntRegion *aRegion)
  {
    NS_RUNTIMEABORT("Should not be reached");
  }

  // An internal identifier for this texture host. Two texture hosts
  // should be considered equal iff their identifiers match. Should
  // not be exposed publicly.
  virtual uint64_t GetIdentifier() const {
    return reinterpret_cast<uint64_t>(this);
  }

  // Texture info
  TextureFlags mFlags;
  BufferMode mBufferMode;
  SurfaceDescriptor* mBuffer;
  gfx::SurfaceFormat mFormat;

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
class CompositingRenderTarget : public TextureSource
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
  virtual void SetTarget(gfxContext *aTarget) = 0;

  virtual void MakeCurrent(bool aForce = false) = 0;

  /**
   * Create a new texture host of a kind specified by aIdentifier
   */
  virtual TemporaryRef<TextureHost>
    CreateTextureHost(TextureHostType aMemoryType,
                      uint32_t aTextureFlags,
                      SurfaceDescriptorType aDescriptorType,
                      ISurfaceDeallocator* aDeAllocator) = 0;

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

protected:
  uint32_t mCompositorID;
};

class CompositingFactory
{
public:
  /**
   * The Create*Client methods each create, configure, and return a new compositable
   * client. If necessary, a message will be sent to the compositor
   * to create a corresponding compositable host.
   */
  static TemporaryRef<ImageClient> CreateImageClient(LayersBackend aBackendType,
                                                     CompositableType aImageHostType,
                                                     CompositableForwarder* aFwd,
                                                     TextureFlags aFlags);
  static TemporaryRef<CanvasClient> CreateCanvasClient(LayersBackend aBackendType,
                                                       CompositableType aImageHostType,
                                                       CompositableForwarder* aFwd,
                                                       TextureFlags aFlags);
  static TemporaryRef<ContentClient> CreateContentClient(LayersBackend aBackendType,
                                                         CompositableType aImageHostType,
                                                         CompositableForwarder* aFwd,
                                                         TextureFlags aFlags);

  static CompositableType TypeForImage(Image* aImage);
};

}
}

#endif /* MOZILLA_GFX_COMPOSITOR_H */
