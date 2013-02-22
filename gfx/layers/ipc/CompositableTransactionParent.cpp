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

// TODO[nical] we should not need this
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
                                                     const bool& isFirstPaint,
                                                     EditReplyVector& replyv)
{
  switch (aEdit.type()) {
    case CompositableOperation::TOpPaintTexture: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint Texture X"));
      const OpPaintTexture& op = aEdit.get_OpPaintTexture();

      Compositor* compositor = nullptr;
      Layer* layer = GetLayerFromOpPaint(op);
      ShadowLayer* shadowLayer = layer ? layer->AsShadowLayer() : nullptr;
      if (shadowLayer) {
         compositor = static_cast<LayerManagerComposite*>(layer->Manager())->GetCompositor();
      } else {
        NS_WARNING("Trying to paint before OpAttachAsyncTexture?");
      }

      TextureParent* textureParent = static_cast<TextureParent*>(op.textureParent());
      const SurfaceDescriptor& descriptor = op.image();
      if (compositor) {
        textureParent->GetCompositableHost()->SetCompositor(compositor);
      }
      textureParent->EnsureTextureHost(descriptor.type());
      if (textureParent->GetCompositorID()) {
        CompositorParent* cp
          = CompositorParent::GetCompositor(textureParent->GetCompositorID());
        cp->ScheduleComposition();
      }
      if (!textureParent->GetTextureHost()) {
        NS_WARNING("failed to create the texture host");
        // in this case we keep the surface descriptor in the texture parent in
        // order to update the texture host as soon as it will be created and
        // not miss the first frame (this can only happen with async video at
        // the moment).
        if (textureParent->HasBuffer()) {
          replyv.push_back(OpTextureSwap(textureParent, nullptr, textureParent->GetBuffer()));
        }
        textureParent->SetBuffer(op.image());
        break;
      }

      TextureHost* host = AsTextureHost(op);
      shadowLayer->SetAllocator(this);
      SurfaceDescriptor newBack;
      RenderTraceInvalidateStart(layer, "FF00FF", layer->GetVisibleRegion().GetBounds());
      host->Update(op.image(), &newBack);
      replyv.push_back(OpTextureSwap(op.textureParent(), nullptr, newBack));

      if (textureParent->HasBuffer()) {
        // TODO[nical] release texureParent->GetBuffer();
      }

      RenderTraceInvalidateEnd(layer, "FF00FF");
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

      thebes->SetAllocator(this);
      OptionalThebesBuffer newBack;
      nsIntRegion newValidRegion;
      OptionalThebesBuffer readOnlyFront;
      nsIntRegion frontUpdatedRegion;
      compositable->UpdateThebes(newFront,
                                 op.updatedRegion(),
                                 &newBack,
                                 thebes->GetValidRegion(),
                                 &readOnlyFront,
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

