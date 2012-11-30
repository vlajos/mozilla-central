#ifndef MOZILLA_LAYERS_TEXTUREBUFFERCHILD_H
#define MOZILLA_LAYERS_TEXTUREBUFFERCHILD_H

#include "mozilla/layers/PTextureBufferChild.h"

namespace mozilla {
namespace layers {

class TextureBufferChild : public PTextureBufferChild,
                           public RefCounted<TextureBufferChild>
{
public:
  PTextureChild* AllocPTexture() { return nullptr; }
  bool DeallocPTexture(PTextureChild* actor) { return false; }
private:
};

} // namespace
} // namespace

#endif