/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_COMPOSITORTYPES_H
#define MOZILLA_LAYERS_COMPOSITORTYPES_H

namespace mozilla {
namespace layers {

enum CompositableType
{
  BUFFER_UNKNOWN,
  BUFFER_IMAGE_SINGLE,  // image/canvas host with one texture
  BUFFER_IMAGE_BUFFERED,  // image/canvas host with buffering
  BUFFER_BRIDGE,  // image bridge protocol image layers
  BUFFER_CONTENT, // thebes layer interface (unbuffered)
  BUFFER_CONTENT_DIRECT,  // thebes layer interface with direct texturing (buffered)
  BUFFER_TILED  // tiled thebes layer interface
};

enum TextureHostFlags
{
  TEXTURE_HOST_DEFAULT = 0,       // The default texture host for the given SurfaceDescriptor should be created
  TEXTURE_HOST_TILED = 1 << 0,    // A texture host that supports tiling should be created
  TEXTURE_HOST_DIRECT = 1 << 1    // The texture host should attempt to use the SurfaceDescriptor for direct texturing
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
 // Wherever we move this, make sure it does not have any c++ dependency becasue IDPL code
 // depends on it. We don't want C++ code that depends on IPDL code that depends on
 // the same C++ code (it breaks build in non obvious ways).
struct TextureInfo
{
  CompositableType mCompositableType;
  uint32_t mTextureHostFlags;
  uint32_t mTextureFlags;

  TextureInfo()
    : mCompositableType(BUFFER_UNKNOWN)
    , mTextureHostFlags(0)
    , mTextureFlags(0)
  {}
};
  
} // namespace
} // namespace

#endif
