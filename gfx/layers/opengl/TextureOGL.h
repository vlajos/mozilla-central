/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */
 
#ifndef MOZILLA_GFX_TEXTUREOGL_H
#define MOZILLA_GFX_TEXTUREOGL_H
 
#include "ImageLayerOGL.h"
#include "mozilla/layers/CompositorOGL.h"
#include "GLContextTypes.h"
#include "gfx2DGlue.h"
#include "mozilla/layers/Effects.h"
#include "gfxReusableSurfaceWrapper.h"
#include "TiledLayerBuffer.h" // for TILEDLAYERBUFFER_TILE_SIZE

namespace mozilla {
namespace layers {

/*
 * TextureHost implementations for the OpenGL backend.
 * Note that it is important to careful about the ownership model with
 * the OpenGL backend, due to some widget limitation on Linux: before
 * the nsBaseWidget associated to our OpenGL context has been completely 
 * deleted, every resource belonging to the OpenGL context MUST have been
 * released. At the moment the teardown sequence happens in the middle of 
 * the nsBaseWidget's destructor, meaning that a givent moment we must be
 * able to easily find and release all the GL resources.
 * The point is: be careful about the ownership model and limit the number 
 * of objects sharing references to GL resources to make the tear down 
 * sequence as simple as possible. 
 */

/**
 * Interface.
 * TextureSourceOGL provides the necessary API for CompositorOGL to composite
 * a TextureSource.
 */
class TextureSourceOGL
{
public:
  virtual bool IsValid() const = 0;
  virtual void BindTexture(GLenum aTextureUnit) = 0;
  virtual gfx::IntSize GetSize() const = 0;
  virtual gl::ShaderProgramType GetShaderProgram() const {
    MOZ_NOT_REACHED("unhandled shader type");
  }
  virtual GLenum GetTextureTarget() const { return LOCAL_GL_TEXTURE_2D; }
  virtual GLenum GetWrapMode() const { return LOCAL_GL_REPEAT; }
};

// we actually don't need this class anymore
class TextureHostOGL : public TextureHost
                     , public TextureSourceOGL
{
public:
  TextureHostOGL(ISurfaceAllocator* aDeAllocator = nullptr)
  : TextureHost(aDeAllocator)
  {}

  TextureSourceOGL* AsSourceOGL() MOZ_OVERRIDE {
    return this;
  }
};

inline gl::ShaderProgramType
GetProgramTypeForTexture(const TextureHost *aTextureHost)
{
  switch (aTextureHost->GetFormat()) {
  case gfx::FORMAT_B8G8R8A8:
    return gl::BGRALayerProgramType;;
  case gfx::FORMAT_B8G8R8X8:
    return gl::BGRXLayerProgramType;;
  case gfx::FORMAT_R8G8B8X8:
    return gl::RGBXLayerProgramType;;
  case gfx::FORMAT_R8G8B8A8:
    return gl::RGBALayerProgramType;;
  default:
    MOZ_NOT_REACHED("unhandled program type");
  }
}

/**
 * TextureHost implementation using a TextureImage as the underlying texture.
 */
class TextureImageTextureHostOGL : public TextureHost
                                   , public TextureSourceOGL
                                   , public TileIterator
{
public:
  TextureImageTextureHostOGL(gl::GLContext* aGL,
                               gl::TextureImage* aTexImage = nullptr,
                               ISurfaceAllocator* aDeallocator = nullptr)
  : TextureHost(aDeallocator), mTexture(aTexImage), mGL(aGL)
  {
    MOZ_COUNT_CTOR(TextureImageTextureHostOGL);
  }

  TextureSourceOGL* AsSourceOGL() MOZ_OVERRIDE
  {
    return this;
  }

  ~TextureImageTextureHostOGL()
  {
    MOZ_COUNT_DTOR(TextureImageTextureHostOGL);
  }

  // TextureHost

  void UpdateImpl(const SurfaceDescriptor& aImage,
                  bool* aIsInitialised = nullptr,
                  bool* aNeedsReset = nullptr,
                  nsIntRegion* aRegion = nullptr);

  bool IsValid() const MOZ_OVERRIDE
  {
    return !!mTexture;
  }

  virtual bool Lock() MOZ_OVERRIDE;

  void Abort() MOZ_OVERRIDE;

  TextureSource* AsTextureSource() MOZ_OVERRIDE
  {
    return this;
  }

  // textureSource
  void BindTexture(GLenum aTextureUnit) MOZ_OVERRIDE
  {
    mTexture->BindTexture(aTextureUnit);
  }

  gfx::IntSize GetSize() const MOZ_OVERRIDE
  {
    if (mTexture) {
      return gfx::IntSize(mTexture->GetSize().width, mTexture->GetSize().height);
    }
    return gfx::IntSize(0, 0);
  }

  gl::ShaderProgramType GetShaderProgram() const MOZ_OVERRIDE
  {
    return GetProgramTypeForTexture(this);
  }

  GLenum GetWrapMode() const MOZ_OVERRIDE
  {
    return mTexture->GetWrapMode();
  }

  gl::TextureImage* GetTextureImage()
  {
    return mTexture;
  }
  void SetTextureImage(gl::TextureImage* aImage)
  {
    mTexture = aImage;
  }

  // TileIterator

  TileIterator* AsTileIterator() MOZ_OVERRIDE
  {
    return this;
  }

  void BeginTileIteration() MOZ_OVERRIDE
  {
    mTexture->BeginTileIteration();
  }

  nsIntRect GetTileRect() MOZ_OVERRIDE
  {
    return mTexture->GetTileRect();
  }
  
  size_t GetTileCount() MOZ_OVERRIDE
  {
    return mTexture->GetTileCount();
  }

  bool NextTile() MOZ_OVERRIDE
  {
    return mTexture->NextTile();
  }

#ifdef MOZ_LAYERS_HAVE_LOG
  virtual const char* Name() { return "TextureImageTextureHostOGL"; }
#endif

protected:
  RefPtr<gl::TextureImage> mTexture;
  gl::GLContext* mGL;
};


/**
 * TextureHost implementation for YCbCr images in the OpenGL backend.
 *
 * This TextureHost is a little bit particular in that it implements
 * the TextureSource interface, as it is required that a TextureHost
 * provides access to a TextureSource, but does not implement the
 * TextureHostOGL interface. Instead it contains 3 channels (one per
 * plane) that implement the TextureSourceOGL interface, and 
 * YCbCrTextureHostOGL's TextureSource implementation provide access
 * to these channels with the GetSubSource method.
 */
class YCbCrTextureHostOGL : public TextureHost
{
public:
  YCbCrTextureHostOGL(gl::GLContext* aGL) : mGL(aGL)
  {
    MOZ_COUNT_CTOR(YCbCrTextureHostOGL);
    mYTexture  = new Channel;
    mCbTexture = new Channel;
    mCrTexture = new Channel;
    mFormat = gfx::FORMAT_YUV;
  }

  ~YCbCrTextureHostOGL()
  {
    MOZ_COUNT_DTOR(YCbCrTextureHostOGL);
  }

  virtual void UpdateImpl(const SurfaceDescriptor& aImage,
                          bool* aIsInitialised = nullptr,
                          bool* aNeedsReset = nullptr,
                          nsIntRegion* aRegion = nullptr) MOZ_OVERRIDE;

  virtual bool Lock() MOZ_OVERRIDE;

  TextureSource* AsTextureSource() MOZ_OVERRIDE
  {
    return this;
  }

  struct Channel : public TextureSourceOGL
                 , public TextureSource
  {
    TextureSourceOGL* AsSourceOGL() MOZ_OVERRIDE
    {
      return this;
    }
    RefPtr<gl::TextureImage> mTexImage;

    void BindTexture(GLenum aUnit) MOZ_OVERRIDE
    {
      mTexImage->BindTexture(aUnit);
    }
    virtual bool IsValid() const MOZ_OVERRIDE
    {
      return !!mTexImage;
    }
    virtual gfx::IntSize GetSize() const MOZ_OVERRIDE
    {
      return gfx::IntSize(mTexImage->GetSize().width, mTexImage->GetSize().height);
    }
    virtual GLenum GetWrapMode() const MOZ_OVERRIDE
    {
      return mTexImage->GetWrapMode();
    }

  };

  // TextureSource implementation

  TextureSource* GetSubSource(int index) MOZ_OVERRIDE
  {
    switch (index) {
      case 0 : return mYTexture.get();
      case 1 : return mCbTexture.get();
      case 2 : return mCrTexture.get();
    }
    return nullptr;
  }

  gfx::IntSize GetSize() const
  {
    if (!mYTexture->mTexImage) {
      NS_WARNING("YCbCrTextureHost::GetSize called but no data has been set yet");
      return gfx::IntSize(0,0);
    }
    return mYTexture->GetSize();
  }

#ifdef MOZ_LAYERS_HAVE_LOG
  virtual const char* Name() { return "YCbCrTextureHostOGL"; }
#endif

private:
  RefPtr<Channel> mYTexture;
  RefPtr<Channel> mCbTexture;
  RefPtr<Channel> mCrTexture;
  gl::GLContext* mGL;
};

class SharedTextureHostOGL : public TextureHostOGL
{
public:
  typedef gfxASurface::gfxContentType ContentType;
  typedef mozilla::gl::GLContext GLContext;
  typedef mozilla::gl::TextureImage TextureImage;

  virtual ~SharedTextureHostOGL()
  {
    mGL->MakeCurrent();
    if (mSharedHandle) {
      mGL->ReleaseSharedHandle(mShareType, mSharedHandle);
    }
    if (mTextureHandle) {
      mGL->fDeleteTextures(1, &mTextureHandle);
    }
  }
  virtual GLuint GetTextureHandle()
  {
    return mTextureHandle;
  }

  TextureSource* AsTextureSource() MOZ_OVERRIDE {
    return this;
  }

  bool IsValid() const MOZ_OVERRIDE { return GetFormat() != gfx::FORMAT_UNKNOWN; }

  // override from TextureHost, we support both buffered
  // and unbuffered operation.
  virtual void UpdateImpl(const SurfaceDescriptor& aImage,
                          bool* aIsInitialised = nullptr,
                          bool* aNeedsReset = nullptr,
                          nsIntRegion* aRegion = nullptr);
  virtual void SwapTexturesImpl(const SurfaceDescriptor& aImage,
                                bool* aIsInitialised = nullptr,
                                bool* aNeedsReset = nullptr,
                                nsIntRegion* aRegion = nullptr);
  virtual bool Lock();
  virtual void Unlock();

  virtual GLenum GetWrapMode() const {
    return mWrapMode;
  }
  virtual void SetWrapMode(GLenum aMode) {
    mWrapMode = aMode;
  }

  gl::ShaderProgramType GetShaderProgram() const MOZ_OVERRIDE
  {
    return mShaderProgram;
  }

  gfx::IntSize GetSize() const {
    return mSize;
  }

  virtual GLenum GetTextureTarget() const MOZ_OVERRIDE
  {
    return mTextureTarget;
  }

  void BindTexture(GLenum activetex) {
    mGL->fActiveTexture(activetex);
    mGL->fBindTexture(mTextureTarget, mTextureHandle);
  }
  void ReleaseTexture() {
  }
  GLuint GetTextureID() { return mTextureHandle; }
  ContentType GetContentType() {
    return (mFormat == gfx::FORMAT_B8G8R8A8) ?
             gfxASurface::CONTENT_COLOR_ALPHA :
             gfxASurface::CONTENT_COLOR;
  }

#ifdef MOZ_LAYERS_HAVE_LOG
  virtual const char* Name() { return "SharedTextureHostOGL"; }
#endif

  SharedTextureHostOGL(GLContext* aGL,
                       ISurfaceAllocator* aDeAllocator = nullptr)

  : TextureHostOGL(aDeAllocator)
  , mGL(aGL)
  , mTextureHandle(0)
  , mWrapMode(LOCAL_GL_CLAMP_TO_EDGE)
  , mSharedHandle(0)
  , mShareType(GLContext::SameProcess)
  {
  }

protected:

  gfx::IntSize mSize;
  nsRefPtr<gl::GLContext> mGL;
  GLuint mTextureHandle;
  GLenum mWrapMode;
  GLenum mTextureTarget;
  gl::SharedTextureHandle mSharedHandle;
  gl::ShaderProgramType mShaderProgram;
  gl::GLContext::SharedTextureShareType mShareType;
};

class TiledTextureHostOGL : public TextureHostOGL
{
public:
  TiledTextureHostOGL(gl::GLContext* aGL) 
    : mTextureHandle(0)
    , mGL(aGL)
  {}
  ~TiledTextureHostOGL();

  // have to pass the size in here (every time) because of DrawQuad API :-(
  virtual void Update(gfxReusableSurfaceWrapper* aReusableSurface, TextureFlags aFlags, const gfx::IntSize& aSize) MOZ_OVERRIDE;
  virtual bool Lock() MOZ_OVERRIDE;
  virtual void Unlock() MOZ_OVERRIDE {}

  virtual TextureSource* AsTextureSource() MOZ_OVERRIDE { return this; }
  virtual bool IsValid() const MOZ_OVERRIDE { return true; }
  virtual void BindTexture(GLenum aTextureUnit) MOZ_OVERRIDE
  {
    mGL->fActiveTexture(aTextureUnit);
    mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mTextureHandle);
  }
  virtual gfx::IntSize GetSize() const MOZ_OVERRIDE
  {
    return mSize;
  }

  gl::ShaderProgramType GetShaderProgram() const MOZ_OVERRIDE
  {
    return GetProgramTypeForTexture(this);
  }

#ifdef MOZ_LAYERS_HAVE_LOG
  virtual const char* Name() { return "TiledTextureHostOGL"; }
#endif

protected:
  virtual uint64_t GetIdentifier() const MOZ_OVERRIDE {
    return static_cast<uint64_t>(mTextureHandle);
  }

private:
  GLenum GetTileType()
  {
    // Deduce the type that was assigned in GetFormatAndTileForImageFormat
    return mGLFormat == LOCAL_GL_RGB ? LOCAL_GL_UNSIGNED_SHORT_5_6_5 : LOCAL_GL_UNSIGNED_BYTE;
  }

  gfx::IntSize mSize;
  GLuint mTextureHandle;
  GLenum mGLFormat;
  gl::GLContext* mGL;
};


} // namespace
} // namespace
 
#endif /* MOZILLA_GFX_TEXTUREOGL_H */
