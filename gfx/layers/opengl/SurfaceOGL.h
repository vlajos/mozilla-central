/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SURFACEOGL_H
#define MOZILLA_GFX_SURFACEOGL_H

#include "Compositor.h"

#ifdef MOZ_DUMP_PAINTING
#include "CompositorOGL.h"
#endif

namespace mozilla {

namespace layers {

class SurfaceOGL : public Surface
{
public:
  SurfaceOGL(GLContext* aGL)
    : mGL(aGL) 
  {}

  ~SurfaceOGL()
  {
    mGL->fDeleteTextures(1, &mTexture);
    mGL->fDeleteFramebuffers(1, &mFBO);
  }

#ifdef MOZ_DUMP_PAINTING
  virtual already_AddRefed<gfxImageSurface> Dump(Compositor* aCompositor)
  {
    CompositorOGL* compositorOGL = static_cast<CompositorOGL*>(aCompositor);
    return mGL->GetTexImage(mTexture, true, compositorOGL->GetFBOLayerProgramType());
  }
#endif

  gfx::IntSize mSize;
  GLuint mTexture;
  GLuint mFBO;

private:
  GLContext* mGL;
};


}
}

#endif /* MOZILLA_GFX_SURFACEOGL_H */
