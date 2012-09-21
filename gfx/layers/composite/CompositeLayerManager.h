/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_COMPOSITELAYERMANAGER_H
#define GFX_COMPOSITELAYERMANAGER_H

#include "Compositor.h"

#include "mozilla/layers/ShadowLayers.h"

#include "mozilla/TimeStamp.h"


#ifdef XP_WIN
#include <windows.h>
#endif

#include "gfxContext.h"
#include "gfx3DMatrix.h"

namespace mozilla {
namespace layers {

class CompositeLayer;
class ShadowThebesLayer;
class ShadowContainerLayer;
class ShadowImageLayer;
class ShadowCanvasLayer;
class ShadowColorLayer;

class THEBES_API CompositeLayerManager :
    public ShadowLayerManager
{
public:
  CompositeLayerManager(Compositor* aCompositor);
  virtual ~CompositeLayerManager()
  {
    Destroy();
  }

  virtual void Destroy();

  /**
   * \return True is initialization was succesful, false when it was not.
   */
  bool Initialize()
  {
    return mCompositor->Initialize();
  }

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

  void BeginTransaction();

  void BeginTransactionWithTarget(gfxContext* aTarget);

  void EndConstruction();

  virtual bool EndEmptyTransaction();
  virtual void EndTransaction(DrawThebesLayerCallback aCallback,
                              void* aCallbackData,
                              EndTransactionFlags aFlags = END_DEFAULT);

  virtual void SetRoot(Layer* aLayer) { mRoot = aLayer; }

  virtual bool CanUseCanvasLayerForSize(const gfxIntSize &aSize)
  {
    return mCompositor->CanUseCanvasLayerForSize(aSize);
  }

  virtual void CreateTextureHostFor(ShadowLayer* aLayer,
                                    const TextureIdentifier& aTextureIdentifier,
                                    TextureFlags aFlags)
  {
    RefPtr<TextureHost> textureHost = mCompositor->CreateTextureHost(aTextureIdentifier, aFlags);
    aLayer->AddTextureHost(aTextureIdentifier, textureHost);
  }

  virtual TextureHostIdentifier GetTextureHostIdentifier()
  {
    return mCompositor->GetTextureHostIdentifier();
  }

  virtual PRInt32 GetMaxTextureSize() const
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
    NS_ERROR("Shouldn't be called for composited layer manager");
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
   * Setup the viewport and projection matrix for rendering
   * to a window of the given dimensions.
   */
  void SetupPipeline(int aWidth, int aHeight)
  {
    mCompositor->SetupPipeline(aWidth, aHeight, mWorldMatrix);
  }

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
    SetupPipeline(viewport.width, viewport.height);
  }

  static EffectMask* MakeMaskEffect(Layer* aMaskLayer);

private:
  /** Region we're clipping our current drawing to. */
  nsIntRegion mClippingRegion;

  /** Current root layer. */
  CompositeLayer *RootLayer() const;

  /**
   * Render the current layer tree to the active target.
   */
  void Render();

  void WorldTransformRect(nsIntRect& aRect);

  /* Thebes layer callbacks; valid at the end of a transaciton,
   * while rendering */
  DrawThebesLayerCallback mThebesLayerCallback;
  void *mThebesLayerCallbackData;
  gfxMatrix mWorldMatrix;
};



/**
 * General information and tree management for OGL layers.
 */
class CompositeLayer
{
public:
  CompositeLayer(CompositeLayerManager *aManager)
    : mCompositeManager(aManager)
    , mCompositor(aManager->GetCompositor())
    , mDestroyed(false)
  { }

  virtual ~CompositeLayer() { }

  virtual CompositeLayer *GetFirstChildComposite() {
    return nullptr;
  }

  /* Do NOT call this from the generic CompositeLayer destructor.  Only from the
   * concrete class destructor
   */
  virtual void Destroy() = 0;

  virtual Layer* GetLayer() = 0;

  virtual void RenderLayer(const nsIntPoint& aOffset,
                           const nsIntRect& aClipRect,
                           Surface* aPreviousSurface = nullptr) = 0;

  virtual void CleanupResources() = 0;

  /**
   * Get a texture host representation of the layer. This should not be used
   * for normal rendering. It is used for using the layer as a mask layer, any
   * layer that can be used as a mask layer should override this method.
   */
  virtual TemporaryRef<TextureHost> AsTextureHost()
  {
    return nullptr;
  }

protected:
  CompositeLayerManager *mCompositeManager;
  Compositor* mCompositor;
  bool mDestroyed;
};


} /* layers */
} /* mozilla */

#endif /* GFX_COMPOSITELAYERMANAGER_H */
