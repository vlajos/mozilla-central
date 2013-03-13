/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
//  * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_TEXTURECLIENTOGL_H
#define MOZILLA_GFX_TEXTURECLIENTOGL_H

#include "mozilla/layers/TextureClient.h"

namespace mozilla {
namespace layers {

class TextureClientSharedGL : public TextureClient
{
public:
  TextureClientSharedGL(CompositableForwarder* aForwarder, CompositableType aCompositableType);
  ~TextureClientSharedGL() { ReleaseResources(); }

  virtual bool SupportsType(TextureClientType aType) MOZ_OVERRIDE { return aType == TEXTURE_SHARED_GL; }
  virtual void EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aType);
  virtual void ReleaseResources();

protected:
  gl::GLContext* mGL;
  gfx::IntSize mSize;

  friend class CompositingFactory;
};

// Doesn't own the surface descriptor, so we shouldn't delete it
class TextureClientSharedGLExternal : public TextureClientSharedGL
{
public:
  TextureClientSharedGLExternal(CompositableForwarder* aForwarder, CompositableType aCompositableType)
    : TextureClientSharedGL(aForwarder, aCompositableType)
  {}

  virtual bool SupportsType(TextureClientType aType) MOZ_OVERRIDE { return aType == TEXTURE_SHARED_GL_EXTERNAL; }
  virtual void ReleaseResources() {}
};

class TextureClientStreamGL : public TextureClient
{
public:
  TextureClientStreamGL(CompositableForwarder* aForwarder, CompositableType aCompositableType)
    : TextureClient(aForwarder, aCompositableType)
  {}
  ~TextureClientStreamGL() { ReleaseResources(); }
  
  virtual bool SupportsType(TextureClientType aType) MOZ_OVERRIDE { return aType == TEXTURE_STREAM_GL; }
  virtual void EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aType) { }
  virtual void ReleaseResources() { mDescriptor = SurfaceDescriptor(); }
};

} // namespace
} // namespace

#endif
