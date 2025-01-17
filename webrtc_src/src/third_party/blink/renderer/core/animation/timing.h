/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_ANIMATION_TIMING_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_ANIMATION_TIMING_H_

#include "base/memory/scoped_refptr.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/renderer/core/animation/animation_time_delta.h"
#include "third_party/blink/renderer/core/style/data_equivalency.h"
#include "third_party/blink/renderer/platform/animation/compositor_keyframe_model.h"
#include "third_party/blink/renderer/platform/animation/timing_function.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/blink/renderer/platform/wtf/math_extras.h"

namespace blink {

class EffectTiming;
class ComputedEffectTiming;
enum class TimelinePhase;

struct CORE_EXPORT Timing {
  USING_FAST_MALLOC(Timing);

 public:
  // Note that logic in CSSAnimations depends on the order of these values.
  enum Phase {
    kPhaseBefore,
    kPhaseActive,
    kPhaseAfter,
    kPhaseNone,
  };
  // Represents the animation direction from the Web Animations spec, see
  // https://drafts.csswg.org/web-animations-1/#animation-direction.
  enum class AnimationDirection {
    kForwards,
    kBackwards,
  };

  // Timing properties set via AnimationEffect.updateTiming override their
  // corresponding CSS properties.
  enum AnimationTimingOverride {
    kOverrideNode = 0,
    kOverrideDirection = 1,
    kOverrideDuration = 1 << 1,
    kOverrideEndDelay = 1 << 2,
    kOverideFillMode = 1 << 3,
    kOverrideIterationCount = 1 << 4,
    kOverrideIterationStart = 1 << 5,
    kOverrideStartDelay = 1 << 6,
    kOverrideTimingFunction = 1 << 7,
    kOverrideAll = (1 << 8) - 1
  };

  using FillMode = CompositorKeyframeModel::FillMode;
  using PlaybackDirection = CompositorKeyframeModel::Direction;

  static double NullValue() { return std::numeric_limits<double>::quiet_NaN(); }

  static String FillModeString(FillMode);
  static FillMode StringToFillMode(const String&);
  static String PlaybackDirectionString(PlaybackDirection);

  Timing() = default;

  void AssertValid() const {
    DCHECK(!start_delay.is_inf());
    DCHECK(!end_delay.is_inf());
    DCHECK(std::isfinite(iteration_start));
    DCHECK_GE(iteration_start, 0);
    DCHECK_GE(iteration_count, 0);
    DCHECK(!iteration_duration ||
           iteration_duration.value() >= AnimationTimeDelta());
    DCHECK(timing_function);
  }

  // https://drafts.csswg.org/web-animations-1/#iteration-duration
  AnimationTimeDelta IterationDuration() const;

  // https://drafts.csswg.org/web-animations-1/#active-duration
  AnimationTimeDelta ActiveDuration() const;
  AnimationTimeDelta EndTimeInternal() const;

  Timing::FillMode ResolvedFillMode(bool is_animation) const;
  EffectTiming* ConvertToEffectTiming() const;

  bool operator==(const Timing& other) const {
    return start_delay == other.start_delay && end_delay == other.end_delay &&
           fill_mode == other.fill_mode &&
           iteration_start == other.iteration_start &&
           iteration_count == other.iteration_count &&
           iteration_duration == other.iteration_duration &&
           direction == other.direction &&
           DataEquivalent(timing_function.get(), other.timing_function.get());
  }

  bool operator!=(const Timing& other) const { return !(*this == other); }

  // Explicit changes to animation timing through the web animations API,
  // override timing changes due to CSS style.
  void SetTimingOverride(AnimationTimingOverride override) {
    timing_overrides |= override;
  }
  bool HasTimingOverride(AnimationTimingOverride override) {
    return timing_overrides & override;
  }
  bool HasTimingOverrides() { return timing_overrides != kOverrideNode; }

  AnimationTimeDelta start_delay;
  AnimationTimeDelta end_delay;
  FillMode fill_mode = FillMode::AUTO;
  double iteration_start = 0;
  double iteration_count = 1;
  // If empty, indicates the 'auto' value.
  absl::optional<AnimationTimeDelta> iteration_duration = absl::nullopt;

  PlaybackDirection direction = PlaybackDirection::NORMAL;
  scoped_refptr<TimingFunction> timing_function =
      LinearTimingFunction::Shared();
  // Mask of timing attributes that are set by calls to
  // AnimationEffect.updateTiming. Once set, these attributes ignore changes
  // based on the CSS style.
  uint16_t timing_overrides = kOverrideNode;

  struct CalculatedTiming {
    DISALLOW_NEW();
    Phase phase = Phase::kPhaseNone;
    absl::optional<double> current_iteration = 0;
    absl::optional<double> progress = 0;
    bool is_current = false;
    bool is_in_effect = false;
    bool is_in_play = false;
    absl::optional<AnimationTimeDelta> local_time;
    AnimationTimeDelta time_to_forwards_effect_change =
        AnimationTimeDelta::Max();
    AnimationTimeDelta time_to_reverse_effect_change =
        AnimationTimeDelta::Max();
    AnimationTimeDelta time_to_next_iteration = AnimationTimeDelta::Max();
  };

  CalculatedTiming CalculateTimings(
      absl::optional<AnimationTimeDelta> local_time,
      absl::optional<Phase> timeline_phase,
      AnimationDirection animation_direction,
      bool is_keyframe_effect,
      absl::optional<double> playback_rate) const;
  ComputedEffectTiming* getComputedTiming(const CalculatedTiming& calculated,
                                          bool is_keyframe_effect) const;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_ANIMATION_TIMING_H_
