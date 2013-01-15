/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


#include "ImageContainerChild.h"
#include "gfxSharedImageSurface.h"
#include "ShadowLayers.h"
#include "mozilla/layers/PLayers.h"
#include "mozilla/layers/SharedImageUtils.h"
#include "ImageContainer.h"
#include "GonkIOSurfaceImage.h"
#include "GrallocImages.h"
#include "mozilla/layers/ShmemYCbCrImage.h"
#include "mozilla/ReentrantMonitor.h"

using namespace mozilla::ipc;
namespace mozilla {
namespace layers {

/*
 * - POOL_MAX_SHARED_IMAGES is the maximum number number of shared images to
 * store in the ImageContainerChild's pool.
 *
 * - MAX_ACTIVE_SHARED_IMAGES is the maximum number of active shared images.
 * the number of active shared images for a given ImageContainerChild is equal
 * to the number of shared images allocated minus the number of shared images
 * dealocated by this ImageContainerChild. What can happen is that the compositor
 * hangs for a moment, while the ImageBridgeChild keep sending images. In such a 
 * scenario the compositor is not sending back shared images so the 
 * ImageContinerChild allocates new ones, and if the compositor hangs for too 
 * long, we can run out of shared memory. MAX_ACTIVE_SHARED_IMAGES is there to
 * throttle that. So when the child side wants to allocate a new shared image 
 * but is already at its maximum of active shared images, it just discards the
 * image (which is therefore not allocated and not sent to the compositor).
 *
 * The values for the two constants are arbitrary and should be tweaked if it 
 * happens that we run into shared memory problems.
 */
static const unsigned int POOL_MAX_SHARED_IMAGES = 5;
static const unsigned int MAX_ACTIVE_SHARED_IMAGES = 10;

ImageContainerChild::ImageContainerChild()
: mImageContainerID(0), mActiveImageCount(0),
  mStop(false), mDispatchedDestroy(false) 
{
  MOZ_COUNT_CTOR(ImageContainerChild);
  // the Release corresponding to this AddRef is in 
  // ImageBridgeChild::DeallocPImageContainer
  AddRef();
}

ImageContainerChild::~ImageContainerChild()
{
  MOZ_COUNT_DTOR(ImageContainerChild);
}

void ImageContainerChild::DispatchStop()
{
  GetMessageLoop()->PostTask(FROM_HERE,
                  NewRunnableMethod(this, &ImageContainerChild::StopChildAndParent));
}

void ImageContainerChild::SetIdleNow() 
{
  if (mStop) return;

  SendFlush();
  ClearSurfaceDescriptorPool();
  mImageQueue.Clear();
}

void ImageContainerChild::DispatchSetIdle()
{
  if (mStop) return;

  GetMessageLoop()->PostTask(FROM_HERE, 
                    NewRunnableMethod(this, &ImageContainerChild::SetIdleNow));
}

void ImageContainerChild::StopChildAndParent()
{
  if (mStop) {
    return;
  }
  mStop = true;    

  SendStop(); // IPC message
  DispatchDestroy();
}

void ImageContainerChild::StopChild()
{
  if (mStop) {
    return;
  }
  mStop = true;    

  DispatchDestroy();
}

bool ImageContainerChild::RecvReturnImage(const SurfaceDescriptor& aImage)
{
  SurfaceDescriptor* img = new SurfaceDescriptor(aImage);
  // Remove oldest image from the queue.
  if (mImageQueue.Length() > 0) {
    mImageQueue.RemoveElementAt(0);
  }
  if (!AddSurfaceDescriptorToPool(img) || mStop) {
    DestroySurfaceDescriptor(*img);
    delete img;
  }
  return true;
}

void ImageContainerChild::DestroySurfaceDescriptor(const SurfaceDescriptor& aImage)
{
  NS_ABORT_IF_FALSE(InImageBridgeChildThread(),
                    "Should be in ImageBridgeChild thread.");

  --mActiveImageCount;
  DeallocSurfaceDescriptorData(this, aImage);
}

bool ImageContainerChild::CopyDataIntoSurfaceDescriptor(Image* src, SurfaceDescriptor* dest)
{
  if ((src->GetFormat() == PLANAR_YCBCR) && 
      (dest->type() == SurfaceDescriptor::TYCbCrImage)) {
    PlanarYCbCrImage *planarYCbCrImage = static_cast<PlanarYCbCrImage*>(src);
    const PlanarYCbCrImage::Data *data =planarYCbCrImage->GetData();
    NS_ASSERTION(data, "Must be able to retrieve yuv data from image!");
    YCbCrImage& yuv = dest->get_YCbCrImage();

    ShmemYCbCrImage shmemImage(yuv.data(), yuv.offset());

    if (!shmemImage.CopyData(data->mYChannel, data->mCbChannel, data->mCrChannel,
                             data->mYSize, data->mYStride,
                             data->mCbCrSize, data->mCbCrStride)) {
      NS_WARNING("Failed to copy image data!");
      return false;
    }
    return true;
  }
  return false; // TODO: support more image formats
}

SurfaceDescriptor* ImageContainerChild::AllocateSurfaceDescriptorFor(Image* image)
{
  NS_ABORT_IF_FALSE(InImageBridgeChildThread(),
                    "Should be in ImageBridgeChild thread.");

  if (!image) {
    return nullptr;
  }
  if (image->GetFormat() == PLANAR_YCBCR ) {
    PlanarYCbCrImage *planarYCbCrImage = static_cast<PlanarYCbCrImage*>(image);
    const PlanarYCbCrImage::Data *data = planarYCbCrImage->GetData();
    NS_ASSERTION(data, "Must be able to retrieve yuv data from image!");
    if (!data) {
      return nullptr;
    }

    SharedMemory::SharedMemoryType shmType = OptimalShmemType();
    size_t size = ShmemYCbCrImage::ComputeMinBufferSize(data->mYSize,
                                                        data->mCbCrSize);
    Shmem shmem;
    if (!AllocUnsafeShmem(size, shmType, &shmem)) {
      return nullptr;
    }

    ShmemYCbCrImage::InitializeBufferInfo(shmem.get<uint8_t>(),
                                          data->mYSize,
                                          data->mCbCrSize);
    ShmemYCbCrImage shmemImage(shmem);

    if (!shmemImage.IsValid() || shmem.Size<uint8_t>() < size) {
      DeallocShmem(shmem);
      return nullptr;
    }

    for (int i = 0; i < data->mYSize.height; i++) {
      memcpy(shmemImage.GetYData() + i * shmemImage.GetYStride(),
             data->mYChannel + i * data->mYStride,
             data->mYSize.width);
    }
    for (int i = 0; i < data->mCbCrSize.height; i++) {
      memcpy(shmemImage.GetCbData() + i * shmemImage.GetCbCrStride(),
             data->mCbChannel + i * data->mCbCrStride,
             data->mCbCrSize.width);
      memcpy(shmemImage.GetCrData() + i * shmemImage.GetCbCrStride(),
             data->mCrChannel + i * data->mCbCrStride,
             data->mCbCrSize.width);
    }

    ++mActiveImageCount;
    return new SurfaceDescriptor(YCbCrImage(shmem, 0, data->GetPictureRect()));
  } else {
    NS_RUNTIMEABORT("TODO: Only YCbCrImage is supported here right now.");
  }
  return nullptr;
}

void ImageContainerChild::RecycleSurfaceDescriptorNow(SurfaceDescriptor* aImage)
{
  NS_ABORT_IF_FALSE(InImageBridgeChildThread(),"Must be in the ImageBridgeChild Thread.");

  if (mStop || !AddSurfaceDescriptorToPool(aImage)) {
    DestroySurfaceDescriptor(*aImage);
    delete aImage;
  }
}

void ImageContainerChild::RecycleSurfaceDescriptor(SurfaceDescriptor* aImage)
{
  if (!aImage) {
    return;
  }
  if (InImageBridgeChildThread()) {
    RecycleSurfaceDescriptorNow(aImage);
    return;
  }
  GetMessageLoop()->PostTask(FROM_HERE,
                             NewRunnableMethod(this,
                                               &ImageContainerChild::RecycleSurfaceDescriptorNow,
                                               aImage));
}

bool ImageContainerChild::AddSurfaceDescriptorToPool(SurfaceDescriptor* img)
{
  NS_ABORT_IF_FALSE(InImageBridgeChildThread(), 
                    "AddSurfaceDescriptorToPool must be called in the ImageBridgeChild thread");
  if (mStop) {
    return false;
  }

  if (mSurfaceDescriptorPool.Length() >= POOL_MAX_SHARED_IMAGES) {
    return false;
  }
  if (img->type() == SurfaceDescriptor::TYCbCrImage) {
    mSurfaceDescriptorPool.AppendElement(img);
    return true;
  }
  return false; // TODO accept more image formats in the pool
}

static bool
SurfaceDescriptorCompatibleWith(SurfaceDescriptor* aSurfaceDescriptor, Image* aImage)
{
  // TODO accept more image formats
  switch (aImage->GetFormat()) {
  case PLANAR_YCBCR: {
    if (aSurfaceDescriptor->type() != SurfaceDescriptor::TYCbCrImage) {
      return false;
    }
    const PlanarYCbCrImage::Data* data =
      static_cast<PlanarYCbCrImage*>(aImage)->GetData();
    const YCbCrImage& yuv = aSurfaceDescriptor->get_YCbCrImage();

    ShmemYCbCrImage shmImg(yuv.data(),yuv.offset());

    if (shmImg.GetYSize() != data->mYSize) {
      return false;
    }
    if (shmImg.GetCbCrSize() != data->mCbCrSize) {
      return false;
    }

    return true;
  }
  default:
    return false;
  }
}

SurfaceDescriptor*
ImageContainerChild::GetSurfaceDescriptorFor(Image* aImage)
{
  while (mSurfaceDescriptorPool.Length() > 0) {
    // i.e., img = mPool.pop()
    nsAutoPtr<SurfaceDescriptor> img(mSurfaceDescriptorPool.LastElement());
    mSurfaceDescriptorPool.RemoveElementAt(mSurfaceDescriptorPool.Length() - 1);

    if (SurfaceDescriptorCompatibleWith(img, aImage)) {
      return img.forget();
    }
    // The cached image is stale, throw it out.
    DeallocSurfaceDescriptorData(this, *img);
  }

  return nullptr;
}

void ImageContainerChild::ClearSurfaceDescriptorPool()
{
  NS_ABORT_IF_FALSE(InImageBridgeChildThread(),
                    "Should be in ImageBridgeChild thread.");
  for(unsigned int i = 0; i < mSurfaceDescriptorPool.Length(); ++i) {
    DeallocSurfaceDescriptorData(this, *mSurfaceDescriptorPool[i]);
  }
  mSurfaceDescriptorPool.Clear();
}

void ImageContainerChild::SendImageNow(Image* aImage)
{
  NS_ABORT_IF_FALSE(InImageBridgeChildThread(),
                    "Should be in ImageBridgeChild thread.");

  if (mStop) {
    return;
  }

  if (aImage->IsSentToCompositor()) {
    return;
  }

  bool needsCopy = false;
  // If the image can be converted to a shared image, no need to do a copy.
  SurfaceDescriptor* img = AsSurfaceDescriptor(aImage);
  if (!img) {
    needsCopy = true;
    // Try to get a compatible shared image from the pool
    img = GetSurfaceDescriptorFor(aImage);
    if (!img && mActiveImageCount < (int)MAX_ACTIVE_SHARED_IMAGES) {
      // If no shared image available, allocate a new one
      img = AllocateSurfaceDescriptorFor(aImage);
    }
  }

  if (img && (!needsCopy || CopyDataIntoSurfaceDescriptor(aImage, img))) {
    // Keep a reference to the image we sent to compositor to maintain a
    // correct reference count.
    aImage->MarkSent();
    mImageQueue.AppendElement(aImage);
    SendPublishImage(*img);
  } else {
    NS_WARNING("Failed to send an image to the compositor");
  }
  delete img;
  return;
}

class ImageBridgeCopyAndSendTask : public Task
{
public:
  ImageBridgeCopyAndSendTask(ImageContainerChild * child, 
                             ImageContainer * aContainer, 
                             Image * aImage)
  : mChild(child), mImageContainer(aContainer), mImage(aImage) {}

  void Run()
  { 
    mChild->SendImageNow(mImage);
  }

  ImageContainerChild* mChild;
  nsRefPtr<ImageContainer> mImageContainer;
  nsRefPtr<Image> mImage;
};

/*
SurfaceDescriptor* ImageContainerChild::AllocateSurfaceDescriptorFor(Image* aImage)
{
  if (mStop) {
    return nullptr;
  }
  if (mActiveImageCount > (int)MAX_ACTIVE_SHARED_IMAGES) {
    // Too many active shared images, perhaps the compositor is hanging.
    // Skipping current image
    return nullptr;
  }

  NS_ABORT_IF_FALSE(InImageBridgeChildThread(),
                    "Should be in ImageBridgeChild thread.");
  SurfaceDescriptor *img = GetSurfaceDescriptorFor(aImage);
  if (img) {
    CopyDataIntoSurfaceDescriptor(aImage, img);  
  } else {
    img = CreateSurfaceDescriptorFromData(aImage);
  }
  // Keep a reference to the image we sent to compositor to maintain a
  // correct reference count.
  mImageQueue.AppendElement(aImage);
  return img;
}
*/
void ImageContainerChild::SendImageAsync(ImageContainer* aContainer,
                                         Image* aImage)
{
  if(!aContainer || !aImage) {
      return;
  }

  if (mStop) {
    return;
  }

  if (InImageBridgeChildThread()) {
    SendImageNow(aImage);
  }

  // Sending images and (potentially) allocating shmems must be done 
  // on the ImageBridgeChild thread.
  Task *t = new ImageBridgeCopyAndSendTask(this, aContainer, aImage);   
  GetMessageLoop()->PostTask(FROM_HERE, t);
}

void ImageContainerChild::DestroyNow()
{
  NS_ABORT_IF_FALSE(InImageBridgeChildThread(),
                    "Should be in ImageBridgeChild thread.");
  NS_ABORT_IF_FALSE(mDispatchedDestroy,
                    "Incorrect state in the destruction sequence.");

  ClearSurfaceDescriptorPool();
  mImageQueue.Clear();

  // will decrease the refcount and, in most cases, delete the ImageContainerChild
  Send__delete__(this);
  Release(); // corresponds to the AddRef in DispatchDestroy
}

void ImageContainerChild::DispatchDestroy()
{
  NS_ABORT_IF_FALSE(mStop, "The state should be 'stopped' when destroying");

  if (mDispatchedDestroy) {
    return;
  }
  mDispatchedDestroy = true;
  AddRef(); // corresponds to the Release in DestroyNow
  GetMessageLoop()->PostTask(FROM_HERE, 
                    NewRunnableMethod(this, &ImageContainerChild::DestroyNow));
}

// We can't pass more than 6 parameters to a 'NewRunableFunction' so some
// parameters are stored in a struct passed by pointer
struct CreateShmemParams
{
  ImageContainerChild* mProtocol;
  size_t mBufSize;
  SharedMemory::SharedMemoryType mType;
  ipc::Shmem* mShmem;
  bool mResult;
};

static void AllocUnsafeShmemNow(CreateShmemParams* aParams,
                                ReentrantMonitor* aBarrier,
                                bool* aDone)
{
  ReentrantMonitorAutoEnter autoBarrier(*aBarrier);
  aParams->mResult = aParams->mProtocol->AllocUnsafeShmem(aParams->mBufSize,
                                                          aParams->mType,
                                                          aParams->mShmem);
*aDone = true;
  aBarrier->NotifyAll();
}


bool ImageContainerChild::AllocUnsafeShmemSync(size_t aBufSize,
                                               SharedMemory::SharedMemoryType aType,
                                               ipc::Shmem* aShmem)
{
  if (mStop) {
    return false;
  }
  if (InImageBridgeChildThread()) {
    AllocUnsafeShmem(aBufSize, aType, aShmem);
  }
  ReentrantMonitor barrier("ImageContainerChild::AllocUnsafeShmemSync");
  ReentrantMonitorAutoEnter autoBarrier(barrier);

  CreateShmemParams p = {
    this,
    aBufSize,
    aType,
    aShmem,
    false,
  };

  bool done = false;
  GetMessageLoop()->PostTask(FROM_HERE,
                 NewRunnableFunction(&AllocUnsafeShmemNow,
                                     &p,
                                     &barrier, &done));
  while (!done) {
    barrier.Wait();
  }

  return p.mResult;
}

static void DeallocShmemNow(ImageContainerChild* aProtocol, ipc::Shmem aShmem)
{
  aProtocol->DeallocShmem(aShmem);
}

void ImageContainerChild::DeallocShmemAsync(ipc::Shmem& aShmem)
{
  if (mStop) {
    return;
  }
  GetMessageLoop()->PostTask(FROM_HERE,
                             NewRunnableFunction(&DeallocShmemNow,
                                                 this,
                                                 aShmem));
}

class SharedPlanarYCbCrImage : public PlanarYCbCrImage
{
public:
  SharedPlanarYCbCrImage(ImageContainerChild* aProtocol)
  : PlanarYCbCrImage(nullptr),
    mImageContainerChild(aProtocol), mAllocated(false) {}

  ~SharedPlanarYCbCrImage() {
    if (mAllocated) {
      mImageContainerChild->RecycleSurfaceDescriptor(ToSurfaceDescriptor());
    }
  }

  virtual SharedPlanarYCbCrImage* AsSharedPlanarYCbCrImage() MOZ_OVERRIDE
  {
    return this;
  }

  virtual already_AddRefed<gfxASurface> GetAsSurface() MOZ_OVERRIDE
  {
    if (!mAllocated) {
      NS_WARNING("Can't get as surface");
      return nullptr;
    }
    return PlanarYCbCrImage::GetAsSurface();
  }

  virtual void SetData(const PlanarYCbCrImage::Data& aData) MOZ_OVERRIDE
  {
    // If mShmem has not been allocated (through Allocate(aData)), allocate it.
    // This code path is slower than the one used when Allocate has been called
    // since it will trigger a full copy.
    if (!mAllocated) {
      Data data = aData;
      if (!Allocate(data)) {
        return;
      }
    }

    // do not set mBuffer like in PlanarYCbCrImage because the later
    // will try to manage this memory without knowing it belongs to a
    // shmem.
    mBufferSize = ShmemYCbCrImage::ComputeMinBufferSize(mData.mYSize,
                                                        mData.mCbCrSize);
    mSize = mData.mPicSize;

    ShmemYCbCrImage shmImg(mShmem);

    if (!shmImg.CopyData(aData.mYChannel, aData.mCbChannel, aData.mCrChannel,
                         aData.mYSize, aData.mYStride,
                         aData.mCbCrSize, aData.mCbCrStride)) {
      NS_WARNING("Failed to copy image data!");
    }
    mData.mYChannel = shmImg.GetYData();
    mData.mCbChannel = shmImg.GetCbData();
    mData.mCrChannel = shmImg.GetCrData();
  }

  virtual bool Allocate(PlanarYCbCrImage::Data& aData)
  {
    NS_ABORT_IF_FALSE(!mAllocated, "This image already has allocated data");

    SharedMemory::SharedMemoryType shmType = OptimalShmemType();
    size_t size = ShmemYCbCrImage::ComputeMinBufferSize(aData.mYSize,
                                                        aData.mCbCrSize);

    if (!mImageContainerChild->AllocUnsafeShmemSync(size, shmType, &mShmem)) {
      return false;
    }
    ShmemYCbCrImage::InitializeBufferInfo(mShmem.get<uint8_t>(),
                                          aData.mYSize,
                                          aData.mCbCrSize);
    ShmemYCbCrImage shmImg(mShmem);
    if (!shmImg.IsValid() || mShmem.Size<uint8_t>() < size) {
      mImageContainerChild->DeallocShmemAsync(mShmem);
      return false;
    }

    aData.mYChannel = shmImg.GetYData();
    aData.mCbChannel = shmImg.GetCbData();
    aData.mCrChannel = shmImg.GetCrData();

    // copy some of aData's values in mData (most of them)
    mData.mYChannel = aData.mYChannel;
    mData.mCbChannel = aData.mCbChannel;
    mData.mCrChannel = aData.mCrChannel;
    mData.mYSize = aData.mYSize;
    mData.mCbCrSize = aData.mCbCrSize;
    mData.mPicX = aData.mPicX;
    mData.mPicY = aData.mPicY;
    mData.mPicSize = aData.mPicSize;
    mData.mStereoMode = aData.mStereoMode;
    // those members are not always equal to aData's, due to potentially different
    // packing.
    mData.mYSkip = 0;
    mData.mCbSkip = 0;
    mData.mCrSkip = 0;
    mData.mYStride = mData.mYSize.width;
    mData.mCbCrStride = mData.mCbCrSize.width;

    mAllocated = true;
    return true;
  }

  virtual bool IsValid() MOZ_OVERRIDE {
    return mAllocated;
  }

  SurfaceDescriptor* ToSurfaceDescriptor() {
    if (mAllocated) {
      return new SurfaceDescriptor(YCbCrImage(mShmem, 0, mData.GetPictureRect()));
    }
    return nullptr;
  }

private:
  Shmem mShmem;
  nsRefPtr<ImageContainerChild> mImageContainerChild;
  bool mAllocated;
};

already_AddRefed<Image> ImageContainerChild::CreateImage(const uint32_t *aFormats,
                                                         uint32_t aNumFormats)
{
  nsRefPtr<Image> img;
#ifdef MOZ_WIDGET_GONK
  for (uint32_t i = 0; i < aNumFormats; i++) {
    switch (aFormats[i]) {
      case PLANAR_YCBCR:
#endif
        img = new SharedPlanarYCbCrImage(this);
        return img.forget();
#ifdef MOZ_WIDGET_GONK
      case GONK_IO_SURFACE:
        img = new GonkIOSurfaceImage();
        return img.forget();
      case GRALLOC_PLANAR_YCBCR:
        img = new GrallocPlanarYCbCrImage();
        return img.forget();
    }
  }

  return nullptr;
#endif
}

SurfaceDescriptor* ImageContainerChild::AsSurfaceDescriptor(Image* aImage)
{
#ifdef MOZ_WIDGET_GONK
  if (aImage->GetFormat() == GONK_IO_SURFACE) {
    GonkIOSurfaceImage* gonkImage = static_cast<GonkIOSurfaceImage*>(aImage);
    SurfaceDescriptor* result = new SurfaceDescriptor(gonkImage->GetSurfaceDescriptor());
    return result;
  } else if (aImage->GetFormat() == GRALLOC_PLANAR_YCBCR) {
    GrallocPlanarYCbCrImage* GrallocImage = static_cast<GrallocPlanarYCbCrImage*>(aImage);
    SurfaceDescriptor* result = new SurfaceDescriptor(GrallocImage->GetSurfaceDescriptor());
    return result;
  }
#endif
  if (aImage->GetFormat() == PLANAR_YCBCR) {
    SharedPlanarYCbCrImage* sharedYCbCr
      = static_cast<PlanarYCbCrImage*>(aImage)->AsSharedPlanarYCbCrImage();
    if (sharedYCbCr) {
      return sharedYCbCr->ToSurfaceDescriptor();
    }
  }
  return nullptr;
}

} // namespace
} // namespace

