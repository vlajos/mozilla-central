/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/PImageBridgeParent.h"
#include "CompositableTransactionParent.h"

class MessageLoop;

namespace mozilla {
namespace layers {

class CompositorParent;
/**
 * ImageBridgeParent is the manager Protocol of ImageContainerParent.
 * It's purpose is mainly to setup the IPDL connection. Most of the
 * interesting stuff is in ImageContainerParent.
 */
class ImageBridgeParent : public PImageBridgeParent,
                          public CompositableParentManager
{
public:
  typedef InfallibleTArray<CompositableOperation> EditArray;
  typedef InfallibleTArray<EditReply> EditReplyArray;

  ImageBridgeParent(MessageLoop* aLoop);
  ~ImageBridgeParent();

  static PImageBridgeParent*
  Create(Transport* aTransport, ProcessId aOtherProcess);

  virtual PGrallocBufferParent*
  AllocPGrallocBuffer(const gfxIntSize&, const uint32_t&, const uint32_t&,
                      MaybeMagicGrallocBufferHandle*) MOZ_OVERRIDE;

  virtual bool
  DeallocPGrallocBuffer(PGrallocBufferParent* actor) MOZ_OVERRIDE;

  // CompositableManager
  Compositor* GetCompositor() MOZ_OVERRIDE { return nullptr; } // TODO[nical] this is actually a bad idea

  // ISurfaceDeallocator
  virtual void DestroySharedSurface(gfxSharedImageSurface* aSurface) MOZ_OVERRIDE;
  virtual void DestroySharedSurface(SurfaceDescriptor* aSurface) MOZ_OVERRIDE;
  virtual bool AllocateUnsafe(size_t aSize,
                              ipc::SharedMemory::SharedMemoryType aType,
                              ipc::Shmem* aShmem) MOZ_OVERRIDE;

  // PImageBridge
  bool RecvUpdate(const EditArray& aEdits, EditReplyArray* aReply);
  bool RecvUpdateNoSwap(const EditArray& aEdits);

//TODO[nical]
/*
  PImageContainerParent* AllocPImageContainer(uint64_t* aID) MOZ_OVERRIDE;
  bool DeallocPImageContainer(PImageContainerParent* toDealloc) MOZ_OVERRIDE;
*/
  PCompositableParent* AllocPCompositable(const CompositableType& aType,
                                          uint64_t*) MOZ_OVERRIDE;
  bool DeallocPCompositable(PCompositableParent* aActor) MOZ_OVERRIDE;


  // Overriden from PImageBridgeParent.
  bool RecvStop() MOZ_OVERRIDE;

  MessageLoop * GetMessageLoop();

private:
  MessageLoop * mMessageLoop;
};

} // layers
} // mozilla

