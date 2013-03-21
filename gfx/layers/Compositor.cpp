/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/Compositor.h"
#include "mozilla/layers/Effects.h"

namespace mozilla {
namespace layers {

/* static */ LayersBackend Compositor::sBackend = LAYERS_NONE;
/* static */ LayersBackend
Compositor::GetBackend()
{
  return sBackend;
}

void
Compositor::DrawColoredBorders(const gfx::Color& aColor,
                               const gfx::Rect& rect,
                               const gfx::Rect* aClipRect,
                               const gfx::Matrix4x4& aTransform,
                               const gfx::Point& aOffset)
{
  if (!mDrawColoredBorders) {
    return;
  }
  EffectChain effects;
  effects.mPrimaryEffect = new EffectSolidColor(aColor);
  int lWidth = 2;
  float opacity = 0.8;
  // left
  this->DrawQuad(gfx::Rect(rect.x, rect.y,
                           lWidth, rect.height),
                 aClipRect, effects, opacity,
                 aTransform, aOffset);
  // top
  this->DrawQuad(gfx::Rect(rect.x, rect.y,
                           rect.width, lWidth),
                 aClipRect, effects, opacity,
                 aTransform, aOffset);
  // right
  this->DrawQuad(gfx::Rect(rect.x + rect.width - lWidth, rect.y + lWidth,
                           lWidth, rect.height - 2 * lWidth),
                 aClipRect, effects, opacity,
                 aTransform, aOffset);
  // bottom
  this->DrawQuad(gfx::Rect(rect.x, rect.y + rect.height-lWidth,
                           rect.width - 2 * lWidth, lWidth),
                 aClipRect, effects, opacity,
                 aTransform, aOffset);
}


} // namespace
} // namespace
