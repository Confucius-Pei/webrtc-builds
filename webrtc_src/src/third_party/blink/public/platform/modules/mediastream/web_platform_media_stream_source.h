// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_PUBLIC_PLATFORM_MODULES_MEDIASTREAM_WEB_PLATFORM_MEDIA_STREAM_SOURCE_H_
#define THIRD_PARTY_BLINK_PUBLIC_PLATFORM_MODULES_MEDIASTREAM_WEB_PLATFORM_MEDIA_STREAM_SOURCE_H_

#include "base/callback.h"
#include "third_party/blink/public/common/mediastream/media_stream_controls.h"
#include "third_party/blink/public/common/mediastream/media_stream_request.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom-shared.h"
#include "third_party/blink/public/platform/modules/mediastream/web_media_stream_source.h"
#include "third_party/blink/public/platform/web_common.h"
#include "third_party/blink/public/platform/web_private_ptr.h"

namespace blink {

class MediaStreamSource;
class WebMediaStreamSource;

class BLINK_PLATFORM_EXPORT WebPlatformMediaStreamSource {
 public:
  using SourceStoppedCallback =
      base::OnceCallback<void(const WebMediaStreamSource& source)>;

  using ConstraintsRepeatingCallback =
      base::RepeatingCallback<void(WebPlatformMediaStreamSource* source,
                                   mojom::MediaStreamRequestResult result,
                                   const WebString& result_name)>;
  using ConstraintsOnceCallback =
      base::OnceCallback<void(WebPlatformMediaStreamSource* source,
                              mojom::MediaStreamRequestResult result,
                              const WebString& result_name)>;

  // Source constraints key for
  // https://dev.w3.org/2011/webrtc/editor/getusermedia.html.
  static const char kSourceId[];

  WebPlatformMediaStreamSource();
  virtual ~WebPlatformMediaStreamSource();

  // Returns device information about a source that has been created by a
  // JavaScript call to GetUserMedia, e.g., a camera or microphone.
  const MediaStreamDevice& device() const { return device_; }

  // Stops the source (by calling DoStopSource()) and runs FinalizeStopSource().
  void StopSource();

  // Sets the source's state to muted or to live.
  void SetSourceMuted(bool is_muted);

  // Sets device information about a source that has been created by a
  // JavaScript call to GetUserMedia. F.E a camera or microphone.
  void SetDevice(const MediaStreamDevice& device);

  // Sets the capture-handle for a source that has been created by a
  // JavaScript call to one of the display-capture APIs (e.g. getDisplayMedia).
  void SetCaptureHandle(media::mojom::CaptureHandlePtr capture_handle);

  // Sets a callback that will be triggered when StopSource is called.
  void SetStopCallback(SourceStoppedCallback stop_callback);

  // Clears the previously-set SourceStoppedCallback so that it will not be run
  // in the future.
  void ResetSourceStoppedCallback();

  // Change the source to the |new_device| by calling DoChangeSource().
  void ChangeSource(const MediaStreamDevice& new_device);

  WebMediaStreamSource Owner();
#if INSIDE_BLINK
  void SetOwner(MediaStreamSource*);
#endif

 protected:
  // Called when StopSource is called. It allows derived classes to implement
  // its own Stop method.
  virtual void DoStopSource() = 0;

  // Called when ChangeSource is called. It allows derived class to implement
  // it's own ChangeSource method.
  virtual void DoChangeSource(const MediaStreamDevice& new_device) = 0;

  // Runs the stop callback (if set) and sets the
  // WebMediaStreamSource::readyState to ended. This can be used by
  // implementations to implement custom stop methods.
  void FinalizeStopSource();

 private:
  MediaStreamDevice device_;
  SourceStoppedCallback stop_callback_;
  WebPrivatePtr<MediaStreamSource,
                kWebPrivatePtrDestructionSameThread,
                WebPrivatePtrStrength::kWeak>
      owner_;

  DISALLOW_COPY_AND_ASSIGN(WebPlatformMediaStreamSource);
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_PUBLIC_PLATFORM_MODULES_MEDIASTREAM_WEB_PLATFORM_MEDIA_STREAM_SOURCE_H_
