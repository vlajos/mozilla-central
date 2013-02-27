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
#include "SurfaceStream.h"
#include "SharedSurface.h"
#include "SharedSurfaceGL.h"
#include "SharedSurfaceEGL.h"

using namespace mozilla::gl;

namespace mozilla {

using namespace gfx;

namespace layers {

static void
MakeTextureIfNeeded(gl::GLContext* gl, GLenum aTarget, GLuint& aTexture)
{
  if (aTexture != 0)
    return;

  gl->fGenTextures(1, &aTexture);

  gl->fActiveTexture(LOCAL_GL_TEXTURE0);
  gl->fBindTexture(aTarget, aTexture);

  gl->fTexParameteri(aTarget, LOCAL_GL_TEXTURE_MIN_FILTER, LOCAL_GL_LINEAR);
  gl->fTexParameteri(aTarget, LOCAL_GL_TEXTURE_MAG_FILTER, LOCAL_GL_LINEAR);
  gl->fTexParameteri(aTarget, LOCAL_GL_TEXTURE_WRAP_S, LOCAL_GL_CLAMP_TO_EDGE);
  gl->fTexParameteri(aTarget, LOCAL_GL_TEXTURE_WRAP_T, LOCAL_GL_CLAMP_TO_EDGE);
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

gfx::SurfaceFormat FormatFromShaderType(ShaderProgramType aShaderType)
{
  switch (aShaderType) {
  case RGBALayerProgramType:
  case RGBALayerExternalProgramType:
  case RGBARectLayerProgramType:
  case RGBAExternalLayerProgramType:
    return FORMAT_R8G8B8A8;
  case RGBXLayerProgramType:
    return FORMAT_R8G8B8X8;
  case BGRALayerProgramType:
    return FORMAT_B8G8R8A8;
  case BGRXLayerProgramType:
    return FORMAT_B8G8R8X8;
  default:
    MOZ_NOT_REACHED("Unsupported texture shader type");
    return FORMAT_UNKNOWN;
  }
}

void TextureImageTextureHostOGL::UpdateImpl(const SurfaceDescriptor& aImage,
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
                                       FlagsToGLFlags(mFlags));
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

  if (mTexture->InUpdate()) {
    mTexture->EndUpdate();
  }

  // XXX - Bas - Get YFlip data out!
  mFormat = FormatFromShaderType(mTexture->GetShaderProgramType());
}

bool
TextureImageTextureHostOGL::Lock()
{
  if (!mTexture) {
    NS_WARNING("TextureImageAsTextureHost to be composited without texture");
    return false;
  }

  NS_ASSERTION(mTexture->GetContentType() != gfxASurface::CONTENT_ALPHA,
                "Image layer has alpha image");

  return true;
}

void
TextureImageTextureHostOGL::Abort()
{
  if (mTexture->InUpdate()) {
    mTexture->EndUpdate();
  }
}

void
SharedTextureHostOGL::UpdateImpl(const SurfaceDescriptor& aImage,
                                 bool* aIsInitialised,
                                 bool* aNeedsReset,
                                 nsIntRegion* aRegion)
{
  // Just retain a reference to the new image, rather than making a copy.
  // This seems potentially bad, but it's what the existing code did.
  SwapTexturesImpl(aImage, aIsInitialised, aNeedsReset, aRegion);
}

void
SharedTextureHostOGL::SwapTexturesImpl(const SurfaceDescriptor& aImage,
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

  mSharedHandle = newHandle;

  GLContext::SharedHandleDetails handleDetails;
  if (mGL->GetSharedHandleDetails(mShareType, mSharedHandle, handleDetails)) {
    mTextureTarget = handleDetails.mTarget;
    mShaderProgram = handleDetails.mProgramType;
    mFormat = FormatFromShaderType(mShaderProgram);
  }

  if (aIsInitialised) {
    *aIsInitialised = true;
  }
}
 
bool
SharedTextureHostOGL::Lock()
{
  MakeTextureIfNeeded(mGL, mTextureTarget, mTextureHandle);

  mGL->fActiveTexture(LOCAL_GL_TEXTURE0);
  mGL->fBindTexture(mTextureTarget, mTextureHandle);
  if (!mGL->AttachSharedHandle(mShareType, mSharedHandle)) {
    NS_ERROR("Failed to bind shared texture handle");
    return false;
  }

  return true;
}

void
SharedTextureHostOGL::Unlock()
{
  mGL->DetachSharedHandle(mShareType, mSharedHandle);
  mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, 0);
}

void
SurfaceStreamHostOGL::SwapTexturesImpl(const SurfaceDescriptor& aImage,
                                       bool* aIsInitialised,
                                       bool* aNeedsReset,
                                       nsIntRegion* aRegion)
{
  NS_ASSERTION(aImage.type() == SurfaceDescriptor::TSurfaceStreamDescriptor,
              "Invalid descriptor");

  if (aIsInitialised) {
    *aIsInitialised = true;
  }
}

void
SurfaceStreamHostOGL::Unlock()
{
  // We don't know what this is unless we're locked
  mFormat = gfx::FORMAT_UNKNOWN;
}

bool
SurfaceStreamHostOGL::Lock()
{
  mGL->MakeCurrent();
  SurfaceStream* surfStream = nullptr;
  SharedSurface* sharedSurf = nullptr;
  const SurfaceStreamDescriptor& streamDesc =
    mBuffer->get_SurfaceStreamDescriptor();

  surfStream = SurfaceStream::FromHandle(streamDesc.handle());
  MOZ_ASSERT(surfStream);

  sharedSurf = surfStream->SwapConsumer();
  if (!sharedSurf) {
    // We don't have a valid surf to show yet.
    return false;
  }

  mGL->MakeCurrent();

  mSize = IntSize(sharedSurf->Size().width, sharedSurf->Size().height);

  gfxImageSurface* toUpload = nullptr;
  switch (sharedSurf->Type()) {
    case SharedSurfaceType::GLTextureShare: {
      mTextureHandle = SharedSurface_GLTexture::Cast(sharedSurf)->Texture();
      MOZ_ASSERT(mTextureHandle);
      mShaderProgram = sharedSurf->HasAlpha() ? RGBALayerProgramType
                                              : RGBXLayerProgramType;
      break;
    }
    case SharedSurfaceType::EGLImageShare: {
      SharedSurface_EGLImage* eglImageSurf =
          SharedSurface_EGLImage::Cast(sharedSurf);

      mTextureHandle = eglImageSurf->AcquireConsumerTexture(mGL);
      if (!mTextureHandle) {
        toUpload = eglImageSurf->GetPixels();
        MOZ_ASSERT(toUpload);
      } else {
        mShaderProgram = sharedSurf->HasAlpha() ? RGBALayerProgramType
                                                : RGBXLayerProgramType;
      }
      break;
    }
    case SharedSurfaceType::Basic: {
      toUpload = SharedSurface_Basic::Cast(sharedSurf)->GetData();
      MOZ_ASSERT(toUpload);
      break;
    }
    default:
      MOZ_NOT_REACHED("Invalid SharedSurface type.");
      return false;
  }

  if (toUpload) {
    // mBounds seems to end up as (0,0,0,0) a lot, so don't use it?
    nsIntSize size(toUpload->GetSize());
    nsIntRect rect(nsIntPoint(0,0), size);
    nsIntRegion bounds(rect);
    mShaderProgram = mGL->UploadSurfaceToTexture(toUpload,
                                                 bounds,
                                                 mUploadTexture,
                                                 true);
    mTextureHandle = mUploadTexture;
  }
    
  mFormat = FormatFromShaderType(mShaderProgram);

  MOZ_ASSERT(mTextureHandle);
  mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mTextureHandle);
  mGL->fTexParameteri(LOCAL_GL_TEXTURE_2D,
                      LOCAL_GL_TEXTURE_WRAP_S,
                      LOCAL_GL_CLAMP_TO_EDGE);
  mGL->fTexParameteri(LOCAL_GL_TEXTURE_2D,
                      LOCAL_GL_TEXTURE_WRAP_T, 
                      LOCAL_GL_CLAMP_TO_EDGE);
  return true;
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

TiledTextureHostOGL::~TiledTextureHostOGL()
{
  if (mTextureHandle) {
    mGL->MakeCurrent();
    mGL->fDeleteTextures(1, &mTextureHandle);

    gl::GLContext::UpdateTextureMemoryUsage(gl::GLContext::MemoryFreed, mGLFormat,
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
TiledTextureHostOGL::Update(gfxReusableSurfaceWrapper* aReusableSurface, TextureFlags aFlags, const gfx::IntSize& aSize)
{
  mSize = aSize;
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
    gl::GLContext::UpdateTextureMemoryUsage(gl::GLContext::MemoryFreed, mGLFormat,
                                            GetTileType(), TILEDLAYERBUFFER_TILE_SIZE);
  }

  GLenum type;
  GetFormatAndTileForImageFormat(aReusableSurface->Format(), mGLFormat, type);

  const unsigned char* buf = aReusableSurface->GetReadOnlyData();
  mGL->fTexImage2D(LOCAL_GL_TEXTURE_2D, 0, mGLFormat,
                   TILEDLAYERBUFFER_TILE_SIZE, TILEDLAYERBUFFER_TILE_SIZE, 0,
                   mGLFormat, type, buf);

  gl::GLContext::UpdateTextureMemoryUsage(gl::GLContext::MemoryAllocated, mGLFormat,
                                          type, TILEDLAYERBUFFER_TILE_SIZE);

  if (mGLFormat == LOCAL_GL_RGB) {
    mFormat = FORMAT_R8G8B8X8;
  } else {
    mFormat = FORMAT_B8G8R8A8;
  }
}

bool
TiledTextureHostOGL::Lock()
{
  if (!mTextureHandle) {
    NS_WARNING("TiledTextureHostOGL not ready to be composited");
    return false;
  }

  //TODO[nrc] would be nice if we didn't need to do this
  mGL->MakeCurrent();
  mGL->fActiveTexture(LOCAL_GL_TEXTURE0);

  return true;
}


} // namespace
} // namespace
