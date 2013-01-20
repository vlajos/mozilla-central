/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */
 
#ifndef MOZILLA_GFX_TEXTUREOGL_H
#define MOZILLA_GFX_TEXTUREOGL_H
 
#include "ImageLayerOGL.h"
#include "mozilla/layers/CompositorOGL.h"
#include "GLContext.h"
#include "gfx2DGlue.h"
#include "mozilla/layers/Effects.h"
#include "gfxReusableSurfaceWrapper.h"
#include "TiledLayerBuffer.h" // for TILEDLAYERBUFFER_TILE_SIZE

namespace mozilla {
namespace layers {

// we actually don't need this class anymore
class TextureHostOGL : public TextureHost
{
public:
  TextureHostOGL(BufferMode aBufferMode = BUFFER_NONE)
  : TextureHost(aBufferMode)
  {}

  //TODO[nrc] each TextureHost should implment tis properly
  virtual LayerRenderState GetRenderState()
  {
    NS_WARNING("TextureHost::GetRenderState should be overriden");
    return LayerRenderState();
  }
};

// Not sure if this is true any more !!!TODO[nrc]TODO[nical] everytime we create an effect we do |new SimpleTextureSourceOGL|
// does that leak? In any case it is horribly inefficent, we should cache the texture source
// or preferably just make most hosts also sources.
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
  virtual GLenum GetWrapMode() const = 0;
};

class TextureImageAsTextureHostOGL : public TextureHost
                                   , public TextureSource
                                   , public TextureSourceOGL
                                   , public TileIterator
{
public:
  TextureImageAsTextureHostOGL(gl::GLContext* aGL,
                               gl::TextureImage* aTexImage = nullptr,
                               BufferMode aBufferMode = BUFFER_NONE,
                               ISurfaceDeallocator* aDeallocator = nullptr)
  : TextureHost(aBufferMode, aDeallocator), mTexture(aTexImage), mGL(aGL)
  {
    MOZ_COUNT_CTOR(TextureImageAsTextureHostOGL);
  }

  TextureSourceOGL* AsSourceOGL() MOZ_OVERRIDE {
    return this;
  }

  ~TextureImageAsTextureHostOGL() {
    MOZ_COUNT_DTOR(TextureImageAsTextureHostOGL);
  }

  // TextureHost

  void UpdateImpl(const SurfaceDescriptor& aImage,
                  bool* aIsInitialised = nullptr,
                  bool* aNeedsReset = nullptr);
  void UpdateRegionImpl(gfxASurface* aSurface, nsIntRegion& aRegion);

  bool IsValid() const MOZ_OVERRIDE {
    return !!mTexture;
  }

  LayerRenderState GetRenderState() MOZ_OVERRIDE {
    return LayerRenderState(); // TODO
  }

  Effect* Lock(const gfx::Filter& aFilter) MOZ_OVERRIDE;

  void Abort() MOZ_OVERRIDE;

  TextureSource* AsTextureSource() MOZ_OVERRIDE {
    return this;
  }

  // textureSource

  void BindTexture(GLenum aTextureUnit) MOZ_OVERRIDE;

  gfx::IntSize GetSize() const MOZ_OVERRIDE {
    return mSize;
  }

  GLenum GetWrapMode() const MOZ_OVERRIDE {
    return mTexture->GetWrapMode();
  }

  gl::TextureImage* GetTextureImage() {
    return mTexture;
  }
  void SetTextureImage(gl::TextureImage* aImage) {
    mTexture = aImage;
  }

  // TileIterator

  TileIterator* AsTileIterator() MOZ_OVERRIDE {
    return this;
  }

  void BeginTileIteration() MOZ_OVERRIDE {
    mTexture->BeginTileIteration();
  }

  nsIntRect GetTileRect() MOZ_OVERRIDE {
    return mTexture->GetTileRect();
  }
  
  size_t GetTileCount() MOZ_OVERRIDE {
    return mTexture->GetTileCount();
  }

  bool NextTile() MOZ_OVERRIDE {
    return mTexture->NextTile();
  }

protected:
  RefPtr<gl::TextureImage> mTexture;
  gl::GLContext* mGL;
  gfx::IntSize mSize;
};



class YCbCrTextureHostOGL : public TextureHostOGL, public TextureSource
{
public:
  YCbCrTextureHostOGL(gl::GLContext* aGL) : mGL(aGL) {
    MOZ_COUNT_CTOR(YCbCrTextureHostOGL);
  }

  ~YCbCrTextureHostOGL() {
    MOZ_COUNT_DTOR(YCbCrTextureHostOGL);
  }

  virtual void UpdateImpl(const SurfaceDescriptor& aImage,
                          bool* aIsInitialised = nullptr,
                          bool* aNeedsReset = nullptr) MOZ_OVERRIDE;

  virtual void UpdateRegionImpl(gfxASurface* aSurface, nsIntRegion& aRegion) {
    NS_RUNTIMEABORT("should not be called");
  }

  Effect* Lock(const gfx::Filter& aFilter) MOZ_OVERRIDE;

  TextureSource* AsTextureSource() MOZ_OVERRIDE {
    NS_WARNING("YCbCrTextureHostOGL does not have a primary TextureSource.");
    return nullptr;
  }

  struct Channel : public TextureSourceOGL
                 , public TextureSource {
    TextureSourceOGL* AsSourceOGL() MOZ_OVERRIDE {
      return this;
    }
    RefPtr<gl::TextureImage> mTexImage;

    void BindTexture(GLenum aUnit) MOZ_OVERRIDE {
      mTexImage->BindTexture(aUnit);
    }
    virtual bool IsValid() const MOZ_OVERRIDE {
      return !!mTexImage;
    }
    virtual gfx::IntSize GetSize() const MOZ_OVERRIDE {
      return gfx::IntSize(mTexImage->GetSize().width, mTexImage->GetSize().height);
    }
    virtual GLenum GetWrapMode() const MOZ_OVERRIDE {
      return mTexImage->GetWrapMode();
    }

  };

  // TextureSource implementation

  TextureSource* GetSubSource(int index) MOZ_OVERRIDE {
    switch (index) {
      case 0 : return &mYTexture;
      case 1 : return &mCbTexture;
      case 2 : return &mCrTexture;
    }
    return nullptr;
  }

  gfx::IntSize GetSize() const {
    if (!mYTexture.mTexImage) {
      NS_WARNING("YCbCrTextureHost::GetSize called but no data has been set yet");
      return gfx::IntSize(0,0);
    }
    return mYTexture.GetSize();
  }

private:
  Channel mYTexture;
  Channel mCbTexture;
  Channel mCrTexture;
  gl::GLContext* mGL;
};



// TODO ----- code below this line needs to be (re)implemented/fixed -----




// TODO[nical] this class isn't implemented/usable
class SharedTextureAsTextureHostOGL : public TextureHostOGL
{
public:
  SharedTextureAsTextureHostOGL(gl::GLContext* aGL)
  {
    // TODO[nical]
    //mSharedTexture = new SharedTextureWrapper;
    mGL = aGL;
  }

  virtual ~SharedTextureAsTextureHostOGL() {
    mGL->MakeCurrent();
    mGL->ReleaseSharedHandle(mShareType,
                                             mSharedHandle);
    if (mTextureHandle) {
      mGL->fDeleteTextures(1, &mTextureHandle);
    }
  }

  virtual GLuint GetTextureHandle() {
    return mTextureHandle;
  }

  virtual void UpdateImpl(const SurfaceDescriptor& aImage,
                      bool* aIsInitialised = nullptr,
                      bool* aNeedsReset = nullptr) MOZ_OVERRIDE;

  virtual Effect* Lock(const gfx::Filter& aFilter) MOZ_OVERRIDE;
  virtual void Unlock() MOZ_OVERRIDE;

  GLenum GetWrapMode() const {
    return mWrapMode;
  }
  void SetWrapMode(GLenum aMode) {
    mWrapMode = aMode;
  }

protected:
  typedef mozilla::gl::GLContext GLContext;
  typedef mozilla::gl::TextureImage TextureImage;

  gl::GLContext* mGL;
  GLuint mTextureHandle;
  GLenum mWrapMode;
  gl::SharedTextureHandle mSharedHandle;
  gl::TextureImage::TextureShareType mShareType;

};


// TODO[nical] this class is not usable yet
class TextureHostOGLShared : public TextureHostOGL
                           , public TextureSourceOGL
                           , public TextureSource
{
public:
  TextureSourceOGL* AsSourceOGL() MOZ_OVERRIDE {
    return this;
  }

  typedef gfxASurface::gfxContentType ContentType;
  typedef mozilla::gl::GLContext GLContext;
  typedef mozilla::gl::TextureImage TextureImage;

  virtual ~TextureHostOGLShared()
  {
    mGL->MakeCurrent();
    mGL->ReleaseSharedHandle(mShareType, mSharedHandle);
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

  bool IsValid() const { return mTextureHandle != 0; }

  // override from TextureHost
  virtual void UpdateImpl(const SurfaceDescriptor& aImage,
                      bool* aIsInitialised = nullptr,
                      bool* aNeedsReset = nullptr);
  virtual Effect* Lock(const gfx::Filter& aFilter);
  virtual void Unlock();

  virtual GLenum GetWrapMode() const {
    return mWrapMode;
  }
  virtual void SetWrapMode(GLenum aMode) {
    mWrapMode = aMode;
  }

  // TODO[nical] temporary garbage:
  gfx::IntSize GetSize() const {
    NS_RUNTIMEABORT("not implemented");
    return gfx::IntSize(0,0);
  }
  void BindTexture(GLenum activetex) {
    mGL->fActiveTexture(activetex);
    mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mTextureHandle);
  }
  void ReleaseTexture() {
    NS_RUNTIMEABORT("not implemented");
  }
  GLuint GetTextureID() { return mTextureHandle; }
  ContentType GetContentType() {
    NS_RUNTIMEABORT("not implemented");
    return gfxASurface::CONTENT_COLOR;
  }

  TextureHostOGLShared(GLContext* aGL,
                       BufferMode aBufferMode = BUFFER_NONE,
                       ISurfaceDeallocator* aDeallocator = nullptr)
  : mGL(aGL)
  {
    SetBufferMode(aBufferMode, aDeallocator);
  }

protected:

  gfx::IntSize mSize;
  gl::GLContext* mGL;
  GLuint mTextureHandle;
  GLenum mWrapMode;
  gl::SharedTextureHandle mSharedHandle;
  gl::TextureImage::TextureShareType mShareType;
};

//TODO[nrc] can we merge with other host/source
class TiledTextureHost : public TextureHostOGL
                       , public TextureSource
                       , public TextureSourceOGL
{
public:
  TiledTextureHost(gl::GLContext* aGL) 
    : mTextureHandle(0)
    , mGL(aGL)
  {}
  ~TiledTextureHost();

  TextureSourceOGL* AsSourceOGL() MOZ_OVERRIDE {
    return this;
  }

  virtual void Update(gfxReusableSurfaceWrapper* aReusableSurface, TextureFlags aFlags);
  virtual Effect* Lock(const gfx::Filter& aFilter);
  virtual void Unlock() {}

  // TODO[nrc] Texture source stuff
  virtual TextureSource* AsTextureSource() { return this; }
  virtual bool IsValid() const { return true; }
  virtual void BindTexture(GLenum aTextureUnit) {}
  virtual gfx::IntSize GetSize() const { return gfx::IntSize(0, 0); }
  virtual GLenum GetWrapMode() const { return LOCAL_GL_REPEAT; }

protected:
  virtual uint64_t GetIdentifier() const MOZ_OVERRIDE {
    return static_cast<uint64_t>(mTextureHandle);
  }

private:
  GLenum GetTileType()
  {
    // Deduce the type that was assigned in GetFormatAndTileForImageFormat
    return mFormat == LOCAL_GL_RGB ? LOCAL_GL_UNSIGNED_SHORT_5_6_5 : LOCAL_GL_UNSIGNED_BYTE;
  }

  GLuint mTextureHandle;
  GLenum mFormat;
  gl::GLContext* mGL;
};


} // namespace
} // namespace
 
#endif /* MOZILLA_GFX_TEXTUREOGL_H */
