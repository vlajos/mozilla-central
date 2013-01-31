/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_LayerManagerComposite_H
#define GFX_LayerManagerComposite_H

#include "mozilla/layers/Compositor.h"
#include "mozilla/layers/ShadowLayers.h"
#include "Composer2D.h"
#include "mozilla/TimeStamp.h"

#ifdef XP_WIN
#include <windows.h>
#endif

#include "gfxContext.h"
#include "gfx3DMatrix.h"

namespace mozilla {
namespace layers {

class LayerComposite;
class ShadowThebesLayer;
class ShadowContainerLayer;
class ShadowImageLayer;
class ShadowCanvasLayer;
class ShadowColorLayer;

class THEBES_API LayerManagerComposite :
    public ShadowLayerManager
{
public:
  LayerManagerComposite(Compositor* aCompositor);
  virtual ~LayerManagerComposite()
  {
    Destroy();
  }

  virtual void Destroy();

  /**
   * return True if initialization was succesful, false when it was not.
   */
  bool Initialize();

  Compositor* GetCompositor() const { return mCompositor; }

  /**
   * Sets the clipping region for this layer manager. This is important on 
   * windows because using OGL we no longer have GDI's native clipping. Therefor
   * widget must tell us what part of the screen is being invalidated,
   * and we should clip to this.
   *
   * \param aClippingRegion Region to clip to. Setting an empty region
   * will disable clipping.
   */
  void SetClippingRegion(const nsIntRegion& aClippingRegion)
  {
    mClippingRegion = aClippingRegion;
  }

  /**
   * LayerManager implementation.
   */
  virtual ShadowLayerManager* AsShadowManager()
  {
    return this;
  }

  void UpdateRenderBounds(const nsIntRect& aRect);

  void BeginTransaction();

  void BeginTransactionWithTarget(gfxContext* aTarget);

  void EndConstruction();
  virtual void NotifyShadowTreeTransaction()
  {
    mCompositor->NotifyShadowTreeTransaction();
  }

  virtual bool EndEmptyTransaction(EndTransactionFlags aFlags = END_DEFAULT);
  virtual void EndTransaction(DrawThebesLayerCallback aCallback,
                              void* aCallbackData,
                              EndTransactionFlags aFlags = END_DEFAULT);

  virtual void SetRoot(Layer* aLayer) { mRoot = aLayer; }

  virtual bool CanUseCanvasLayerForSize(const gfxIntSize &aSize)
  {
    return mCompositor->CanUseCanvasLayerForSize(aSize);
  }

  virtual TextureFactoryIdentifier GetTextureFactoryIdentifier()
  {
    return mCompositor->GetTextureFactoryIdentifier();
  }

  virtual int32_t GetMaxTextureSize() const
  {
    return mCompositor->GetMaxTextureSize();
  }

  virtual already_AddRefed<ThebesLayer> CreateThebesLayer();

  virtual already_AddRefed<ContainerLayer> CreateContainerLayer();

  virtual already_AddRefed<ImageLayer> CreateImageLayer();

  virtual already_AddRefed<ColorLayer> CreateColorLayer();

  virtual already_AddRefed<CanvasLayer> CreateCanvasLayer();

  virtual already_AddRefed<ShadowThebesLayer> CreateShadowThebesLayer();
  virtual already_AddRefed<ShadowContainerLayer> CreateShadowContainerLayer();
  virtual already_AddRefed<ShadowImageLayer> CreateShadowImageLayer();
  virtual already_AddRefed<ShadowColorLayer> CreateShadowColorLayer();
  virtual already_AddRefed<ShadowCanvasLayer> CreateShadowCanvasLayer();
  virtual already_AddRefed<ShadowRefLayer> CreateShadowRefLayer();

  virtual LayersBackend GetBackendType()
  {
    //TODO[nrc] is being called!
    MOZ_ASSERT(false, "Shouldn't be called for composited layer manager");
    return LAYERS_NONE;
  }
  virtual void GetBackendName(nsAString& name)
  {
    NS_ERROR("Shouldn't be called for composited layer manager");
    name.AssignLiteral("Composite");
  }

  virtual already_AddRefed<gfxASurface>
    CreateOptimalMaskSurface(const gfxIntSize &aSize);


  DrawThebesLayerCallback GetThebesLayerCallback() const
  { return mThebesLayerCallback; }

  void* GetThebesLayerCallbackData() const
  { return mThebesLayerCallbackData; }

  /*
   * Helper functions for our layers
   */
  void CallThebesLayerDrawCallback(ThebesLayer* aLayer,
                                   gfxContext* aContext,
                                   const nsIntRegion& aRegionToDraw)
  {
    NS_ASSERTION(mThebesLayerCallback,
                 "CallThebesLayerDrawCallback without callback!");
    mThebesLayerCallback(aLayer, aContext,
                         aRegionToDraw, nsIntRegion(),
                         mThebesLayerCallbackData);
  }

#ifdef MOZ_LAYERS_HAVE_LOG
  virtual const char* Name() const { return "OGL(Compositor)"; }
#endif // MOZ_LAYERS_HAVE_LOG

  enum WorldTransforPolicy {
    ApplyWorldTransform,
    DontApplyWorldTransform
  };

  /**
   * Setup World transform matrix.
   * Transform will be ignored if it is not PreservesAxisAlignedRectangles
   * or has non integer scale
   */
  void SetWorldTransform(const gfxMatrix& aMatrix);
  gfxMatrix& GetWorldTransform(void);

  void SaveViewport()
  {
    mCompositor->SaveViewport();
  }
  void RestoreViewport()
  {
    gfx::IntRect viewport = mCompositor->RestoreViewport();
    mCompositor->PrepareViewport(viewport.width, viewport.height, mWorldMatrix);
  }

  static bool AddMaskEffect(Layer* aMaskLayer,
                            EffectChain& aEffect,
                            bool aIs3D = false);

  bool CompositingDisabled() { return mCompositingDisabled; }
  void SetCompositingDisabled(bool aCompositingDisabled) { mCompositingDisabled = aCompositingDisabled; }

  /**
   * Creates a DrawTarget which is optimized for inter-operating with this
   * layermanager.
   */
  virtual TemporaryRef<mozilla::gfx::DrawTarget>
    CreateDrawTarget(const mozilla::gfx::IntSize &aSize,
                     mozilla::gfx::SurfaceFormat aFormat);

  nsIntSize* GetWidgetSize() {
    return mCompositor->GetWidgetSize();
  }

  TemporaryRef<CompositableHost> CreateCompositableHost(BufferType aType);

  /**
   * Calculates the 'completeness' of the rendering that intersected with the
   * screen on the last render. This is only useful when progressive tile
   * drawing is enabled, otherwise this will always return 1.0.
   * This function's expense scales with the size of the layer tree and the
   * complexity of individual layers' valid regions.
   */
  float ComputeRenderIntegrity();

private:
  /** Region we're clipping our current drawing to. */
  nsIntRegion mClippingRegion;
  nsIntRect mRenderBounds;

  /** Current root layer. */
  LayerComposite *RootLayer() const;
 
  /**
   * Recursive helper method for use by ComputeRenderIntegrity. Subtracts
   * any incomplete rendering on aLayer from aScreenRegion. Any low-precision
   * rendering is included in aLowPrecisionScreenRegion. aTransform is the
   * accumulated transform of intermediate surfaces beneath aLayer.
   */
  static void ComputeRenderIntegrityInternal(Layer* aLayer,
                                             nsIntRegion& aScreenRegion,
                                             nsIntRegion& aLowPrecisionScreenRegion,
                                             const gfx3DMatrix& aTransform);

  /**
   * Render the current layer tree to the active target.
   */
  void Render();

  void WorldTransformRect(nsIntRect& aRect);

  /** Our more efficient but less powerful alter ego, if one is available. */
  nsRefPtr<Composer2D> mComposer2D;

  /* Thebes layer callbacks; valid at the end of a transaciton,
   * while rendering */
  DrawThebesLayerCallback mThebesLayerCallback;
  void *mThebesLayerCallbackData;
  gfxMatrix mWorldMatrix;
  bool mCompositingDisabled;
  bool mInTransaction;
};



/**
 * General information and tree management for layers.
 */
class LayerComposite
{
public:
  LayerComposite(LayerManagerComposite *aManager)
    : mCompositeManager(aManager)
    , mCompositor(aManager->GetCompositor())
    , mDestroyed(false)
  { }

  virtual ~LayerComposite() { }

  virtual LayerComposite *GetFirstChildComposite() {
    return nullptr;
  }

  /* Do NOT call this from the generic LayerComposite destructor.  Only from the
   * concrete class destructor
   */
  virtual void Destroy() = 0;

  virtual Layer* GetLayer() = 0;

  virtual void RenderLayer(const nsIntPoint& aOffset,
                           const nsIntRect& aClipRect,
                           CompositingRenderTarget* aPreviousSurface = nullptr) = 0;

  virtual CompositableHost* GetCompositableHost() = 0;

  virtual void CleanupResources() = 0;

protected:
  LayerManagerComposite* mCompositeManager;
  Compositor* mCompositor;
  bool mDestroyed;
};


} /* layers */
} /* mozilla */

#endif /* GFX_LayerManagerComposite_H */
