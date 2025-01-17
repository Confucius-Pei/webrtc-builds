// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/breakout_box/media_stream_video_track_underlying_source.h"

#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/modules/breakout_box/frame_queue_transferring_optimizer.h"
#include "third_party/blink/renderer/modules/breakout_box/metrics.h"
#include "third_party/blink/renderer/modules/mediastream/media_stream_track.h"
#include "third_party/blink/renderer/modules/mediastream/media_stream_video_track.h"
#include "third_party/blink/renderer/platform/scheduler/public/post_cross_thread_task.h"
#include "third_party/blink/renderer/platform/scheduler/public/thread.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_functional.h"

namespace blink {

MediaStreamVideoTrackUnderlyingSource::MediaStreamVideoTrackUnderlyingSource(
    ScriptState* script_state,
    MediaStreamComponent* track,
    ScriptWrappable* media_stream_track_processor,
    wtf_size_t max_queue_size)
    : FrameQueueUnderlyingSource(script_state, max_queue_size),
      media_stream_track_processor_(media_stream_track_processor),
      track_(track) {
  DCHECK(track_);
  RecordBreakoutBoxUsage(BreakoutBoxUsage::kReadableVideo);
}

void MediaStreamVideoTrackUnderlyingSource::Trace(Visitor* visitor) const {
  FrameQueueUnderlyingSource::Trace(visitor);
  visitor->Trace(media_stream_track_processor_);
  visitor->Trace(track_);
}

std::unique_ptr<ReadableStreamTransferringOptimizer>
MediaStreamVideoTrackUnderlyingSource::GetStreamTransferOptimizer() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return std::make_unique<VideoFrameQueueTransferOptimizer>(
      this, GetRealmRunner(), MaxQueueSize(),
      CrossThreadBindOnce(
          &MediaStreamVideoTrackUnderlyingSource::OnSourceTransferStarted,
          WrapCrossThreadWeakPersistent(this)));
}

scoped_refptr<base::SequencedTaskRunner>
MediaStreamVideoTrackUnderlyingSource::GetIOTaskRunner() {
  return Platform::Current()->GetIOTaskRunner();
}

void MediaStreamVideoTrackUnderlyingSource::OnSourceTransferStarted(
    scoped_refptr<base::SequencedTaskRunner> transferred_runner,
    TransferredVideoFrameQueueUnderlyingSource* source) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  TransferSource(source);
  RecordBreakoutBoxUsage(BreakoutBoxUsage::kReadableVideoWorker);
}

void MediaStreamVideoTrackUnderlyingSource::OnFrameFromTrack(
    scoped_refptr<media::VideoFrame> media_frame,
    std::vector<scoped_refptr<media::VideoFrame>> /*scaled_media_frames*/,
    base::TimeTicks estimated_capture_time) {
  DCHECK(GetIOTaskRunner()->RunsTasksInCurrentSequence());
  // The scaled video frames are currently ignored.
  QueueFrame(std::move(media_frame));
}

bool MediaStreamVideoTrackUnderlyingSource::StartFrameDelivery() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (connected_track())
    return true;

  MediaStreamVideoTrack* video_track = MediaStreamVideoTrack::From(track_);
  if (!video_track)
    return false;

  ConnectToTrack(WebMediaStreamTrack(track_),
                 ConvertToBaseRepeatingCallback(CrossThreadBindRepeating(
                     &MediaStreamVideoTrackUnderlyingSource::OnFrameFromTrack,
                     WrapCrossThreadPersistent(this))),
                 MediaStreamVideoSink::IsSecure::kNo,
                 MediaStreamVideoSink::UsesAlpha::kDefault);
  return true;
}

void MediaStreamVideoTrackUnderlyingSource::StopFrameDelivery() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DisconnectFromTrack();
}

}  // namespace blink
