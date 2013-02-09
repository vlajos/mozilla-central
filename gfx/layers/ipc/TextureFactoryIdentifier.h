/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_TEXTUREFACTORYIDENTIFIER_H
#define MOZILLA_LAYERS_TEXTUREFACTORYIDENTIFIER_H

namespace mozilla {
namespace layers {

enum CompositableType
{
  BUFFER_UNKNOWN,
  BUFFER_SINGLE,
  BUFFER_SHARED, // TODO: is that relevent to compositable?
  BUFFER_DIRECT, // TODO: is that relevent to compositable?
  BUFFER_BRIDGE,
  BUFFER_CONTENT,
  BUFFER_CONTENT_DIRECT, // TODO: is that relevent to compositable?
  BUFFER_TILED
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
static const TextureHostType TEXTURE_TILE     = 1 << 7; 

/**
 * Sent from the compositor to the drawing LayerManager, includes properties
 * of the compositor and should (in the future) include information (CompositableType)
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
  CompositableType compositableType;
  TextureHostType memoryType;
  uint32_t textureFlags;
};
  
} // namespace
} // namespace

#endif
