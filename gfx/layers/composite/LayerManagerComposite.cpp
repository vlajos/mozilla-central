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
#include "CompositableHost.h"
#include "mozilla/gfx/Matrix.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/Preferences.h"
#include "mozilla/layers/ImageHost.h"
#include "mozilla/layers/ContentHost.h"

#include "gfxContext.h"
#include "gfxUtils.h"
#include "gfx2DGlue.h"
#ifdef XP_MACOSX
#include "gfxPlatformMac.h"
#else
#include "gfxPlatform.h"
#endif

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
 * LayerManagerComposite
 */
LayerManagerComposite::LayerManagerComposite(Compositor* aCompositor)
{
  mCompositor = aCompositor;
}

bool
LayerManagerComposite::Initialize()
{
  mComposer2D = mCompositor->GetWidget()->GetComposer2D();
  return mCompositor->Initialize();
}

void
LayerManagerComposite::Destroy()
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
LayerManagerComposite::BeginTransaction()
{
  mInTransaction = true;
}

void
LayerManagerComposite::BeginTransactionWithTarget(gfxContext *aTarget)
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
LayerManagerComposite::EndEmptyTransaction(EndTransactionFlags aFlags)
{
  mInTransaction = false;

  if (!mRoot)
    return false;

  EndTransaction(nullptr, nullptr);
  return true;
}

void
LayerManagerComposite::EndTransaction(DrawThebesLayerCallback aCallback,
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
LayerManagerComposite::CreateOptimalMaskSurface(const gfxIntSize &aSize)
{
  NS_ERROR("Should only be called on the drawing side");
  return nullptr;
}

already_AddRefed<ThebesLayer>
LayerManagerComposite::CreateThebesLayer()
{
  NS_ERROR("Should only be called on the drawing side");
  return nullptr;
}

already_AddRefed<ContainerLayer>
LayerManagerComposite::CreateContainerLayer()
{
  NS_ERROR("Should only be called on the drawing side");
  return nullptr;
}

already_AddRefed<ImageLayer>
LayerManagerComposite::CreateImageLayer()
{
  NS_ERROR("Should only be called on the drawing side");
  return nullptr;
}

already_AddRefed<ColorLayer>
LayerManagerComposite::CreateColorLayer()
{
  NS_ERROR("Should only be called on the drawing side");
  return nullptr;
}

already_AddRefed<CanvasLayer>
LayerManagerComposite::CreateCanvasLayer()
{
  NS_ERROR("Should only be called on the drawing side");
  return nullptr;
}

LayerComposite*
LayerManagerComposite::RootLayer() const
{
  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }

  return static_cast<LayerComposite*>(mRoot->ImplData());
}

void
LayerManagerComposite::Render()
{
  SAMPLE_LABEL("LayerManagerComposite", "Render");
  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return;
  }

  if (mComposer2D && mComposer2D->TryRender(mRoot, mWorldMatrix)) {
    mCompositor->EndFrameForExternalComposition(mWorldMatrix);
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

  mCompositor->EndFrame(mWorldMatrix);
}

void
LayerManagerComposite::SetWorldTransform(const gfxMatrix& aMatrix)
{
  NS_ASSERTION(aMatrix.PreservesAxisAlignedRectangles(),
               "SetWorldTransform only accepts matrices that satisfy PreservesAxisAlignedRectangles");
  NS_ASSERTION(!aMatrix.HasNonIntegerScale(),
               "SetWorldTransform only accepts matrices with integer scale");

  mWorldMatrix = aMatrix;
}

gfxMatrix&
LayerManagerComposite::GetWorldTransform(void)
{
  return mWorldMatrix;
}

void
LayerManagerComposite::WorldTransformRect(nsIntRect& aRect)
{
  gfxRect grect(aRect.x, aRect.y, aRect.width, aRect.height);
  grect = mWorldMatrix.TransformBounds(grect);
  aRect.SetRect(grect.X(), grect.Y(), grect.Width(), grect.Height());
}

already_AddRefed<ShadowThebesLayer>
LayerManagerComposite::CreateShadowThebesLayer()
{
  if (LayerManagerComposite::mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }
//#ifdef FORCE_BASICTILEDTHEBESLAYER
//  return nsRefPtr<ShadowThebesLayer>(new TiledThebesLayerOGL(this)).forget();
//#else
  return nsRefPtr<ThebesLayerComposite>(new ThebesLayerComposite(this)).forget();
//#endif
}

already_AddRefed<ShadowContainerLayer>
LayerManagerComposite::CreateShadowContainerLayer()
{
  if (LayerManagerComposite::mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }
  return nsRefPtr<ContainerLayerComposite>(new ContainerLayerComposite(this)).forget();
}

already_AddRefed<ShadowImageLayer>
LayerManagerComposite::CreateShadowImageLayer()
{
  if (LayerManagerComposite::mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }
  return nsRefPtr<ImageLayerComposite>(new ImageLayerComposite(this)).forget();
}

already_AddRefed<ShadowColorLayer>
LayerManagerComposite::CreateShadowColorLayer()
{
  if (LayerManagerComposite::mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }
  return nsRefPtr<ColorLayerComposite>(new ColorLayerComposite(this)).forget();
}

already_AddRefed<ShadowCanvasLayer>
LayerManagerComposite::CreateShadowCanvasLayer()
{
  if (LayerManagerComposite::mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }
  return nsRefPtr<CanvasLayerComposite>(new CanvasLayerComposite(this)).forget();
}

already_AddRefed<ShadowRefLayer>
LayerManagerComposite::CreateShadowRefLayer()
{
  if (LayerManagerComposite::mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }
  return nsRefPtr<CompositeRefLayer>(new CompositeRefLayer(this)).forget();
}

bool
LayerManagerComposite::AddMaskEffect(Layer* aMaskLayer, EffectChain& aEffects, bool aIs3D)
{
  if (!aMaskLayer) {
    return false;
  }
  LayerComposite* maskLayerComposite = static_cast<LayerComposite*>(aMaskLayer->ImplData());
  gfx::Matrix4x4 transform;
  ToMatrix4x4(aMaskLayer->GetEffectiveTransform(), transform);
  return maskLayerComposite->GetCompositableHost()->AddMaskEffect(aEffects, transform, aIs3D);
}

TemporaryRef<DrawTarget>
LayerManagerComposite::CreateDrawTarget(const IntSize &aSize,
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

TemporaryRef<CompositableHost>
LayerManagerComposite::CreateCompositableHost(BufferType aType)
{
  RefPtr<CompositableHost> result;
  switch (aType) {
  case BUFFER_YCBCR:
    result = new YCbCrImageHost(this);
    return result;
#ifdef MOZ_WIDGET_GONK
  case BUFFER_DIRECT_EXTERNAL:
#endif
  case BUFFER_TILED:
    result = new TiledContentHost(mCompositor);
    return result;
  case BUFFER_SHARED:
  case BUFFER_TEXTURE:
  case BUFFER_DIRECT: //TODO[nrc] fuck up - should be using Texture id and we used buffer id :-(
    result = new ImageHostSingle(this, aType);
    return result;
  case BUFFER_BRIDGE:
    result = new ImageHostBridge(this);
    return result;
  case BUFFER_CONTENT:
    result = new ContentHostTexture(mCompositor);
    return result;
  case BUFFER_CONTENT_DIRECT:
    result = new ContentHostDirect(mCompositor);
    return result;
  default:
    NS_ERROR("Unknown BufferType");
    return nullptr;
  }
}

} /* layers */
} /* mozilla */
