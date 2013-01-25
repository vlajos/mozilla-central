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

    #ifdef MOZ_WIDGET_GONK // TODO[nical] fix the fps stuff with non-OMTC
      if (sDrawFPS) {
        if (!mFPS) {
          mFPS = new FPSState();
        }
        double fps = mFPS->mCompositionFps.AddFrameAndGetFps(TimeStamp::Now());
        printf_stderr("HWComposer: FPS is %g\n", fps);
      }
      // This lets us reftest and screenshot content rendered by the
      // 2d composer.
      if (mTarget) {
        MakeCurrent();
        CopyToTarget(mTarget);
        gl()->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, 0);
      }
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

void
LayerManagerOGL::Render()
{
  SAMPLE_LABEL("LayerManagerOGL", "Render");
  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return;
  }

  nsIntRect clipRect;
  if (mRoot->GetClipRect()) {
    clipRect = *mRoot->GetClipRect();
    WorldTransformRect(clipRect);
    Rect rect(clipRect.x, clipRect.y, clipRect.width, clipRect.height);
    mCompositor->BeginFrame(&rect, mWorldMatrix);
  } else {
    gfx::Rect rect;
    mCompositor->BeginFrame(nullptr, mWorldMatrix, &rect);
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

static void
SubtractTransformedRegion(nsIntRegion& aRegion,
                          const nsIntRegion& aRegionToSubtract,
                          const gfx3DMatrix& aTransform)
{
  if (aRegionToSubtract.IsEmpty()) {
    return;
  }

  // For each rect in the region, find out its bounds in screen space and
  // subtract it from the screen region.
  nsIntRegionRectIterator it(aRegionToSubtract);
  while (const nsIntRect* rect = it.Next()) {
    gfxRect incompleteRect = aTransform.TransformBounds(gfxRect(*rect));
    aRegion.Sub(aRegion, nsIntRect(incompleteRect.x,
                                   incompleteRect.y,
                                   incompleteRect.width,
                                   incompleteRect.height));
  }
}

/* static */ void
LayerManagerOGL::ComputeRenderIntegrityInternal(Layer* aLayer,
                                                nsIntRegion& aScreenRegion,
                                                nsIntRegion& aLowPrecisionScreenRegion,
                                                const gfx3DMatrix& aTransform)
{
  if (aLayer->GetOpacity() <= 0.f ||
      (aScreenRegion.IsEmpty() && aLowPrecisionScreenRegion.IsEmpty())) {
    return;
  }

  // If the layer's a container, recurse into all of its children
  ContainerLayer* container = aLayer->AsContainerLayer();
  if (container) {
    // Accumulate the transform of intermediate surfaces
    gfx3DMatrix transform = aTransform;
    if (container->UseIntermediateSurface()) {
      transform = aLayer->GetEffectiveTransform();
      transform.PreMultiply(aTransform);
    }
    for (Layer* child = aLayer->GetFirstChild(); child;
         child = child->GetNextSibling()) {
      ComputeRenderIntegrityInternal(child, aScreenRegion, aLowPrecisionScreenRegion, transform);
    }
    return;
  }

  // Only thebes layers can be incomplete
  ThebesLayer* thebesLayer = aLayer->AsThebesLayer();
  if (!thebesLayer) {
    return;
  }

  // See if there's any incomplete rendering
  nsIntRegion incompleteRegion = aLayer->GetEffectiveVisibleRegion();
  incompleteRegion.Sub(incompleteRegion, thebesLayer->GetValidRegion());

  if (!incompleteRegion.IsEmpty()) {
    // Calculate the transform to get between screen and layer space
    gfx3DMatrix transformToScreen = aLayer->GetEffectiveTransform();
    transformToScreen.PreMultiply(aTransform);

    SubtractTransformedRegion(aScreenRegion, incompleteRegion, transformToScreen);

    // See if there's any incomplete low-precision rendering
    TiledLayerComposer* composer = nullptr;
    ShadowLayer* shadow = aLayer->AsShadowLayer();
    if (shadow) {
      composer = shadow->AsTiledLayerComposer();
      if (composer) {
        incompleteRegion.Sub(incompleteRegion, composer->GetValidLowPrecisionRegion());
        if (!incompleteRegion.IsEmpty()) {
          SubtractTransformedRegion(aLowPrecisionScreenRegion, incompleteRegion, transformToScreen);
        }
      }
    }

    // If we can't get a valid low precision region, assume it's the same as
    // the high precision region.
    if (!composer) {
      SubtractTransformedRegion(aLowPrecisionScreenRegion, incompleteRegion, transformToScreen);
    }
  }
}

static int
GetRegionArea(const nsIntRegion& aRegion)
{
  int area = 0;
  nsIntRegionRectIterator it(aRegion);
  while (const nsIntRect* rect = it.Next()) {
    area += rect->width * rect->height;
  }
  return area;
}

#ifdef MOZ_ANDROID_OMTC
static float
GetDisplayportCoverage(const gfx::Rect& aDisplayPort,
                       const gfx3DMatrix& aTransformToScreen,
                       const nsIntRect& aScreenRect)
{
  gfxRect transformedDisplayport =
    aTransformToScreen.TransformBounds(gfxRect(aDisplayPort.x,
                                               aDisplayPort.y,
                                               aDisplayPort.width,
                                               aDisplayPort.height));
  transformedDisplayport.RoundOut();
  nsIntRect displayport = nsIntRect(transformedDisplayport.x,
                                    transformedDisplayport.y,
                                    transformedDisplayport.width,
                                    transformedDisplayport.height);
  if (!displayport.Contains(aScreenRect)) {
    nsIntRegion coveredRegion;
    coveredRegion.And(aScreenRect, displayport);
    return GetRegionArea(coveredRegion) / (float)(aScreenRect.width * aScreenRect.height);
  }

  return 1.0f;
}
#endif // MOZ_ANDROID_OMTC

float
LayerManagerOGL::ComputeRenderIntegrity()
{
  // We only ever have incomplete rendering when progressive tiles are enabled.
  Layer* root = GetRoot();
  if (!gfxPlatform::UseProgressiveTilePainting() || !root) {
    return 1.f;
  }

  const FrameMetrics& rootMetrics = root->AsContainerLayer()->GetFrameMetrics();
  nsIntRect screenRect(rootMetrics.mCompositionBounds.x,
                       rootMetrics.mCompositionBounds.y,
                       rootMetrics.mCompositionBounds.width,
                       rootMetrics.mCompositionBounds.height);

  float lowPrecisionMultiplier = 1.0f;
  float highPrecisionMultiplier = 1.0f;
#ifdef MOZ_ANDROID_OMTC
  // Use the transform on the primary scrollable layer and its FrameMetrics
  // to find out how much of the viewport the current displayport covers
  Layer* primaryScrollable = GetPrimaryScrollableLayer();
  if (primaryScrollable) {
    // This is derived from the code in
    // gfx/layers/ipc/CompositorParent.cpp::TransformShadowTree.
    const gfx3DMatrix& rootTransform = root->GetTransform();
    float devPixelRatioX = 1 / rootTransform.GetXScale();
    float devPixelRatioY = 1 / rootTransform.GetYScale();

    gfx3DMatrix transform = primaryScrollable->GetEffectiveTransform();
    transform.ScalePost(devPixelRatioX, devPixelRatioY, 1);
    const FrameMetrics& metrics = primaryScrollable->AsContainerLayer()->GetFrameMetrics();

    // Clip the screen rect to the document bounds
    gfxRect documentBounds =
      transform.TransformBounds(gfxRect(metrics.mScrollableRect.x - metrics.mScrollOffset.x,
                                        metrics.mScrollableRect.y - metrics.mScrollOffset.y,
                                        metrics.mScrollableRect.width,
                                        metrics.mScrollableRect.height));
    documentBounds.RoundOut();
    screenRect = screenRect.Intersect(nsIntRect(documentBounds.x, documentBounds.y,
                                                documentBounds.width, documentBounds.height));

    // If the screen rect is empty, the user has scrolled entirely into
    // over-scroll and so we can be considered to have full integrity.
    if (screenRect.IsEmpty()) {
      return 1.0f;
    }

    // Work out how much of the critical display-port covers the screen
    bool hasLowPrecision = false;
    if (!metrics.mCriticalDisplayPort.IsEmpty()) {
      hasLowPrecision = true;
      highPrecisionMultiplier =
        GetDisplayportCoverage(metrics.mCriticalDisplayPort, transform, screenRect);
    }

    // Work out how much of the display-port covers the screen
    if (!metrics.mDisplayPort.IsEmpty()) {
      if (hasLowPrecision) {
        lowPrecisionMultiplier =
          GetDisplayportCoverage(metrics.mDisplayPort, transform, screenRect);
      } else {
        lowPrecisionMultiplier = highPrecisionMultiplier =
          GetDisplayportCoverage(metrics.mDisplayPort, transform, screenRect);
      }
    }
  }

  // If none of the screen is covered, we have zero integrity.
  if (highPrecisionMultiplier <= 0.0f && lowPrecisionMultiplier <= 0.0f) {
    return 0.0f;
  }
#endif // MOZ_ANDROID_OMTC

  nsIntRegion screenRegion(screenRect);
  nsIntRegion lowPrecisionScreenRegion(screenRect);  
gfx3DMatrix transform;
  //nsIntRect screenRect(0, 0, mCompositor->mWidgetSize.width, mCompositor->mWidgetSize.height);
  ComputeRenderIntegrityInternal(root, screenRegion,
                                 lowPrecisionScreenRegion, transform);

  if (!screenRegion.IsEqual(screenRect)) {
    // Calculate the area of the region. All rects in an nsRegion are
    // non-overlapping.
    float screenArea = screenRect.width * screenRect.height;
    float highPrecisionIntegrity = GetRegionArea(screenRegion) / screenArea;
    float lowPrecisionIntegrity = 1.f;
    if (!lowPrecisionScreenRegion.IsEqual(screenRect)) {
      lowPrecisionIntegrity = GetRegionArea(lowPrecisionScreenRegion) / screenArea;
    }

    return ((highPrecisionIntegrity * highPrecisionMultiplier) +
            (lowPrecisionIntegrity * lowPrecisionMultiplier)) / 2;
  }

  return 1.f;
}

} /* layers */
} /* mozilla */
