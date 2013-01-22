/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_BUFFERCLIENT_H
#define MOZILLA_GFX_BUFFERCLIENT_H

#include "mozilla/layers/Compositor.h"

namespace mozilla {
namespace layers {

class PLayerChild;
class PLayersChild;
class TextureBufferChild;

class CompositableClient : public RefCounted<CompositableClient>
{
public:
  CompositableClient();
  virtual ~CompositableClient();

  //TODO[nrc] does anyone ever call this?
  void Initialize(PLayerChild* aLayer,
                  PLayersChild* aShadowManager);
private:
};

} // namespace
} // namespace

#endif
