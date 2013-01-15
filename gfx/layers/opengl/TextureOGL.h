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

#define CLASS_NAME(name) virtual char* ClassName() const { return #name ;}

namespace mozilla {
namespace layers {

class TextureHostOGL : public TextureHost
{
public:
  TextureHostOGL(TextureHost::Buffering aBuffering = TextureHost::Buffering::NONE)
  : TextureHost(aBuffering)
  {}

  // TODO[nical] does not belong here
  virtual GLenum GetWrapMode() const { return LOCAL_GL_REPEAT; }

  //TODO[nrc] each TextureHost should implment tis properly
  virtual LayerRenderState GetRenderState()
  {
    NS_WARNING("TextureHost::GetRenderState should be overriden");
    return LayerRenderState();
  }

protected:
};

/**
 * Interface.
 */
class TextureSourceOGL : public TextureSource
{
public:
  TextureSourceOGL* AsSourceOGL() MOZ_OVERRIDE {
    return this;
  }
  virtual bool IsValid() const = 0;
  virtual void BindTexture(GLenum aTextureUnit) = 0;
  virtual gfx::IntSize GetSize() const = 0;
  virtual GLenum GetWrapMode() const = 0;
};

class TextureImageAsTextureHostOGL : public TextureHost
                                   , public TextureSourceOGL
                                   , public TileIterator
{
public:
  TextureImageAsTextureHostOGL(gl::GLContext* aGL,
                               gl::TextureImage* aTexImage = nullptr,
                               TextureHost::Buffering aBuffering = TextureHost::Buffering::NONE,
                               ISurfaceDeallocator* aDeallocator = nullptr)
  : TextureHost(aBuffering, aDeallocator), mGL(aGL), mTexture(aTexImage)
  {
    //SetBuffering(aBuffering);
  }

  // TextureHost

  virtual void UpdateImpl(const SurfaceDescriptor& aImage,
                          bool* aIsInitialised = nullptr,
                          bool* aNeedsReset = nullptr);
  virtual void UpdateRegionImpl(gfxASurface* aSurface, nsIntRegion& aRegion);

  virtual bool IsValid() const MOZ_OVERRIDE {
    return !!mTexture;
  }

  virtual LayerRenderState GetRenderState() MOZ_OVERRIDE {
    return LayerRenderState(); // TODO
  }

  virtual Effect* Lock(const gfx::Filter& aFilter) MOZ_OVERRIDE;

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

  void Abort() MOZ_OVERRIDE;

  // TileIterator, TODO[nical] this belongs to TextureSource
  virtual TileIterator* AsTileIterator() MOZ_OVERRIDE { return this; }
  void SetFilter(const gfx::Filter& aFilter) { mTexture->SetFilter(gfx::ThebesFilter(aFilter)); }
  virtual void BeginTileIteration() { mTexture->BeginTileIteration(); }
  virtual nsIntRect GetTileRect() { return mTexture->GetTileRect(); }
  virtual size_t GetTileCount() { return mTexture->GetTileCount(); }
  virtual bool NextTile() { return mTexture->NextTile(); }

protected:
  RefPtr<gl::TextureImage> mTexture;
  gl::GLContext* mGL;
  gfx::IntSize mSize;
};

class SharedTextureWrapper : public gl::BindableTexture
{
public:
  //virtual nsIntSize GetSize() const = 0;

  virtual void BindTexture(GLenum aTextureUnit) = 0;
  virtual void ReleaseTexture() {}

  virtual GLuint GetTextureID() = 0; // TODO[nical] const

  virtual ContentType GetContentType() const = 0;

  virtual bool InUpdate() const = 0;

  virtual void EndUpdate() = 0;

  gl::GLContext* mGL;
  GLuint mTextureHandle;
  GLenum mWrapMode;
  gl::SharedTextureHandle mSharedHandle;
  gl::TextureImage::TextureShareType mShareType;
};


// TODO[nical] this class isn't implemented/usable
class SharedTextureAsTextureHostOGL : public TextureHostOGL
{
public:
  SharedTextureAsTextureHostOGL(gl::GLContext* aGL)
  {
    // TODO[nical]
    //mSharedTexture = new SharedTextureWrapper;
    mSharedTexture->mGL = aGL;
  }

  virtual ~SharedTextureAsTextureHostOGL() {
    mSharedTexture->mGL->MakeCurrent();
    mSharedTexture->mGL->ReleaseSharedHandle(mSharedTexture->mShareType,
                                             mSharedTexture->mSharedHandle);
    if (mSharedTexture->mTextureHandle) {
      mSharedTexture->mGL->fDeleteTextures(1, &mSharedTexture->mTextureHandle);
    }
  }

  virtual GLuint GetTextureHandle() {
    return mSharedTexture->mTextureHandle;
  }

  // override from TextureHost
  virtual void UpdateImpl(const SurfaceDescriptor& aImage,
                      bool* aIsInitialised = nullptr,
                      bool* aNeedsReset = nullptr) MOZ_OVERRIDE;

  virtual Effect* Lock(const gfx::Filter& aFilter) MOZ_OVERRIDE;
  virtual void Unlock() MOZ_OVERRIDE;
/*
  gfx::IntSize GetSize() const MOZ_OVERRIDE {
    NS_RUNTIMEABORT("not implemented");
    return gfx::IntSize(0,0);
  }
*/
  GLenum GetWrapMode() const {
    return mSharedTexture->mWrapMode;
  }
  void SetWrapMode(GLenum aMode) {
    mSharedTexture->mWrapMode = aMode;
  }

protected:
  typedef mozilla::gl::GLContext GLContext;
  typedef mozilla::gl::TextureImage TextureImage;

  RefPtr<SharedTextureWrapper> mSharedTexture;
};

// TODO[nical] rewrite the TextureHost classes so that we don't do something
// stupid like this.
template<typename T>
struct TextureProxyHack : public gl::BindableTexture
{
  typedef gfxASurface::gfxContentType ContentType;

  TextureProxyHack(const T* aHost) : mHost(const_cast<T*>(aHost)) {}

  virtual void BindTexture(GLenum aTextureUnit) MOZ_OVERRIDE {
    return mHost->BindTexture(aTextureUnit);
  }

  virtual void ReleaseTexture() MOZ_OVERRIDE {
    return mHost->ReleaseTexture();
  }

  virtual GLuint GetTextureID() MOZ_OVERRIDE {
    return mHost->GetTextureID();
  }

  RefPtr<T> mHost;
};

/*
// a TextureImageAsTextureHost for use with main thread composition
// i.e., where we draw to it directly, and do not have a texture client
class TextureImageHost : public TextureImageAsTextureHost
{
public:
  CLASS_NAME(TextureImageHost);
  TextureImageHost(GLContext* aGL, TextureImage* aTexImage);
 
  TextureImage* GetTextureImage() { return mTexImage; }
  void SetTextureImage(TextureImage* aTexImage)
  {
    mTexImage = aTexImage;
    mSize = gfx::IntSize(mTexImage->mSize.width, mTexImage->mSize.height);
  }

  virtual void UpdateImpl(const SurfaceDescriptor& aImage,
                      bool* aIsInitialised = nullptr,
                      bool* aNeedsReset = nullptr) {}
  virtual void UpdateRegionImpl(gfxASurface* aSurface, nsIntRegion& aRegion) {}
};

*/

// TODO[nical] this class is not usable yet
class TextureHostOGLShared : public TextureHostOGL
                           , public TextureSourceOGL
{
public:
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
                       TextureHost::Buffering aBuffering = TextureHost::Buffering::NONE,
                       ISurfaceDeallocator* aDeallocator = nullptr)
  : mGL(aGL)
  {
    SetBuffering(aBuffering, aDeallocator);
  }

protected:

  gfx::IntSize mSize;
  gl::GLContext* mGL;
  GLuint mTextureHandle;
  GLenum mWrapMode;
  gl::SharedTextureHandle mSharedHandle;
  gl::TextureImage::TextureShareType mShareType;
};

class YCbCrTextureHostOGL : public TextureHostOGL, public TextureSource
{
public:
  YCbCrTextureHostOGL(gl::GLContext* aGL) : mGL(aGL) {}

  virtual void UpdateImpl(const SurfaceDescriptor& aImage,
                          bool* aIsInitialised = nullptr,
                          bool* aNeedsReset = nullptr) MOZ_OVERRIDE;

  virtual void UpdateRegionImpl(gfxASurface* aSurface, nsIntRegion& aRegion) {
    NS_RUNTIMEABORT("should not be called");
  }

  Effect* Lock(const gfx::Filter& aFilter) MOZ_OVERRIDE;

  gfx::IntSize GetSize() const {  // TODO[nical] remove ?
    if (!mYTexture.mTexImage) {
      NS_WARNING("YCbCrTextureHost::GetSize called but no data has been set yet");
      return gfx::IntSize(0,0);
    }
    return mYTexture.GetSize();
  }

  TextureSource* AsTextureSource() MOZ_OVERRIDE {
    NS_WARNING("YCbCrTextureHostOGL does not have a primary TextureSource.");
    return nullptr;
  }

  struct Channel : public TextureSourceOGL {
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

private:
  Channel mYTexture;
  Channel mCbTexture;
  Channel mCrTexture;
  gl::GLContext* mGL;
};

} // namespace
} // namespace
 
#endif /* MOZILLA_GFX_TEXTUREOGL_H */
