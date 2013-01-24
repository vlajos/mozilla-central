/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ContentClient.h"
#include "BasicThebesLayer.h"
#include "BasicTiledThebesLayer.h"
#include "nsIWidget.h"
#include "sampler.h"
#include "gfxUtils.h"
#include <algorithm>

#ifdef GFX_TILEDLAYER_DEBUG_OVERLAY
#include "cairo.h"
#include <sstream>
using mozilla::layers::Layer;
static void DrawDebugOverlay(gfxImageSurface* imgSurf, int x, int y)
{
  gfxContext c(imgSurf);

  // Draw border
  c.NewPath();
  c.SetDeviceColor(gfxRGBA(0.0, 0.0, 0.0, 1.0));
  c.Rectangle(gfxRect(gfxPoint(0,0),imgSurf->GetSize()));
  c.Stroke();

  // Build tile description
  std::stringstream ss;
  ss << x << ", " << y;

  // Draw text using cairo toy text API
  cairo_t* cr = c.GetCairo();
  cairo_set_font_size(cr, 25);
  cairo_text_extents_t extents;
  cairo_text_extents(cr, ss.str().c_str(), &extents);

  int textWidth = extents.width + 6;

  c.NewPath();
  c.SetDeviceColor(gfxRGBA(0.0, 0.0, 0.0, 1.0));
  c.Rectangle(gfxRect(gfxPoint(2,2),gfxSize(textWidth, 30)));
  c.Fill();

  c.NewPath();
  c.SetDeviceColor(gfxRGBA(1.0, 0.0, 0.0, 1.0));
  c.Rectangle(gfxRect(gfxPoint(2,2),gfxSize(textWidth, 30)));
  c.Stroke();

  c.NewPath();
  cairo_move_to(cr, 4, 28);
  cairo_show_text(cr, ss.str().c_str());

}

#endif

namespace mozilla {
namespace layers {

ContentClientBasic::ContentClientBasic(BasicLayerManager* aManager)
  : mManager(aManager)
{}


already_AddRefed<gfxASurface>
ContentClientBasic::CreateBuffer(ContentType aType,
                                 const nsIntSize& aSize,
                                 uint32_t aFlags)
{
  nsRefPtr<gfxASurface> referenceSurface = GetBuffer();
  if (!referenceSurface) {
    gfxContext* defaultTarget = mManager->GetDefaultTarget();
    if (defaultTarget) {
      referenceSurface = defaultTarget->CurrentSurface();
    } else {
      nsIWidget* widget = mManager->GetRetainerWidget();
      if (!widget || !(referenceSurface = widget->GetThebesSurface())) {
        referenceSurface = mManager->GetTarget()->CurrentSurface();
      }
    }
  }
  return referenceSurface->CreateSimilarSurface(
    aType, gfxIntSize(aSize.width, aSize.height));
}

already_AddRefed<gfxASurface>
ContentClientRemote::CreateBuffer(ContentType aType,
                                  const nsIntSize& aSize,
                                  uint32_t aFlags)
{
  NS_ABORT_IF_FALSE(!mIsNewBuffer,
                    "Bad! Did we create a buffer twice without painting?");

  mIsNewBuffer = true;

  mOldTextures.AppendElement(mTextureClient);

  if (mTextureClient) {
    mTextureClient->Destroyed(mLayer);
  }

  mTextureClient = static_cast<TextureClientShmem*>(
    mLayerForwarder->CreateTextureClientFor(TEXTURE_SHMEM, GetType(),
                                            mLayer, AllowRepeat, true).drop());
  mTextureClient->EnsureTextureClient(gfx::IntSize(aSize.width, aSize.height),
                                      aType);
  nsRefPtr<gfxASurface> ret = mTextureClient->LockSurface();
  return ret.forget();
}

void
ContentClientRemote::BeginPaint()
{
  if (mTextureClient) {
    SetBuffer(mTextureClient->LockSurface());
  }
}

void
ContentClientRemote::EndPaint()
{
  if (mTextureClient) {
    mTextureClient->Unlock();
  }
  SetBuffer(nullptr);
  mOldTextures.Clear();
}

nsIntRegion 
ContentClientRemote::GetUpdatedRegion(const nsIntRegion& aRegionToDraw,
                                      const nsIntRegion& aVisibleRegion,
                                      bool aDidSelfCopy)
{
  nsIntRegion updatedRegion;
  if (mIsNewBuffer || aDidSelfCopy) {
    // A buffer reallocation clears both buffers. The front buffer has all the
    // content by now, but the back buffer is still clear. Here, in effect, we
    // are saying to copy all of the pixels of the front buffer to the back.
    // Also when we self-copied in the buffer, the buffer space
    // changes and some changed buffer content isn't reflected in the
    // draw or invalidate region (on purpose!).  When this happens, we
    // need to read back the entire buffer too.
    updatedRegion = aVisibleRegion;
    mIsNewBuffer = false;
  } else {
    updatedRegion = aRegionToDraw;
  }

  NS_ASSERTION(BufferRect().Contains(aRegionToDraw.GetBounds()),
               "Update outside of buffer rect!");
  NS_ABORT_IF_FALSE(mTextureClient, "should have a back buffer by now");

  return updatedRegion;
}

void
ContentClientRemote::SetBackingBuffer(gfxASurface* aBuffer,
                                      const nsIntRect& aRect,
                                      const nsIntPoint& aRotation)
{
#ifdef DEBUG
  gfxIntSize prevSize = gfxIntSize(BufferRect().width, BufferRect().height);
  gfxIntSize newSize = aBuffer->GetSize();
  NS_ABORT_IF_FALSE(newSize == prevSize,
                    "Swapped-in buffer size doesn't match old buffer's!");
#endif
  nsRefPtr<gfxASurface> oldBuffer;
  oldBuffer = SetBuffer(aBuffer, aRect, aRotation);
}

ContentClientDirect::~ContentClientDirect()
{
  // Finish any use of mROFrontBuffer since the last ForwardTransaction(),
  // before the Shadow frees the surface.
  if (OptionalThebesBuffer::Tnull_t != mROFrontBuffer.type()) {
    ShadowLayerForwarder::PlatformSyncBeforeUpdate();
  }
}

void
ContentClientDirect::SetBufferAttrs(const nsIntRegion& aValidRegion,
                                    const OptionalThebesBuffer& aReadOnlyFrontBuffer,
                                    const nsIntRegion& aFrontUpdatedRegion,
                                    const nsIntRect& aBufferRect,
                                    const nsIntPoint& aBufferRotation)
{
  MOZ_ASSERT(OptionalThebesBuffer::Tnull_t != aReadOnlyFrontBuffer.type());

  mFrontAndBackBufferDiffer = true;
  mROFrontBuffer = aReadOnlyFrontBuffer;
  mFrontUpdatedRegion = aFrontUpdatedRegion;
}

struct AutoTextureClient {
  AutoTextureClient()
    : mTexture(nullptr)
  {}
  ~AutoTextureClient()
  {
    if (mTexture) {
      mTexture->Unlock();
    }
  }
  gfxASurface* GetSurface(TextureClient* aTexture)
  {
    mTexture = aTexture;
    return mTexture->LockSurface();
  }

private:
  TextureClient* mTexture;
};

void
ContentClientDirect::SyncFrontBufferToBackBuffer()
{
  if (!mFrontAndBackBufferDiffer) {
    return;
  }

  gfxASurface* backBuffer = GetBuffer();

  if (!mTextureClient) {
    MOZ_ASSERT(!backBuffer);
    MOZ_ASSERT(mROFrontBuffer.type() == OptionalThebesBuffer::TThebesBuffer);
    const ThebesBuffer roFront = mROFrontBuffer.get_ThebesBuffer();
    AutoOpenSurface roFrontBuffer(OPEN_READ_ONLY, roFront.buffer());
    nsIntSize size = roFrontBuffer.Size();
    mTextureClient = static_cast<TextureClientShmem*>(
      mLayerForwarder->CreateTextureClientFor(TEXTURE_SHMEM, GetType(),
                                              mLayer, AllowRepeat, true).drop());
    mTextureClient->EnsureTextureClient(gfx::IntSize(size.width, size.height),
                                        roFrontBuffer.ContentType());
  }

  AutoTextureClient autoTexture;
  if (!backBuffer) {
    backBuffer = autoTexture.GetSurface(mTextureClient);
  }

  MOZ_LAYERS_LOG(("BasicShadowableThebes(%p): reading back <x=%d,y=%d,w=%d,h=%d>",
                  this,
                  mFrontUpdatedRegion.GetBounds().x,
                  mFrontUpdatedRegion.GetBounds().y,
                  mFrontUpdatedRegion.GetBounds().width,
                  mFrontUpdatedRegion.GetBounds().height));

  const ThebesBuffer roFront = mROFrontBuffer.get_ThebesBuffer();
  AutoOpenSurface autoROFront(OPEN_READ_ONLY, roFront.buffer());
  SetBackingBufferAndUpdateFrom(backBuffer,
                                autoROFront.Get(),
                                roFront.rect(),
                                roFront.rotation(),
                                mFrontUpdatedRegion);
  mIsNewBuffer = false;
  mFrontAndBackBufferDiffer = false;
}

void
ContentClientDirect::SetBackingBufferAndUpdateFrom(gfxASurface* aBuffer,
                                                   gfxASurface* aSource,
                                                   const nsIntRect& aRect,
                                                   const nsIntPoint& aRotation,
                                                   const nsIntRegion& aUpdateRegion)
{
  SetBackingBuffer(aBuffer, aRect, aRotation);
  nsRefPtr<gfxContext> destCtx =
    GetContextForQuadrantUpdate(aUpdateRegion.GetBounds());
  destCtx->SetOperator(gfxContext::OPERATOR_SOURCE);
  if (IsClippingCheap(destCtx, aUpdateRegion)) {
    gfxUtils::ClipToRegion(destCtx, aUpdateRegion);
  }

  ContentClientDirect srcBuffer(aSource, aRect, aRotation);
  srcBuffer.DrawBufferWithRotation(destCtx, 1.0, nullptr, nullptr);
}

void
ContentClientTexture::SetBufferAttrs(const nsIntRegion& aValidRegion,
                                     const OptionalThebesBuffer& aReadOnlyFrontBuffer,
                                     const nsIntRegion& aFrontUpdatedRegion,
                                     const nsIntRect& aBufferRect,
                                     const nsIntPoint& aBufferRotation)
{
  // We didn't get back a read-only ref to our old back buffer (the
  // parent's new front buffer).  If the parent is pushing updates
  // to a texture it owns, then we probably got back the same buffer
  // we pushed in the update and all is well.  If not, ...
  MOZ_ASSERT(OptionalThebesBuffer::Tnull_t == aReadOnlyFrontBuffer.type());

  mBackBufferRect = aBufferRect;
  mBackBufferRectRotation = aBufferRotation;
  mFrontAndBackBufferDiffer = true;
}

void
ContentClientTexture::SyncFrontBufferToBackBuffer()
{
  if (!mFrontAndBackBufferDiffer) {
    return;
  }

  gfxASurface* backBuffer = GetBuffer();
  NS_ASSERTION(!mTextureClient ||
                 backBuffer, 
               "SyncFrontBufferToBackBuffer called outside of Begin/End Paint?");

  nsRefPtr<gfxASurface> oldBuffer;
  oldBuffer = SetBuffer(backBuffer,
                        mBackBufferRect,
                        mBackBufferRectRotation);

  mIsNewBuffer = false;
  mFrontAndBackBufferDiffer = false;
}

bool
BasicTiledLayerBuffer::HasFormatChanged() const
{
  return mThebesLayer->CanUseOpaqueSurface() != mLastPaintOpaque;
}


gfxASurface::gfxContentType
BasicTiledLayerBuffer::GetContentType() const
{
  if (mThebesLayer->CanUseOpaqueSurface()) {
    return gfxASurface::CONTENT_COLOR;
  } else {
    return gfxASurface::CONTENT_COLOR_ALPHA;
  }
}

void
BasicTiledLayerBuffer::LockCopyAndWrite()
{
  ReadLock();
  // Create a heap copy owned and released by the compositor. This is needed
  // since we're sending this over an async message and content needs to be
  // be able to modify the tiled buffer in the next transaction.
  // TODO: Remove me once Bug 747811 lands.
  BasicTiledLayerBuffer *heapCopy = new BasicTiledLayerBuffer(*this);
  mManager->PaintedTiledLayerBuffer(mManager->Hold(mThebesLayer), heapCopy);
  ClearPaintedRegion();    
}

void
BasicTiledLayerBuffer::PaintThebes(const nsIntRegion& aNewValidRegion,
                                   const nsIntRegion& aPaintRegion,
                                   LayerManager::DrawThebesLayerCallback aCallback,
                                   void* aCallbackData)
{
  mCallback = aCallback;
  mCallbackData = aCallbackData;

#ifdef GFX_TILEDLAYER_PREF_WARNINGS
  long start = PR_IntervalNow();
#endif

  // If this region is empty XMost() - 1 will give us a negative value.
  NS_ASSERTION(!aPaintRegion.GetBounds().IsEmpty(), "Empty paint region\n");

  bool useSinglePaintBuffer = UseSinglePaintBuffer();
  if (useSinglePaintBuffer) {
    // Check if the paint only spans a single tile. If that's
    // the case there's no point in using a single paint buffer.
    nsIntRect paintBounds = aPaintRegion.GetBounds();
    useSinglePaintBuffer = GetTileStart(paintBounds.x) !=
                           GetTileStart(paintBounds.XMost() - 1) ||
                           GetTileStart(paintBounds.y) !=
                           GetTileStart(paintBounds.YMost() - 1);
  }

  if (useSinglePaintBuffer) {
    const nsIntRect bounds = aPaintRegion.GetBounds();
    {
      SAMPLE_LABEL("BasicTiledLayerBuffer", "PaintThebesSingleBufferAlloc");
      mSinglePaintBuffer = new gfxImageSurface(
        gfxIntSize(ceilf(bounds.width * mResolution),
                   ceilf(bounds.height * mResolution)),
        gfxPlatform::GetPlatform()->OptimalFormatForContent(GetContentType()), !mThebesLayer->CanUseOpaqueSurface());
      mSinglePaintBufferOffset = nsIntPoint(bounds.x, bounds.y);
    }
    nsRefPtr<gfxContext> ctxt = new gfxContext(mSinglePaintBuffer);
    ctxt->NewPath();
    ctxt->Scale(mResolution, mResolution);
    ctxt->Translate(gfxPoint(-bounds.x, -bounds.y));
#ifdef GFX_TILEDLAYER_PREF_WARNINGS
    if (PR_IntervalNow() - start > 3) {
      printf_stderr("Slow alloc %i\n", PR_IntervalNow() - start);
    }
    start = PR_IntervalNow();
#endif
    SAMPLE_LABEL("BasicTiledLayerBuffer", "PaintThebesSingleBufferDraw");

    mCallback(mThebesLayer, ctxt, aPaintRegion, nsIntRegion(), mCallbackData);
  }

#ifdef GFX_TILEDLAYER_PREF_WARNINGS
  if (PR_IntervalNow() - start > 30) {
    const nsIntRect bounds = aPaintRegion.GetBounds();
    printf_stderr("Time to draw %i: %i, %i, %i, %i\n", PR_IntervalNow() - start, bounds.x, bounds.y, bounds.width, bounds.height);
    if (aPaintRegion.IsComplex()) {
      printf_stderr("Complex region\n");
      nsIntRegionRectIterator it(aPaintRegion);
      for (const nsIntRect* rect = it.Next(); rect != nullptr; rect = it.Next()) {
        printf_stderr(" rect %i, %i, %i, %i\n", rect->x, rect->y, rect->width, rect->height);
      }
    }
  }
  start = PR_IntervalNow();
#endif

  SAMPLE_LABEL("BasicTiledLayerBuffer", "PaintThebesUpdate");
  Update(aNewValidRegion, aPaintRegion);

#ifdef GFX_TILEDLAYER_PREF_WARNINGS
  if (PR_IntervalNow() - start > 10) {
    const nsIntRect bounds = aPaintRegion.GetBounds();
    printf_stderr("Time to tile %i: %i, %i, %i, %i\n", PR_IntervalNow() - start, bounds.x, bounds.y, bounds.width, bounds.height);
  }
#endif

  mLastPaintOpaque = mThebesLayer->CanUseOpaqueSurface();
  mThebesLayer = nullptr;
  mCallback = nullptr;
  mCallbackData = nullptr;
  mSinglePaintBuffer = nullptr;
}

BasicTiledLayerTile
BasicTiledLayerBuffer::ValidateTileInternal(BasicTiledLayerTile aTile,
                                            const nsIntPoint& aTileOrigin,
                                            const nsIntRect& aDirtyRect)
{
  if (aTile.IsPlaceholderTile()) {
    RefPtr<TextureClient> textureClient =
      mManager->CreateTextureClientFor(TEXTURE_TILED, BUFFER_TILED, mThebesLayer, true);
    aTile.mTextureClient = static_cast<TextureClientTile*>(textureClient.get());
  }
  aTile.mTextureClient->EnsureTextureClient(gfx::IntSize(GetTileLength(), GetTileLength()), GetContentType());


  gfxASurface* writableSurface = aTile.mTextureClient->LockSurface();
  // Bug 742100, this gfxContext really should live on the stack.
  nsRefPtr<gfxContext> ctxt = new gfxContext(writableSurface);

  if (mSinglePaintBuffer) {
    gfxRect drawRect(aDirtyRect.x - aTileOrigin.x, aDirtyRect.y - aTileOrigin.y,
                     aDirtyRect.width, aDirtyRect.height);

    ctxt->SetOperator(gfxContext::OPERATOR_SOURCE);
    ctxt->NewPath();
    ctxt->SetSource(mSinglePaintBuffer.get(),
                    gfxPoint((mSinglePaintBufferOffset.x - aDirtyRect.x + drawRect.x) *
                             mResolution,
                             (mSinglePaintBufferOffset.y - aDirtyRect.y + drawRect.y) *
                             mResolution));
    drawRect.Scale(mResolution, mResolution);
    ctxt->Rectangle(drawRect, true);
    ctxt->Fill();
  } else {
    ctxt->NewPath();
    ctxt->Scale(mResolution, mResolution);
    ctxt->Translate(gfxPoint(-aTileOrigin.x, -aTileOrigin.y));
    nsIntPoint a = nsIntPoint(aTileOrigin.x, aTileOrigin.y);
    mCallback(mThebesLayer, ctxt,
              nsIntRegion(nsIntRect(a, nsIntSize(GetScaledTileLength(),
                                                 GetScaledTileLength()))),
              nsIntRegion(), mCallbackData);
  }

#ifdef GFX_TILEDLAYER_DEBUG_OVERLAY
  DrawDebugOverlay(writableSurface, aTileOrigin.x * mResolution,
                   aTileOrigin.y * mResolution);
#endif

  return aTile;
}

BasicTiledLayerTile
BasicTiledLayerBuffer::ValidateTile(BasicTiledLayerTile aTile,
                                    const nsIntPoint& aTileOrigin,
                                    const nsIntRegion& aDirtyRegion)
{

  SAMPLE_LABEL("BasicTiledLayerBuffer", "ValidateTile");

#ifdef GFX_TILEDLAYER_PREF_WARNINGS
  if (aDirtyRegion.IsComplex()) {
    printf_stderr("Complex region\n");
  }
#endif

  nsIntRegionRectIterator it(aDirtyRegion);
  for (const nsIntRect* rect = it.Next(); rect != nullptr; rect = it.Next()) {
#ifdef GFX_TILEDLAYER_PREF_WARNINGS
    printf_stderr(" break into subrect %i, %i, %i, %i\n", rect->x, rect->y, rect->width, rect->height);
#endif
    aTile = ValidateTileInternal(aTile, aTileOrigin, *rect);
  }

  return aTile;
}

static nsIntRect
RoundedTransformViewportBounds(const gfx::Rect& aViewport,
                               const gfx::Point& aScrollOffset,
                               const gfxSize& aResolution,
                               float aScaleX,
                               float aScaleY,
                               const gfx3DMatrix& aTransform)
{
  gfxRect transformedViewport(aViewport.x - (aScrollOffset.x * aResolution.width),
                              aViewport.y - (aScrollOffset.y * aResolution.height),
                              aViewport.width, aViewport.height);
  transformedViewport.Scale((aScaleX / aResolution.width) / aResolution.width,
                            (aScaleY / aResolution.height) / aResolution.height);
  transformedViewport = aTransform.TransformBounds(transformedViewport);

  return nsIntRect((int32_t)floor(transformedViewport.x),
                   (int32_t)floor(transformedViewport.y),
                   (int32_t)ceil(transformedViewport.width),
                   (int32_t)ceil(transformedViewport.height));
}

bool
BasicTiledLayerBuffer::ComputeProgressiveUpdateRegion(const nsIntRegion& aInvalidRegion,
                                                      const nsIntRegion& aOldValidRegion,
                                                      nsIntRegion& aRegionToPaint,
                                                      BasicTiledLayerPaintData* aPaintData,
                                                      bool aIsRepeated)
{
  aRegionToPaint = aInvalidRegion;

  // If this is a low precision buffer, we force progressive updates. The
  // assumption is that the contents is less important, so visual coherency
  // is lower priority than speed.
  bool drawingLowPrecision = IsLowPrecision();

  // Find out if we have any non-stale content to update.
  nsIntRegion staleRegion;
  staleRegion.And(aInvalidRegion, aOldValidRegion);

  // Find out the current view transform to determine which tiles to draw
  // first, and see if we should just abort this paint. Aborting is usually
  // caused by there being an incoming, more relevant paint.
  gfx::Rect viewport;
  float scaleX, scaleY;
  if (mManager->ProgressiveUpdateCallback(!staleRegion.Contains(aInvalidRegion),
                                          viewport,
                                          scaleX, scaleY, !drawingLowPrecision)) {
    SAMPLE_MARKER("Abort painting");
    aRegionToPaint.SetEmpty();
    return aIsRepeated;
  }

  // Transform the screen coordinates into local layer coordinates.
  nsIntRect roundedTransformedViewport =
    RoundedTransformViewportBounds(viewport, aPaintData->mScrollOffset, aPaintData->mResolution,
                                   scaleX, scaleY, aPaintData->mTransformScreenToLayer);

  // Paint tiles that have stale content or that intersected with the screen
  // at the time of issuing the draw command in a single transaction first.
  // This is to avoid rendering glitches on animated page content, and when
  // layers change size/shape.
  nsIntRect criticalViewportRect = roundedTransformedViewport.Intersect(aPaintData->mCompositionBounds);
  aRegionToPaint.And(aInvalidRegion, criticalViewportRect);
  aRegionToPaint.Or(aRegionToPaint, staleRegion);
  bool drawingStale = !aRegionToPaint.IsEmpty();
  if (!drawingStale) {
    aRegionToPaint = aInvalidRegion;
  }

  // Prioritise tiles that are currently visible on the screen.
  bool paintVisible = false;
  if (aRegionToPaint.Intersects(roundedTransformedViewport)) {
    aRegionToPaint.And(aRegionToPaint, roundedTransformedViewport);
    paintVisible = true;
  }

  // Paint area that's visible and overlaps previously valid content to avoid
  // visible glitches in animated elements, such as gifs.
  bool paintInSingleTransaction = paintVisible && (drawingStale || aPaintData->mFirstPaint);

  // The following code decides what order to draw tiles in, based on the
  // current scroll direction of the primary scrollable layer.
  NS_ASSERTION(!aRegionToPaint.IsEmpty(), "Unexpectedly empty paint region!");
  nsIntRect paintBounds = aRegionToPaint.GetBounds();

  int startX, incX, startY, incY;
  int tileLength = GetScaledTileLength();
  if (aPaintData->mScrollOffset.x >= aPaintData->mLastScrollOffset.x) {
    startX = RoundDownToTileEdge(paintBounds.x);
    incX = tileLength;
  } else {
    startX = RoundDownToTileEdge(paintBounds.XMost() - 1);
    incX = -tileLength;
  }

  if (aPaintData->mScrollOffset.y >= aPaintData->mLastScrollOffset.y) {
    startY = RoundDownToTileEdge(paintBounds.y);
    incY = tileLength;
  } else {
    startY = RoundDownToTileEdge(paintBounds.YMost() - 1);
    incY = -tileLength;
  }

  // Find a tile to draw.
  nsIntRect tileBounds(startX, startY, tileLength, tileLength);
  int32_t scrollDiffX = aPaintData->mScrollOffset.x - aPaintData->mLastScrollOffset.x;
  int32_t scrollDiffY = aPaintData->mScrollOffset.y - aPaintData->mLastScrollOffset.y;
  // This loop will always terminate, as there is at least one tile area
  // along the first/last row/column intersecting with regionToPaint, or its
  // bounds would have been smaller.
  while (true) {
    aRegionToPaint.And(aInvalidRegion, tileBounds);
    if (!aRegionToPaint.IsEmpty()) {
      break;
    }
    if (std::abs(scrollDiffY) >= std::abs(scrollDiffX)) {
      tileBounds.x += incX;
    } else {
      tileBounds.y += incY;
    }
  }

  if (!aRegionToPaint.Contains(aInvalidRegion)) {
    // The region needed to paint is larger then our progressive chunk size
    // therefore update what we want to paint and ask for a new paint transaction.

    // If we need to draw more than one tile to maintain coherency, make
    // sure it happens in the same transaction by requesting this work be
    // repeated immediately.
    // If this is unnecessary, the remaining work will be done tile-by-tile in
    // subsequent transactions.
    if (!drawingLowPrecision && paintInSingleTransaction) {
      return true;
    }

    mManager->SetRepeatTransaction();
    return false;
  }

  // We're not repeating painting and we've not requested a repeat transaction,
  // so the paint is finished. If there's still a separate low precision
  // paint to do, it will get marked as unfinished later.
  aPaintData->mPaintFinished = true;
  return false;
}

bool
BasicTiledLayerBuffer::ProgressiveUpdate(nsIntRegion& aValidRegion,
                                         nsIntRegion& aInvalidRegion,
                                         const nsIntRegion& aOldValidRegion,
                                         BasicTiledLayerPaintData* aPaintData,
                                         LayerManager::DrawThebesLayerCallback aCallback,
                                         void* aCallbackData)
{
  bool repeat = false;
  do {
    // Compute the region that should be updated. Repeat as many times as
    // is required.
    nsIntRegion regionToPaint;
    repeat = ComputeProgressiveUpdateRegion(aInvalidRegion,
                                            aOldValidRegion,
                                            regionToPaint,
                                            aPaintData,
                                            repeat);

    // There's no further work to be done, return if nothing has been
    // drawn, or give what has been drawn to the shadow layer to upload.
    if (regionToPaint.IsEmpty()) {
      if (repeat) {
        break;
      } else {
        return false;
      }
    }

    // Keep track of what we're about to refresh.
    aValidRegion.Or(aValidRegion, regionToPaint);

    // aValidRegion may have been altered by InvalidateRegion, but we still
    // want to display stale content until it gets progressively updated.
    // Create a region that includes stale content.
    nsIntRegion validOrStale;
    validOrStale.Or(aValidRegion, aOldValidRegion);

    // Paint the computed region and subtract it from the invalid region.
    PaintThebes(validOrStale, regionToPaint, aCallback, aCallbackData);
    aInvalidRegion.Sub(aInvalidRegion, regionToPaint);
  } while (repeat);

  return true;
}


}
}
