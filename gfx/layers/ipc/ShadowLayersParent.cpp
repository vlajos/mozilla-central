/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=8 et :
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <vector>

#include "AutoOpenSurface.h"
#include "CompositorParent.h"
#include "gfxSharedImageSurface.h"
#include "ImageLayers.h"
#include "mozilla/layout/RenderFrameParent.h"
#include "mozilla/unused.h"
#include "RenderTrace.h"
#include "ShadowLayerParent.h"
#include "ShadowLayersParent.h"
#include "ShadowLayers.h"
#include "ShadowLayerUtils.h"
#include "TiledLayerBuffer.h"
#include "gfxPlatform.h" 
#include "mozilla/layers/TextureParent.h"
#include "CompositableHost.h"

typedef std::vector<mozilla::layers::EditReply> EditReplyVector;

using mozilla::layout::RenderFrameParent;

namespace mozilla {
namespace layers {

//--------------------------------------------------
// Convenience accessors
static ShadowLayerParent*
cast(const PLayerParent* in)
{ 
  return const_cast<ShadowLayerParent*>(
    static_cast<const ShadowLayerParent*>(in));
}

template<class OpPaintT>
static TextureHost*
AsTextureHost(const OpPaintT& op)
{
  return static_cast<TextureParent*>(op.textureParent())->GetTextureHost();
}

template<class OpPaintT>
Layer* GetLayerFromOpPaint(const OpPaintT& op)
{
  PTextureParent* textureParent = op.textureParent();
  MOZ_ASSERT(textureParent);
  ShadowLayerParent* shadow = cast(textureParent->Manager());

  return shadow->AsLayer();
}

template<class OpCreateT>
static ShadowLayerParent*
AsShadowLayer(const OpCreateT& op)
{
  return cast(op.layerParent());
}

static ShadowLayerParent*
AsShadowLayer(const OpSetRoot& op)
{
  return cast(op.rootParent());
}

static ShadowLayerParent*
ShadowContainer(const OpInsertAfter& op)
{
  return cast(op.containerParent());
}
static ShadowLayerParent*
ShadowChild(const OpInsertAfter& op)
{
  return cast(op.childLayerParent());
}
static ShadowLayerParent*
ShadowAfter(const OpInsertAfter& op)
{
  return cast(op.afterParent());
}

static ShadowLayerParent*
ShadowContainer(const OpAppendChild& op)
{
  return cast(op.containerParent());
}
static ShadowLayerParent*
ShadowChild(const OpAppendChild& op)
{
  return cast(op.childLayerParent());
}

static ShadowLayerParent*
ShadowContainer(const OpRemoveChild& op)
{
  return cast(op.containerParent());
}
static ShadowLayerParent*
ShadowChild(const OpRemoveChild& op)
{
  return cast(op.childLayerParent());
}

static ShadowLayerParent*
ShadowContainer(const OpRepositionChild& op)
{
  return cast(op.containerParent());
}
static ShadowLayerParent*
ShadowChild(const OpRepositionChild& op)
{
  return cast(op.childLayerParent());
}
static ShadowLayerParent*
ShadowAfter(const OpRepositionChild& op)
{
  return cast(op.afterParent());
}

static ShadowLayerParent*
ShadowContainer(const OpRaiseToTopChild& op)
{
  return cast(op.containerParent());
}
static ShadowLayerParent*
ShadowChild(const OpRaiseToTopChild& op)
{
  return cast(op.childLayerParent());
}

//--------------------------------------------------
// ShadowLayersParent
ShadowLayersParent::ShadowLayersParent(ShadowLayerManager* aManager,
                                       ShadowLayersManager* aLayersManager,
                                       uint64_t aId)
  : mLayerManager(aManager)
  , mShadowLayersManager(aLayersManager)
  , mId(aId)
  , mDestroyed(false)
{
  MOZ_COUNT_CTOR(ShadowLayersParent);
}

ShadowLayersParent::~ShadowLayersParent()
{
  MOZ_COUNT_DTOR(ShadowLayersParent);
}

void
ShadowLayersParent::Destroy()
{
  mDestroyed = true;
  for (size_t i = 0; i < ManagedPLayerParent().Length(); ++i) {
    ShadowLayerParent* slp =
      static_cast<ShadowLayerParent*>(ManagedPLayerParent()[i]);
    slp->Destroy();
  }
}

/* virtual */
bool
ShadowLayersParent::RecvUpdateNoSwap(const InfallibleTArray<Edit>& cset,
                                     const TargetConfig& targetConfig,
                                     const bool& isFirstPaint)
{
  InfallibleTArray<EditReply> noReplies;
  bool success = RecvUpdate(cset, targetConfig, isFirstPaint, &noReplies);
  NS_ABORT_IF_FALSE(noReplies.Length() == 0, "RecvUpdateNoSwap requires a sync Update to carry Edits");
  return success;
}

bool
ShadowLayersParent::RecvUpdate(const InfallibleTArray<Edit>& cset,
                               const TargetConfig& targetConfig,
                               const bool& isFirstPaint,
                               InfallibleTArray<EditReply>* reply)
{
#ifdef COMPOSITOR_PERFORMANCE_WARNING
  TimeStamp updateStart = TimeStamp::Now();
#endif

  MOZ_LAYERS_LOG(("[ParentSide] received txn with %d edits", cset.Length()));

  if (mDestroyed || layer_manager()->IsDestroyed()) {
    return true;
  }

  EditReplyVector replyv;

  layer_manager()->BeginTransactionWithTarget(NULL);

  for (EditArray::index_type i = 0; i < cset.Length(); ++i) {
    const Edit& edit = cset[i];

    switch (edit.type()) {
    // Create* ops
    case Edit::TOpCreateThebesLayer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateThebesLayer"));

      nsRefPtr<ShadowThebesLayer> layer =
        layer_manager()->CreateShadowThebesLayer();
      AsShadowLayer(edit.get_OpCreateThebesLayer())->Bind(layer);
      break;
    }
    case Edit::TOpCreateContainerLayer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateContainerLayer"));

      nsRefPtr<ContainerLayer> layer = layer_manager()->CreateShadowContainerLayer();
      AsShadowLayer(edit.get_OpCreateContainerLayer())->Bind(layer);
      break;
    }
    case Edit::TOpCreateImageLayer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateImageLayer"));

      nsRefPtr<ShadowImageLayer> layer =
        layer_manager()->CreateShadowImageLayer();
      AsShadowLayer(edit.get_OpCreateImageLayer())->Bind(layer);
      break;
    }
    case Edit::TOpCreateColorLayer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateColorLayer"));

      nsRefPtr<ShadowColorLayer> layer = layer_manager()->CreateShadowColorLayer();
      AsShadowLayer(edit.get_OpCreateColorLayer())->Bind(layer);
      break;
    }
    case Edit::TOpCreateCanvasLayer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateCanvasLayer"));

      nsRefPtr<ShadowCanvasLayer> layer = 
        layer_manager()->CreateShadowCanvasLayer();
      AsShadowLayer(edit.get_OpCreateCanvasLayer())->Bind(layer);
      break;
    }
    case Edit::TOpCreateRefLayer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateRefLayer"));

      nsRefPtr<ShadowRefLayer> layer =
        layer_manager()->CreateShadowRefLayer();
      AsShadowLayer(edit.get_OpCreateRefLayer())->Bind(layer);
      break;
    }

      // Attributes
    case Edit::TOpSetLayerAttributes: {
      MOZ_LAYERS_LOG(("[ParentSide] SetLayerAttributes"));

      const OpSetLayerAttributes& osla = edit.get_OpSetLayerAttributes();
      Layer* layer = AsShadowLayer(osla)->AsLayer();
      const LayerAttributes& attrs = osla.attrs();

      const CommonLayerAttributes& common = attrs.common();
      layer->SetVisibleRegion(common.visibleRegion());
      layer->SetContentFlags(common.contentFlags());
      layer->SetOpacity(common.opacity());
      layer->SetClipRect(common.useClipRect() ? &common.clipRect() : NULL);
      layer->SetBaseTransform(common.transform().value());
      layer->SetPostScale(common.postXScale(), common.postYScale());
      static bool fixedPositionLayersEnabled = getenv("MOZ_ENABLE_FIXED_POSITION_LAYERS") != 0;
      if (fixedPositionLayersEnabled) {
        layer->SetIsFixedPosition(common.isFixedPosition());
        layer->SetFixedPositionAnchor(common.fixedPositionAnchor());
      }
      if (PLayerParent* maskLayer = common.maskLayerParent()) {
        layer->SetMaskLayer(cast(maskLayer)->AsLayer());
      } else {
        layer->SetMaskLayer(NULL);
      }
      layer->SetAnimations(common.animations());

      typedef SpecificLayerAttributes Specific;
      const SpecificLayerAttributes& specific = attrs.specific();
      switch (specific.type()) {
      case Specific::Tnull_t:
        break;

      case Specific::TThebesLayerAttributes: {
        MOZ_LAYERS_LOG(("[ParentSide]   thebes layer"));

        ShadowThebesLayer* thebesLayer =
          static_cast<ShadowThebesLayer*>(layer);
        const ThebesLayerAttributes& attrs =
          specific.get_ThebesLayerAttributes();

        thebesLayer->SetValidRegion(attrs.validRegion());

        break;
      }
      case Specific::TContainerLayerAttributes: {
        MOZ_LAYERS_LOG(("[ParentSide]   container layer"));

        ContainerLayer* containerLayer =
          static_cast<ContainerLayer*>(layer);
        const ContainerLayerAttributes& attrs =
          specific.get_ContainerLayerAttributes();
        containerLayer->SetFrameMetrics(attrs.metrics());
        containerLayer->SetPreScale(attrs.preXScale(), attrs.preYScale());
        break;
      }
      case Specific::TColorLayerAttributes:
        MOZ_LAYERS_LOG(("[ParentSide]   color layer"));

        static_cast<ColorLayer*>(layer)->SetColor(
          specific.get_ColorLayerAttributes().layerColor().value());
        break;

      case Specific::TCanvasLayerAttributes:
        MOZ_LAYERS_LOG(("[ParentSide]   canvas layer"));

        static_cast<CanvasLayer*>(layer)->SetFilter(
          specific.get_CanvasLayerAttributes().filter());
        break;

      case Specific::TRefLayerAttributes:
        MOZ_LAYERS_LOG(("[ParentSide]   ref layer"));

        static_cast<RefLayer*>(layer)->SetReferentId(
          specific.get_RefLayerAttributes().id());
        break;

      case Specific::TImageLayerAttributes: {
        MOZ_LAYERS_LOG(("[ParentSide]   image layer"));

        ImageLayer* imageLayer = static_cast<ImageLayer*>(layer);
        const ImageLayerAttributes& attrs = specific.get_ImageLayerAttributes();
        imageLayer->SetFilter(attrs.filter());
        break;
      }
      default:
        NS_RUNTIMEABORT("not reached");
      }
      break;
    }
    // Tree ops
    case Edit::TOpSetRoot: {
      MOZ_LAYERS_LOG(("[ParentSide] SetRoot"));

      mRoot = AsShadowLayer(edit.get_OpSetRoot())->AsContainer();
      break;
    }
    case Edit::TOpInsertAfter: {
      MOZ_LAYERS_LOG(("[ParentSide] InsertAfter"));

      const OpInsertAfter& oia = edit.get_OpInsertAfter();
      ShadowContainer(oia)->AsContainer()->InsertAfter(
        ShadowChild(oia)->AsLayer(), ShadowAfter(oia)->AsLayer());
      break;
    }
    case Edit::TOpAppendChild: {
      MOZ_LAYERS_LOG(("[ParentSide] AppendChild"));

      const OpAppendChild& oac = edit.get_OpAppendChild();
      ShadowContainer(oac)->AsContainer()->InsertAfter(
        ShadowChild(oac)->AsLayer(), NULL);
      break;
    }
    case Edit::TOpRemoveChild: {
      MOZ_LAYERS_LOG(("[ParentSide] RemoveChild"));

      const OpRemoveChild& orc = edit.get_OpRemoveChild();
      Layer* childLayer = ShadowChild(orc)->AsLayer();
      ShadowContainer(orc)->AsContainer()->RemoveChild(childLayer);
      break;
    }
    case Edit::TOpRepositionChild: {
      MOZ_LAYERS_LOG(("[ParentSide] RepositionChild"));

      const OpRepositionChild& orc = edit.get_OpRepositionChild();
      ShadowContainer(orc)->AsContainer()->RepositionChild(
        ShadowChild(orc)->AsLayer(), ShadowAfter(orc)->AsLayer());
      break;
    }
    case Edit::TOpRaiseToTopChild: {
      MOZ_LAYERS_LOG(("[ParentSide] RaiseToTopChild"));

      const OpRaiseToTopChild& rtc = edit.get_OpRaiseToTopChild();
      ShadowContainer(rtc)->AsContainer()->RepositionChild(
        ShadowChild(rtc)->AsLayer(), NULL);
      break;
    }

    case Edit::TOpPaintTiledLayerBuffer: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint TiledLayerBuffer"));
      const OpPaintTiledLayerBuffer& op = edit.get_OpPaintTiledLayerBuffer();
      ShadowLayerParent* shadow = AsShadowLayer(op);

      ShadowThebesLayer* shadowLayer = static_cast<ShadowThebesLayer*>(shadow->AsLayer());
      TiledLayerComposer* tileComposer = shadowLayer->GetTiledLayerComposer();

      NS_ASSERTION(tileComposer, "shadowLayer is not a tile composer");

      BasicTiledLayerBuffer* p = (BasicTiledLayerBuffer*)op.tiledLayerBuffer();
      tileComposer->PaintedTiledLayerBuffer(p);
      break;
    }
    case Edit::TOpPaintThebesBuffer: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint ThebesLayer"));

      const OpPaintThebesBuffer& op = edit.get_OpPaintThebesBuffer();
      ShadowThebesLayer* thebes =
        static_cast<ShadowThebesLayer*>(GetLayerFromOpPaint(op)->AsShadowLayer());
      const ThebesBuffer& newFront = op.newFrontBuffer();

      RenderTraceInvalidateStart(thebes, "FF00FF", op.updatedRegion().GetBounds());

      OptionalThebesBuffer newBack;
      nsIntRegion newValidRegion;
      OptionalThebesBuffer readonlyFront;
      nsIntRegion frontUpdatedRegion;
      thebes->Swap(newFront, op.updatedRegion(),
                   &newBack, &newValidRegion,
                   &readonlyFront, &frontUpdatedRegion);

      // We have to invalidate the pixels painted into the new buffer.
      // They might overlap with our old pixels.
      //aNewValidRegionFront->Sub(needsReset ? nsIntRegion() : aOldValidRegionFront, aUpdated);

      replyv.push_back(
        OpThebesBufferSwap(
          op.textureParent(), NULL,
          newBack, newValidRegion,
          readonlyFront, frontUpdatedRegion));

      RenderTraceInvalidateEnd(thebes, "FF00FF");
      break;
    }
    case Edit::TOpPaintCanvas: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint CanvasLayer"));

      const OpPaintCanvas& op = edit.get_OpPaintCanvas();
      ShadowCanvasLayer* canvas = static_cast<ShadowCanvasLayer*>(GetLayerFromOpPaint(op)->AsShadowLayer());

      RenderTraceInvalidateStart(canvas, "FF00FF", canvas->GetVisibleRegion().GetBounds());

      canvas->SetAllocator(this);
      TextureHost* textureHost = AsTextureHost(op);

      SurfaceDescriptor newBack;
      bool success;
      textureHost->Update(op.newFrontBuffer(), &newBack, &success);
      if (!success) {
        NS_ASSERTION(newBack.type() == SurfaceDescriptor::Tnull_t, "fail should give null result");
      }

      canvas->Updated();
      replyv.push_back(OpTextureSwap(op.textureParent(), nullptr, newBack));

      RenderTraceInvalidateEnd(canvas, "FF00FF");
      break;
    }
    case Edit::TOpAttachTexture: {
      const OpAttachTexture& op = edit.get_OpAttachTexture();
      TextureParent* textureParent = static_cast<TextureParent*>(op.textureParent());
      ShadowLayerParent* layerParent = static_cast<ShadowLayerParent*>(op.layerParent());
      Layer* layer = layerParent->AsLayer();
      MOZ_ASSERT(layer);

      Compositor* compositor
        = static_cast<LayerManagerComposite*>(layer->Manager())->GetCompositor();
      const TextureInfo& info = textureParent->GetTextureInfo();
      RefPtr<CompositableHost> bufferHost
        = compositor->CreateCompositableHost(textureParent->GetTextureInfo().imageType);
      RefPtr<TextureHost> textureHost
        = compositor->CreateTextureHost(info.imageType,
                                        info.memoryType,
                                        info.textureFlags,
                                        this);

      textureHost->SetCompositorID(compositor->GetCompositorID());
      textureParent->SetTextureHost(textureHost.get());
      textureHost->SetTextureParent(textureParent);
      bufferHost->AddTextureHost(textureHost);

      // TODO[nical] in the future, we'll want to set the TextureSource rather than the CompositableHost
      layer->AsShadowLayer()->SetCompositableHost(bufferHost.get());
      layer->AsShadowLayer()->SetAllocator(this);
      break;
    }
    case Edit::TOpAttachAsyncTexture: {
      // For ImageBridge
      const OpAttachAsyncTexture& op = edit.get_OpAttachAsyncTexture();
      TextureHost* host = AsTextureHost(op);
      MOZ_ASSERT(host, "TextureHost not created");
      host->SetAsyncContainerID(op.containerID());
      break;
    }
    case Edit::TOpPaintTexture: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint Texture X"));

      const OpPaintTexture& op = edit.get_OpPaintTexture();

      Layer* layer = GetLayerFromOpPaint(op);
      ShadowLayer* shadowLayer = layer->AsShadowLayer();

      RenderTraceInvalidateStart(layer, "FF00FF", layer->GetVisibleRegion().GetBounds());
      TextureHost* host = AsTextureHost(op);
      shadowLayer->SetAllocator(this);
      SurfaceDescriptor newBack;
      host->Update(op.image(), &newBack);
      replyv.push_back(OpTextureSwap(op.textureParent(), nullptr, newBack));
 
      RenderTraceInvalidateEnd(layer, "FF00FF");
      break;
    }
    case Edit::TOpPaintTextureRegion: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint ThebesLayer"));

      const OpPaintTextureRegion& op = edit.get_OpPaintTextureRegion();
      ShadowThebesLayer* thebes =
        static_cast<ShadowThebesLayer*>(GetLayerFromOpPaint(op));
      const ThebesBuffer& newFront = op.newFrontBuffer();

      RenderTraceInvalidateStart(thebes, "FF00FF", op.updatedRegion().GetBounds());

      thebes->SetAllocator(this);
      OptionalThebesBuffer newBack;
      nsIntRegion newValidRegion;
      OptionalThebesBuffer readonlyFront;
      nsIntRegion frontUpdatedRegion;
      thebes->SwapTexture(newFront, op.updatedRegion(),
                          &newBack, &newValidRegion,
                          &readonlyFront, &frontUpdatedRegion);
      replyv.push_back(
        OpThebesBufferSwap(
          op.textureParent(), nullptr,
          newBack, newValidRegion,
          readonlyFront, frontUpdatedRegion));

      RenderTraceInvalidateEnd(thebes, "FF00FF");
      break;
    }
    case Edit::TOpUpdatePictureRect: {
      const OpUpdatePictureRect& op = edit.get_OpUpdatePictureRect();
      ShadowLayerParent* shadow = AsShadowLayer(op);
      ShadowImageLayer* image =
        static_cast<ShadowImageLayer*>(shadow->AsLayer());

      image->SetPictureRect(op.picture());
      break;
    }
    default:
      NS_RUNTIMEABORT("not reached");
    }
  }

  layer_manager()->EndTransaction(NULL, NULL, LayerManager::END_NO_IMMEDIATE_REDRAW);

  reply->SetCapacity(replyv.size());
  if (replyv.size() > 0) {
    reply->AppendElements(&replyv.front(), replyv.size());
  }

  // Ensure that any pending operations involving back and front
  // buffers have completed, so that neither process stomps on the
  // other's buffer contents.
  ShadowLayerManager::PlatformSyncBeforeReplyUpdate();

  mShadowLayersManager->ShadowLayersUpdated(this, targetConfig, isFirstPaint);

#ifdef COMPOSITOR_PERFORMANCE_WARNING
  int compositeTime = (int)(mozilla::TimeStamp::Now() - updateStart).ToMilliseconds();
  if (compositeTime > 15) {
    printf_stderr("Compositor: Layers update took %i ms (blocking gecko).\n", compositeTime);
  }
#endif

  return true;
}

bool
ShadowLayersParent::RecvClearCachedResources()
{
  if (mRoot) {
    // NB: |mRoot| here is the *child* context's root.  In this parent
    // context, it's just a subtree root.  We need to scope the clear
    // of resources to exactly that subtree, so we specify it here.
    mLayerManager->ClearCachedResources(mRoot);
  }
  return true;
}

PGrallocBufferParent*
ShadowLayersParent::AllocPGrallocBuffer(const gfxIntSize& aSize,
                                        const gfxContentType& aContent,
                                        MaybeMagicGrallocBufferHandle* aOutHandle)
{
#ifdef MOZ_HAVE_SURFACEDESCRIPTORGRALLOC
  return GrallocBufferActor::Create(aSize, aContent, aOutHandle);
#else
  NS_RUNTIMEABORT("No gralloc buffers for you");
  return nullptr;
#endif
}

bool
ShadowLayersParent::DeallocPGrallocBuffer(PGrallocBufferParent* actor)
{
#ifdef MOZ_HAVE_SURFACEDESCRIPTORGRALLOC
  delete actor;
  return true;
#else
  NS_RUNTIMEABORT("Um, how did we get here?");
  return false;
#endif
}

PLayerParent*
ShadowLayersParent::AllocPLayer()
{
  return new ShadowLayerParent();
}

bool
ShadowLayersParent::DeallocPLayer(PLayerParent* actor)
{
  delete actor;
  return true;
}

void
ShadowLayersParent::DestroySharedSurface(gfxSharedImageSurface* aSurface)
{
  layer_manager()->DestroySharedSurface(aSurface, this);
}

void
ShadowLayersParent::DestroySharedSurface(SurfaceDescriptor* aSurface)
{
  layer_manager()->DestroySharedSurface(aSurface, this);
}

} // namespace layers
} // namespace mozilla
