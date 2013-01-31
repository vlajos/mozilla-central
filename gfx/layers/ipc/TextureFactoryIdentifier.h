/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_TEXTUREFACTORYIDENTIFIER_H
#define MOZILLA_LAYERS_TEXTUREFACTORYIDENTIFIER_H

namespace mozilla {
namespace layers {

enum BufferType
{
  BUFFER_UNKNOWN,
  BUFFER_DIRECT_EXTERNAL,
  BUFFER_SHARED,
  BUFFER_TEXTURE,
  BUFFER_BRIDGE,
  BUFFER_CONTENT,
  BUFFER_CONTENT_DIRECT,
  BUFFER_TILED,
  BUFFER_DIRECT
};

typedef int32_t TextureHostType;
static const TextureHostType TEXTURE_UNKNOWN  = 0;
static const TextureHostType TEXTURE_SHMEM    = 0;
static const TextureHostType TEXTURE_BUFFERED = 1 << 0;
static const TextureHostType TEXTURE_SHARED   = 1 << 2;
static const TextureHostType TEXTURE_DIRECT   = 1 << 3;
static const TextureHostType TEXTURE_ASYNC    = 1 << 4;
static const TextureHostType TEXTURE_EXTERNAL = 1 << 5;
static const TextureHostType TEXTURE_DXGI     = 1 << 6; // TODO belongs here?
static const TextureHostType TEXTURE_TILE     = 1 << 7; // TODO[nical] remove? No

/**
 * Sent from the compositor to the drawing LayerManager, includes properties
 * of the compositor and should (in the future) include information (BufferType)
 * about what kinds of buffer and texture clients to create.
 */
struct TextureFactoryIdentifier
{
  LayersBackend mParentBackend;
  int32_t mMaxTextureSize;
};

/**
 * Identifies a texture client/host pair and their type. Sent with updates
 * from a drawing layers to a compositing layer, it should be passed directly
 * to the CompositableHost. How the identifier is used depends on the buffer
 * client/host pair.
 */
struct TextureInfo
{
  BufferType imageType;
  TextureHostType memoryType;
  uint32_t textureFlags;
};
  
} // namespace
} // namespace

#endif
