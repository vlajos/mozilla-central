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

/**
 * Manages one or more textures that are updated at the same time. Handles
 * Deserialization from SharedImage into objects implementing BindableTexture
 * (UpdateImpl), owns these objects, provides acces to them (GetTexture) and
 * creates the corresponding effects to pass to the compositor (Lock).
 * This class is meant to be used on the compositor thread.
 */
class TextureHostOGL : public TextureHost
{
public:
  TextureHostOGL(TextureHost::Buffering aBuffering = TextureHost::Buffering::NONE)
  : TextureHost(aBuffering)
  {}
/*
  virtual void UpdateImpl(const SharedImage& aImage,
                          bool* aIsInitialised = nullptr,
                          bool* aNeedsReset = nullptr);
  virtual void UpdateRegionImpl(gfxASurface* aSurface, nsIntRegion& aRegion);
*/
  /**
   * Returns an object that can e binded as OpenGL texture, given an index.
   * In most cases we use only one texture, so the index will be ignored.
   * Sometimes however, we can have several textures, for intance YCbCrImages
   * have one texture per channel. 
   */
  virtual gl::BindableTexture* GetTexture(uint32_t index = 0) const = 0; /*{
    if (mTextures.size() <= index) {
      return nullptr;
    }
    return mTextures[index].get();
  }*/

  /**
   * @return true if all the textures are in valid state.
   */
  virtual bool IsValid() const = 0;

  virtual GLenum GetWrapMode() const { return LOCAL_GL_REPEAT; }

  //TODO[nrc] each TextureHost should implment this properly
  virtual LayerRenderState GetRenderState()
  {
    NS_WARNING("TextureHost::GetRenderState should be overriden");
    return LayerRenderState();
  }

  TemporaryRef<TextureSource> GetPrimaryTextureSource() MOZ_OVERRIDE;

  bool AddMaskEffect(EffectChain& aEffects,
                     const gfx::Matrix4x4& aTransform,
                     bool aIs3D) MOZ_OVERRIDE;

protected:
/*
  // ----------------- taken from ImageLayerOGL
  nsRefPtr<TextureImage> mTexImage;

  // For SharedTextureHandle
  gl::SharedTextureHandle mSharedHandle;
  gl::GLContext::SharedTextureShareType mShareType;
  bool mInverted;
  GLuint mTexture;

  // For direct texturing with OES_EGL_image_external extension. This
  // texture is allocated when the image supports binding with
  // BindExternalBuffer.
  GLTexture mExternalBufferTexture;

  gl::BasicTexture mYUVTexture[3];
  gfxIntSize mSize;
  gfxIntSize mCbCrSize;
  nsIntRect mPictureRect;

  // ----------------

  gl::GLContext* mGL;
*/
};

/**
 * Interface.
 * Provides access to a BindableTexture owned by a TextureHost.
 * an index may be used for cases where the TextureHost manages
 * several textures (like YCbCr frames that have one texture per channel).
 *
 * Users of TextureSourceOGL should check IsValid() before calling GetTexture().
 */
class TextureSourceOGL
{
public:
  virtual bool IsValid() const = 0;
  virtual gl::BindableTexture* GetTexture() const = 0;
  virtual gfx::IntSize GetSize() const = 0;
  virtual GLenum GetWrapMode() const = 0;
};

/**
 * Implementation of TextureSourceOGL.
 */
class SimpleTextureSourceOGL : public TextureSource,
                               public TextureSourceOGL
{
public:
  SimpleTextureSourceOGL(TextureHostOGL* aHost, uint32_t aIndex = 0)
  : mHost(aHost), mIndex(aIndex) {}

  TextureSourceOGL* AsSourceOGL()  MOZ_OVERRIDE { return this; } 

  bool IsValid() const MOZ_OVERRIDE {
    return !!mHost && mHost->IsValid();
  }

  gl::BindableTexture* GetTexture() const MOZ_OVERRIDE {
    MOZ_ASSERT(IsValid());
    return mHost->GetTexture();
  }
  virtual gfx::IntSize GetSize() const MOZ_OVERRIDE {
    return mHost->GetSize();
  }
  virtual GLenum GetWrapMode() const MOZ_OVERRIDE {
    return mHost->GetWrapMode();
  }
private:
  RefPtr<TextureHostOGL> mHost;
  uint32_t mIndex;
};



// ---------------------------------- code below will be removed ------




class TextureImageAsTextureHostOGL : public TextureHostOGL
                                   , public TileIterator
{
public:
  TextureImageAsTextureHostOGL(gl::GLContext* aGL, TextureHost::Buffering aBuffering = TextureHost::Buffering::NONE,
                        gl::TextureImage* aTexImage = nullptr)
  : mGL(aGL), mTexture(aTexImage)
  {
    SetBuffering(aBuffering);
  }

  virtual void UpdateImpl(const SharedImage& aImage,
                          bool* aIsInitialised = nullptr,
                          bool* aNeedsReset = nullptr);
  virtual void UpdateRegionImpl(gfxASurface* aSurface, nsIntRegion& aRegion);

  virtual gl::BindableTexture* GetTexture(uint32_t index = 0) const MOZ_OVERRIDE {
    MOZ_ASSERT(index == 0);
    return mTexture.get();
  }

  virtual bool IsValid() const MOZ_OVERRIDE {
    return !!mTexture;
  }

  virtual Effect* Lock(const gfx::Filter& aFilter) MOZ_OVERRIDE;

  void SetTextureImage(gl::TextureImage* aTexImage) {
    mTexture = aTexImage;
  }

  gfx::IntSize GetSize() const MOZ_OVERRIDE {
    return mSize;
  }

  GLenum GetWrapMode() const MOZ_OVERRIDE {
    return mTexture->GetWrapMode();
  }

  void Abort() MOZ_OVERRIDE;

  // TileIterator, TODO[nical] this belongs to TextureSource
  virtual TileIterator* GetAsTileIterator() { return this; }
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

  virtual gl::BindableTexture* GetTexture(uint32_t) const MOZ_OVERRIDE {
    return mSharedTexture.get();
  }

  virtual GLuint GetTextureHandle() {
    return mSharedTexture->mTextureHandle;
  }

  // override from TextureHost
  virtual void UpdateImpl(const SharedImage& aImage,
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

  virtual void UpdateImpl(const SharedImage& aImage,
                      bool* aIsInitialised = nullptr,
                      bool* aNeedsReset = nullptr) {}
  virtual void UpdateRegionImpl(gfxASurface* aSurface, nsIntRegion& aRegion) {}
};

*/

// TODO[nical] this class is not usable yet
class TextureHostOGLShared : public TextureHostOGL
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

  bool IsValid() const { return mTextureHandle != 0; }

  // override from TextureHost
  virtual void UpdateImpl(const SharedImage& aImage,
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

  gl::BindableTexture* GetTexture(uint32_t index = 0) const MOZ_OVERRIDE {
    return new TextureProxyHack<TextureHostOGLShared>(this);
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
                       TextureHost::Buffering aBuffering = TextureHost::Buffering::NONE)
  : mGL(aGL)
  {
    SetBuffering(aBuffering);
  }

protected:

  gfx::IntSize mSize;
  gl::GLContext* mGL;
  GLuint mTextureHandle;
  GLenum mWrapMode;
  gl::SharedTextureHandle mSharedHandle;
  gl::TextureImage::TextureShareType mShareType;
};

class YCbCrTextureHostOGL : public TextureHostOGL
{
public:
  YCbCrTextureHostOGL(gl::GLContext* aGL) : mGL(aGL) {}

  gl::BindableTexture* GetTexture(uint32_t channel) const MOZ_OVERRIDE {
    switch (channel) {
      case 0 : return mYTexture;
      case 1 : return mCbTexture;
      case 2 : return mCrTexture;
    }
    return nullptr;
  }

  bool IsValid() const MOZ_OVERRIDE {
    return !!mYTexture && !!mCbTexture && !! mCrTexture;
  }

  virtual void UpdateImpl(const SharedImage& aImage,
                          bool* aIsInitialised = nullptr,
                          bool* aNeedsReset = nullptr) MOZ_OVERRIDE;

  virtual void UpdateRegionImpl(gfxASurface* aSurface, nsIntRegion& aRegion) {
    NS_RUNTIMEABORT("should not be called");
  }

  Effect* Lock(const gfx::Filter& aFilter) MOZ_OVERRIDE;

  gfx::IntSize GetSize() const MOZ_OVERRIDE {
    if (!mYTexture) {
      NS_WARNING("YCbCrTextureHost::GetSize called but no data has been set yet");
      return gfx::IntSize(0,0);
    }
    nsIntSize s = mYTexture->GetSize();
    return gfx::IntSize(s.width, s.height);
  }

  bool AddMaskEffect(EffectChain& aEffects,
                     const gfx::Matrix4x4& aTransform,
                     bool aIs3D) MOZ_OVERRIDE
  {
    NS_WARNING("YCbCrTextureHostOGL cannot be used as mask");
    return false;
  }

private:
  RefPtr<gl::TextureImage> mYTexture;
  RefPtr<gl::TextureImage> mCbTexture;
  RefPtr<gl::TextureImage> mCrTexture;
  gl::GLContext* mGL;
};

} // namespace
} // namespace
 
#endif /* MOZILLA_GFX_TEXTUREOGL_H */
