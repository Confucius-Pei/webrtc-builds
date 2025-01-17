// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_PUBLIC_PLATFORM_MEDIA_REMOTE_PLAYBACK_CLIENT_WRAPPER_IMPL_H_
#define THIRD_PARTY_BLINK_PUBLIC_PLATFORM_MEDIA_REMOTE_PLAYBACK_CLIENT_WRAPPER_IMPL_H_

#include <string>

#include "media/renderers/remote_playback_client_wrapper.h"
#include "third_party/blink/public/platform/web_common.h"

namespace blink {
class WebMediaPlayerClient;
class WebRemotePlaybackClient;
}  // namespace blink

namespace media {

// Wraps a WebRemotePlaybackClient to expose only the methods used by the
// FlingingRendererClientFactory. This avoids dependencies on the blink layer.
class BLINK_PLATFORM_EXPORT RemotePlaybackClientWrapperImpl
    : public RemotePlaybackClientWrapper {
 public:
  explicit RemotePlaybackClientWrapperImpl(blink::WebMediaPlayerClient* client);
  ~RemotePlaybackClientWrapperImpl() override;

  std::string GetActivePresentationId() override;

 private:
  blink::WebRemotePlaybackClient* remote_playback_client_;
};

}  // namespace media

#endif  // THIRD_PARTY_BLINK_PUBLIC_PLATFORM_MEDIA_REMOTE_PLAYBACK_CLIENT_WRAPPER_IMPL_H_
