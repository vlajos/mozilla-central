/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_COMPOSITINGRENDERTARGETOGL_H
#define MOZILLA_GFX_COMPOSITINGRENDERTARGETOGL_H

#include "mozilla/layers/Compositor.h"

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
  typedef mozilla::gl::GLContext GLContext;

public:
  CompositingRenderTargetOGL(GLContext* aGL, GLuint aTexure, GLuint aFBO)
    : mGL(aGL), mFBO(aFBO), mTexture(new gl::BasicTexture(aGL, aTexure))
  {}

  TextureSourceOGL* AsSourceOGL() MOZ_OVERRIDE { return this; }

  gfx::IntSize GetSize() const MOZ_OVERRIDE { return mSize; }

  void BindTexture(GLenum aTextureUnit) MOZ_OVERRIDE {
    mTexture->BindTexture(aTextureUnit);
  }

  bool IsValid() const MOZ_OVERRIDE {
    return mTexture->GetTextureID() != 0;
  }

  GLuint GetFBO() const {
    return mFBO;
  }

  ~CompositingRenderTargetOGL()
  {
    mTexture = nullptr;
    mGL->fDeleteFramebuffers(1, &mFBO);
  }

  GLenum GetWrapMode() const MOZ_OVERRIDE {
    return LOCAL_GL_REPEAT; // TODO[nical] not sure this is the right thing to return by default.
  }

#ifdef MOZ_DUMP_PAINTING
  virtual already_AddRefed<gfxImageSurface> Dump(Compositor* aCompositor)
  {
    CompositorOGL* compositorOGL = static_cast<CompositorOGL*>(aCompositor);
    return mGL->GetTexImage(mTexture->GetTextureID(), true, compositorOGL->GetFBOLayerProgramType());
  }
#endif

  GLContext* mGL;
  GLuint mFBO;
  gfx::IntSize mSize;
  GLuint mTextureHandle;

  RefPtr<gl::BasicTexture> mTexture;
};


}
}

#endif /* MOZILLA_GFX_SURFACEOGL_H */
