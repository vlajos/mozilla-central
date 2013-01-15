/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_TEXTURECHILD_H
#define MOZILLA_LAYERS_TEXTURECHILD_H

#include "mozilla/layers/PTextureChild.h"
#include "mozilla/layers/SurfaceDeallocatorIPC.h"

namespace mozilla {
namespace layers {

class TextureClient;

class TextureChild : public PTextureChild, public SurfaceDeallocator<TextureChild>
{
public:
    TextureChild()
    : mTextureClient(nullptr)
    {}

    void SetTextureClient(TextureClient* aClient) {
        mTextureClient = aClient;
    }

    TextureClient* GetTextureClient() const {
        return mTextureClient;
    }
private:
    TextureClient* mTextureClient;
};

} // namespace
} // namespace

#endif
