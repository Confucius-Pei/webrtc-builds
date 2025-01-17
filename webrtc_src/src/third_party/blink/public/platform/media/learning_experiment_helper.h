// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_PUBLIC_PLATFORM_MEDIA_LEARNING_EXPERIMENT_HELPER_H_
#define THIRD_PARTY_BLINK_PUBLIC_PLATFORM_MEDIA_LEARNING_EXPERIMENT_HELPER_H_

#include <memory>

#include "base/macros.h"
#include "media/learning/common/feature_dictionary.h"
#include "media/learning/common/labelled_example.h"
#include "media/learning/common/learning_task.h"
#include "media/learning/common/learning_task_controller.h"
#include "third_party/blink/public/platform/web_common.h"

namespace media {

// Helper for adding a learning experiment to existing code.
class BLINK_PLATFORM_EXPORT LearningExperimentHelper {
 public:
  // If |controller| is null, then everything else no-ops.
  LearningExperimentHelper(
      std::unique_ptr<learning::LearningTaskController> controller);

  // Cancels any existing observation.
  ~LearningExperimentHelper();

  // Start a new observation.  Any existing observation is cancelled.  Does
  // nothing if there's no controller.
  void BeginObservation(const learning::FeatureDictionary& dictionary);

  // Complete any pending observation.  Does nothing if none is in progress.
  void CompleteObservationIfNeeded(const learning::TargetValue& target);

  // Cancel any pending observation.
  void CancelObservationIfNeeded();

 private:
  // May be null.
  std::unique_ptr<learning::LearningTaskController> controller_;

  // May be null if no observation is in flight.  Must be null if |controller_|
  // is null.
  base::UnguessableToken observation_id_;

  DISALLOW_COPY_AND_ASSIGN(LearningExperimentHelper);
};

}  // namespace media

#endif  // THIRD_PARTY_BLINK_PUBLIC_PLATFORM_MEDIA_LEARNING_EXPERIMENT_HELPER_H_
