/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ColorLayerOGL.h"
#include "LayerImplComposite.h"

namespace mozilla {
namespace layers {

void
ColorLayerOGL::RenderLayer(const nsIntPoint& aOffset, const nsIntRect& aClipRect,
                           CompositingRenderTarget*)
{
  RenderColorLayer(this, mOGLManager->GetCompositor(), aOffset, aClipRect);
}


} /* layers */
} /* mozilla */
