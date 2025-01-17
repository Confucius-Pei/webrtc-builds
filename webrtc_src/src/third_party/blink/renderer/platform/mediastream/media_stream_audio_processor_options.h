// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_MEDIASTREAM_MEDIA_STREAM_AUDIO_PROCESSOR_OPTIONS_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_MEDIASTREAM_MEDIA_STREAM_AUDIO_PROCESSOR_OPTIONS_H_

#include <string>

#include "base/files/file.h"
#include "base/threading/thread_checker.h"
#include "build/build_config.h"
#include "media/base/audio_point.h"
#include "media/base/audio_processing.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/common/mediastream/media_stream_request.h"
#include "third_party/blink/renderer/platform/mediastream/media_constraints.h"
#include "third_party/blink/renderer/platform/platform_export.h"
#include "third_party/webrtc/api/media_stream_interface.h"
#include "third_party/webrtc/media/base/media_channel.h"
#include "third_party/webrtc/modules/audio_processing/include/audio_processing.h"
#include "third_party/webrtc/rtc_base/task_queue.h"

namespace webrtc {

class TypingDetection;

}

namespace blink {

using webrtc::AudioProcessing;

static constexpr int kAudioProcessingSampleRate =
#if defined(OS_ANDROID)
    AudioProcessing::kSampleRate16kHz;
#else
    AudioProcessing::kSampleRate48kHz;
#endif

// Simple struct with audio-processing properties.
struct PLATFORM_EXPORT AudioProcessingProperties {
  enum class EchoCancellationType {
    // Echo cancellation disabled.
    kEchoCancellationDisabled,
    // The WebRTC-provided AEC3 echo canceller.
    kEchoCancellationAec3,
    // System echo canceller, for example an OS-provided or hardware echo
    // canceller.
    kEchoCancellationSystem
  };

  // Disables properties that are enabled by default.
  void DisableDefaultProperties();

  // Returns whether echo cancellation is enabled.
  bool EchoCancellationEnabled() const;

  // Returns whether WebRTC-provided echo cancellation is enabled.
  bool EchoCancellationIsWebRtcProvided() const;

  bool HasSameReconfigurableSettings(
      const AudioProcessingProperties& other) const;

  bool HasSameNonReconfigurableSettings(
      const AudioProcessingProperties& other) const;

  // Converts this struct to an equivalent media::AudioProcessingSettings.
  // TODO(https://crbug.com/878757): Eliminate this class in favor of the media
  // one.
  media::AudioProcessingSettings ToAudioProcessingSettings() const;

  EchoCancellationType echo_cancellation_type =
      EchoCancellationType::kEchoCancellationAec3;
  // Indicates whether system-level gain control and noise suppression
  // functionalities are present that fill a role comparable to the browser
  // counterparts.
  bool system_gain_control_activated = false;
  bool system_noise_suppression_activated = false;

  // Used for an experiment for forcing certain system-level
  // noise suppression functionalities to be off. In contrast to
  // `system_noise_suppression_activated` the system-level noise suppression
  // referred to does not correspond to something that can replace the browser
  // counterpart. I.e., the browser counterpart should be on, even if
  // `disable_hw_noise_suppression` is false.
  bool disable_hw_noise_suppression = false;

  bool goog_audio_mirroring = false;
  bool goog_auto_gain_control = true;
  bool goog_experimental_echo_cancellation =
#if defined(OS_ANDROID)
      false;
#else
      true;
#endif
  bool goog_noise_suppression = true;
  bool goog_experimental_noise_suppression = true;
  bool goog_highpass_filter = true;
  bool goog_experimental_auto_gain_control = true;
};

// Starts the echo cancellation dump in
// |audio_processing|. |worker_queue| must be kept alive until either
// |audio_processing| is destroyed, or
// StopEchoCancellationDump(audio_processing) is called.
PLATFORM_EXPORT void StartEchoCancellationDump(
    AudioProcessing* audio_processing,
    base::File aec_dump_file,
    rtc::TaskQueue* worker_queue);

// Stops the echo cancellation dump in |audio_processing|.
// This method has no impact if echo cancellation dump has not been started on
// |audio_processing|.
PLATFORM_EXPORT void StopEchoCancellationDump(
    AudioProcessing* audio_processing);

// WebRTC Hybrid AGC experiment parameters.
struct PLATFORM_EXPORT WebRtcHybridAgcParams {
  bool dry_run;
  int vad_reset_period_ms;
  int adjacent_speech_frames_threshold;
  float max_gain_change_db_per_second;
  float max_output_noise_level_dbfs;
  bool sse2_allowed;
  bool avx2_allowed;
  bool neon_allowed;
};

// WebRTC analog AGC clipping control parameters.
struct PLATFORM_EXPORT WebRtcAnalogAgcClippingControlParams {
  int mode;
  // Mode can be the following:
  // 0: Clipping event prediction,
  // 1: Adaptive step clipping peak prediction,
  // 2: Fixed step clipping peak prediction.

  int window_length;
  int reference_window_length;
  int reference_window_delay;
  float clipping_threshold;
  float crest_factor_margin;
  int clipped_level_step;
  float clipped_ratio_threshold;
  int clipped_wait_frames;
};

// Changes the automatic gain control configuration in `apm_config` if
// `properties.goog_auto_gain_control` or
// `properties.goog_experimental_auto_gain_control` are true. If both are true
// and `hybrid_agc_params` is specified, the hybrid AGC configuration will be
// used - i.e., analog AGC1 and adaptive digital AGC2.
// When `properties.goog_auto_gain_control` is true,
// `properties.goog_experimental_auto_gain_control` is false and
// `compression_gain_db` is specified, the AGC2 fixed digital controller is
// enabled.
// TODO(bugs.webrtc.org/7494): Clean up once hybrid AGC experiment finalized.
PLATFORM_EXPORT void ConfigAutomaticGainControl(
    const AudioProcessingProperties& properties,
    const absl::optional<WebRtcHybridAgcParams>& hybrid_agc_params,
    const absl::optional<WebRtcAnalogAgcClippingControlParams>&
        clipping_control_params,
    absl::optional<double> compression_gain_db,
    webrtc::AudioProcessing::Config& apm_config);

PLATFORM_EXPORT void PopulateApmConfig(
    AudioProcessing::Config* apm_config,
    const AudioProcessingProperties& properties,
    const absl::optional<std::string>& audio_processing_platform_config_json,
    absl::optional<double>* gain_control_compression_gain_db);

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_MEDIASTREAM_MEDIA_STREAM_AUDIO_PROCESSOR_OPTIONS_H_
