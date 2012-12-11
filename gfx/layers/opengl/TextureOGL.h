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
 * Abstract class.
 * Manages one or more textures that are updated at the same time. Handles
 * Deserialization from SharedImage into objects implementing BindableTexture
 * (UpdateImpl), owns these objects, provides acces to them (GetTexture) and
 * creates the corresponding effects to pass to the compositor (Lock).
 */
class TextureHostOGL : public TextureHost
{
public:
  /**
   * Returns an object that can e binded as OpenGL texture, given an index.
   * In most cases we use only one texture, so the index will be ignored.
   * Sometimes however, we can have several textures, for intance YCbCrImages
   * have one texture per channel. 
   */
  virtual gl::BindableTexture* GetTexture(uint32_t index = 0) const = 0;

  /**
   * @return true if all the textures are in valid state.
   */
  virtual bool IsValid() const = 0;

protected:
  TextureHostOGL(TextureHost::Buffering aBuffering = TextureHost::Buffering::NONE)
  : TextureHost(aBuffering)
  {}
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
    nsIntSize s = GetTexture()->GetSize();
    return gfx::IntSize(s.width, s.height);
  }
private:
  RefPtr<TextureHostOGL> mHost;
  uint32_t mIndex;
};


// TODO[nical] obviously the naming will change when I get to cleanup all the 
// TextureHost classes and free some names
class AwesomeTextureHostOGL : public TextureHostOGL
                            , public TileIterator
{
public:
  AwesomeTextureHostOGL(gl::GLContext* aGL, TextureHost::Buffering aBuffering = TextureHost::Buffering::NONE,
                        gl::TextureImage* aTexImage = nullptr)
  : mGL(aGL), mTexture(aTexImage)
  {
    SetBuffering(aBuffering);
  }

  virtual void UpdateImpl(const SharedImage& aImage,
                          bool* aIsInitialised = nullptr,
                          bool* aNeedsReset = nullptr);
  virtual void UpdateImpl(gfxASurface* aSurface, nsIntRegion& aRegion);

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
    nsIntSize s = mTexture->GetSize();
    return gfx::IntSize(s.width, s.height);
  }

  void Abort()
  {
    if (mTexture->InUpdate()) {
      mTexture->EndUpdate();
    }
  }

  // TileIterator
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
  virtual nsIntSize GetSize() const = 0;

  virtual void BindTexture(GLenum aTextureUnit) = 0;
  virtual void ReleaseTexture() {}

  virtual GLenum GetWrapMode() const = 0;
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

  gfx::IntSize GetSize() const MOZ_OVERRIDE {
    NS_RUNTIMEABORT("not implemented");
    return gfx::IntSize(0,0);
  }

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

  virtual nsIntSize GetSize() const MOZ_OVERRIDE
  {
    gfx::IntSize s = mHost->GetSize();
    return nsIntSize(s.width, s.height);
  }

  virtual void BindTexture(GLenum aTextureUnit) MOZ_OVERRIDE {
    return mHost->BindTexture(aTextureUnit);
  }

  virtual void ReleaseTexture() MOZ_OVERRIDE {
    return mHost->ReleaseTexture();
  }

  virtual GLenum GetWrapMode() const MOZ_OVERRIDE {
    return mHost->GetWrapMode();
  }

  virtual GLuint GetTextureID() MOZ_OVERRIDE {
    return mHost->GetTextureID();
  }

  virtual ContentType GetContentType() const MOZ_OVERRIDE {
    return mHost->GetContentType();
  }

  virtual bool InUpdate() const MOZ_OVERRIDE {
    return false;
  }

  virtual void EndUpdate() MOZ_OVERRIDE {}

  RefPtr<T> mHost;
};

/*
//thin TextureHost wrapper around a TextureImage
class TextureImageAsTextureHost : public TextureSourceHostOGL
                                , public TileIterator
{
public:
  virtual GLuint GetTextureHandle()
  {
    return mTexImage->GetTextureID();
  }
 
  virtual GLenum GetWrapMode() const
  {
    return mTexImage->mWrapMode;
  }
 
  virtual void SetWrapMode(GLenum aWrapMode)
  {
    mTexImage->mWrapMode = aWrapMode;
  }
 
  virtual void UpdateImpl(const SharedImage& aImage,
                      bool* aIsInitialised = nullptr,
                      bool* aNeedsReset = nullptr);
  virtual void UpdateImpl(gfxASurface* aSurface, nsIntRegion& aRegion);
  
  virtual void Abort();

  virtual TileIterator* GetAsTileIterator() { return this; }
  virtual Effect* Lock(const gfx::Filter& aFilter);
 
  void SetFilter(const gfx::Filter& aFilter) { mTexImage->SetFilter(gfx::ThebesFilter(aFilter)); }
  virtual void BeginTileIteration() { mTexImage->BeginTileIteration(); }
  virtual nsIntRect GetTileRect() { return mTexImage->GetTileRect(); }
  virtual size_t GetTileCount() { return mTexImage->GetTileCount(); }
  virtual bool NextTile() { return mTexImage->NextTile(); }

#ifdef DEBUG
  virtual bool IsAlpha() { return mTexImage->GetContentType() == gfxASurface::CONTENT_ALPHA; }
#endif
 
#ifdef MOZ_DUMP_PAINTING
  virtual already_AddRefed<gfxImageSurface> Dump()
  {
    return mGL->GetTexImage(GetTextureHandle(), false, mTexImage->GetShaderProgramType());
  }
#endif

  virtual void BindTexture(int aUnit) { mTexImage->BindTexture(aUnit); }
 
protected:
  typedef mozilla::gl::GLContext GLContext;
  typedef mozilla::gl::TextureImage TextureImage;
 
  TextureImageAsTextureHost(GLContext* aGL)
    : mGL(aGL)
    , mTexImage(nullptr)
  {}
 
  GLContext* mGL;
  nsRefPtr<TextureImage> mTexImage;
 
  friend class CompositorOGL;
};
*/
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
  virtual void UpdateImpl(gfxASurface* aSurface, nsIntRegion& aRegion) {}
};

// TODO[nical] this class should desapear (buffering is done in TextureHost)
class TextureImageAsTextureHostWithBuffer : public TextureImageAsTextureHost
{
public:
  CLASS_NAME(TextureImageAsTextureHostWithBuffer)
  ~TextureImageAsTextureHostWithBuffer();
 
  virtual void UpdateImpl(const SurfaceDescriptor& aNewBuffer,
                          bool* aIsInitialised = nullptr,
                          bool* aNeedsReset = nullptr);
  virtual void SetDeAllocator(ISurfaceDeAllocator* aDeAllocator)
  {
    NS_ASSERTION(!mDeAllocator || mDeAllocator == aDeAllocator, "Stomping allocator?");
    mDeAllocator = aDeAllocator;
  }
 
protected:
  TextureImageAsTextureHostWithBuffer(GLContext* aGL)
    : TextureImageAsTextureHost(aGL)
  {
    SetBuffering(TextureHost::Buffering::BUFFERED);
  }
 
  // returns true if the buffer was reset
  bool EnsureBuffer(nsIntSize aSize);
 
  ISurfaceDeAllocator* mDeAllocator;
 
  friend class CompositorOGL;
};
*/
class TextureHostOGLShared : public TextureHostOGL
{
public:
  typedef gfxASurface::gfxContentType ContentType;

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

  bool IsValid() const { return true; } // TODO[nical]
 
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

protected:
  typedef mozilla::gl::GLContext GLContext;
  typedef mozilla::gl::TextureImage TextureImage;
 
  TextureHostOGLShared(GLContext* aGL)
    : mGL(aGL)
  {}
 
  gfx::IntSize mSize;
  GLContext* mGL;
  GLuint mTextureHandle;
  GLenum mWrapMode;
  gl::SharedTextureHandle mSharedHandle;
  gl::TextureImage::TextureShareType mShareType;
 
  friend class CompositorOGL;
};

// TODO[nical] this class should desapear (buffering is done in TextureHost)
class TextureHostOGLSharedWithBuffer : public TextureHostOGLShared
{
public:
  CLASS_NAME(TextureHostOGLSharedWithBuffer)
 
protected:
    virtual void UpdateImpl(const SharedImage& aImage,
                      bool* aIsInitialised = nullptr,
                      bool* aNeedsReset = nullptr) MOZ_OVERRIDE;

  TextureHostOGLSharedWithBuffer(GLContext* aGL)
    : TextureHostOGLShared(aGL)
  {
    SetBuffering(TextureHost::Buffering::BUFFERED);
  }
  
  friend class CompositorOGL;
};

class YCbCrTextureHostOGL : public TextureHostOGL
{
public:
  YCbCrTextureHostOGL(gl::GLContext* aGL) : mGL(aGL) {}

  gl::BindableTexture* GetTexture(uint32_t channel) const MOZ_OVERRIDE {
    switch (channel) {
      case 0 : return mYTexture;
      case 1 : return mYTexture;
      case 2 : return mYTexture;
    }
    return nullptr;
  }

  bool IsValid() const MOZ_OVERRIDE {
    return !!mYTexture && !!mCbTexture && !! mCrTexture;
  }

  virtual void UpdateImpl(const SharedImage& aImage,
                          bool* aIsInitialised = nullptr,
                          bool* aNeedsReset = nullptr) {
    NS_RUNTIMEABORT("Not implemented");    
  }
  virtual void UpdateImpl(gfxASurface* aSurface, nsIntRegion& aRegion) {
    NS_RUNTIMEABORT("should not be called");
  }

  gfx::IntSize GetSize() const MOZ_OVERRIDE {
    if (!mYTexture) {
      NS_WARNING("YCbCrTextureHost::GetSize called but no data has been set yet");
      return gfx::IntSize(0,0);
    }
    nsIntSize s = mYTexture->GetSize();
    return gfx::IntSize(s.width, s.height);
  }

  RefPtr<gl::TextureImage> mYTexture;
  RefPtr<gl::TextureImage> mCbTexture;
  RefPtr<gl::TextureImage> mCrTexture;
  gl::GLContext* mGL;
};

/*
class GLTextureAsTextureSource : public TextureSourceOGL,
                                 public RefCounted<GLTextureAsTextureSource>
{
  typedef mozilla::gl::GLContext GLContext;
  typedef RefCounted<GLTextureAsTextureSource> RefCounter;
public:
  CLASS_NAME(GLTextureAsTextureSource)
  virtual void AddRef() { RefCounter::AddRef(); }
  virtual void Release() { RefCounter::Release(); }

  ~GLTextureAsTextureSource()
  {
    mTexture.Release();
  }

  void UpdateImpl(gfx::IntSize aSize, uint8_t* aData, uint32_t aStride, GLContext* aGL);

  virtual GLuint GetTextureHandle()
  {
    return mTexture.GetTextureID();
  }

  virtual gfx::IntSize GetSize() const
  {
    return mSize;
  }

  virtual GLenum GetWrapMode() const
  {
    return LOCAL_GL_REPEAT;
  }

  virtual void SetWrapMode(GLenum aMode) {
    NS_RUNTIMEABORT("not implemented"); // TODO[nical] move interfaces around to avoid this
  }

private:
  GLTexture mTexture;
  gfx::IntSize mSize;
};

class GLTextureAsTextureHost : public TextureSourceHostOGL
{
  typedef mozilla::gl::GLContext GLContext;
public:
  CLASS_NAME(GLTextureAsTextureHost);
  GLTextureAsTextureHost(GLContext* aGL)
    : mGL(aGL)
  {}
 
  ~GLTextureAsTextureHost()
  {
    mTexture.Release();
  }
 
  virtual GLuint GetTextureHandle()
  {
    return mTexture.GetTextureID();
  }
 
  virtual void UpdateImpl(const SharedImage& aImage,
                      bool* aIsInitialised = nullptr,
                      bool* aNeedsReset = nullptr);

  // TODO[nical] added to compile, not sure it belongs here, should be initialized
  virtual GLenum GetWrapMode() const {
    return mWrapMode;
  }
  virtual void SetWrapMode(GLenum aMode) {
    mWrapMode = aMode;
  }
private:
  nsRefPtr<GLContext> mGL;
  GLTexture mTexture;
  GLenum mWrapMode;
};

// a texture host with all three plains in one texture
//TODO used by YUV not YCbCr
class YCbCrTextureHost : public TextureHostOGL,
                         public RefCounted<YCbCrTextureHost>
{
  typedef mozilla::gl::GLContext GLContext;
  typedef RefCounted<YCbCrTextureHost> RefCounter;
public:
  CLASS_NAME(YCbCrTextureHost)
  YCbCrTextureHost(GLContext* aGL)
    : mGL(aGL)
  {}

  virtual void AddRef() { RefCounter::AddRef(); }
  virtual void Release() { RefCounter::Release(); }

  virtual void UpdateImpl(const SharedImage& aImage,
                      bool* aIsInitialised = nullptr,
                      bool* aNeedsReset = nullptr);
  virtual Effect* Lock(const gfx::Filter& aFilter);

private:
  nsRefPtr<GLContext> mGL;
  GLTextureAsTextureSource mTextures[3];
};

#ifdef MOZ_WIDGET_GONK
// For direct texturing with OES_EGL_image_external extension. This
// texture is allocated when the image supports binding with
// BindExternalBuffer.
class DirectExternalTextureHost : public TextureSourceHostOGL
{
  typedef mozilla::gl::GLContext GLContext;
public:
  CLASS_NAME(DirectExternalTextureHost)
  GLTextureAsTextureHost(GLContext* aGL)
    : mGL(aGL)
  {}
 
  ~GLTextureAsTextureHost()
  {
    mExternalBufferTexture.Release();
  }
 
  virtual GLuint GetTextureHandle()
  {
    return mExternalBufferTexture.GetTextureID()
  }
 
  virtual void UpdateImpl(const SharedImage& aImage,
                      bool* aIsInitialised = nullptr,
                      bool* aNeedsReset = nullptr);
  virtual Effect* Lock(const gfx::Filter& aFilter);
 
private:
  nsRefPtr<GLContext> mGL;
  GLTexture mExternalBufferTexture;
};
#endif
*/

} // namespace
} // namespace
 
#endif /* MOZILLA_GFX_TEXTUREOGL_H */
