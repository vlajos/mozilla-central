/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/thread.h"

#include "mozilla/layers/CompositorParent.h"
#include "mozilla/layers/ImageBridgeParent.h"
#include "CompositableHost.h"
#include "nsTArray.h"
#include "nsXULAppAPI.h"

using namespace base;
using namespace mozilla::ipc;

namespace mozilla {
namespace layers {


ImageBridgeParent::ImageBridgeParent(MessageLoop* aLoop)
: mMessageLoop(aLoop)
{
  // TODO[nical] b2g can have several bridges, so this should not be done here
  CompositableMap::Create();
}

ImageBridgeParent::~ImageBridgeParent()
{
  // TODO[nical] b2g can have several bridges, so this should not be done here
  CompositableMap::Destroy();
}

bool
ImageBridgeParent::RecvUpdate(const EditArray& aEdits, EditReplyArray* aReply)
{
  printf("ImageBridgeParent::RecvUpdate");
  EditReplyVector replyv;
  for (EditArray::index_type i = 0; i < aEdits.Length(); ++i) {
    bool isFirstPaint = false;
    printf("ImageBridgeParent::ReceiveCompositableUpdate");
    ReceiveCompositableUpdate(aEdits[i],
                              isFirstPaint,
                              replyv);
  }

  aReply->SetCapacity(replyv.size());
  if (replyv.size() > 0) {
    aReply->AppendElements(&replyv.front(), replyv.size());
  }

  // Ensure that any pending operations involving back and front
  // buffers have completed, so that neither process stomps on the
  // other's buffer contents.
  ShadowLayerManager::PlatformSyncBeforeReplyUpdate();

  // TODO[nical] schedule composition
  return true;
}

bool
ImageBridgeParent::RecvUpdateNoSwap(const EditArray& aEdits)
{
  printf("ImageBridgeParent::RecvUpdateNoSwap");
  InfallibleTArray<EditReply> noReplies;
  bool success = RecvUpdate(aEdits, &noReplies);
  NS_ABORT_IF_FALSE(noReplies.Length() == 0, "RecvUpdateNoSwap requires a sync Update to carry Edits");
  return success;
}


static void
ConnectImageBridgeInParentProcess(ImageBridgeParent* aBridge,
                                  Transport* aTransport,
                                  ProcessHandle aOtherProcess)
{
  aBridge->Open(aTransport, aOtherProcess,
                XRE_GetIOMessageLoop(), AsyncChannel::Parent);
}

/*static*/ PImageBridgeParent*
ImageBridgeParent::Create(Transport* aTransport, ProcessId aOtherProcess)
{
  ProcessHandle processHandle;
  if (!base::OpenProcessHandle(aOtherProcess, &processHandle)) {
    return nullptr;
  }

  MessageLoop* loop = CompositorParent::CompositorLoop();
  ImageBridgeParent* bridge = new ImageBridgeParent(loop);
  loop->PostTask(FROM_HERE,
                 NewRunnableFunction(ConnectImageBridgeInParentProcess,
                                     bridge, aTransport, processHandle));
  return bridge;
}

bool ImageBridgeParent::RecvStop()
{
/*
  unsigned int numChildren = ManagedPImageContainerParent().Length();
  for (unsigned int i = 0; i < numChildren; ++i) {
    static_cast<ImageContainerParent*>(
      ManagedPImageContainerParent()[i]
    )->DoStop();
  }
*/
  return true;
}

static  uint64_t GenImageContainerID() {
  static uint64_t sNextImageID = 1;
  
  ++sNextImageID;
  return sNextImageID;
}
  
PGrallocBufferParent*
ImageBridgeParent::AllocPGrallocBuffer(const gfxIntSize& aSize,
                                       const uint32_t& aFormat,
                                       const uint32_t& aUsage,
                                       MaybeMagicGrallocBufferHandle* aOutHandle)
{
#ifdef MOZ_HAVE_SURFACEDESCRIPTORGRALLOC
  return GrallocBufferActor::Create(aSize, aFormat, aUsage, aOutHandle);
#else
  NS_RUNTIMEABORT("No gralloc buffers for you");
  return nullptr;
#endif
}

bool
ImageBridgeParent::DeallocPGrallocBuffer(PGrallocBufferParent* actor)
{
#ifdef MOZ_HAVE_SURFACEDESCRIPTORGRALLOC
  delete actor;
  return true;
#else
  NS_RUNTIMEABORT("Um, how did we get here?");
  return false;
#endif
}
/*
PImageContainerParent* ImageBridgeParent::AllocPImageContainer(uint64_t* aID)
{
  uint64_t id = GenImageContainerID();
  *aID = id;
  return new ImageContainerParent(id);
}

bool ImageBridgeParent::DeallocPImageContainer(PImageContainerParent* toDealloc)
{
  delete toDealloc;
  return true;
}
*/
PCompositableParent*
ImageBridgeParent::AllocPCompositable(const CompositableType& aType,
                                                         uint64_t* aID)
{
  uint64_t id = GenImageContainerID();
  *aID = id;
  return new CompositableParent(this, aType, id);
}

bool ImageBridgeParent::DeallocPCompositable(PCompositableParent* aActor)
{
  delete aActor;
  return true;
}



MessageLoop * ImageBridgeParent::GetMessageLoop() {
  return mMessageLoop;
}

void ImageBridgeParent::DestroySharedSurface(gfxSharedImageSurface* aSurface)
{
  NS_RUNTIMEABORT("Implement me");
  // TODO[nical]
}
void ImageBridgeParent::DestroySharedSurface(SurfaceDescriptor* aSurface)
{
  NS_RUNTIMEABORT("Implement me");
  // TODO[nical]
}
bool ImageBridgeParent::AllocateUnsafe(size_t aSize,
                            ipc::SharedMemory::SharedMemoryType aType,
                            ipc::Shmem* aShmem)
{
  return AllocUnsafeShmem(aSize, aType, aShmem);
}

} // layers
} // mozilla

