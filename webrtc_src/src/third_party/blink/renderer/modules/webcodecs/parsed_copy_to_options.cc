// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/webcodecs/parsed_copy_to_options.h"

#include <algorithm>

#include "base/numerics/checked_math.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_plane_layout.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_video_frame_copy_to_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_video_frame_rect.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"

namespace blink {

ParsedCopyToOptions::ParsedCopyToOptions(VideoFrameCopyToOptions* options,
                                         media::VideoPixelFormat format,
                                         const gfx::Size& coded_size,
                                         const gfx::Rect& visible_rect,
                                         ExceptionState& exception_state)
    : num_planes(
          static_cast<wtf_size_t>(media::VideoFrame::NumPlanes(format))) {
  uint32_t coded_width = static_cast<uint32_t>(coded_size.width());
  uint32_t coded_height = static_cast<uint32_t>(coded_size.height());

  // Parse |rect|
  gfx::Rect rect = visible_rect;
  if (options->hasRect()) {
    uint32_t left = options->rect()->left();
    uint32_t top = options->rect()->top();
    uint32_t width = options->rect()->width();
    uint32_t height = options->rect()->height();

    // Implicitly checks that left <= kMaxDimension.
    if (left >= coded_width) {
      exception_state.ThrowDOMException(
          DOMExceptionCode::kConstraintError,
          String::Format("Invalid rect.left %u with codedWidth %u.", left,
                         coded_width));
      return;
    }

    // If left and width are <= kMaxDimension then their sum will not overflow.
    if (width > coded_width || left + width > coded_width) {
      exception_state.ThrowDOMException(
          DOMExceptionCode::kConstraintError,
          String::Format("Invalid rect.width %u with rect.left %u and "
                         "codedWidth %u.",
                         width, left, coded_width));
      return;
    }

    // Implicitly checks that top <= kMaxDimension.
    if (top >= coded_height) {
      exception_state.ThrowDOMException(
          DOMExceptionCode::kConstraintError,
          String::Format("Invalid rect.top %u with codedHeight %u.", top,
                         coded_height));
      return;
    }

    // If top and height are <= kMaxDimension then their sum will not overflow.
    if (height > coded_height || top + height > coded_height) {
      exception_state.ThrowDOMException(
          DOMExceptionCode::kConstraintError,
          String::Format("Invalid rect.height %u with rect.top %u and "
                         "codedHeight %u.",
                         height, top, coded_height));
      return;
    }

    rect = gfx::Rect(left, top, width, height);
  }

  // Rect must be non-empty.
  if (rect.IsEmpty()) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kConstraintError,
        String::Format("Invalid rect with width %d and height %d. Rect must "
                       "have positive area.",
                       rect.width(), rect.height()));
    return;
  }

  // Rect must be sample-aligned.
  // TODO(crbug.com/1205166): media::VideoFrame does not enforce that visible
  // rects are sample-aligned, so we may have to deal with this case somehow.
  // Options:
  //   - Crop VideoFrame.visibleRect to sample boundaries and use that.
  //     (May result in differences between rendering paths.)
  //   - Expand or contract the crop to sample boundaries, potentially
  //     per-plane.
  //   - Enforce this restriction on media::VideoFrame and see if anything
  //     breaks.
  for (wtf_size_t i = 0; i < num_planes; i++) {
    gfx::Size sample_size = media::VideoFrame::SampleSize(format, i);
    if (rect.x() % sample_size.width() != 0) {
      exception_state.ThrowDOMException(
          DOMExceptionCode::kConstraintError,
          String::Format("rect.left %d is not sample-aligned in plane %u.",
                         rect.x(), i));
      return;
    } else if (rect.width() % sample_size.width() != 0) {
      exception_state.ThrowDOMException(
          DOMExceptionCode::kConstraintError,
          String::Format("rect.width %d is not sample-aligned in plane %u.",
                         rect.width(), i));
      return;
    } else if (rect.y() % sample_size.height() != 0) {
      exception_state.ThrowDOMException(
          DOMExceptionCode::kConstraintError,
          String::Format("rect.top %d is not sample-aligned in plane %u.",
                         rect.y(), i));
      return;
    } else if (rect.height() % sample_size.height() != 0) {
      exception_state.ThrowDOMException(
          DOMExceptionCode::kConstraintError,
          String::Format("rect.height %d is not sample-aligned in plane %u.",
                         rect.height(), i));
      return;
    }
  }

  // Parse |layout|.
  bool has_explicit_layout = options->hasLayout();
  if (has_explicit_layout) {
    if (options->layout().size() != num_planes) {
      exception_state.ThrowDOMException(
          DOMExceptionCode::kConstraintError,
          String::Format("Invalid layout. Expected %u planes, found %u.",
                         num_planes, options->layout().size()));
      return;
    }

    for (wtf_size_t i = 0; i < num_planes; i++) {
      planes[i].offset = options->layout()[i]->offset();
      planes[i].stride = options->layout()[i]->stride();
    }
  }

  // Compute the resulting layout.
  uint32_t end_offset[media::VideoFrame::kMaxPlanes] = {0};
  for (wtf_size_t i = 0; i < num_planes; i++) {
    gfx::Size sample_size = media::VideoFrame::SampleSize(format, i);
    uint32_t sample_bytes = media::VideoFrame::BytesPerElement(format, i);

    planes[i].top = rect.y() / sample_size.height();
    planes[i].height = rect.height() / sample_size.height();
    planes[i].left_bytes = rect.x() / sample_size.width() * sample_bytes;
    planes[i].width_bytes = rect.width() / sample_size.width() * sample_bytes;

    // If an explicit layout was not provided, planes and rows are tightly
    // packed.
    if (!has_explicit_layout) {
      planes[i].offset = min_buffer_size;
      planes[i].stride = planes[i].width_bytes;
    } else {
      if (planes[i].stride < planes[i].width_bytes) {
        exception_state.ThrowDOMException(
            DOMExceptionCode::kConstraintError,
            String::Format("Invalid layout, plane %u must have stride at least "
                           "%u, found %u.",
                           i, planes[i].width_bytes, planes[i].stride));
        return;
      }
    }

    // Note: this calculation implies that the whole stride is allocated, even
    // on the last row.
    const auto plane_size =
        base::CheckedNumeric<uint32_t>(planes[i].stride) * planes[i].height;
    if (!plane_size.IsValid()) {
      exception_state.ThrowDOMException(
          DOMExceptionCode::kConstraintError,
          String::Format("Invalid layout, plane %u with stride %u is too "
                         "large.",
                         i, planes[i].stride));
      return;
    }
    const auto plane_end = plane_size + planes[i].offset;
    if (!plane_end.IsValid()) {
      exception_state.ThrowDOMException(
          DOMExceptionCode::kConstraintError,
          String::Format("Invalid layout, plane %u with offset %u and stride "
                         "%u exceeds bounds.",
                         i, planes[i].offset, planes[i].stride));
      return;
    }
    end_offset[i] = plane_end.ValueOrDie();
    min_buffer_size = std::max(min_buffer_size, end_offset[i]);

    // Verify that planes do not overlap. Only happens with explicit layouts.
    for (wtf_size_t j = 0; j < i; j++) {
      // If plane A ends before plane B starts, they do not overlap.
      if (end_offset[i] <= planes[j].offset ||
          end_offset[j] <= planes[i].offset) {
        continue;
      }
      DCHECK(has_explicit_layout);
      exception_state.ThrowDOMException(
          DOMExceptionCode::kConstraintError,
          String::Format("Invalid layout, plane %u overlaps with plane %u.", i,
                         j));
      return;
    }
  }
}

}  // namespace blink
