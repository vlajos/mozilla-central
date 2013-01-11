/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_TEXTUREPARENT_H
#define MOZILLA_LAYERS_TEXTUREPARENT_H

#include "mozilla/layers/PTextureParent.h"
#include "mozilla/layers/TextureFactoryIdentifier.h"

namespace mozilla {
namespace layers {

class TextureHost;
class BufferHost;
class TextureInfo;

class TextureParent : public PTextureParent
{
public:
    TextureParent(const TextureInfo& aInfo);
    virtual ~TextureParent();

    void SetTextureHost(TextureHost* aHost);
    TextureHost* GetTextureHost() const;

    const TextureInfo& GetTextureInfo() const {
        return mTextureInfo;
    }
private:
    TextureInfo mTextureInfo;
    RefPtr<TextureHost> mTextureHost;
};

} // namespace
} // namespace

#endif
