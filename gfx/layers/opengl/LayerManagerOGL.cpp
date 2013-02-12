/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/PLayers.h"
#include <algorithm>

/* This must occur *after* layers/PLayers.h to avoid typedefs conflicts. */
#include "mozilla/Util.h"

#include "LayerManagerOGL.h"
#include "ThebesLayerOGL.h"
#include "ContainerLayerOGL.h"
#include "ImageLayerOGL.h"
#include "ColorLayerOGL.h"
#include "CanvasLayerOGL.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/Preferences.h"
#include "TexturePoolOGL.h"
#include "TiledLayerBuffer.h"

#include "gfxContext.h"
#include "gfxUtils.h"
#include "gfxPlatform.h"
#include "nsIWidget.h"

#include "GLContext.h"
#include "GLContextProvider.h"
#include "Composer2D.h"

#include "nsIServiceManager.h"
#include "nsIConsoleService.h"

#include "gfxCrashReporterUtils.h"

#include "sampler.h"

#ifdef MOZ_WIDGET_ANDROID
#include <android/log.h>
#endif
#ifdef XP_MACOSX
#include "gfxPlatformMac.h"
#endif

namespace mozilla {
namespace layers {

using namespace mozilla::gfx;
using namespace mozilla::gl;

/**
 * LayerManagerOGL
 */
LayerManagerOGL::LayerManagerOGL(nsIWidget *aWidget)
{
  mCompositor = new CompositorOGL(aWidget);
}

LayerManagerOGL::~LayerManagerOGL()
{
  Destroy();
}

void*
LayerManagerOGL::GetNSOpenGLContext() const
{
  return gl()->GetNativeData(GLContext::NativeGLContext);
}

int32_t
LayerManagerOGL::GetMaxTextureSize() const
{
  int32_t maxSize;
  gl()->fGetIntegerv(LOCAL_GL_MAX_TEXTURE_SIZE, &maxSize);
  return maxSize;
}


void
LayerManagerOGL::Destroy()
{
  if (mDestroyed)
    return;

  if (mRoot) {
    RootLayer()->Destroy();
    mRoot = nullptr;
  }

  mCompositor->Destroy();

  mDestroyed = true;
}

void
LayerManagerOGL::UpdateRenderBounds(const nsIntRect& aRect)
{
  mRenderBounds = aRect;
}

void
LayerManagerOGL::BeginTransaction()
{
  mInTransaction = true;
}

void
LayerManagerOGL::BeginTransactionWithTarget(gfxContext *aTarget)
{
  mInTransaction = true;

#ifdef MOZ_LAYERS_HAVE_LOG
  MOZ_LAYERS_LOG(("[----- BeginTransaction"));
  Log();
#endif

  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return;
  }

  mCompositor->SetTarget(aTarget);
}

bool
LayerManagerOGL::EndEmptyTransaction(EndTransactionFlags aFlags)
{
  mInTransaction = false;

  if (!mRoot)
    return false;

  EndTransaction(nullptr, nullptr, aFlags);
  return true;
}

void
LayerManagerOGL::EndTransaction(DrawThebesLayerCallback aCallback,
                                void* aCallbackData,
                                EndTransactionFlags aFlags)
{
  mInTransaction = false;

#ifdef MOZ_LAYERS_HAVE_LOG
  MOZ_LAYERS_LOG(("  ----- (beginning paint)"));
  Log();
#endif

  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return;
  }

  if (mRoot && !(aFlags & END_NO_IMMEDIATE_REDRAW)) {
    if (aFlags & END_NO_COMPOSITE) {
      // Apply pending tree updates before recomputing effective
      // properties.
      mRoot->ApplyPendingUpdatesToSubtree();
    }

    // The results of our drawing always go directly into a pixel buffer,
    // so we don't need to pass any global transform here.
    mRoot->ComputeEffectiveTransforms(gfx3DMatrix());

    mThebesLayerCallback = aCallback;
    mThebesLayerCallbackData = aCallbackData;
    SetCompositingDisabled(aFlags & END_NO_COMPOSITE);

    bool needGLRender = true;
    if (mComposer2D && mComposer2D->TryRender(mRoot, mWorldMatrix)) {
      needGLRender = false;

    #ifdef MOZ_WIDGET_GONK
      mCompositor->AddFrameAndGetFps(TimeStamp::Now());
    #endif
      MOZ_ASSERT(!needGLRender);
    }
    if (needGLRender) {
      Render();
    }

    mThebesLayerCallback = nullptr;
    mThebesLayerCallbackData = nullptr;
  }

  mCompositor->SetTarget(nullptr);

#ifdef MOZ_LAYERS_HAVE_LOG
  Log();
  MOZ_LAYERS_LOG(("]----- EndTransaction"));
#endif
}

already_AddRefed<gfxASurface>
LayerManagerOGL::CreateOptimalMaskSurface(const gfxIntSize &aSize)
{
  return gfxPlatform::GetPlatform()->
    CreateOffscreenImageSurface(aSize, gfxASurface::CONTENT_ALPHA);
}

already_AddRefed<ThebesLayer>
LayerManagerOGL::CreateThebesLayer()
{
  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }

  nsRefPtr<ThebesLayer> layer = new ThebesLayerOGL(this);
  return layer.forget();
}

already_AddRefed<ContainerLayer>
LayerManagerOGL::CreateContainerLayer()
{
  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }

  nsRefPtr<ContainerLayer> layer = new ContainerLayerOGL(this);
  return layer.forget();
}

already_AddRefed<ImageLayer>
LayerManagerOGL::CreateImageLayer()
{
  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }

  nsRefPtr<ImageLayer> layer = new ImageLayerOGL(this);
  return layer.forget();
}

already_AddRefed<ColorLayer>
LayerManagerOGL::CreateColorLayer()
{
  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }

  nsRefPtr<ColorLayer> layer = new ColorLayerOGL(this);
  return layer.forget();
}

already_AddRefed<CanvasLayer>
LayerManagerOGL::CreateCanvasLayer()
{
  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }

  nsRefPtr<CanvasLayer> layer = new CanvasLayerOGL(this);
  return layer.forget();
}

LayerOGL*
LayerManagerOGL::RootLayer() const
{
  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }

  return static_cast<LayerOGL*>(mRoot->ImplData());
}

static LayerOGL*
ToLayerOGL(Layer* aLayer)
{
  return static_cast<LayerOGL*>(aLayer->ImplData());
}

static void ClearSubtree(Layer* aLayer)
{
  ToLayerOGL(aLayer)->CleanupResources();
  for (Layer* child = aLayer->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    ClearSubtree(child);
  }
}

void
LayerManagerOGL::ClearCachedResources(Layer* aSubtree)
{
  MOZ_ASSERT(!aSubtree || aSubtree->Manager() == this);
  Layer* subtree = aSubtree ? aSubtree : mRoot.get();
  if (!subtree) {
    return;
  }

  ClearSubtree(subtree);
#ifdef DEBUG
  // If this subtree is reachable from the root layer, then it's
  // possibly onscreen, and the resource clear means that composites
  // until the next received transaction may draw garbage to the
  // framebuffer.
  for(; subtree && subtree != mRoot; subtree = subtree->GetParent());
  mMaybeInvalidTree = (subtree == mRoot);
#endif  // DEBUG
}

// |aTexCoordRect| is the rectangle from the texture that we want to
// draw using the given program.  The program already has a necessary
// offset and scale, so the geometry that needs to be drawn is a unit
// square from 0,0 to 1,1.
//
// |aTexSize| is the actual size of the texture, as it can be larger
// than the rectangle given by |aTexCoordRect|.
void 
LayerManagerOGL::BindAndDrawQuadWithTextureRect(ShaderProgramOGL *aProg,
                                                const nsIntRect& aTexCoordRect,
                                                const nsIntSize& aTexSize,
                                                GLenum aWrapMode /* = LOCAL_GL_REPEAT */,
                                                bool aFlipped /* = false */)
{
  NS_ASSERTION(aProg->HasInitialized(), "Shader program not correctly initialized");
  GLuint vertAttribIndex =
    aProg->AttribLocation(ShaderProgramOGL::VertexCoordAttrib);
  GLuint texCoordAttribIndex =
    aProg->AttribLocation(ShaderProgramOGL::TexCoordAttrib);
  NS_ASSERTION(texCoordAttribIndex != GLuint(-1), "no texture coords?");

  // clear any bound VBO so that glVertexAttribPointer() goes back to
  // "pointer mode"
  gl()->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, 0);

  // Given what we know about these textures and coordinates, we can
  // compute fmod(t, 1.0f) to get the same texture coordinate out.  If
  // the texCoordRect dimension is < 0 or > width/height, then we have
  // wraparound that we need to deal with by drawing multiple quads,
  // because we can't rely on full non-power-of-two texture support
  // (which is required for the REPEAT wrap mode).

  GLContext::RectTriangles rects;

  nsIntSize realTexSize = aTexSize;
  if (!gl()->CanUploadNonPowerOfTwo()) {
    realTexSize = nsIntSize(gfx::NextPowerOfTwo(aTexSize.width),
                            gfx::NextPowerOfTwo(aTexSize.height));
  }

  if (aWrapMode == LOCAL_GL_REPEAT) {
    rects.addRect(/* dest rectangle */
                  0.0f, 0.0f, 1.0f, 1.0f,
                  /* tex coords */
                  aTexCoordRect.x / GLfloat(realTexSize.width),
                  aTexCoordRect.y / GLfloat(realTexSize.height),
                  aTexCoordRect.XMost() / GLfloat(realTexSize.width),
                  aTexCoordRect.YMost() / GLfloat(realTexSize.height),
                  aFlipped);
  } else {
    nsIntRect tcRect(aTexCoordRect.x, aTexCoordRect.y,
                     aTexCoordRect.width, aTexCoordRect.height);
    GLContext::DecomposeIntoNoRepeatTriangles(tcRect,
                                              nsIntSize(realTexSize.width, realTexSize.height),
                                              rects, aFlipped);
  }

  gl()->fVertexAttribPointer(vertAttribIndex, 2,
                                   LOCAL_GL_FLOAT, LOCAL_GL_FALSE, 0,
                                   rects.vertexPointer());

  gl()->fVertexAttribPointer(texCoordAttribIndex, 2,
                                   LOCAL_GL_FLOAT, LOCAL_GL_FALSE, 0,
                                   rects.texCoordPointer());

  {
    gl()->fEnableVertexAttribArray(texCoordAttribIndex);
    {
      gl()->fEnableVertexAttribArray(vertAttribIndex);

      gl()->fDrawArrays(LOCAL_GL_TRIANGLES, 0, rects.elements());

      gl()->fDisableVertexAttribArray(vertAttribIndex);
    }
    gl()->fDisableVertexAttribArray(texCoordAttribIndex);
  }
}

void
LayerManagerOGL::Render()
{
  SAMPLE_LABEL("LayerManagerOGL", "Render");
  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return;
  }

  nsIntRect clipRect;
  Rect bounds(mRenderBounds.x, mRenderBounds.y, mRenderBounds.width, mRenderBounds.height);
  if (mRoot->GetClipRect()) {
    clipRect = *mRoot->GetClipRect();
    WorldTransformRect(clipRect);
    Rect rect(clipRect.x, clipRect.y, clipRect.width, clipRect.height);
    mCompositor->BeginFrame(&rect, mWorldMatrix, bounds);
  } else {
    gfx::Rect rect;
    mCompositor->BeginFrame(nullptr, mWorldMatrix, bounds, &rect);
    clipRect = nsIntRect(rect.x, rect.y, rect.width, rect.height);
  }

  // Render our layers.
  RootLayer()->RenderLayer(nsIntPoint(0, 0), clipRect, nullptr);

  if (CompositingDisabled()) {
    mCompositor->AbortFrame();
    return;
  }

  mCompositor->EndFrame(mWorldMatrix);
}

void
LayerManagerOGL::SetWorldTransform(const gfxMatrix& aMatrix)
{
  NS_ASSERTION(aMatrix.PreservesAxisAlignedRectangles(),
               "SetWorldTransform only accepts matrices that satisfy PreservesAxisAlignedRectangles");
  NS_ASSERTION(!aMatrix.HasNonIntegerScale(),
               "SetWorldTransform only accepts matrices with integer scale");

  mWorldMatrix = aMatrix;
}

gfxMatrix&
LayerManagerOGL::GetWorldTransform(void)
{
  return mWorldMatrix;
}

void
LayerManagerOGL::WorldTransformRect(nsIntRect& aRect)
{
  gfxRect grect(aRect.x, aRect.y, aRect.width, aRect.height);
  grect = mWorldMatrix.TransformBounds(grect);
  aRect.SetRect(grect.X(), grect.Y(), grect.Width(), grect.Height());
}

TemporaryRef<DrawTarget>
LayerManagerOGL::CreateDrawTarget(const IntSize &aSize,
                                  SurfaceFormat aFormat)
{
#ifdef XP_MACOSX
  // We don't want to accelerate if the surface is too small which indicates
  // that it's likely used for an icon/static image. We also don't want to
  // accelerate anything that is above the maximum texture size of weakest gpu.
  // Safari uses 5000 area as the minimum for acceleration, we decided 64^2 is more logical.
  bool useAcceleration = aSize.width <= 4096 && aSize.height <= 4096 &&
                         aSize.width > 64 && aSize.height > 64 &&
                         gfxPlatformMac::GetPlatform()->UseAcceleratedCanvas();
  if (useAcceleration) {
    return Factory::CreateDrawTarget(BACKEND_COREGRAPHICS_ACCELERATED,
                                     aSize, aFormat);
  }
#endif
  return LayerManager::CreateDrawTarget(aSize, aFormat);
}

} /* layers */
} /* mozilla */
