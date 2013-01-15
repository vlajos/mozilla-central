/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ipc/AutoOpenSurface.h"
#include "mozilla/layers/PLayers.h"
#include "TiledLayerBuffer.h"
#include "mozilla/layers/TextureOGL.h"
#include "mozilla/layers/CompositingRenderTargetOGL.h"
#include "LayerManagerComposite.h"

/* This must occur *after* layers/PLayers.h to avoid typedefs conflicts. */
#include "mozilla/Util.h"

#include "ThebesLayerBuffer.h"
#include "ThebesLayerOGL.h"
#include "mozilla/layers/ContentHost.h"
#include "gfxUtils.h"
#include "gfx2DGlue.h"
#include "gfxTeeSurface.h"

#include "base/message_loop.h"

namespace mozilla {
namespace layers {

using gl::GLContext;
using gl::TextureImage;

static const int ALLOW_REPEAT = ThebesLayerBuffer::ALLOW_REPEAT;

GLenum
WrapMode(GLContext *aGl, uint32_t aFlags)
{
  if ((aFlags & ALLOW_REPEAT) &&
      (aGl->IsExtensionSupported(GLContext::ARB_texture_non_power_of_two) ||
       aGl->IsExtensionSupported(GLContext::OES_texture_npot))) {
    return LOCAL_GL_REPEAT;
  }
  return LOCAL_GL_CLAMP_TO_EDGE;
}

// BindAndDrawQuadWithTextureRect can work with either GL_REPEAT (preferred)
// or GL_CLAMP_TO_EDGE textures. If ALLOW_REPEAT is set in aFlags, we
// select based on whether REPEAT is valid for non-power-of-two textures --
// if we have NPOT support we use it, otherwise we stick with CLAMP_TO_EDGE and
// decompose.
// If ALLOW_REPEAT is not set, we always use GL_CLAMP_TO_EDGE.
static already_AddRefed<TextureImage>
CreateClampOrRepeatTextureImage(GLContext *aGl,
                                const nsIntSize& aSize,
                                TextureImage::ContentType aContentType,
                                uint32_t aFlags)
{

  return aGl->CreateTextureImage(aSize, aContentType, WrapMode(aGl, aFlags));
}

static void
SetAntialiasingFlags(Layer* aLayer, gfxContext* aTarget)
{
  nsRefPtr<gfxASurface> surface = aTarget->CurrentSurface();
  if (surface->GetContentType() != gfxASurface::CONTENT_COLOR_ALPHA) {
    // Destination doesn't have alpha channel; no need to set any special flags
    return;
  }

  surface->SetSubpixelAntialiasingEnabled(
      !(aLayer->GetContentFlags() & Layer::CONTENT_COMPONENT_ALPHA));
}

class ThebesLayerBufferOGL : public CompositingThebesLayerBuffer
{
public:
  void Abort()
  {
    if (mTextureHost) {
      mTextureHost->Abort();
    }
    if (mTextureHostOnWhite) {
      mTextureHostOnWhite->Abort();
    }
  }

  enum { PAINT_WILL_RESAMPLE = ThebesLayerBuffer::PAINT_WILL_RESAMPLE };

#ifdef MOZ_DUMP_PAINTING
  virtual already_AddRefed<gfxImageSurface> Dump()
  {
    return mTextureHost->Dump();
  }
#endif

protected:
  ThebesLayerBufferOGL(ThebesLayer* aLayer, LayerOGL* aOGLLayer, Compositor* aCompositor)
    : CompositingThebesLayerBuffer(aCompositor)
    , mLayer(aLayer)
    , mOGLLayer(aOGLLayer)
  {}

  GLContext* gl() const { return mOGLLayer->gl(); }

  ThebesLayer* mLayer;
  LayerOGL* mOGLLayer;
};

// This implementation is the fast-path for when our TextureImage is
// permanently backed with a server-side ASurface.  We can simply
// reuse the ThebesLayerBuffer logic in its entirety and profit.
class SurfaceBufferOGL : public ThebesLayerBufferOGL, private ThebesLayerBuffer
{
public:
  typedef CompositingThebesLayerBuffer::ContentType ContentType;
  typedef CompositingThebesLayerBuffer::PaintState PaintState;

  SurfaceBufferOGL(ThebesLayerOGL* aLayer, Compositor* aCompositor)
    : ThebesLayerBufferOGL(aLayer, aLayer, aCompositor)
    , ThebesLayerBuffer(SizedToVisibleBounds)
  {
  }
  virtual ~SurfaceBufferOGL() {}

  // CompositingThebesLayerBuffer interface
  virtual PaintState BeginPaint(ContentType aContentType, 
                                uint32_t aFlags)
  {
    // Let ThebesLayerBuffer do all the hard work for us! :D
    return ThebesLayerBuffer::BeginPaint(mLayer, 
                                         aContentType, 
                                         aFlags);
  }

  // ThebesLayerBuffer interface
  virtual already_AddRefed<gfxASurface>
  CreateBuffer(ContentType aType, const nsIntSize& aSize, uint32_t aFlags)
  {
    NS_ASSERTION(gfxASurface::CONTENT_ALPHA != aType,"ThebesBuffer has color");

    nsRefPtr<TextureImage> texImage = CreateClampOrRepeatTextureImage(gl(), aSize, aType, aFlags);
    if (!texImage) {
      mTextureHost = nullptr;
      return nullptr;
    }

    mTextureHost = new TextureImageAsTextureHostOGL(gl(), texImage, BUFFER_NONE);
    return texImage->GetBackingSurface();
  }

protected:
  virtual nsIntPoint GetOriginOffset() {
    return BufferRect().TopLeft() - BufferRotation();
  }
};


// This implementation is (currently) the slow-path for when we can't
// implement pixel retaining using thebes.  This implementation and
// the above could be unified by abstracting buffer-copy operations
// and implementing them here using GL hacketry.
class BasicBufferOGL : public ThebesLayerBufferOGL
{
public:
  BasicBufferOGL(ThebesLayerOGL* aLayer, Compositor* aCompositor)
    : ThebesLayerBufferOGL(aLayer, aLayer, aCompositor)
    , mBufferRect(0,0,0,0)
    , mBufferRotation(0,0)
  {}
  virtual ~BasicBufferOGL() {}

  virtual PaintState BeginPaint(ContentType aContentType,
                                uint32_t aFlags);

protected:
  enum XSide {
    LEFT, RIGHT
  };
  enum YSide {
    TOP, BOTTOM
  };
  nsIntRect GetQuadrantRectangle(XSide aXSide, YSide aYSide);

  virtual nsIntPoint GetOriginOffset() {
    return mBufferRect.TopLeft() - mBufferRotation;
  }

private:
  nsIntRect mBufferRect;
  nsIntPoint mBufferRotation;
};

static void
WrapRotationAxis(int32_t* aRotationPoint, int32_t aSize)
{
  if (*aRotationPoint < 0) {
    *aRotationPoint += aSize;
  } else if (*aRotationPoint >= aSize) {
    *aRotationPoint -= aSize;
  }
}

nsIntRect
BasicBufferOGL::GetQuadrantRectangle(XSide aXSide, YSide aYSide)
{
  // quadrantTranslation is the amount we translate the top-left
  // of the quadrant by to get coordinates relative to the layer
  nsIntPoint quadrantTranslation = -mBufferRotation;
  quadrantTranslation.x += aXSide == LEFT ? mBufferRect.width : 0;
  quadrantTranslation.y += aYSide == TOP ? mBufferRect.height : 0;
  return mBufferRect + quadrantTranslation;
}

static void
FillSurface(gfxASurface* aSurface, const nsIntRegion& aRegion,
            const nsIntPoint& aOffset, const gfxRGBA& aColor)
{
  nsRefPtr<gfxContext> ctx = new gfxContext(aSurface);
  ctx->Translate(-gfxPoint(aOffset.x, aOffset.y));
  gfxUtils::ClipToRegion(ctx, aRegion);
  ctx->SetColor(aColor);
  ctx->Paint();
}

BasicBufferOGL::PaintState
BasicBufferOGL::BeginPaint(ContentType aContentType,
                           uint32_t aFlags)
{
  PaintState result;
  // We need to disable rotation if we're going to be resampled when
  // drawing, because we might sample across the rotation boundary.
  bool canHaveRotation =  !(aFlags & PAINT_WILL_RESAMPLE);

  nsIntRegion validRegion = mLayer->GetValidRegion();

  Layer::SurfaceMode mode;
  ContentType contentType;
  nsIntRegion neededRegion;
  bool canReuseBuffer;
  nsIntRect destBufferRect;

  // TODO Assuming this is really bad.
  nsRefPtr<TextureImage> texImage = mTextureHost
                                    ? static_cast<TextureImageAsTextureHostOGL*>(mTextureHost.get())->GetTextureImage()
                                    : nullptr;
  nsRefPtr<TextureImage> texImageOnWhite = mTextureHostOnWhite 
                                           ? static_cast<TextureImageAsTextureHostOGL*>(mTextureHostOnWhite.get())->GetTextureImage()
                                           : nullptr;
  while (true) {
    mode = mLayer->GetSurfaceMode();
    contentType = aContentType;
    neededRegion = mLayer->GetVisibleRegion();
    // If we're going to resample, we need a buffer that's in clamp mode.
    canReuseBuffer = neededRegion.GetBounds().Size() <= mBufferRect.Size() &&
      texImage &&
      (!(aFlags & PAINT_WILL_RESAMPLE) ||
       texImage->GetWrapMode() == LOCAL_GL_CLAMP_TO_EDGE);

    if (canReuseBuffer) {
      if (mBufferRect.Contains(neededRegion.GetBounds())) {
        // We don't need to adjust mBufferRect.
        destBufferRect = mBufferRect;
      } else {
        // The buffer's big enough but doesn't contain everything that's
        // going to be visible. We'll move it.
        destBufferRect = nsIntRect(neededRegion.GetBounds().TopLeft(), mBufferRect.Size());
      }
    } else {
      destBufferRect = neededRegion.GetBounds();
    }

    if (mode == Layer::SURFACE_COMPONENT_ALPHA) {
#ifdef MOZ_GFX_OPTIMIZE_MOBILE
      mode = Layer::SURFACE_SINGLE_CHANNEL_ALPHA;
#else
      if (!mLayer->GetParent() || !mLayer->GetParent()->SupportsComponentAlphaChildren()) {
        mode = Layer::SURFACE_SINGLE_CHANNEL_ALPHA;
      } else {
        contentType = gfxASurface::CONTENT_COLOR;
      }
 #endif
    }
 
    if ((aFlags & PAINT_WILL_RESAMPLE) &&
        (!neededRegion.GetBounds().IsEqualInterior(destBufferRect) ||
         neededRegion.GetNumRects() > 1)) {
      // The area we add to neededRegion might not be painted opaquely
      if (mode == Layer::SURFACE_OPAQUE) {
        contentType = gfxASurface::CONTENT_COLOR_ALPHA;
        mode = Layer::SURFACE_SINGLE_CHANNEL_ALPHA;
      }
      // For component alpha layers, we leave contentType as CONTENT_COLOR.

      // We need to validate the entire buffer, to make sure that only valid
      // pixels are sampled
      neededRegion = destBufferRect;
    }

    if (texImage &&
        (texImage->GetContentType() != contentType ||
         (mode == Layer::SURFACE_COMPONENT_ALPHA) != (texImageOnWhite != nullptr))) {
      // We're effectively clearing the valid region, so we need to draw
      // the entire needed region now.
      result.mRegionToInvalidate = mLayer->GetValidRegion();
      validRegion.SetEmpty();
      texImage = nullptr;
      texImageOnWhite = nullptr;
      mBufferRect.SetRect(0, 0, 0, 0);
      mBufferRotation.MoveTo(0, 0);
      // Restart decision process with the cleared buffer. We can only go
      // around the loop one more iteration, since texImage is null now.
      continue;
    }

    break;
  }

  result.mRegionToDraw.Sub(neededRegion, validRegion);
  if (result.mRegionToDraw.IsEmpty())
    return result;

  if (destBufferRect.width > gl()->GetMaxTextureImageSize() ||
      destBufferRect.height > gl()->GetMaxTextureImageSize()) {
    return result;
  }

  nsIntRect drawBounds = result.mRegionToDraw.GetBounds();
  nsRefPtr<TextureImage> destBuffer;
  nsRefPtr<TextureImage> destBufferOnWhite;

  uint32_t bufferFlags = canHaveRotation ? ALLOW_REPEAT : 0;
  if (canReuseBuffer) {
    nsIntRect keepArea;
    if (keepArea.IntersectRect(destBufferRect, mBufferRect)) {
      // Set mBufferRotation so that the pixels currently in mBuffer
      // will still be rendered in the right place when mBufferRect
      // changes to destBufferRect.
      nsIntPoint newRotation = mBufferRotation +
        (destBufferRect.TopLeft() - mBufferRect.TopLeft());
      WrapRotationAxis(&newRotation.x, mBufferRect.width);
      WrapRotationAxis(&newRotation.y, mBufferRect.height);
      NS_ASSERTION(nsIntRect(nsIntPoint(0,0), mBufferRect.Size()).Contains(newRotation),
                   "newRotation out of bounds");
      int32_t xBoundary = destBufferRect.XMost() - newRotation.x;
      int32_t yBoundary = destBufferRect.YMost() - newRotation.y;
      if ((drawBounds.x < xBoundary && xBoundary < drawBounds.XMost()) ||
          (drawBounds.y < yBoundary && yBoundary < drawBounds.YMost()) ||
          (newRotation != nsIntPoint(0,0) && !canHaveRotation)) {
        // The stuff we need to redraw will wrap around an edge of the
        // buffer, so we will need to do a self-copy
        // If mBufferRotation == nsIntPoint(0,0) we could do a real
        // self-copy but we're not going to do that in GL yet.
        // We can't do a real self-copy because the buffer is rotated.
        // So allocate a new buffer for the destination.
        destBufferRect = neededRegion.GetBounds();
        destBuffer = CreateClampOrRepeatTextureImage(gl(), destBufferRect.Size(), contentType, bufferFlags);
        if (!destBuffer)
          return result;
        if (mode == Layer::SURFACE_COMPONENT_ALPHA) {
          destBufferOnWhite =
            CreateClampOrRepeatTextureImage(gl(), destBufferRect.Size(), contentType, bufferFlags);
          if (!destBufferOnWhite)
            return result;
        }
      } else {
        mBufferRect = destBufferRect;
        mBufferRotation = newRotation;
      }
    } else {
      // No pixels are going to be kept. The whole visible region
      // will be redrawn, so we don't need to copy anything, so we don't
      // set destBuffer.
      mBufferRect = destBufferRect;
      mBufferRotation = nsIntPoint(0,0);
    }
  } else {
    // The buffer's not big enough, so allocate a new one
    destBuffer = CreateClampOrRepeatTextureImage(gl(), destBufferRect.Size(), contentType, bufferFlags);
    if (!destBuffer)
      return result;

    if (mode == Layer::SURFACE_COMPONENT_ALPHA) {
      destBufferOnWhite = 
        CreateClampOrRepeatTextureImage(gl(), destBufferRect.Size(), contentType, bufferFlags);
      if (!destBufferOnWhite)
        return result;
    }
  }
  NS_ASSERTION(!(aFlags & PAINT_WILL_RESAMPLE) || destBufferRect == neededRegion.GetBounds(),
               "If we're resampling, we need to validate the entire buffer");

  if (!destBuffer && !texImage) {
    return result;
  }

  if (destBuffer) {
    if (texImage && (mode != Layer::SURFACE_COMPONENT_ALPHA || texImageOnWhite)) {
      // BlitTextureImage depends on the FBO texture target being
      // TEXTURE_2D.  This isn't the case on some older X1600-era Radeons.
      if (mOGLLayer->OGLManager()->FBOTextureTarget() == LOCAL_GL_TEXTURE_2D) {
        nsIntRect overlap;

        // The buffer looks like:
        //  ______
        // |1  |2 |  Where the center point is offset by mBufferRotation from the top-left corner.
        // |___|__|
        // |3  |4 |
        // |___|__|
        //
        // This is drawn to the screen as:
        //  ______
        // |4  |3 |  Where the center point is { width - mBufferRotation.x, height - mBufferRotation.y } from
        // |___|__|  from the top left corner - rotationPoint.
        // |2  |1 |
        // |___|__|
        //

        // The basic idea below is to take all quadrant rectangles from the src and transform them into rectangles
        // in the destination. Unfortunately, it seems it is overly complex and could perhaps be simplified.

        nsIntRect srcBufferSpaceBottomRight(mBufferRotation.x, mBufferRotation.y, mBufferRect.width - mBufferRotation.x, mBufferRect.height - mBufferRotation.y);
        nsIntRect srcBufferSpaceTopRight(mBufferRotation.x, 0, mBufferRect.width - mBufferRotation.x, mBufferRotation.y);
        nsIntRect srcBufferSpaceTopLeft(0, 0, mBufferRotation.x, mBufferRotation.y);
        nsIntRect srcBufferSpaceBottomLeft(0, mBufferRotation.y, mBufferRotation.x, mBufferRect.height - mBufferRotation.y);

        overlap.IntersectRect(mBufferRect, destBufferRect);

        nsIntRect srcRect(overlap), dstRect(overlap);
        srcRect.MoveBy(- mBufferRect.TopLeft() + mBufferRotation);
        dstRect.MoveBy(- destBufferRect.TopLeft());
        
        if (mBufferRotation != nsIntPoint(0, 0)) {
          // If mBuffer is rotated, then BlitTextureImage will only be copying the bottom-right section
          // of the buffer. We need to invalidate the remaining sections so that they get redrawn too.
          // Alternatively we could teach BlitTextureImage to rearrange the rotated segments onto
          // the new buffer.
          
          // When the rotated buffer is reorganised, the bottom-right section will be drawn in the top left.
          // Find the point where this content ends.
          nsIntPoint rotationPoint(mBufferRect.x + mBufferRect.width - mBufferRotation.x, 
                                   mBufferRect.y + mBufferRect.height - mBufferRotation.y);
        }
        nsIntRect srcRectDrawTopRight(srcRect);
        nsIntRect srcRectDrawTopLeft(srcRect);
        nsIntRect srcRectDrawBottomLeft(srcRect);
        // transform into the different quadrants
        srcRectDrawTopRight  .MoveBy(-nsIntPoint(0, mBufferRect.height));
        srcRectDrawTopLeft   .MoveBy(-nsIntPoint(mBufferRect.width, mBufferRect.height));
        srcRectDrawBottomLeft.MoveBy(-nsIntPoint(mBufferRect.width, 0));

        // Intersect with the quadrant
        srcRect               = srcRect              .Intersect(srcBufferSpaceBottomRight);
        srcRectDrawTopRight   = srcRectDrawTopRight  .Intersect(srcBufferSpaceTopRight);
        srcRectDrawTopLeft    = srcRectDrawTopLeft   .Intersect(srcBufferSpaceTopLeft);
        srcRectDrawBottomLeft = srcRectDrawBottomLeft.Intersect(srcBufferSpaceBottomLeft);

        dstRect = srcRect;
        nsIntRect dstRectDrawTopRight(srcRectDrawTopRight);
        nsIntRect dstRectDrawTopLeft(srcRectDrawTopLeft);
        nsIntRect dstRectDrawBottomLeft(srcRectDrawBottomLeft);

        // transform back to src buffer space
        dstRect              .MoveBy(-mBufferRotation);
        dstRectDrawTopRight  .MoveBy(-mBufferRotation + nsIntPoint(0, mBufferRect.height));
        dstRectDrawTopLeft   .MoveBy(-mBufferRotation + nsIntPoint(mBufferRect.width, mBufferRect.height));
        dstRectDrawBottomLeft.MoveBy(-mBufferRotation + nsIntPoint(mBufferRect.width, 0));

        // transform back to draw coordinates
        dstRect              .MoveBy(mBufferRect.TopLeft());
        dstRectDrawTopRight  .MoveBy(mBufferRect.TopLeft());
        dstRectDrawTopLeft   .MoveBy(mBufferRect.TopLeft());
        dstRectDrawBottomLeft.MoveBy(mBufferRect.TopLeft());

        // transform to destBuffer space
        dstRect              .MoveBy(-destBufferRect.TopLeft());
        dstRectDrawTopRight  .MoveBy(-destBufferRect.TopLeft());
        dstRectDrawTopLeft   .MoveBy(-destBufferRect.TopLeft());
        dstRectDrawBottomLeft.MoveBy(-destBufferRect.TopLeft());

        TextureImage* texImageSource
          = static_cast<TextureImageAsTextureHostOGL*>(mTextureHost.get())->GetTextureImage();
        
        destBuffer->Resize(destBufferRect.Size());

        gl()->BlitTextureImage(texImageSource, srcRect,
                               destBuffer, dstRect);
        if (mBufferRotation != nsIntPoint(0, 0)) {
          // Draw the remaining quadrants. We call BlitTextureImage 3 extra
          // times instead of doing a single draw call because supporting that
          // with a tiled source is quite tricky.

          if (!srcRectDrawTopRight.IsEmpty())
            gl()->BlitTextureImage(texImageSource, srcRectDrawTopRight,
                                   destBuffer, dstRectDrawTopRight);
          if (!srcRectDrawTopLeft.IsEmpty())
            gl()->BlitTextureImage(texImageSource, srcRectDrawTopLeft,
                                   destBuffer, dstRectDrawTopLeft);
          if (!srcRectDrawBottomLeft.IsEmpty())
            gl()->BlitTextureImage(texImageSource, srcRectDrawBottomLeft,
                                   destBuffer, dstRectDrawBottomLeft);
        }
        destBuffer->MarkValid();

        if (mode == Layer::SURFACE_COMPONENT_ALPHA) {
          destBufferOnWhite->Resize(destBufferRect.Size());
          TextureImage* texImageSourceOnWhite 
            = static_cast<TextureImageAsTextureHostOGL*>(mTextureHostOnWhite.get())->GetTextureImage();
        
          gl()->BlitTextureImage(texImageSourceOnWhite, srcRect,
                                 destBufferOnWhite, dstRect);
          if (mBufferRotation != nsIntPoint(0, 0)) {
            // draw the remaining quadrants
            if (!srcRectDrawTopRight.IsEmpty())
              gl()->BlitTextureImage(texImageSourceOnWhite, srcRectDrawTopRight,
                                     destBufferOnWhite, dstRectDrawTopRight);
            if (!srcRectDrawTopLeft.IsEmpty())
              gl()->BlitTextureImage(texImageSourceOnWhite, srcRectDrawTopLeft,
                                     destBufferOnWhite, dstRectDrawTopLeft);
            if (!srcRectDrawBottomLeft.IsEmpty())
              gl()->BlitTextureImage(texImageSourceOnWhite  , srcRectDrawBottomLeft,
                                     destBufferOnWhite, dstRectDrawBottomLeft);
          }
          destBufferOnWhite->MarkValid();
        }
      } else {
        // can't blit, just draw everything
        destBuffer = CreateClampOrRepeatTextureImage(gl(), destBufferRect.Size(), contentType, bufferFlags);
        if (mode == Layer::SURFACE_COMPONENT_ALPHA) {
          destBufferOnWhite = 
            CreateClampOrRepeatTextureImage(gl(), destBufferRect.Size(), contentType, bufferFlags);
        }
      }
    }

    texImage = destBuffer.forget();
    if (mode == Layer::SURFACE_COMPONENT_ALPHA) {
      texImageOnWhite = destBufferOnWhite.forget();
    }
    mBufferRect = destBufferRect;
    mBufferRotation = nsIntPoint(0,0);
  }
  NS_ASSERTION(canHaveRotation || mBufferRotation == nsIntPoint(0,0),
               "Rotation disabled, but we have nonzero rotation?");

  if (mTextureHost) {
    static_cast<TextureImageAsTextureHostOGL*>(mTextureHost.get())->SetTextureImage(texImage);
  } else {
    mTextureHost = new TextureImageAsTextureHostOGL(gl(), texImage);
  }

  if (!texImageOnWhite) {
    mTextureHostOnWhite = nullptr;
  } else {
    if (mTextureHostOnWhite) {
      static_cast<TextureImageAsTextureHostOGL*>(mTextureHostOnWhite.get())->SetTextureImage(texImageOnWhite);
    } else if (texImageOnWhite) {
      mTextureHostOnWhite = new TextureImageAsTextureHostOGL(gl(), texImageOnWhite);
    }
  }

  nsIntRegion invalidate;
  invalidate.Sub(mLayer->GetValidRegion(), destBufferRect);
  result.mRegionToInvalidate.Or(result.mRegionToInvalidate, invalidate);

  // Figure out which quadrant to draw in
  int32_t xBoundary = mBufferRect.XMost() - mBufferRotation.x;
  int32_t yBoundary = mBufferRect.YMost() - mBufferRotation.y;
  XSide sideX = drawBounds.XMost() <= xBoundary ? RIGHT : LEFT;
  YSide sideY = drawBounds.YMost() <= yBoundary ? BOTTOM : TOP;
  nsIntRect quadrantRect = GetQuadrantRectangle(sideX, sideY);
  NS_ASSERTION(quadrantRect.Contains(drawBounds), "Messed up quadrants");

  nsIntPoint offset = -nsIntPoint(quadrantRect.x, quadrantRect.y);

  // Make the region to draw relative to the buffer, before
  // passing to BeginUpdate.
  result.mRegionToDraw.MoveBy(offset);
  // BeginUpdate is allowed to modify the given region,
  // if it wants more to be repainted than we request.
  if (mode == Layer::SURFACE_COMPONENT_ALPHA) {
    nsIntRegion drawRegionCopy = result.mRegionToDraw;
    gfxASurface *onBlack = texImage->BeginUpdate(drawRegionCopy);
    gfxASurface *onWhite = texImageOnWhite->BeginUpdate(result.mRegionToDraw);
    NS_ASSERTION(result.mRegionToDraw == drawRegionCopy,
                 "BeginUpdate should always modify the draw region in the same way!");
    FillSurface(onBlack, result.mRegionToDraw, nsIntPoint(0,0), gfxRGBA(0.0, 0.0, 0.0, 1.0));
    FillSurface(onWhite, result.mRegionToDraw, nsIntPoint(0,0), gfxRGBA(1.0, 1.0, 1.0, 1.0));
    gfxASurface* surfaces[2] = { onBlack, onWhite };
    nsRefPtr<gfxTeeSurface> surf = new gfxTeeSurface(surfaces, ArrayLength(surfaces));

    // XXX If the device offset is set on the individual surfaces instead of on
    // the tee surface, we render in the wrong place. Why?
    gfxPoint deviceOffset = onBlack->GetDeviceOffset();
    onBlack->SetDeviceOffset(gfxPoint(0, 0));
    onWhite->SetDeviceOffset(gfxPoint(0, 0));
    surf->SetDeviceOffset(deviceOffset);

    // Using this surface as a source will likely go horribly wrong, since
    // only the onBlack surface will really be used, so alpha information will
    // be incorrect.
    surf->SetAllowUseAsSource(false);
    result.mContext = new gfxContext(surf);
  } else {
    result.mContext = new gfxContext(texImage->BeginUpdate(result.mRegionToDraw));
    if (texImage->GetContentType() == gfxASurface::CONTENT_COLOR_ALPHA) {
      gfxUtils::ClipToRegion(result.mContext, result.mRegionToDraw);
      result.mContext->SetOperator(gfxContext::OPERATOR_CLEAR);
      result.mContext->Paint();
      result.mContext->SetOperator(gfxContext::OPERATOR_OVER);
    }
  }
  if (!result.mContext) {
    NS_WARNING("unable to get context for update");
    return result;
  }
  result.mContext->Translate(-gfxPoint(quadrantRect.x, quadrantRect.y));
  // Move rgnToPaint back into position so that the thebes callback
  // gets the right coordintes.
  result.mRegionToDraw.MoveBy(-offset);

  // If we do partial updates, we have to clip drawing to the regionToDraw.
  // If we don't clip, background images will be fillrect'd to the region correctly,
  // while text or lines will paint outside of the regionToDraw. This becomes apparent
  // with concave regions. Right now the scrollbars invalidate a narrow strip of the TextureImageAsbar
  // although they never cover it. This leads to two draw rects, the narow strip and the actually
  // newly exposed area. It would be wise to fix this glitch in any way to have simpler
  // clip and draw regions.
  gfxUtils::ClipToRegion(result.mContext, result.mRegionToDraw);

  return result;
}

ThebesLayerOGL::ThebesLayerOGL(LayerManagerOGL* aManager)
  : ThebesLayer(aManager, nullptr)
  , LayerOGL(aManager)
  , mBuffer(nullptr)
{
  mImplData = static_cast<LayerOGL*>(this);
}

ThebesLayerOGL::~ThebesLayerOGL()
{
  Destroy();
}

void
ThebesLayerOGL::Destroy()
{
  if (!mDestroyed) {
    mBuffer = nullptr;
    mDestroyed = true;
  }
}

bool
ThebesLayerOGL::CreateSurface()
{
  NS_ASSERTION(!mBuffer, "buffer already created?");

  if (mVisibleRegion.IsEmpty()) {
    return false;
  }

  if (gl()->TextureImageSupportsGetBackingSurface()) {
    // use the ThebesLayerBuffer fast-path
    mBuffer = new SurfaceBufferOGL(this, mOGLManager->GetCompositor());
  } else {
    mBuffer = new BasicBufferOGL(this, mOGLManager->GetCompositor());
  }
  return true;
}

void
ThebesLayerOGL::SetVisibleRegion(const nsIntRegion &aRegion)
{
  if (aRegion.IsEqual(mVisibleRegion))
    return;
  ThebesLayer::SetVisibleRegion(aRegion);
}

void
ThebesLayerOGL::InvalidateRegion(const nsIntRegion &aRegion)
{
  mInvalidRegion.Or(mInvalidRegion, aRegion);
  mInvalidRegion.SimplifyOutward(10);
  mValidRegion.Sub(mValidRegion, mInvalidRegion);
}

void
ThebesLayerOGL::RenderLayer(const nsIntPoint& aOffset,
                            const nsIntRect& aClipRect,
                            CompositingRenderTarget* aPreviousTarget)
{
  if (!mBuffer && !CreateSurface()) {
    return;
  }
  NS_ABORT_IF_FALSE(mBuffer, "should have a buffer here");

  mOGLManager->MakeCurrent();
  gl()->fActiveTexture(LOCAL_GL_TEXTURE0);

  TextureImage::ContentType contentType =
    CanUseOpaqueSurface() ? gfxASurface::CONTENT_COLOR :
                            gfxASurface::CONTENT_COLOR_ALPHA;

  uint32_t flags = 0;
#ifndef MOZ_GFX_OPTIMIZE_MOBILE
  gfxMatrix transform2d;
  bool paintWillResample = !GetEffectiveTransform().Is2D(&transform2d) ||
                           transform2d.HasNonIntegerTranslation();
  if (paintWillResample) {
    flags |= ThebesLayerBufferOGL::PAINT_WILL_RESAMPLE;
  }
  mBuffer->SetPaintWillResample(paintWillResample);
#endif

  Buffer::PaintState state = mBuffer->BeginPaint(contentType, flags);
  mValidRegion.Sub(mValidRegion, state.mRegionToInvalidate);

  if (state.mContext) {
    state.mRegionToInvalidate.And(state.mRegionToInvalidate, mVisibleRegion);

    LayerManager::DrawThebesLayerCallback callback =
      mOGLManager->GetThebesLayerCallback();
    if (!callback) {
      NS_ERROR("GL should never need to update ThebesLayers in an empty transaction");
    } else {
      void* callbackData = mOGLManager->GetThebesLayerCallbackData();
      SetAntialiasingFlags(this, state.mContext);
      callback(this, state.mContext, state.mRegionToDraw,
               state.mRegionToInvalidate, callbackData);
      // Everything that's visible has been validated. Do this instead of just
      // OR-ing with aRegionToDraw, since that can lead to a very complex region
      // here (OR doesn't automatically simplify to the simplest possible
      // representation of a region.)
      nsIntRegion tmp;
      tmp.Or(mVisibleRegion, state.mRegionToDraw);
      mValidRegion.Or(mValidRegion, tmp);
    }
  }

  if (mOGLManager->CompositingDisabled()) {
    mBuffer->Abort();
    return;
  }

  // Drawing thebes layers can change the current context, reset it.
  gl()->MakeCurrent();
  if (aPreviousTarget) {
    gl()->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER,
                           static_cast<CompositingRenderTargetOGL*>(aPreviousTarget)->mFBO);
  } else {
    gl()->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, 0);
  }

  gfx::Matrix4x4 transform;
  ToMatrix4x4(GetEffectiveTransform(), transform);
  gfx::Rect clipRect(aClipRect.x, aClipRect.y, aClipRect.width, aClipRect.height);

#ifdef MOZ_DUMP_PAINTING
  if (gfxUtils::sDumpPainting) {
    nsRefPtr<gfxImageSurface> surf = mBuffer->Dump();
    WriteSnapshotToDumpFile(this, surf);
  }
#endif

  EffectChain effectChain;
  LayerManagerComposite::AddMaskEffect(mMaskLayer, effectChain);

  mBuffer->Composite(effectChain,
                     GetEffectiveOpacity(),
                     transform,
                     gfx::Point(aOffset.x, aOffset.y),
                     gfx::FILTER_LINEAR,
                     clipRect,
                     &GetEffectiveVisibleRegion());
}

Layer*
ThebesLayerOGL::GetLayer()
{
  return this;
}

bool
ThebesLayerOGL::IsEmpty()
{
  return !mBuffer;
}

void
ThebesLayerOGL::CleanupResources()
{
  mBuffer = nullptr;
}

} // layers
} // mozilla
