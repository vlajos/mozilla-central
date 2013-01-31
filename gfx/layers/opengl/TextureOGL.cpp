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

using namespace gfx;

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

void TextureImageAsTextureHostOGL::UpdateImpl(const SurfaceDescriptor& aImage,
                                              bool* aIsInitialised,
                                              bool* aNeedsReset,
                                              nsIntRegion* aRegion)
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
  nsIntRegion updateRegion;
  
  if (!aRegion) {
    updateRegion = nsIntRegion(nsIntRect(0, 0, size.width, size.height));
  } else {
    updateRegion = *aRegion;
  }
  mTexture->DirectUpdate(surf.Get(), updateRegion);

  if (aIsInitialised) {
    *aIsInitialised = true;
  }
}

bool
TextureImageAsTextureHostOGL::Lock()
{
  if (!mTexture) {
    NS_WARNING("TextureImageAsTextureHost to be composited without texture");
    return false;
  }
  NS_ASSERTION(mTexture->GetContentType() != gfxASurface::CONTENT_ALPHA,
                "Image layer has alpha image");

  if (mTexture->InUpdate()) {
    mTexture->EndUpdate();
  }

  // XXX - Bas - Get YFlip data out!
  switch (mTexture->GetShaderProgramType()) {
#if 0
  case gl::RGBXLayerProgramType :
    return new EffectRGBX(this, true, aFilter, mFlags & NeedsYFlip);
  case gl::RGBALayerProgramType :
    return new EffectRGBA(this, true, aFilter, mFlags & NeedsYFlip);
#endif
  case gl::BGRXLayerProgramType :
    mFormat = FORMAT_B8G8R8X8;
    break;
  case gl::BGRALayerProgramType :
    mFormat = FORMAT_B8G8R8A8;
    break;
  default:
    // FIXME [bjacob] unhandled cases were reported as GCC warnings; with this,
    // at least we'll known if we run into them.
    MOZ_NOT_REACHED("unhandled program type");
  }
  return true;
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
                                 bool* aNeedsReset,
                                 nsIntRegion* aRegion)
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
 
bool
TextureHostOGLShared::Lock()
{
  GLContext::SharedHandleDetails handleDetails;
  if (!mGL->GetSharedHandleDetails(mShareType, mSharedHandle, handleDetails)) {
    NS_ERROR("Failed to get shared handle details");
    return false;
  }

  MakeTextureIfNeeded(mGL, mTextureHandle);

  mGL->fActiveTexture(LOCAL_GL_TEXTURE0);
  mGL->fBindTexture(handleDetails.mTarget, mTextureHandle);
  if (!mGL->AttachSharedHandle(mShareType, mSharedHandle)) {
    NS_ERROR("Failed to bind shared texture handle");
    return false;
  }

#if 0
  // XXX - Look into dealing with texture transform for RGBALayerExternal!!
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
#endif
  return true;
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
                                bool* aNeedsReset,
                                nsIntRegion* aRegion)
{
  NS_ASSERTION(aImage.type() == SurfaceDescriptor::TYCbCrImage, "SurfaceDescriptor mismatch");

  ShmemYCbCrImage shmemImage(aImage.get_YCbCrImage().data(),
                             aImage.get_YCbCrImage().offset());


  gfxIntSize gfxSize = shmemImage.GetYSize();
  gfxIntSize gfxCbCrSize = shmemImage.GetCbCrSize();
  nsIntRect picture = aImage.get_YCbCrImage().picture();

  if (!mYTexture->mTexImage || mYTexture->mTexImage->GetSize() != gfxSize) {
    mYTexture->mTexImage = mGL->CreateTextureImage(gfxSize,
                                        gfxASurface::CONTENT_ALPHA,
                                        WrapMode(mGL, mFlags), // TODO check the flags
                                        FlagsToGLFlags(mFlags)).get();
  }
  if (!mCbTexture->mTexImage || mCbTexture->mTexImage->GetSize() != gfxCbCrSize) {
    mCbTexture->mTexImage = mGL->CreateTextureImage(gfxCbCrSize,
                                         gfxASurface::CONTENT_ALPHA,
                                         WrapMode(mGL, mFlags), // TODO check the flags
                                         FlagsToGLFlags(mFlags)).get();
  }
  if (!mCrTexture->mTexImage || mCrTexture->mTexImage->GetSize() != gfxSize) {
    mCrTexture->mTexImage = mGL->CreateTextureImage(gfxCbCrSize,
                                         gfxASurface::CONTENT_ALPHA,
                                         WrapMode(mGL, mFlags), // TODO check the flags
                                         FlagsToGLFlags(mFlags)).get();
  }

  RefPtr<gfxImageSurface> tempY = new gfxImageSurface(shmemImage.GetYData(),
                                      gfxSize, shmemImage.GetYStride(),
                                      gfxASurface::ImageFormatA8);
  RefPtr<gfxImageSurface> tempCb = new gfxImageSurface(shmemImage.GetCbData(),
                                       gfxCbCrSize, shmemImage.GetCbCrStride(),
                                       gfxASurface::ImageFormatA8);
  RefPtr<gfxImageSurface> tempCr = new gfxImageSurface(shmemImage.GetCrData(),
                                       gfxCbCrSize, shmemImage.GetCbCrStride(),
                                       gfxASurface::ImageFormatA8);

  nsIntRegion yRegion(nsIntRect(0, 0, gfxSize.width, gfxSize.height));
  nsIntRegion cbCrRegion(nsIntRect(0, 0, gfxCbCrSize.width, gfxCbCrSize.height));
  
  mYTexture->mTexImage->DirectUpdate(tempY, yRegion);
  mCbTexture->mTexImage->DirectUpdate(tempCb, cbCrRegion);
  mCrTexture->mTexImage->DirectUpdate(tempCr, cbCrRegion);

  if (aIsInitialised) {
    *aIsInitialised = true;
  }
}

bool
YCbCrTextureHostOGL::Lock()
{
  return true;
}

TiledTextureHost::~TiledTextureHost()
{
  if (mTextureHandle) {
    mGL->MakeCurrent();
    mGL->fDeleteTextures(1, &mTextureHandle);

    gl::GLContext::UpdateTextureMemoryUsage(gl::GLContext::MemoryFreed, mFormat,
                                            GetTileType(), TILEDLAYERBUFFER_TILE_SIZE);
  }
}

static void
GetFormatAndTileForImageFormat(gfxASurface::gfxImageFormat aFormat,
                               GLenum& aOutFormat,
                               GLenum& aOutType)
{
  if (aFormat == gfxASurface::ImageFormatRGB16_565) {
    aOutFormat = LOCAL_GL_RGB;
    aOutType = LOCAL_GL_UNSIGNED_SHORT_5_6_5;
  } else {
    aOutFormat = LOCAL_GL_RGBA;
    aOutType = LOCAL_GL_UNSIGNED_BYTE;
  }
}

void
TiledTextureHost::Update(gfxReusableSurfaceWrapper* aReusableSurface, TextureFlags aFlags)
{
  mGL->MakeCurrent();
  if (aFlags & NewTile) {
    SetFlags(aFlags);
    mGL->fGenTextures(1, &mTextureHandle);
    mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mTextureHandle);
    mGL->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_MIN_FILTER, LOCAL_GL_LINEAR);
    mGL->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_MAG_FILTER, LOCAL_GL_LINEAR);
    mGL->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_WRAP_S, LOCAL_GL_CLAMP_TO_EDGE);
    mGL->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_WRAP_T, LOCAL_GL_CLAMP_TO_EDGE);
  } else {
    mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mTextureHandle);
    // We're re-using a texture, but the format may change. Update the memory
    // reporter with a free and alloc (below) using the old and new formats.
    gl::GLContext::UpdateTextureMemoryUsage(gl::GLContext::MemoryFreed, mFormat,
                                            GetTileType(), TILEDLAYERBUFFER_TILE_SIZE);
  }

  GLenum type;
  GetFormatAndTileForImageFormat(aReusableSurface->Format(), mGLFormat, type);

  const unsigned char* buf = aReusableSurface->GetReadOnlyData();
  mGL->fTexImage2D(LOCAL_GL_TEXTURE_2D, 0, mFormat,
                   TILEDLAYERBUFFER_TILE_SIZE, TILEDLAYERBUFFER_TILE_SIZE, 0,
                   mFormat, type, buf);

  gl::GLContext::UpdateTextureMemoryUsage(gl::GLContext::MemoryAllocated, mFormat,
                                          type, TILEDLAYERBUFFER_TILE_SIZE);

  if (mGLFormat == LOCAL_GL_RGB) {
    mFormat = FORMAT_B8G8R8X8;
  } else {
    mFormat = FORMAT_B8G8R8A8;
  }
}

bool
TiledTextureHost::Lock()
{
  if (!mTextureHandle) {
    NS_WARNING("TiledTextureHost not ready to be composited");
    return false;
  }

  //TODO[nrc] would be nice if we didn't need to do this
  mGL->MakeCurrent();
  mGL->fActiveTexture(LOCAL_GL_TEXTURE0);

  return true;
}


} // namespace
} // namespace
