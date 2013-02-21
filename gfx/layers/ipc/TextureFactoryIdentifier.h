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

enum TextureHostFlags
{
  TEXTURE_HOST_DEFAULT = 0,       // The default texture host for the given SurfaceDescriptor should be created
  TEXTURE_HOST_TILED = 1 << 0,    // A texture host that supports tiling should be created
  TEXTURE_HOST_BUFFERED = 1 << 1, // The texture host should support swapping multiple buffers
  TEXTURE_HOST_DIRECT = 1 << 2    // The texture host should attempt to use the SurfaceDescriptor for direct texturing
};

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
 //TODO field naming conventions
 //TODO(maybe) move this file out of /ipc
struct TextureInfo
{
  CompositableType compositableType;
  uint32_t textureHostFlags;
  uint32_t textureFlags;

  TextureInfo()
    : compositableType(BUFFER_UNKNOWN)
    , textureHostFlags(0)
    , textureFlags(0)
  {}
};
  
} // namespace
} // namespace

#endif
