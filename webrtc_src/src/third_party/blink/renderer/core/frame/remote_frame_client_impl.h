// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_REMOTE_FRAME_CLIENT_IMPL_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_REMOTE_FRAME_CLIENT_IMPL_H_

#include "third_party/blink/public/mojom/input/focus_type.mojom-blink-forward.h"
#include "third_party/blink/renderer/core/frame/frame_client.h"

namespace blink {
class WebRemoteFrameImpl;

class RemoteFrameClientImpl final : public FrameClient {
 public:
  explicit RemoteFrameClientImpl(WebRemoteFrameImpl*);

  void Trace(Visitor*) const override;

  // FrameClient overrides:
  bool IsRemoteFrameClient() const override;
  bool InShadowTree() const override;
  void Detached(FrameDetachType) override;
  unsigned BackForwardLength() override;

  WebRemoteFrameImpl* GetWebFrame() const { return web_frame_; }

 private:
  Member<WebRemoteFrameImpl> web_frame_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_REMOTE_FRAME_CLIENT_IMPL_H_
