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
class PlanarYCbCrImage;


/**
 * This class allows texture clients to draw into textures through Azure or
 * thebes and applies locking semantics to allow GPU or CPU level
 * synchronization.
 * TextureClient's purpose is for the texture data to be
 * forwarded to the right place on the compositor side and with correct locking
 * semantics.
 *
 * When modifying a TextureClient's data, first call LockDescriptor, modify the
 * data in the descriptor, and then call Unlock. This makes sure that if the data
 * is shared with the compositor, the later will not try to read while the data is
 * being modified (on the other side, TextureHost also has Lock/Unlock semantic).
 * after unlocking, call Updated in order to add the modification to the current
 * layer transaction.
 * Depending on whether the data is shared or copied, Lock/Unlock and Updated can be
 * no-ops. What's important is that the Client/Host pair implement the same semantic.
 *
 * TextureClient owns the data in its surface descriptor and is responsible for
 * releasing it appropriately.
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
  
  /**
   * The Lock* methods lock the texture client for drawing into, providing some 
   * object that can be used for drawing to. Once the user is finished
   * with the object it should call Unlock.
   */
  // XXX[nical] these will be removed
  virtual gfxImageSurface* LockImageSurface() { return nullptr; }
  virtual gfxASurface* LockSurface() { return nullptr; }
  virtual SharedTextureHandle LockHandle(GLContext* aGL, GLContext::SharedTextureShareType aFlags) { return 0; }
  // XXX[nical] only this one should remain (and be called just "Lock")
  virtual SurfaceDescriptor* LockSurfaceDescriptor() { return &mDescriptor; }
  virtual void ReleaseResources() {}
  /**
   * This unlocks the current DrawableTexture and allows the host to composite
   * it directly.
   */
  virtual void Unlock() {}

  // ensure that the texture client is suitable for the given size and content type
  // and that any initialisation has taken place
  virtual void EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aType) = 0;

  /**
   * _Only_ used at the end of the layer transaction when receiving a reply from the compositor.
   */
  virtual void SetDescriptor(const SurfaceDescriptor& aDescriptor)
  {
    mDescriptor = aDescriptor;
  }

  /**
   * Adds this TextureClient's data to the current layer transaction.
   */
  virtual void Updated();
  virtual void UpdatedRegion(const nsIntRegion& aUpdatedRegion,
                             const nsIntRect& aBufferRect,
                             const nsIntPoint& aBufferRotation);
  virtual void Destroyed();

  void SetIPDLActor(PTextureChild* aTextureChild);
  PTextureChild* GetIPDLActor() const {
    return mTextureChild;
  }

  CompositableForwarder* GetLayerForwarder() const {
    return mLayerForwarder;
  }

  void SetFlags(TextureFlags aFlags)
  {
    mTextureInfo.textureFlags = aFlags;
  }

protected:
  TextureClient(CompositableForwarder* aForwarder, CompositableType aCompositableType);

  CompositableForwarder* mLayerForwarder;
  // So far all TextureClients use a SurfaceDescriptor, so it makes sense to keep
  // the reference here.
  SurfaceDescriptor mDescriptor;
  TextureInfo mTextureInfo;
  PTextureChild* mTextureChild;
};


/*
 * The logic of converting input image data into a Surface descriptor should be
 * outside of TextureClient. For Image layers we implement them in the AutoLock*
 * idiom so that the states need for the purpose of convertion only exist within
 * the conversion operation, and to avoid adding special interfaces in 
 * TextureClient tht are only used in one place and not implemented everywhere.
 * We should do this for all the input data type.
 */

/**
 * Base class for AutoLock*Client.
 * handles lock/unlock
 */
class AutoLockTextureClient
{
public:
  AutoLockTextureClient(TextureClient* aTexture) {
    mTextureClient = aTexture;
    mDescriptor = aTexture->LockSurfaceDescriptor();
  }

  virtual ~AutoLockTextureClient() {
    mTextureClient->Unlock();
  }
protected:
  TextureClient* mTextureClient;
  SurfaceDescriptor* mDescriptor;
};

/**
 * Writes the content of a PlanarYCbCrImage into a SurfaceDescriptor.
 */
class AutoLockYCbCrClient : public AutoLockTextureClient
{
public:
  AutoLockYCbCrClient(TextureClient* aTexture) : AutoLockTextureClient(aTexture) {}
  bool Update(PlanarYCbCrImage* aImage);
protected:
  bool EnsureTextureClient(PlanarYCbCrImage* aImage);
};

/**
 * Writes the content of a gfxASurface into a SurfaceDescriptor.
 */
class AutoLockShmemClient : public AutoLockTextureClient
{
public:
  AutoLockShmemClient(TextureClient* aTexture) : AutoLockTextureClient(aTexture) {}
  bool Update(Image* aImage, uint32_t aContentFlags, gfxASurface* surface);
protected:
  bool EnsureTextureClient(nsIntSize aSize,
                           gfxASurface* surface,
                           gfxASurface::gfxContentType contentType);
};

/**
 * Writes a texture handle into a SurfaceDescriptor.
 */
class AutoLockHandleClient : public AutoLockTextureClient
{
public:
  AutoLockHandleClient(TextureClient* aClient,
                       gl::GLContext* aGL,
                       gfx::IntSize aSize,
                       gl::GLContext::SharedTextureShareType aFlags);
  gl::SharedTextureHandle GetHandle() { return mHandle; }
protected:
  gl::SharedTextureHandle mHandle;
};




class TextureClientShmem : public TextureClient
{
public:
  virtual ~TextureClientShmem();
  TextureClientShmem(CompositableForwarder* aForwarder, CompositableType aCompositableType);

  virtual already_AddRefed<gfxContext> LockContext();
  virtual gfxImageSurface* LockImageSurface();
  virtual gfxASurface* LockSurface() { return GetSurface(); }
  virtual void Unlock();
  virtual void EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aType);

private:
  gfxASurface* GetSurface();

  nsRefPtr<gfxASurface> mSurface;
  nsRefPtr<gfxImageSurface> mSurfaceAsImage;

  gfxASurface::gfxContentType mContentType;
  gfx::IntSize mSize;

  friend class CompositingFactory;
};

class TextureClientShmemYCbCr : public TextureClient
{
public:
  void EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aType) MOZ_OVERRIDE;
};

// this class is just a place holder really
class TextureClientShared : public TextureClient
{
public:
  virtual ~TextureClientShared();

  TextureClientShared(CompositableForwarder* aForwarder, CompositableType aCompositableType)
    : TextureClient(aForwarder, aCompositableType)
  {
  }

  virtual void EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aType) {}

protected:
};

class TextureClientSharedGL : public TextureClientShared
{
public:
  virtual ~TextureClientSharedGL();
  virtual void EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aType);
  virtual gl::SharedTextureHandle LockHandle(GLContext* aGL, gl::GLContext::SharedTextureShareType aFlags);
  virtual void Unlock();

  TextureClientSharedGL(CompositableForwarder* aForwarder, CompositableType aCompositableType);
protected:

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

  TextureClientBridge(CompositableForwarder* aForwarder, CompositableType aCompositableType);
};

class TextureClientTile : public TextureClient
{
public:
  TextureClientTile(const TextureClientTile& aOther)
    : TextureClient(mLayerForwarder, mTextureInfo.compositableType)
    , mSurface(aOther.mSurface)
  {}

  virtual void EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aType) MOZ_OVERRIDE;

  virtual gfxImageSurface* LockImageSurface() MOZ_OVERRIDE;

  gfxReusableSurfaceWrapper* GetReusableSurfaceWrapper()
  {
    return mSurface;
  }

  virtual void SetDescriptor(const SurfaceDescriptor& aDescriptor) MOZ_OVERRIDE
  {
    MOZ_ASSERT(false, "Tiled texture clients don't use SurfaceDescriptors.");
  }

  TextureClientTile(CompositableForwarder* aForwarder, CompositableType aCompositableType)
    : TextureClient(aForwarder, aCompositableType)
    , mSurface(nullptr)
  {
    mTextureInfo.textureHostFlags = TEXTURE_HOST_TILED;
  }
private:
  nsRefPtr<gfxReusableSurfaceWrapper> mSurface;

  friend class CompositingFactory;
};

}
}
#endif
