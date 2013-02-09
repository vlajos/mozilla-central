/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_COMPOSITINGRENDERTARGETOGL_H
#define MOZILLA_GFX_COMPOSITINGRENDERTARGETOGL_H

#include "mozilla/layers/Compositor.h"
#include "gfxASurface.h"

#ifdef MOZ_DUMP_PAINTING
#include "mozilla/layers/CompositorOGL.h"
#endif

namespace mozilla {
namespace gl {
  class TextureImage;
  class BindableTexture;
}
namespace layers {

class CompositingRenderTargetOGL : public CompositingRenderTarget,
                                   public TextureSourceOGL
{
  typedef gfxASurface::gfxContentType ContentType;
  typedef mozilla::gl::GLContext GLContext;

public:
  CompositingRenderTargetOGL(GLContext* aGL, GLuint aTexure, GLuint aFBO)
    : mGL(aGL), mTextureHandle(aTexure), mFBO(aFBO)
  {}

  TextureSourceOGL* AsSourceOGL() MOZ_OVERRIDE { return this; }

  gfx::IntSize GetSize() const MOZ_OVERRIDE { return mSize; }

  void BindTexture(GLenum aTextureUnit) MOZ_OVERRIDE {
    MOZ_ASSERT(mTextureHandle != 0);
    mGL->fActiveTexture(aTextureUnit);
    mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mTextureHandle);
  }

  bool IsValid() const MOZ_OVERRIDE {
    return mTextureHandle != 0;
  }

  GLuint GetFBO() const {
    return mFBO;
  }

  ~CompositingRenderTargetOGL()
  {
    mGL->fDeleteTextures(1, &mTextureHandle);
    mGL->fDeleteFramebuffers(1, &mFBO);
  }

  GLenum GetWrapMode() const MOZ_OVERRIDE {
    return LOCAL_GL_REPEAT; // TODO[nical] not sure this is the right thing to return by default. [nrc] - Almost certainly not, probably there should not be a default.
                            // maybe return mWrapMode, although that never gets set. I have no
    // idea what is going here, TBH
  }

#ifdef MOZ_DUMP_PAINTING
  virtual already_AddRefed<gfxImageSurface> Dump(Compositor* aCompositor)
  {
    CompositorOGL* compositorOGL = static_cast<CompositorOGL*>(aCompositor);
    return mGL->GetTexImage(mTextureHandle, true, compositorOGL->GetFBOLayerProgramType());
  }
#endif

  GLContext* mGL;
  GLuint mTextureHandle;
  GLuint mFBO;

  //TODO[nical] none of these ever get set!
  gfx::IntSize mSize;
  GLenum mWrapMode;
  ContentType mContentType;

};


}
}

#endif /* MOZILLA_GFX_SURFACEOGL_H */
