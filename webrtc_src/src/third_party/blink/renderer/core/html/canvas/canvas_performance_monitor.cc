// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/html/canvas/canvas_performance_monitor.h"

#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/trace_event/trace_event.h"
#include "third_party/blink/renderer/core/html/canvas/canvas_rendering_context.h"

namespace {

using base::TimeDelta;
using base::TimeTicks;
using blink::CanvasRenderingContext;
using blink::CanvasResourceProvider;

const char* const kHostTypeName_Canvas = ".Canvas";
const char* const kHostTypeName_OffscreenCanvas = ".OffscreenCanvas";

const char* const kContextTypeName_2D_Accelerated = ".2D.Accelerated";
const char* const kContextTypeName_2D_Unaccelerated = ".2D.Unaccelerated";
const char* const kContextTypeName_WebGL = ".WebGL";
const char* const kContextTypeName_WebGL2 = ".WebGL2";
const char* const kContextTypeName_WebGPU = ".WebGPU";
const char* const kContextTypeName_ImageBitmap = ".ImageBitmap";

const char* const kFilterName_All = ".All";
const char* const kFilterName_Animation = ".Animation";

const char* const kMeasurementName_RenderTaskDuration = ".RenderTaskDuration";
const char* const kMeasurementName_PartitionAlloc = ".PartitionAlloc";
const char* const kMeasurementName_BlinkGC = ".BlinkGC";

// The inverse of the probability that a given task will be measured.
// I.e. a value of X means that each task has a probability 1/X of being
// measured.
static constexpr int kSamplingProbabilityInv = 100;

// Encodes and decodes information about a CanvasRenderingContext as a
// 32-bit value.
class RenderingContextDescriptionCodec {
 public:
  explicit RenderingContextDescriptionCodec(const CanvasRenderingContext*);
  explicit RenderingContextDescriptionCodec(const uint32_t& key);

  bool IsOffscreen() const { return key_.get<IsOffscreenField>(); }
  bool IsAccelerated() const { return key_.get<IsAcceleratedField>(); }
  CanvasRenderingContext::ContextType ContextType() const;
  uint32_t GetKey() const { return key_.bits(); }
  bool IsValid() const { return is_valid_; }

  const char* GetHostTypeName() const;
  const char* GetContextTypeName() const;

 private:
  using Key = WTF::SingleThreadedBitField<uint32_t>;
  using IsOffscreenField = Key::DefineFirstValue<bool, 1>;
  using IsAcceleratedField = IsOffscreenField::DefineNextValue<bool, 1>;
  using ContextTypeField = IsAcceleratedField::DefineNextValue<uint32_t, 8>;
  using PaddingField = ContextTypeField::DefineNextValue<bool, 1>;

  Key key_;
  bool is_valid_;
};

RenderingContextDescriptionCodec::RenderingContextDescriptionCodec(
    const CanvasRenderingContext* context) {
  is_valid_ = context->Host() && context->Host()->ResourceProvider();
  if (!is_valid_)
    return;

  key_.set<IsOffscreenField>(context->Host()->IsOffscreenCanvas());
  key_.set<IsOffscreenField>(context->IsAccelerated());
  key_.set<ContextTypeField>(context->GetContextType());
  // The padding field ensures at least one bit is set in the key in order
  // to avoid a key == 0, which is not supported by WTF::HashSet
  key_.set<PaddingField>(true);
}

RenderingContextDescriptionCodec::RenderingContextDescriptionCodec(
    const uint32_t& key)
    : key_(key), is_valid_(true) {}

CanvasRenderingContext::ContextType
RenderingContextDescriptionCodec::ContextType() const {
  return static_cast<CanvasRenderingContext::ContextType>(
      key_.get<ContextTypeField>());
}

const char* RenderingContextDescriptionCodec::GetHostTypeName() const {
  return IsOffscreen() ? kHostTypeName_OffscreenCanvas : kHostTypeName_Canvas;
}

const char* RenderingContextDescriptionCodec::GetContextTypeName() const {
  switch (ContextType()) {
    case CanvasRenderingContext::kContext2D:
      return IsAccelerated() ? kContextTypeName_2D_Accelerated
                             : kContextTypeName_2D_Unaccelerated;
    case CanvasRenderingContext::kContextExperimentalWebgl:
    case CanvasRenderingContext::kContextWebgl:
      return kContextTypeName_WebGL;
    case CanvasRenderingContext::kContextWebgl2:
      return kContextTypeName_WebGL2;
    case CanvasRenderingContext::kContextGPUPresent:
      return kContextTypeName_WebGPU;
    case CanvasRenderingContext::kContextImageBitmap:
      return kContextTypeName_ImageBitmap;
    default:
      NOTREACHED();
      return "";
  }
}

}  // unnamed namespace

namespace blink {

void CanvasPerformanceMonitor::CurrentTaskDrawsToContext(
    CanvasRenderingContext* context) {
  if (!is_render_task_) {
    // The current task was not previously known to be a render task.

    Thread::Current()->AddTaskTimeObserver(this);
    is_render_task_ = true;

    // The logic of determining whether the current task is to be sampled by
    // the metrics must be executed exactly once per render task to avoid
    // sampling biases that would skew metrics for cases that render to multiple
    // canvases per render task.
    measure_current_task_ = !(task_counter_++ % kSamplingProbabilityInv);

    if (LIKELY(!measure_current_task_))
      return;

    call_type_ = CallType::kOther;
    if (context->Host()) {
      ExecutionContext* ec = context->Host()->GetTopExecutionContext();
      if (ec && ec->IsInRequestAnimationFrame()) {
        call_type_ = CallType::kAnimation;
      }
    }
    // TODO(crbug.com/1206028): Add support for CallType::kUserInput
  }

  if (LIKELY(!measure_current_task_))
    return;

  RenderingContextDescriptionCodec desc(context);

  if (LIKELY(desc.IsValid())) {
    rendering_context_descriptions_.insert(desc.GetKey());
  }
}

void CanvasPerformanceMonitor::WillProcessTask(TimeTicks start_time) {
  // If this method is ever called within Chrome, there's a serious
  // programming error somewhere.  If it is called in a unit test, it probably
  // means that either the failing test or a test that ran before it called
  // CanvasRenderingContext::DidDraw outside the scope of a task runner.
  // To resolve the problem, try calling this in the test's tear-down:
  // CanvasRenderingContext::GetCanvasPerformanceMonitor().ResetForTesting()
  NOTREACHED();
}

void CanvasPerformanceMonitor::RecordMetrics(TimeTicks start_time,
                                             TimeTicks end_time) {
  TRACE_EVENT0("blink", "CanvasPerformanceMonitor::RecordMetrics");
  TimeDelta elapsed_time = end_time - start_time;
  constexpr size_t kKiloByte = 1024;
  int partition_alloc_kb = WTF::Partitions::TotalActiveBytes() / kKiloByte;
  int blink_gc_alloc_kb = ProcessHeap::TotalAllocatedObjectSize() / kKiloByte;

  while (!rendering_context_descriptions_.IsEmpty()) {
    RenderingContextDescriptionCodec desc(
        rendering_context_descriptions_.TakeAny());

    // Note: We cannot use the UMA_HISTOGRAM_* macros here due to dynamic
    // naming. See comments at top of base/metrics/histogram_macros.h for more
    // info.
    WTF::String histogram_name_prefix =
        WTF::String("Blink") + desc.GetHostTypeName();
    WTF::String histogram_name_radical = WTF::String(desc.GetContextTypeName());

    // Render task duration metric for all render tasks.
    {
      WTF::String histogram_name = histogram_name_prefix +
                                   kMeasurementName_RenderTaskDuration +
                                   histogram_name_radical + kFilterName_All;
      base::UmaHistogramMicrosecondsTimes(histogram_name.Latin1(),
                                          elapsed_time);
    }

    // Render task duration metric for rAF callbacks only.
    if (call_type_ == CallType::kAnimation) {
      WTF::String histogram_name =
          histogram_name_prefix + kMeasurementName_RenderTaskDuration +
          histogram_name_radical + kFilterName_Animation;
      base::UmaHistogramMicrosecondsTimes(histogram_name.Latin1(),
                                          elapsed_time);
    }

    // PartitionAlloc heap size metric
    {
      WTF::String histogram_name = histogram_name_prefix +
                                   kMeasurementName_PartitionAlloc +
                                   histogram_name_radical;
      base::UmaHistogramMemoryKB(histogram_name.Latin1(), partition_alloc_kb);
    }

    // Blink garbage collected heap size metric
    {
      WTF::String histogram_name = histogram_name_prefix +
                                   kMeasurementName_BlinkGC +
                                   histogram_name_radical;
      base::UmaHistogramMemoryKB(histogram_name.Latin1(), blink_gc_alloc_kb);
    }
  }

}

void CanvasPerformanceMonitor::DidProcessTask(TimeTicks start_time,
                                              TimeTicks end_time) {
  DCHECK(is_render_task_);
  Thread::Current()->RemoveTaskTimeObserver(this);

  if (measure_current_task_)
    RecordMetrics(start_time, end_time);

  is_render_task_ = false;
  draw_flags_ = 0;
}

void CanvasPerformanceMonitor::ResetForTesting() {
  if (is_render_task_)
    Thread::Current()->RemoveTaskTimeObserver(this);
  is_render_task_ = false;
  draw_flags_ = 0;
  rendering_context_descriptions_.clear();
  call_type_ = CallType::kOther;
  task_counter_ = 0;
  measure_current_task_ = false;
}

}  // namespace blink
