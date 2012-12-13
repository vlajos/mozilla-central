/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/PLayersChild.h"
#include "mozilla/layers/TextureOGL.h"
#include "TiledThebesLayerOGL.h"
#include "BasicTiledThebesLayer.h"
#include "gfxImageSurface.h"
#include "gfx2DGlue.h"

namespace mozilla {
namespace layers {

using mozilla::gl::GLContext;

TiledLayerBufferOGL::~TiledLayerBufferOGL()
{
  if (mRetainedTiles.Length() == 0)
    return;

  mContext->MakeCurrent();
  for (size_t i = 0; i < mRetainedTiles.Length(); i++) {
    if (mRetainedTiles[i] == GetPlaceholderTile())
      continue;
    mContext->fDeleteTextures(1, &mRetainedTiles[i].mTextureHandle);
  }
}

void
TiledLayerBufferOGL::ReleaseTile(TiledTexture aTile)
{
  // We've made current prior to calling TiledLayerBufferOGL::Update
  if (aTile == GetPlaceholderTile())
    return;
  mContext->fDeleteTextures(1, &aTile.mTextureHandle);
}

void
TiledLayerBufferOGL::Upload(const BasicTiledLayerBuffer* aMainMemoryTiledBuffer,
                            const nsIntRegion& aNewValidRegion,
                            const nsIntRegion& aInvalidateRegion,
                            const gfxSize& aResolution)
{
#ifdef GFX_TILEDLAYER_PREF_WARNINGS
  printf_stderr("Upload %i, %i, %i, %i\n", aInvalidateRegion.GetBounds().x, aInvalidateRegion.GetBounds().y, aInvalidateRegion.GetBounds().width, aInvalidateRegion.GetBounds().height);
  long start = PR_IntervalNow();
#endif

  mResolution = aResolution;
  mMainMemoryTiledBuffer = aMainMemoryTiledBuffer;
  mContext->MakeCurrent();
  Update(aNewValidRegion, aInvalidateRegion);
  mMainMemoryTiledBuffer = nullptr;
#ifdef GFX_TILEDLAYER_PREF_WARNINGS
  if (PR_IntervalNow() - start > 10) {
    printf_stderr("Time to upload %i\n", PR_IntervalNow() - start);
  }
#endif
}

void
TiledLayerBufferOGL::GetFormatAndTileForImageFormat(gfxASurface::gfxImageFormat aFormat,
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

TiledTexture
TiledLayerBufferOGL::ValidateTile(TiledTexture aTile,
                                  const nsIntPoint& aTileOrigin,
                                  const nsIntRegion& aDirtyRect)
{
#ifdef GFX_TILEDLAYER_PREF_WARNINGS
  printf_stderr("Upload tile %i, %i\n", aTileOrigin.x, aTileOrigin.y);
  long start = PR_IntervalNow();
#endif
  if (aTile == GetPlaceholderTile()) {
    mContext->fGenTextures(1, &aTile.mTextureHandle);
    mContext->fBindTexture(LOCAL_GL_TEXTURE_2D, aTile.mTextureHandle);
    mContext->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_MIN_FILTER, LOCAL_GL_LINEAR);
    mContext->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_MAG_FILTER, LOCAL_GL_LINEAR);
    mContext->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_WRAP_S, LOCAL_GL_CLAMP_TO_EDGE);
    mContext->fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_WRAP_T, LOCAL_GL_CLAMP_TO_EDGE);
  } else {
    mContext->fBindTexture(LOCAL_GL_TEXTURE_2D, aTile.mTextureHandle);
  }

  nsRefPtr<gfxReusableSurfaceWrapper> reusableSurface = mMainMemoryTiledBuffer->GetTile(aTileOrigin).mSurface.get();
  GLenum format, type;
  GetFormatAndTileForImageFormat(reusableSurface->Format(), format, type);

  const unsigned char* buf = reusableSurface->GetReadOnlyData();
  mContext->fTexImage2D(LOCAL_GL_TEXTURE_2D, 0, format,
                       GetTileLength(), GetTileLength(), 0,
                       format, type, buf);

  aTile.mFormat = format;

#ifdef GFX_TILEDLAYER_PREF_WARNINGS
  if (PR_IntervalNow() - start > 1) {
    printf_stderr("Tile Time to upload %i\n", PR_IntervalNow() - start);
  }
#endif
  return aTile;
}

} // mozilla
} // layers
