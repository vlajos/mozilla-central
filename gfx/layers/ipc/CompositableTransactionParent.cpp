/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=8 et :
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CompositableTransactionParent.h"
#include "ShadowLayers.h"
#include "RenderTrace.h"
#include "ShadowLayersManager.h"
#include "CompositableHost.h"
#include "ShadowLayerParent.h"
#include "TiledLayerBuffer.h"
#include "mozilla/layers/TextureParent.h"
#include "LayerManagerComposite.h"
#include "CompositorParent.h"

namespace mozilla {
namespace layers {

//--------------------------------------------------
// Convenience accessors
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
  CompositableHost* compoHost
    = static_cast<CompositableParent*>(textureParent->Manager())->GetCompositableHost();
  return compoHost ? compoHost->GetLayer() : nullptr;
}

bool
CompositableParentManager::ReceiveCompositableUpdate(const CompositableOperation& aEdit,
                                                     EditReplyVector& replyv)
{
  switch (aEdit.type()) {
    case CompositableOperation::TOpPaintTexture: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint Texture X"));
      const OpPaintTexture& op = aEdit.get_OpPaintTexture();

      TextureParent* textureParent = static_cast<TextureParent*>(op.textureParent());
      CompositableHost* compositable = textureParent->GetCompositableHost();
      Layer* layer = GetLayerFromOpPaint(op);
      ShadowLayer* shadowLayer = layer ? layer->AsShadowLayer() : nullptr;
      if (shadowLayer) {
        Compositor* compositor = static_cast<LayerManagerComposite*>(layer->Manager())->GetCompositor();
        compositable->SetCompositor(compositor);
        compositable->SetLayer(layer);
      } else {
        // if we reach this branch, it most likely means that async textures
        // are coming in before we had time to attach the conmpositable to a
        // layer. Don't panic, it is okay in this case. it should not be
        // happenning continuously, though.
      }

      const SurfaceDescriptor& descriptor = op.image();
      textureParent->EnsureTextureHost(descriptor.type());
      MOZ_ASSERT(textureParent->GetTextureHost());

      SurfaceDescriptor newBack;
      bool shouldRecomposite = compositable->Update(op.image(), &newBack);
      if (IsSurfaceDescriptorValid(newBack)) {
        replyv.push_back(OpTextureSwap(op.textureParent(), nullptr, newBack));
      }

      if (shouldRecomposite && textureParent->GetCompositorID()) {
        CompositorParent* cp
          = CompositorParent::GetCompositor(textureParent->GetCompositorID());
        if (cp) {
          cp->ScheduleComposition();
        }
      }

      if (layer) {
        RenderTraceInvalidateStart(layer, "FF00FF", layer->GetVisibleRegion().GetBounds());

        //TODO shouldn't there be something between RenderTraceInvalidateStart and End?

        RenderTraceInvalidateEnd(layer, "FF00FF");
      }

      break;
    }
    case CompositableOperation::TOpPaintTextureRegion: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint ThebesLayer"));

      const OpPaintTextureRegion& op = aEdit.get_OpPaintTextureRegion();
      ShadowThebesLayer* thebes =
        static_cast<ShadowThebesLayer*>(GetLayerFromOpPaint(op));
      TextureParent* textureParent = 
        static_cast<TextureParent*>(op.textureParent());
      CompositableHost* compositable =
        textureParent->GetCompositableHost();

      const ThebesBuffer& newFront = op.newFrontBuffer();

      RenderTraceInvalidateStart(thebes, "FF00FF", op.updatedRegion().GetBounds());

      textureParent->EnsureTextureHost(newFront.buffer().type());

      OptionalThebesBuffer newBack;
      nsIntRegion newValidRegion;
      OptionalThebesBuffer readOnlyFront;
      nsIntRegion frontUpdatedRegion;
      compositable->UpdateThebes(newFront,
                                 op.updatedRegion(),
                                 &readOnlyFront,
                                 thebes->GetValidRegion(),
                                 &newBack,
                                 &newValidRegion,
                                 &frontUpdatedRegion);
      replyv.push_back(
        OpThebesBufferSwap(
          op.textureParent(), nullptr,
          newBack, newValidRegion,
          readOnlyFront, frontUpdatedRegion));

      RenderTraceInvalidateEnd(thebes, "FF00FF");
      break;
    }
    case CompositableOperation::TOpUpdatePictureRect: {
      const OpUpdatePictureRect& op = aEdit.get_OpUpdatePictureRect();
      CompositableHost* compositable
       = static_cast<CompositableParent*>(op.compositableParent())->GetCompositableHost();
      MOZ_ASSERT(compositable);
      compositable->SetPictureRect(op.picture());
      break;
    }
    default: {
      MOZ_ASSERT(false, "bad type");
    }
  }

  return true;
}

} // namespace
} // namespace

