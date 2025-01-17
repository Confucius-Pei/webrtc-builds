// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_BREAKOUT_BOX_MEDIA_STREAM_VIDEO_TRACK_UNDERLYING_SOURCE_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_BREAKOUT_BOX_MEDIA_STREAM_VIDEO_TRACK_UNDERLYING_SOURCE_H_

#include "base/sequence_checker.h"
#include "third_party/blink/public/web/modules/mediastream/media_stream_video_sink.h"
#include "third_party/blink/renderer/core/streams/readable_stream_transferring_optimizer.h"
#include "third_party/blink/renderer/modules/breakout_box/frame_queue_underlying_source.h"
#include "third_party/blink/renderer/modules/breakout_box/transferred_frame_queue_underlying_source.h"
#include "third_party/blink/renderer/modules/modules_export.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"

namespace blink {

class MediaStreamComponent;

class MODULES_EXPORT MediaStreamVideoTrackUnderlyingSource
    : public VideoFrameQueueUnderlyingSource,
      public MediaStreamVideoSink {
 public:
  using CrossThreadFrameQueueSource =
      CrossThreadPersistent<TransferredVideoFrameQueueUnderlyingSource>;

  explicit MediaStreamVideoTrackUnderlyingSource(
      ScriptState*,
      MediaStreamComponent*,
      ScriptWrappable* media_stream_track_processor,
      wtf_size_t queue_size);
  MediaStreamVideoTrackUnderlyingSource(
      const MediaStreamVideoTrackUnderlyingSource&) = delete;
  MediaStreamVideoTrackUnderlyingSource& operator=(
      const MediaStreamVideoTrackUnderlyingSource&) = delete;

  MediaStreamComponent* Track() const { return track_.Get(); }

  void Trace(Visitor*) const override;

  std::unique_ptr<ReadableStreamTransferringOptimizer>
  GetStreamTransferOptimizer();

 private:
  scoped_refptr<base::SequencedTaskRunner> GetIOTaskRunner();

  // FrameQueueUnderlyingSource implementation.
  bool StartFrameDelivery() override;
  void StopFrameDelivery() override;

  void OnSourceTransferStarted(scoped_refptr<base::SequencedTaskRunner>,
                               TransferredVideoFrameQueueUnderlyingSource*);

  void OnFrameFromTrack(
      scoped_refptr<media::VideoFrame> media_frame,
      std::vector<scoped_refptr<media::VideoFrame>> scaled_media_frames,
      base::TimeTicks estimated_capture_time);

  // Only used to prevent the gargabe collector from reclaiming the media
  // stream track processor that created |this|.
  const Member<ScriptWrappable> media_stream_track_processor_;

  const Member<MediaStreamComponent> track_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_BREAKOUT_BOX_MEDIA_STREAM_VIDEO_TRACK_UNDERLYING_SOURCE_H_
