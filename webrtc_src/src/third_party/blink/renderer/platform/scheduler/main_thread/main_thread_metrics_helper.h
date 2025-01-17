// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_SCHEDULER_MAIN_THREAD_MAIN_THREAD_METRICS_HELPER_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_SCHEDULER_MAIN_THREAD_MAIN_THREAD_METRICS_HELPER_H_

#include "base/time/time.h"
#include "components/scheduling_metrics/task_duration_metric_reporter.h"
#include "components/scheduling_metrics/total_duration_metric_reporter.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/platform/task_type.h"
#include "third_party/blink/renderer/platform/platform_export.h"
#include "third_party/blink/renderer/platform/scheduler/common/metrics_helper.h"
#include "third_party/blink/renderer/platform/scheduler/common/thread_load_tracker.h"
#include "third_party/blink/renderer/platform/scheduler/main_thread/main_thread_task_queue.h"
#include "third_party/blink/renderer/platform/scheduler/main_thread/use_case.h"
#include "third_party/blink/renderer/platform/scheduler/public/frame_status.h"
#include "third_party/blink/renderer/platform/scheduler/public/thread_type.h"

namespace blink {
namespace scheduler {

enum class MainThreadTaskLoadState;
class MainThreadTaskQueue;
class MainThreadSchedulerImpl;

enum class UkmRecordingStatus {
  kSuccess = 0,
  kErrorMissingFrame = 1,
  kErrorDetachedFrame = 2,
  kErrorMissingUkmRecorder = 3,

  kCount = 4,
};

// Helper class to take care of metrics on behalf of MainThreadScheduler.
// This class should be used only on the main thread.
class PLATFORM_EXPORT MainThreadMetricsHelper : public MetricsHelper {
 public:
  MainThreadMetricsHelper(MainThreadSchedulerImpl* main_thread_scheduler,
                          bool has_cpu_timing_for_each_task,
                          base::TimeTicks now,
                          bool renderer_backgrounded);
  MainThreadMetricsHelper(const MainThreadMetricsHelper&) = delete;
  MainThreadMetricsHelper& operator=(const MainThreadMetricsHelper&) = delete;
  ~MainThreadMetricsHelper();

  void RecordTaskMetrics(
      MainThreadTaskQueue* queue,
      const base::sequence_manager::Task& task,
      const base::sequence_manager::TaskQueue::TaskTiming& task_timing);
  void RecordTaskSliceMetrics(base::TimeTicks now);

  void OnRendererForegrounded(base::TimeTicks now);
  void OnRendererBackgrounded(base::TimeTicks now);
  void OnRendererShutdown(base::TimeTicks now);

  void OnSafepointEntered(base::TimeTicks now);
  void OnSafepointExited(base::TimeTicks now);

  void RecordMainThreadTaskLoad(base::TimeTicks time, double load);
  void RecordForegroundMainThreadTaskLoad(base::TimeTicks time, double load);
  void RecordBackgroundMainThreadTaskLoad(base::TimeTicks time, double load);

  void ResetForTest(base::TimeTicks now);

 private:
  using TaskDurationPerQueueTypeMetricReporter =
      scheduling_metrics::TaskDurationMetricReporter<
          MainThreadTaskQueue::QueueType>;

  void ReportLowThreadLoadForPageAlmostIdleSignal(int load_percentage);

  // Record metrics of only top-level tasks with safepoints.
  void RecordMetricsForTasksWithSafepoints(
      const base::sequence_manager::TaskQueue::TaskTiming& task_timing);

  MainThreadSchedulerImpl* main_thread_scheduler_;  // NOT OWNED

  // Set to true when OnRendererShutdown is called. Used to ensure that metrics
  // that need to cross IPC boundaries aren't sent, as they cause additional
  // useless tasks to be posted.
  bool renderer_shutting_down_;

  absl::optional<base::TimeTicks> last_reported_task_;

  ThreadLoadTracker main_thread_load_tracker_;
  ThreadLoadTracker background_main_thread_load_tracker_;
  ThreadLoadTracker foreground_main_thread_load_tracker_;

  using TaskDurationPerTaskTypeMetricReporter =
      scheduling_metrics::TaskDurationMetricReporter<TaskType>;

  TaskDurationPerTaskTypeMetricReporter per_task_type_duration_reporter_;

  // The next three reporters are used to report the duration per task type
  // split by renderer scheduler use case (check use_case.h for reference):
  // None, Loading, and User Input (aggregation of multiple input-handling
  // related use cases).
  TaskDurationPerTaskTypeMetricReporter
      no_use_case_per_task_type_duration_reporter_;
  TaskDurationPerTaskTypeMetricReporter
      loading_per_task_type_duration_reporter_;
  TaskDurationPerTaskTypeMetricReporter
      input_handling_per_task_type_duration_reporter_;

  TaskDurationPerTaskTypeMetricReporter
      foreground_per_task_type_duration_reporter_;
  TaskDurationPerTaskTypeMetricReporter
      background_per_task_type_duration_reporter_;
  TaskDurationPerTaskTypeMetricReporter
      background_after_fifth_minute_per_task_type_duration_reporter_;
  TaskDurationPerTaskTypeMetricReporter
      background_after_tenth_minute_per_task_type_duration_reporter_;

  scheduling_metrics::TotalDurationMetricReporter total_task_time_reporter_;

  MainThreadTaskLoadState main_thread_task_load_state_;

  base::TimeTicks current_task_slice_start_time_;

  // Number of safepoints during inside the current top-level tasks in which
  // cooperative scheduling had a chance to run a task (as we don't necessarily
  // run a task in each safepoint).
  int safepoints_in_current_toplevel_task_count_;
};

}  // namespace scheduler
}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_SCHEDULER_MAIN_THREAD_MAIN_THREAD_METRICS_HELPER_H_
