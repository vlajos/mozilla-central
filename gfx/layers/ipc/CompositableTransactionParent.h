/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=8 et :
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/ISurfaceAllocator.h"
#include "mozilla/layers/LayerTransaction.h"

namespace mozilla {
namespace layers {

class Compositor;

typedef std::vector<mozilla::layers::EditReply> EditReplyVector;

// interface.
// since PCompositble has two potential manager protocols, we can't just call
// the Manager() method usually generated when there's one manager protocol,
// so both manager protocols implement this and we keep a reference to them
// through this interface.
class CompositableParentManager : public ISurfaceAllocator
{
public:
  virtual Compositor* GetCompositor() = 0;

protected:
  bool ReceiveCompositableUpdate(const CompositableOperation& aEdit,
                                 const bool& isFirstPaint, // TODO[nical] this doesn't seem to be used
                                 EditReplyVector& replyv);
};




} // namespace
} // namespace
