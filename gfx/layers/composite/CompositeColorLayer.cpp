/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CompositeColorLayer.h"
#include "LayerImpl.h"
#include "gfx2DGlue.h"

namespace mozilla {
namespace layers {


void
CompositeColorLayer::RenderLayer(const nsIntPoint& aOffset,
                                 const nsIntRect& aClipRect,
                                 Surface*)
{
  RenderColorLayer(this, mCompositor, aOffset, aClipRect);
}


} /* layers */
} /* mozilla */
