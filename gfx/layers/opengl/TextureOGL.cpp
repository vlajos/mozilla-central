/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */
 
#include "TextureOGL.h"
#include "ipc/AutoOpenSurface.h"
#include "gfx2DGlue.h"
#include "ShmemYCbCrImage.h"
#include "GLContext.h"
#include "gfxImageSurface.h"

using namespace mozilla::gl;

namespace mozilla {
namespace layers {
 
static uint32_t
DataOffset(uint32_t aStride, uint32_t aPixelSize, const nsIntPoint &aPoint)
{
  unsigned int data = aPoint.y * aStride;
  data += aPoint.x * aPixelSize;
  return data;
}
 
static void
MakeTextureIfNeeded(gl::GLContext* gl, GLuint& aTexture)
{
  if (aTexture != 0)
    return;
 
  gl->fGenTextures(1, &aTexture);
 
  gl->fActiveTexture(LOCAL_GL_TEXTURE0);
  gl->fBindTexture(LOCAL_GL_TEXTURE_2D, aTexture);
 
  gl->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_MIN_FILTER, LOCAL_GL_LINEAR);
  gl->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_MAG_FILTER, LOCAL_GL_LINEAR);
  gl->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_WRAP_S, LOCAL_GL_CLAMP_TO_EDGE);
  gl->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_WRAP_T, LOCAL_GL_CLAMP_TO_EDGE);
}
 
static gl::TextureImage::Flags FlagsToGLFlags(TextureFlags aFlags)
{
  uint32_t result = TextureImage::NoFlags;
   
  if (aFlags & UseNearestFilter)
    result |= TextureImage::UseNearestFilter;
  if (aFlags & NeedsYFlip)
    result |= TextureImage::NeedsYFlip;
  if (aFlags & ForceSingleTile)
    result |= TextureImage::ForceSingleTile;
 
  return static_cast<gl::TextureImage::Flags>(result);
}
 
GLenum
WrapMode(gl::GLContext *aGl, bool aAllowRepeat)
{
  if (aAllowRepeat &&
      (aGl->IsExtensionSupported(GLContext::ARB_texture_non_power_of_two) ||
       aGl->IsExtensionSupported(GLContext::OES_texture_npot))) {
    return LOCAL_GL_REPEAT;
  }
  return LOCAL_GL_CLAMP_TO_EDGE;
}

void TextureImageAsTextureHostOGL::UpdateImpl(const SharedImage& aImage,
                                       bool* aIsInitialised,
                                       bool* aNeedsReset)
{
  SurfaceDescriptor surface = aImage.get_SurfaceDescriptor();
 
  AutoOpenSurface surf(OPEN_READ_ONLY, surface);
  nsIntSize size = surf.Size();

  if (!mTexture ||
      mTexture->GetSize() != size ||
      mTexture->GetContentType() != surf.ContentType()) {
    mTexture = mGL->CreateTextureImage(size,
                                       surf.ContentType(),
                                       WrapMode(mGL, mFlags & AllowRepeat),
                                       FlagsToGLFlags(mFlags)).get(); // TODO[nical] eeek!
    mSize = gfx::IntSize(size.width, size.height);
  }
 
  // XXX this is always just ridiculously slow
  nsIntRegion updateRegion(nsIntRect(0, 0, size.width, size.height));
  mTexture->DirectUpdate(surf.Get(), updateRegion);

    // mTexture = texImage.forget();

  if (aIsInitialised) {
    *aIsInitialised = true;
  }
}

// thebes
void
TextureImageAsTextureHostOGL::UpdateImpl(gfxASurface* aSurface,
                                  nsIntRegion& aRegion)
{
  if (!mTexture ||
      mTexture->GetSize() != aSurface->GetSize() ||
      mTexture->GetContentType() != aSurface->GetContentType()) {
    mTexture = mGL->CreateTextureImage(aSurface->GetSize(),
                                        aSurface->GetContentType(),
                                        WrapMode(mGL, mFlags & AllowRepeat),
                                        FlagsToGLFlags(mFlags)).get();
    mSize = gfx::IntSize(mTexture->GetSize().width, mTexture->GetSize().height);
  }
 
  mTexture->DirectUpdate(aSurface, aRegion);
}

Effect*
TextureImageAsTextureHostOGL::Lock(const gfx::Filter& aFilter)
{
  if (!mTexture) {
    NS_WARNING("TextureImageAsTextureHost to be composited without texture");
    return nullptr;
  }
  NS_ASSERTION(mTexture->GetContentType() != gfxASurface::CONTENT_ALPHA,
                "Image layer has alpha image");

  if (mTexture->InUpdate()) {
    mTexture->EndUpdate();
  }

  switch (mTexture->GetShaderProgramType()) {
  case gl::RGBXLayerProgramType :
    return new EffectRGBX(new SimpleTextureSourceOGL(this),
                          true, aFilter, mFlags & NeedsYFlip);
  case gl::BGRXLayerProgramType :
    return new EffectBGRX(new SimpleTextureSourceOGL(this),
                          true, aFilter, mFlags & NeedsYFlip);
  case gl::BGRALayerProgramType :
    return new EffectBGRA(new SimpleTextureSourceOGL(this),
                          true, aFilter, mFlags & NeedsYFlip);
  case gl::RGBALayerProgramType :
    return new EffectRGBA(new SimpleTextureSourceOGL(this),
                          true, aFilter, mFlags & NeedsYFlip);
  }
  NS_RUNTIMEABORT("Shader type not yet supported");
  return nullptr;
}

bool
TextureImageAsTextureHostOGL::AddMaskEffect(EffectChain& aEffects,
                       const gfx::Matrix4x4& aTransform,
                       bool aIs3D)
{
  EffectMask* effect = new EffectMask(new SimpleTextureSourceOGL(this), aTransform);
  effect->mIs3D = aIs3D;
  aEffects.mEffects[EFFECT_MASK] = effect;
  return true;
}


/*
void
TextureImageAsTextureHost::UpdateImpl(const SharedImage& aImage,
                                  bool* aIsInitialised,
                                  bool* aNeedsReset)
{
  SurfaceDescriptor surface = aImage.get_SurfaceDescriptor();
 
  AutoOpenSurface surf(OPEN_READ_ONLY, surface);
  nsIntSize size = surf.Size();
 
  if (!mTexImage ||
      mTexImage->mSize != size ||
      mTexImage->GetContentType() != surf.ContentType()) {
    mTexImage = mGL->CreateTextureImage(size,
                                        surf.ContentType(),
                                        WrapMode(mGL, mFlags & AllowRepeat),
                                        FlagsToGLFlags(mFlags));
    mSize = gfx::IntSize(size.width, size.height);
  }
 
  // XXX this is always just ridiculously slow
  nsIntRegion updateRegion(nsIntRect(0, 0, size.width, size.height));
  mTexImage->DirectUpdate(surf.Get(), updateRegion);
 
  if (aIsInitialised) {
    *aIsInitialised = true;
  }
}
 
void
TextureImageAsTextureHost::UpdateImpl(gfxASurface* aSurface,
                                      nsIntRegion& aRegion)
{
  if (!mTexImage ||
      mTexImage->mSize != aSurface->GetSize() ||
      mTexImage->GetContentType() != aSurface->GetContentType()) {
    mTexImage = mGL->CreateTextureImage(aSurface->GetSize(),
                                        aSurface->GetContentType(),
                                        WrapMode(mGL, mFlags & AllowRepeat),
                                        FlagsToGLFlags(mFlags));
    mSize = gfx::IntSize(mTexImage->mSize.width, mTexImage->mSize.height);
  }
 
  mTexImage->DirectUpdate(aSurface, aRegion);
}
 
Effect*
TextureImageAsTextureHost::Lock(const gfx::Filter& aFilter)
{
  NS_ASSERTION(mTexImage->GetContentType() != gfxASurface::CONTENT_ALPHA,
              "Image layer has alpha image");
 
  if (mTexImage->InUpdate()) {
    mTexImage->EndUpdate();
  }

 
  if (mTexImage->GetShaderProgramType() == gl::RGBXLayerProgramType) {
      return new EffectRGBX(this, true, aFilter, mFlags & NeedsYFlip);
  } else if (mTexImage->GetShaderProgramType() == gl::BGRXLayerProgramType) {
    return new EffectBGRX(this, true, aFilter, mFlags & NeedsYFlip);
  } else if (mTexImage->GetShaderProgramType() == gl::BGRALayerProgramType) {
    return new EffectBGRA(this, true, aFilter, mFlags & NeedsYFlip);
  } else if (mTexImage->GetShaderProgramType() == gl::RGBALayerProgramType) {
    return new EffectRGBA(this, true, aFilter, mFlags & NeedsYFlip);
  } else {
    NS_RUNTIMEABORT("Shader type not yet supported");
    return nullptr;
  }
}

void
TextureImageAsTextureHost::Abort()
{
  if (mTexImage->InUpdate()) {
    mTexImage->EndUpdate();
  }
}

TextureImageHost::TextureImageHost(GLContext* aGL, TextureImage* aTexImage)
  : TextureImageAsTextureHost(aGL)
{
  mTexImage = aTexImage;
  mSize = gfx::IntSize(mTexImage->mSize.width, mTexImage->mSize.height);
}
 
TextureImageAsTextureHostWithBuffer::~TextureImageAsTextureHostWithBuffer()
{
}

void
TextureImageAsTextureHostWithBuffer::UpdateImpl(const SurfaceDescriptor& aNewBuffer,
                                                bool* aIsInitialised,
                                                bool* aNeedsReset)
{
  MOZ_ASSERT(IsBuffered());
  AutoOpenSurface newBack(OPEN_READ_ONLY, aNewBuffer);
  if (aNeedsReset) {
    *aNeedsReset = EnsureBuffer(newBack.Size());
  }

  if (!IsSurfaceDescriptorValid(aNewBuffer)) {
    if (aIsInitialised) {
      *aIsInitialised = false;
    }
    return;
  }
 
  nsRefPtr<TextureImage> texImage =
      ShadowLayerManager::OpenDescriptorForDirectTexturing(
        mGL, aNewBuffer, WrapMode(mGL, mFlags & AllowRepeat));
 
  if (!texImage) {
    NS_WARNING("Could not create texture for direct texturing");
    mTexImage = nullptr;

    if (aIsInitialised) {
      *aIsInitialised = false;
    }
    return;
  }
 
  mTexImage = texImage;
  if (aIsInitialised) {
    *aIsInitialised = true;
  }
}

bool
TextureImageAsTextureHostWithBuffer::EnsureBuffer(nsIntSize aSize)
{
  MOZ_ASSERT(IsBuffered());
  if (GetBuffer()->type() != SharedImage::TSurfaceDescriptor) {
    return false;
  }
  SurfaceDescriptor& surfDesc = GetBuffer()->get_SurfaceDescriptor();
  AutoOpenSurface surf(OPEN_READ_ONLY, surfDesc);
  if (surf.Size() != aSize) {
    mTexImage = nullptr;
    if (IsSurfaceDescriptorValid(surfDesc)) {
      mDeAllocator->DestroySharedSurface(&surfDesc);
    }
    return true;
  }
 
  return false;
}
*/
void
TextureHostOGLShared::UpdateImpl(const SharedImage& aImage,
                             bool* aIsInitialised,
                             bool* aNeedsReset)
{
  NS_ASSERTION(aImage.type() == SurfaceDescriptor::TSharedTextureDescriptor,
              "Invalid descriptor");

  SurfaceDescriptor surface = aImage.get_SurfaceDescriptor();
  SharedTextureDescriptor texture = surface.get_SharedTextureDescriptor();
 
  SharedTextureHandle newHandle = texture.handle();
  nsIntSize size = texture.size();
  mSize = gfx::IntSize(size.width, size.height);
  if (texture.inverted()) {
    mFlags |= NeedsYFlip;
  }
  mShareType = texture.shareType();
 
  if (mSharedHandle &&
      newHandle != mSharedHandle) {
    mGL->ReleaseSharedHandle(mShareType, mSharedHandle);
  }
  mSharedHandle = newHandle;
 
  if (aIsInitialised) {
    *aIsInitialised = true;
  }
}
 
Effect*
TextureHostOGLShared::Lock(const gfx::Filter& aFilter)
{
  GLContext::SharedHandleDetails handleDetails;
  if (!mGL->GetSharedHandleDetails(mShareType, mSharedHandle, handleDetails)) {
    NS_ERROR("Failed to get shared handle details");
    return nullptr;
  }
 
  MakeTextureIfNeeded(mGL, mTextureHandle);
 
  mGL->fActiveTexture(LOCAL_GL_TEXTURE0);
  mGL->fBindTexture(handleDetails.mTarget, mTextureHandle);
  if (!mGL->AttachSharedHandle(mShareType, mSharedHandle)) {
    NS_ERROR("Failed to bind shared texture handle");
    return nullptr;
  }
 
  if (mFlags & UseOpaqueSurface) {
    return new EffectRGBX(new SimpleTextureSourceOGL(this), true, aFilter, mFlags & NeedsYFlip);
  } else if (handleDetails.mProgramType == gl::RGBALayerProgramType) {
    return new EffectRGBA(new SimpleTextureSourceOGL(this), true, aFilter, mFlags & NeedsYFlip);
  } else if (handleDetails.mProgramType == gl::RGBALayerExternalProgramType) {
    gfx::Matrix4x4 textureTransform;
    ToMatrix4x4(handleDetails.mTextureTransform, textureTransform);
    return new EffectRGBAExternal(new SimpleTextureSourceOGL(this), textureTransform, true, aFilter, mFlags & NeedsYFlip);
  } else {
    NS_RUNTIMEABORT("Shader type not yet supported");
    return nullptr;
  }
}
 
void
TextureHostOGLShared::Unlock()
{
  mGL->DetachSharedHandle(mShareType, mSharedHandle);
  mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, 0);
}
 
void
TextureHostOGLSharedWithBuffer::UpdateImpl(const SharedImage& aImage,
                                       bool* aIsInitialised,
                                       bool* aNeedsReset)
{
  TextureHostOGLShared::UpdateImpl(aImage);

  if (aIsInitialised) {
    *aIsInitialised = true;
  }
}

void
YCbCrTextureHostOGL::UpdateImpl(const SharedImage& aImage,
                         bool* aIsInitialised,
                         bool* aNeedsReset)
{
  NS_ASSERTION(aImage.type() == SharedImage::TYCbCrImage, "SharedImage mismatch");

  ShmemYCbCrImage shmemImage(aImage.get_YCbCrImage().data(),
                             aImage.get_YCbCrImage().offset());

  gfxIntSize gfxSize = shmemImage.GetYSize();
  gfx::IntSize size = gfx::IntSize(gfxSize.width, gfxSize.height);
  gfxIntSize gfxCbCrSize = shmemImage.GetCbCrSize();
  gfx::IntSize CbCrSize = gfx::IntSize(gfxCbCrSize.width, gfxCbCrSize.height);

  if (!mYTexture || mYTexture->GetSize() != gfxSize) {
    mYTexture = mGL->CreateTextureImage(gfxSize,
                                        gfxASurface::CONTENT_ALPHA,
                                        WrapMode(mGL, mFlags), // TODO check the flags
                                        FlagsToGLFlags(mFlags)).get();
  }
  if (!mCbTexture || mCbTexture->GetSize() != gfxCbCrSize) {
    mCbTexture = mGL->CreateTextureImage(gfxCbCrSize,
                                         gfxASurface::CONTENT_ALPHA,
                                         WrapMode(mGL, mFlags), // TODO check the flags
                                         FlagsToGLFlags(mFlags)).get();
  }
  if (!mCrTexture || mCrTexture->GetSize() != gfxSize) {
    mCrTexture = mGL->CreateTextureImage(gfxCbCrSize,
                                         gfxASurface::CONTENT_ALPHA,
                                         WrapMode(mGL, mFlags), // TODO check the flags
                                         FlagsToGLFlags(mFlags)).get();
  }

  gfxImageSurface tempY(shmemImage.GetYData(),
                        gfxSize, shmemImage.GetYStride(),
                        gfxASurface::ImageFormatA8);
  gfxImageSurface tempCb(shmemImage.GetCbData(),
                         gfxCbCrSize, shmemImage.GetCbCrStride(),
                         gfxASurface::ImageFormatA8);
  gfxImageSurface tempCr(shmemImage.GetCrData(),
                         gfxCbCrSize, shmemImage.GetCbCrStride(),
                         gfxASurface::ImageFormatA8);

  nsIntRegion yRegion(nsIntRect(0, 0, gfxSize.width, gfxSize.height));
  nsIntRegion cbCrRegion(nsIntRect(0, 0, gfxCbCrSize.width, gfxCbCrSize.height));
  
  mYTexture->DirectUpdate(&tempY, yRegion);
  mCbTexture->DirectUpdate(&tempCb, cbCrRegion);
  mCrTexture->DirectUpdate(&tempCr, cbCrRegion);

  if (aIsInitialised) {
    *aIsInitialised = true;
  }
}

Effect*
YCbCrTextureHostOGL::Lock(const gfx::Filter& aFilter)
{
  EffectYCbCr* effect = new EffectYCbCr(new SimpleTextureSourceOGL(this, 0),
                                        new SimpleTextureSourceOGL(this, 1),
                                        new SimpleTextureSourceOGL(this, 2),
                                        aFilter);
  return effect;
}


/*
void
GLTextureAsTextureHost::UpdateImpl(const SharedImage& aImage,
                               bool* aIsInitialised,
                               bool* aNeedsReset)
{
  AutoOpenSurface surf(OPEN_READ_ONLY, aImage.get_SurfaceDescriptor());
     
  mSize = gfx::IntSize(surf.Size().width, surf.Size().height);
 
  if (!mTexture.IsAllocated()) {
    mTexture.Allocate(mGL);
 
    NS_ASSERTION(mTexture.IsAllocated(),
                  "Texture allocation failed!");
 
    mGL->MakeCurrent();
    mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mTexture.GetTextureID());
    mGL->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_WRAP_S, LOCAL_GL_CLAMP_TO_EDGE);
    mGL->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_WRAP_T, LOCAL_GL_CLAMP_TO_EDGE);
  }
 
  //TODO[nrc] I don't see why we need a new image surface here, but should check
  //nsRefPtr<gfxASurface> surf = new gfxImageSurface(aData.mYChannel,
  //                                                  mSize,
  //                                                  aData.mYStride,
  //                                                  gfxASurface::ImageFormatA8);
  GLuint textureId = mTexture.GetTextureID();
  mGL->UploadSurfaceToTexture(surf.GetAsImage(),
                              nsIntRect(0, 0, mSize.width, mSize.height),
                              textureId,
                              true);
  NS_ASSERTION(textureId == mTexture.GetTextureID(), "texture handle id changed");
 
  if (aIsInitialised) {
    *aIsInitialised = true;
  }
}

void
GLTextureAsTextureSource::UpdateImpl(gfx::IntSize aSize, uint8_t* aData, uint32_t aStride, GLContext* aGL)
{
  if (aSize != mSize || !mTexture.IsAllocated()) {
    mSize = aSize;

    if (!mTexture.IsAllocated()) {
      mTexture.Allocate(aGL);
    }

    aGL->MakeCurrent();
    aGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mTexture.GetTextureID());
    aGL->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_WRAP_S, LOCAL_GL_CLAMP_TO_EDGE);
    aGL->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_WRAP_T, LOCAL_GL_CLAMP_TO_EDGE);
  }

  GLuint textureId = mTexture.GetTextureID();
  nsRefPtr<gfxASurface> surf = new gfxImageSurface(aData,
                                                   gfxIntSize(mSize.width, mSize.height),
                                                   aStride,
                                                   gfxASurface::ImageFormatA8);
  aGL->UploadSurfaceToTexture(surf,
                              nsIntRect(0, 0, mSize.width, mSize.height),
                              textureId,
                              true);
  NS_ASSERTION(textureId == mTexture.GetTextureID(), "texture handle id changed");
}

void
YCbCrTextureHost::UpdateImpl(const SharedImage& aImage,
                         bool* aIsInitialised,
                         bool* aNeedsReset)
{
  NS_ASSERTION(aImage.type() == SharedImage::TYCbCrImage, "SharedImage mismatch");

  ShmemYCbCrImage shmemImage(aImage.get_YCbCrImage().data(),
                             aImage.get_YCbCrImage().offset());

  gfxIntSize gfxSize = shmemImage.GetYSize();
  gfx::IntSize size = gfx::IntSize(gfxSize.width, gfxSize.height);
  gfxIntSize gfxCbCrSize = shmemImage.GetCbCrSize();
  gfx::IntSize CbCrSize = gfx::IntSize(gfxCbCrSize.width, gfxCbCrSize.height);

  mTextures[0].UpdateImpl(size, shmemImage.GetYData(), shmemImage.GetYStride(), mGL);
  mTextures[1].UpdateImpl(CbCrSize, shmemImage.GetCbData(), shmemImage.GetCbCrStride(), mGL);
  mTextures[2].UpdateImpl(CbCrSize, shmemImage.GetCrData(), shmemImage.GetCbCrStride(), mGL);

  if (aIsInitialised) {
    *aIsInitialised = true;
  }
}

Effect*
YCbCrTextureHost::Lock(const gfx::Filter& aFilter)
{
  EffectYCbCr* effect = new EffectYCbCr(&mTextures[0],
                                        &mTextures[1],
                                        &mTextures[2],
                                        aFilter);

  return effect;
}

#ifdef MOZ_WIDGET_GONK
void
DirectExternalTextureHost::UpdateImpl(const SharedImage& aImage,
                                      bool* aIsInitialised,
                                      bool* aNeedsReset)
{
  NS_ASSERTION(aImage->type() == SharedImage::TSurfaceDescriptor);
  NS_ASSERTION(aImage->get_SurfaceDescriptor().type() == SurfaceDescriptor::TSurfaceDescriptorGralloc));

  const SurfaceDescriptorGralloc& desc = aImage->get_SurfaceDescriptor().get_SurfaceDescriptorGralloc();
  sp<GraphicBuffer> graphicBuffer = GrallocBufferActor::GetFrom(desc);
  mSize = gfx::IntSize(graphicBuffer->getWidth(), graphicBuffer->getHeight());
  if (!mExternalBufferTexture.IsAllocated()) {
    mExternalBufferTexture.Allocate(mGL);
  }
  mGL->MakeCurrent();
  mGL->fActiveTexture(LOCAL_GL_TEXTURE0);
  mGL->BindExternalBuffer(mExternalBufferTexture.GetTextureID(), graphicBuffer->getNativeBuffer());

  if (aIsInitialised) {
    *aIsInitialised = true;
  }
}

Effect*
DirectExternalTextureHost::Lock(const gfx::Filter& aFilter)
{
  if (!mExternalBufferTexture.IsAllocated()) {
    return nullptr;
  }

  return new EffectRGBAExternal(this, gfx::Matrix4x4(), false, aFilter, false);
}
#endif

*/

} // namespace
} // namespace
