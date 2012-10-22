/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/PLayers.h"

/* This must occur *after* layers/PLayers.h to avoid typedefs conflicts. */
#include "mozilla/Util.h"

#include "LayerManagerComposite.h"
#include "ThebesLayerComposite.h"
#include "ContainerLayerComposite.h"
#include "ImageLayerComposite.h"
#include "ColorLayerComposite.h"
#include "CanvasLayerComposite.h"
//TODO[nrc]
//#include "TiledThebesLayerComposite.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/Preferences.h"

#include "gfxContext.h"
#include "gfxUtils.h"
#include "gfx2DGlue.h"
#include "gfxPlatform.h"
#include "nsIWidget.h"

#include "nsIServiceManager.h"
#include "nsIConsoleService.h"

#include "gfxCrashReporterUtils.h"

#include "sampler.h"

#ifdef MOZ_WIDGET_ANDROID
#include <android/log.h>
#endif

namespace mozilla {
namespace layers {

using namespace mozilla::gfx;


/**
 * CompositeLayerManager
 */
CompositeLayerManager::CompositeLayerManager(Compositor* aCompositor)
{
  mCompositor = aCompositor;
}

void
CompositeLayerManager::Destroy()
{
  if (!mDestroyed) {
    if (mRoot) {
      RootLayer()->Destroy();
    }
    mRoot = nullptr;

    mCompositor->Destroy();

    mDestroyed = true;
  }
}


void
CompositeLayerManager::BeginTransaction()
{
  mInTransaction = true;
}

void
CompositeLayerManager::BeginTransactionWithTarget(gfxContext *aTarget)
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
CompositeLayerManager::EndEmptyTransaction(EndTransactionFlags aFlags)
{
  mInTransaction = false;

  if (!mRoot)
    return false;

  EndTransaction(nullptr, nullptr);
  return true;
}

void
CompositeLayerManager::EndTransaction(DrawThebesLayerCallback aCallback,
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

    Render();

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
CompositeLayerManager::CreateOptimalMaskSurface(const gfxIntSize &aSize)
{
  NS_ERROR("Should only be called on the drawing side");
  return nullptr;
}

already_AddRefed<ThebesLayer>
CompositeLayerManager::CreateThebesLayer()
{
  NS_ERROR("Should only be called on the drawing side");
  return nullptr;
}

already_AddRefed<ContainerLayer>
CompositeLayerManager::CreateContainerLayer()
{
  NS_ERROR("Should only be called on the drawing side");
  return nullptr;
}

already_AddRefed<ImageLayer>
CompositeLayerManager::CreateImageLayer()
{
  NS_ERROR("Should only be called on the drawing side");
  return nullptr;
}

already_AddRefed<ColorLayer>
CompositeLayerManager::CreateColorLayer()
{
  NS_ERROR("Should only be called on the drawing side");
  return nullptr;
}

already_AddRefed<CanvasLayer>
CompositeLayerManager::CreateCanvasLayer()
{
  NS_ERROR("Should only be called on the drawing side");
  return nullptr;
}

CompositeLayer*
CompositeLayerManager::RootLayer() const
{
  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }

  return static_cast<CompositeLayer*>(mRoot->ImplData());
}

void
CompositeLayerManager::Render()
{
  SAMPLE_LABEL("CompositeLayerManager", "Render");
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

  mCompositor->EndFrame();
}

void
CompositeLayerManager::SetWorldTransform(const gfxMatrix& aMatrix)
{
  NS_ASSERTION(aMatrix.PreservesAxisAlignedRectangles(),
               "SetWorldTransform only accepts matrices that satisfy PreservesAxisAlignedRectangles");
  NS_ASSERTION(!aMatrix.HasNonIntegerScale(),
               "SetWorldTransform only accepts matrices with integer scale");

  mWorldMatrix = aMatrix;
}

gfxMatrix&
CompositeLayerManager::GetWorldTransform(void)
{
  return mWorldMatrix;
}

void
CompositeLayerManager::WorldTransformRect(nsIntRect& aRect)
{
  gfxRect grect(aRect.x, aRect.y, aRect.width, aRect.height);
  grect = mWorldMatrix.TransformBounds(grect);
  aRect.SetRect(grect.X(), grect.Y(), grect.Width(), grect.Height());
}

already_AddRefed<ShadowThebesLayer>
CompositeLayerManager::CreateShadowThebesLayer()
{
  if (CompositeLayerManager::mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }
//#ifdef FORCE_BASICTILEDTHEBESLAYER
//  return nsRefPtr<ShadowThebesLayer>(new TiledThebesLayerOGL(this)).forget();
//#else
  return nsRefPtr<CompositeThebesLayer>(new CompositeThebesLayer(this)).forget();
//#endif
}

already_AddRefed<ShadowContainerLayer>
CompositeLayerManager::CreateShadowContainerLayer()
{
  if (CompositeLayerManager::mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }
  return nsRefPtr<CompositeContainerLayer>(new CompositeContainerLayer(this)).forget();
}

already_AddRefed<ShadowImageLayer>
CompositeLayerManager::CreateShadowImageLayer()
{
  if (CompositeLayerManager::mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }
  return nsRefPtr<CompositeImageLayer>(new CompositeImageLayer(this)).forget();
}

already_AddRefed<ShadowColorLayer>
CompositeLayerManager::CreateShadowColorLayer()
{
  if (CompositeLayerManager::mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }
  return nsRefPtr<CompositeColorLayer>(new CompositeColorLayer(this)).forget();
}

already_AddRefed<ShadowCanvasLayer>
CompositeLayerManager::CreateShadowCanvasLayer()
{
  if (CompositeLayerManager::mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }
  return nsRefPtr<CompositeCanvasLayer>(new CompositeCanvasLayer(this)).forget();
}

already_AddRefed<ShadowRefLayer>
CompositeLayerManager::CreateShadowRefLayer()
{
  if (CompositeLayerManager::mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }
  return nsRefPtr<CompositeRefLayer>(new CompositeRefLayer(this)).forget();
}

/* static */EffectMask*
CompositeLayerManager::MakeMaskEffect(Layer* aMaskLayer)
{
  if (aMaskLayer) {
    CompositeLayer* maskLayerComposite = static_cast<CompositeLayer*>(aMaskLayer->ImplData());
    //TODO[nrc] change AsTextureHost to AsTextureSource
    RefPtr<TextureHost> maskHost = maskLayerComposite->AsTextureHost();
    Matrix4x4 transform;
    ToMatrix4x4(aMaskLayer->GetEffectiveTransform(), transform);
    return new EffectMask(maskHost->GetAsTextureSource(), transform);
  }

  return nullptr;
}

TemporaryRef<DrawTarget>
CompositeLayerManager::CreateDrawTarget(const IntSize &aSize,
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
