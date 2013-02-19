/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_COMPOSITORD3D11_H
#define MOZILLA_GFX_COMPOSITORD3D11_H

#include "mozilla/layers/Compositor.h"
#include "TextureD3D11.h"
#include <d3d11.h>
#include <stack>

class nsWidget;

namespace mozilla {
namespace layers {

#define LOGD3D11(param)

struct VertexShaderConstants
{
  float layerTransform[4][4];
  float projection[4][4];
  float renderTargetOffset[4];
  gfx::Rect textureCoords;
  gfx::Rect layerQuad;
  gfx::Rect maskQuad;
};

struct PixelShaderConstants
{
  float layerColor[4];
  float layerOpacity[4];
};

struct DeviceAttachmentsD3D11;

class CompositorD3D11 : public Compositor
{
public:
  CompositorD3D11(nsIWidget *aWidget);
  ~CompositorD3D11();

  virtual bool Initialize();
  virtual void Destroy() { }

  virtual TextureFactoryIdentifier
    GetTextureFactoryIdentifier();

  virtual bool CanUseCanvasLayerForSize(const gfxIntSize &aSize);
  virtual int32_t GetMaxTextureSize() const MOZ_FINAL;

  virtual void SetTarget(gfxContext *aTarget) { mTarget = aTarget; }

  virtual void MakeCurrent(bool) { }

  virtual TemporaryRef<TextureHost>
    CreateTextureHost(TextureHostType aMemoryType,
                      uint32_t aTextureFlags,
                      SurfaceDescriptorType aDescriptorType,
                      ISurfaceDeallocator* aDeAllocator);

  virtual TemporaryRef<CompositingRenderTarget>
    CreateRenderTarget(const gfx::IntRect &aRect,
                       SurfaceInitMode aInit);

  virtual TemporaryRef<CompositingRenderTarget>
    CreateRenderTargetFromSource(const gfx::IntRect &aRect,
                                 const CompositingRenderTarget *aSource);

  virtual void SetRenderTarget(CompositingRenderTarget *aSurface);

  virtual void SetRenderTargetSize(int aWidth, int aHeight) { }

  virtual void DrawQuad(const gfx::Rect &aRect, const gfx::Rect *aClipRect,
                        const EffectChain &aEffectChain,
                        gfx::Float aOpacity, const gfx::Matrix4x4 &aTransform,
                        const gfx::Point &aOffset);

  /**
   * Start a new frame. If aClipRectIn is null, sets *aClipRectOut to the screen dimensions. 
   */
  virtual void BeginFrame(const gfx::Rect *aClipRectIn, const gfxMatrix& aTransform,
    const gfx::Rect& aRenderBounds, gfx::Rect *aClipRectOut = nullptr);

  /**
   * Flush the current frame to the screen.
   */
  virtual void EndFrame(const gfxMatrix& aTransform);

  /**
   * Post rendering stuff if the rendering is outside of this Compositor
   * e.g., by Composer2D
   */
  virtual void EndFrameForExternalComposition(const gfxMatrix& aTransform) { }

  /**
   * Tidy up if BeginFrame has been called, but EndFrame won't be
   */
  virtual void AbortFrame() { }

  /**
   * Setup the viewport and projection matrix for rendering
   * to a window of the given dimensions.
   */
  virtual void PrepareViewport(int aWidth, int aHeight, const gfxMatrix& aWorldTransform);

  // save the current viewport
  virtual void SaveViewport();
  // resotre the previous viewport and return its bounds
  virtual gfx::IntRect RestoreViewport();

  virtual bool SupportsPartialTextureUpdate() { return true; }

  virtual const char* Name() const { return "Direct3D 11"; }

  virtual void NotifyShadowTreeTransaction() { }

  virtual nsIWidget* GetWidget() const { return mWidget; }
  virtual nsIntSize* GetWidgetSize();

private:
  enum MaskMode {
    UNMASKED = 0,
    MASKED = 1,
    MASKED3D
  };

  void VerifyBufferSize();
  void UpdateRenderTarget();
  bool CreateShaders();
  void UpdateConstantBuffers();
  void SetSamplerForFilter(gfx::Filter aFilter);
  void SetPSForEffect(Effect *aEffect, MaskMode aMaskMode);
  void PaintToTarget();

  RefPtr<ID3D11DeviceContext> mContext;
  RefPtr<ID3D11Device> mDevice;
  RefPtr<IDXGISwapChain> mSwapChain;
  RefPtr<ID3D11RenderTargetView> mDefaultRT;
  
  DeviceAttachmentsD3D11 *mAttachments;

  nsRefPtr<gfxContext> mTarget;

  nsIWidget *mWidget;
  // XXX - Bas - wth?
  nsIntSize mSize;

  HWND mHwnd;

  D3D_FEATURE_LEVEL mFeatureLevel;

  VertexShaderConstants mVSConstants;
  PixelShaderConstants mPSConstants;

  std::stack<gfx::IntRect> mViewportStack;
};

}
}

#endif
