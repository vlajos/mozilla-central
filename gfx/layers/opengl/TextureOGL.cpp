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

void TextureImageAsTextureHostOGL::BindTexture(GLenum aTextureUnit) {
  mTexture->BindTexture(aTextureUnit);
}

void TextureImageAsTextureHostOGL::UpdateImpl(const SurfaceDescriptor& aImage,
                                       bool* aIsInitialised,
                                       bool* aNeedsReset)
{
  AutoOpenSurface surf(OPEN_READ_ONLY, aImage);
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

  if (aIsInitialised) {
    *aIsInitialised = true;
  }
}

// thebes
void
TextureImageAsTextureHostOGL::UpdateRegionImpl(gfxASurface* aSurface,
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
    return new EffectRGBX(this, true, aFilter, mFlags & NeedsYFlip);
  case gl::BGRXLayerProgramType :
    return new EffectBGRX(this, true, aFilter, mFlags & NeedsYFlip);
  case gl::BGRALayerProgramType :
    return new EffectBGRA(this, true, aFilter, mFlags & NeedsYFlip);
  case gl::RGBALayerProgramType :
    return new EffectRGBA(this, true, aFilter, mFlags & NeedsYFlip);
  }
  NS_RUNTIMEABORT("Shader type not yet supported");
  return nullptr;
}

void
TextureImageAsTextureHostOGL::Abort()
{
  if (mTexture->InUpdate()) {
    mTexture->EndUpdate();
  }
}

void
TextureHostOGLShared::UpdateImpl(const SurfaceDescriptor& aImage,
                             bool* aIsInitialised,
                             bool* aNeedsReset)
{
  NS_ASSERTION(aImage.type() == SurfaceDescriptor::TSharedTextureDescriptor,
              "Invalid descriptor");

  SharedTextureDescriptor texture = aImage.get_SharedTextureDescriptor();

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
    return new EffectRGBX(this, true, aFilter, mFlags & NeedsYFlip);
  } else if (handleDetails.mProgramType == gl::RGBALayerProgramType) {
    return new EffectRGBA(this, true, aFilter, mFlags & NeedsYFlip);
  } else if (handleDetails.mProgramType == gl::RGBALayerExternalProgramType) {
    gfx::Matrix4x4 textureTransform;
    ToMatrix4x4(handleDetails.mTextureTransform, textureTransform);
    return new EffectRGBAExternal(this, textureTransform, true, aFilter, mFlags & NeedsYFlip);
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
YCbCrTextureHostOGL::UpdateImpl(const SurfaceDescriptor& aImage,
                         bool* aIsInitialised,
                         bool* aNeedsReset)
{
  NS_ASSERTION(aImage.type() == SurfaceDescriptor::TYCbCrImage, "SurfaceDescriptor mismatch");

  ShmemYCbCrImage shmemImage(aImage.get_YCbCrImage().data(),
                             aImage.get_YCbCrImage().offset());

  gfxIntSize gfxSize = shmemImage.GetYSize();
  gfxIntSize gfxCbCrSize = shmemImage.GetCbCrSize();

  if (!mYTexture.mTexImage || mYTexture.mTexImage->GetSize() != gfxSize) {
    mYTexture.mTexImage = mGL->CreateTextureImage(gfxSize,
                                        gfxASurface::CONTENT_ALPHA,
                                        WrapMode(mGL, mFlags), // TODO check the flags
                                        FlagsToGLFlags(mFlags)).get();
  }
  if (!mCbTexture.mTexImage || mCbTexture.mTexImage->GetSize() != gfxCbCrSize) {
    mCbTexture.mTexImage = mGL->CreateTextureImage(gfxCbCrSize,
                                         gfxASurface::CONTENT_ALPHA,
                                         WrapMode(mGL, mFlags), // TODO check the flags
                                         FlagsToGLFlags(mFlags)).get();
  }
  if (!mCrTexture.mTexImage || mCrTexture.mTexImage->GetSize() != gfxSize) {
    mCrTexture.mTexImage = mGL->CreateTextureImage(gfxCbCrSize,
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
  
  mYTexture.mTexImage->DirectUpdate(&tempY, yRegion);
  mCbTexture.mTexImage->DirectUpdate(&tempCb, cbCrRegion);
  mCrTexture.mTexImage->DirectUpdate(&tempCr, cbCrRegion);

  if (aIsInitialised) {
    *aIsInitialised = true;
  }
}

Effect*
YCbCrTextureHostOGL::Lock(const gfx::Filter& aFilter)
{
  return new EffectYCbCr(this, aFilter);
}


} // namespace
} // namespace
