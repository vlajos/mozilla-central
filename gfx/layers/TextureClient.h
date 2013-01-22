/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_TEXTURECLIENT_H
#define MOZILLA_GFX_TEXTURECLIENT_H

#include "mozilla/layers/LayersSurfaces.h"
#include "gfxASurface.h"
#include "mozilla/layers/Compositor.h"
#include "mozilla/layers/ShadowLayers.h"
#include "mozilla/layers/TextureFactoryIdentifier.h" // for TextureInfo
#include "GLContext.h"
#include "gfxReusableSurfaceWrapper.h"

namespace mozilla {

namespace gl {
  class GLContext;
}

namespace layers {

class TextureChild;
class ContentClient;

// this may repalce TextureClient (or get removed)
class AwesomeTextureClient {
public:
  AwesomeTextureClient() {
    MOZ_COUNT_CTOR(AwesomeTextureClient);
  }

  virtual ~AwesomeTextureClient() {
    MOZ_ASSERT(!IsLocked());
    MOZ_COUNT_DTOR(AwesomeTextureClient);
  }

  virtual SurfaceDescriptor* Lock() {
    return &mData;
  }

  virtual void Unlock() {};

  virtual bool IsLocked() {
    return false;
  };

  void AddToTransaction(ShadowLayerForwarder* aFwd);

  virtual void Set(const SurfaceDescriptor& aImage) {
    if (mData.type() != SurfaceDescriptor::T__None) {
      ReleaseResources();
    }
    mData = aImage;
  }

  virtual bool Allocate(SurfaceDescriptor::Type aType, gfx::IntSize aSize);

  virtual void ReleaseResources();

  void SetTextureChild(TextureChild* aChild) {
    mTextureChild = aChild;
  }
  TextureChild* GetTextureChild() const {
    return mTextureChild;
  }

protected:
  SurfaceDescriptor mData;
  //TextureInfo mTextureInfo;
  TextureChild* mTextureChild;
};

/* This class allows texture clients to draw into textures through Azure or
 * thebes and applies locking semantics to allow GPU or CPU level
 * synchronization.
 */
class TextureClient : public RefCounted<TextureClient>
{
public:
  typedef gl::SharedTextureHandle SharedTextureHandle;
  typedef gl::GLContext GLContext;
  typedef gl::TextureImage TextureImage;

  virtual ~TextureClient();

  /* This will return an identifier that can be sent accross a process or
   * thread boundary and used to construct a TextureHost object
   * which can then be used as a texture for rendering by a compatible
   * compositor. This texture should have been created with the
   * TextureHostIdentifier specified by the compositor that this identifier
   * is to be used with.
   */
  virtual const TextureInfo& GetTextureInfo() const
  {
    return mTextureInfo;
  }
  
  void SetAsyncContainerID(uint64_t aDescriptor);

  /**
   * The Lock* methods lock the texture client for drawing into, providing some 
   * object that can be used for drawing to. Once the user is finished
   * with the object it should call Unlock.
   */
  virtual already_AddRefed<gfxContext> LockContext()  { return nullptr; }
  virtual TemporaryRef<gfx::DrawTarget> LockDT() { return nullptr; } 
  virtual gfxImageSurface* LockImageSurface() { return nullptr; }
  virtual gfxASurface* LockSurface() { return nullptr; }
  virtual SharedTextureHandle LockHandle(GLContext* aGL, TextureImage::TextureShareType aFlags) { return 0; }
  virtual SurfaceDescriptor* LockSurfaceDescriptor() { return &mDescriptor; }

  /**
   * This unlocks the current DrawableTexture and allows the host to composite
   * it directly.
   */
  virtual void Unlock() {}

  // ensure that the texture client is suitable for the given size and content type
  // and that any initialisation has taken place
  virtual void EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aType) = 0;

  void SetDescriptor(const SurfaceDescriptor& aDescriptor)
  {
    mDescriptor = aDescriptor;
  }

  virtual void Updated(ShadowableLayer* aLayer);
  virtual void UpdatedRegion(const nsIntRegion& aUpdatedRegion,
                             const nsIntRect& aBufferRect,
                             const nsIntPoint& aBufferRotation);
  virtual void Destroyed(ShadowableLayer* aLayer);

  void SetTextureChild(PTextureChild* aTextureChild) {
    mTextureChild = aTextureChild;
  }
  PTextureChild* GetTextureChild() const {
    return mTextureChild;
  }
protected:
  TextureClient(ShadowLayerForwarder* aLayerForwarder, BufferType aBufferType);

  ShadowLayerForwarder* mLayerForwarder;
  SurfaceDescriptor mDescriptor;
  TextureInfo mTextureInfo;
  PTextureChild* mTextureChild;
};

class TextureClientShmem : public TextureClient
{
public:
  virtual ~TextureClientShmem();

  virtual already_AddRefed<gfxContext> LockContext();
  virtual gfxImageSurface* LockImageSurface();
  virtual gfxASurface* LockSurface() { return GetSurface(); }
  virtual void Unlock();
  virtual void EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aType);

private:
  TextureClientShmem(ShadowLayerForwarder* aLayerForwarder, BufferType aBufferType);

  gfxASurface* GetSurface();

  nsRefPtr<gfxASurface> mSurface;
  nsRefPtr<gfxImageSurface> mSurfaceAsImage;

  gfxASurface::gfxContentType mContentType;
  gfx::IntSize mSize;

  friend class CompositingFactory;
};

// this class is just a place holder really
class TextureClientShared : public TextureClient
{
public:
  virtual ~TextureClientShared();

  virtual void EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aType) {}

protected:
  TextureClientShared(ShadowLayerForwarder* aLayerForwarder, BufferType aBufferType)
    : TextureClient(aLayerForwarder, aBufferType)
  {
    mTextureInfo.memoryType = TEXTURE_SHARED;
  }

  friend class CompositingFactory;
};

class TextureClientSharedGL : public TextureClientShared
{
public:
  virtual ~TextureClientSharedGL();
  virtual void EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aType);
  virtual gl::SharedTextureHandle LockHandle(GLContext* aGL, gl::TextureImage::TextureShareType aFlags);
  virtual void Unlock();

protected:
  TextureClientSharedGL(ShadowLayerForwarder* aLayerForwarder, BufferType aBufferType);

  gl::GLContext* mGL;
  gfx::IntSize mSize;

  friend class CompositingFactory;
};

// there is no corresponding texture host for ImageBridge clients
// we only use the texture client to update the host
class TextureClientBridge : public TextureClient
{
public:
  // always ok
  virtual void EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aType) {}

protected:
  TextureClientBridge(ShadowLayerForwarder* aLayerForwarder, BufferType aBufferType);

  friend class CompositingFactory;
};

struct BasicTiledLayerTile;

class TextureClientTile : public TextureClient
{
public:
  virtual void EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aType);

  virtual gfxImageSurface* LockImageSurface();

private:
  TextureClientTile(ShadowLayerForwarder* aLayerForwarder, BufferType aBufferType)
    : TextureClient(aLayerForwarder, aBufferType)
    , mSurface(nullptr)
  {}

  nsRefPtr<gfxReusableSurfaceWrapper> mSurface;

  friend class CompositingFactory;
  friend struct BasicTiledLayerTile;
};

}
}
#endif
