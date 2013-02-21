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

class CompositingRenderTargetOGL : public CompositingRenderTarget
{
  typedef gfxASurface::gfxContentType ContentType;
  typedef mozilla::gl::GLContext GLContext;

public:
  CompositingRenderTargetOGL(GLContext* aGL, GLuint aTexure, GLuint aFBO)
    : mGL(aGL), mTextureHandle(aTexure), mFBO(aFBO)
  {}

  ~CompositingRenderTargetOGL()
  {
    mGL->fDeleteTextures(1, &mTextureHandle);
    mGL->fDeleteFramebuffers(1, &mFBO);
  }

  void BindTexture(GLenum aTextureUnit) {
    MOZ_ASSERT(mTextureHandle != 0);
    mGL->fActiveTexture(aTextureUnit);
    mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mTextureHandle);
  }

  // TextureSourceOGL
  TextureSourceOGL* AsSourceOGL() MOZ_OVERRIDE
  {
    MOZ_ASSERT(false, "CompositingRenderTargetOGL should not be used as a TextureSource");
    return nullptr;
  }
  gfx::IntSize GetSize() const MOZ_OVERRIDE
  {
    MOZ_ASSERT(false, "CompositingRenderTargetOGL should not be used as a TextureSource");
    return gfx::IntSize(0, 0);
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
};


}
}

#endif /* MOZILLA_GFX_SURFACEOGL_H */
