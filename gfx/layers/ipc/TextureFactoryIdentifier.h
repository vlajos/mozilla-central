
#ifndef MOZILLA_LAYERS_TEXTUREFACTORYIDENTIFIER_H
#define MOZILLA_LAYERS_TEXTUREFACTORYIDENTIFIER_H

namespace mozilla {
namespace layers {

enum BufferType
{
  BUFFER_UNKNOWN,
  BUFFER_YCBCR,
  BUFFER_DIRECT_EXTERNAL,
  BUFFER_SHARED,
  BUFFER_TEXTURE,
  BUFFER_BRIDGE,
  BUFFER_CONTENT,
  BUFFER_CONTENT_DIRECT,
  BUFFER_DIRECT
};

enum TextureHostType
{
  TEXTURE_UNKNOWN,
  TEXTURE_SHMEM,
  TEXTURE_SHMEM_YCBCR, // TODO[nical]
  TEXTURE_SHARED,
  TEXTURE_SHARED_BUFFERED, // webgl
  TEXTURE_BRIDGE
};

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
