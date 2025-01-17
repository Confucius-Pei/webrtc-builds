// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_MEDIA_WEB_MEDIA_SOURCE_IMPL_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_MEDIA_WEB_MEDIA_SOURCE_IMPL_H_

#include <vector>

#include "third_party/blink/public/platform/web_media_source.h"
#include "third_party/blink/renderer/platform/platform_export.h"

namespace media {
class AudioDecoderConfig;
class ChunkDemuxer;
class VideoDecoderConfig;

class PLATFORM_EXPORT WebMediaSourceImpl : public blink::WebMediaSource {
 public:
  WebMediaSourceImpl(ChunkDemuxer* demuxer);
  WebMediaSourceImpl(const WebMediaSourceImpl&) = delete;
  WebMediaSourceImpl& operator=(const WebMediaSourceImpl&) = delete;
  ~WebMediaSourceImpl() override;

  // blink::WebMediaSource implementation.
  std::unique_ptr<blink::WebSourceBuffer> AddSourceBuffer(
      const blink::WebString& content_type,
      const blink::WebString& codecs,
      AddStatus& out_status /* out */) override;
  std::unique_ptr<blink::WebSourceBuffer> AddSourceBuffer(
      std::unique_ptr<AudioDecoderConfig> audio_config,
      AddStatus& out_status /* out */) override;
  std::unique_ptr<blink::WebSourceBuffer> AddSourceBuffer(
      std::unique_ptr<VideoDecoderConfig> video_config,
      AddStatus& out_status /* out */) override;
  double Duration() override;
  void SetDuration(double duration) override;
  void MarkEndOfStream(EndOfStreamStatus status) override;
  void UnmarkEndOfStream() override;

 private:
  ChunkDemuxer* demuxer_;  // Owned by WebMediaPlayerImpl.
};

}  // namespace media

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_MEDIA_WEB_MEDIA_SOURCE_IMPL_H_
