// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_NG_NG_SPACE_UTILS_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_NG_NG_SPACE_UTILS_H_

#include "base/memory/scoped_refptr.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/layout/ng/ng_layout_input_node.h"
#include "third_party/blink/renderer/platform/geometry/layout_unit.h"

namespace blink {

class ComputedStyle;
struct NGBfcOffset;

// Adjusts {@code offset} to the clearance line.
CORE_EXPORT bool AdjustToClearance(LayoutUnit clearance_offset,
                                   NGBfcOffset* offset);

// Calculate and set the available inline fallback size for orthogonal flow
// children. This size will be used if it's not resolvable via other means [1].
//
// TODO(mstensho): The spec [1] says to use the size of the nearest scrollport
// as constraint, if that's smaller than the initial containing block, but we
// haven't implemented that yet; we always just use the initial containing
// block size.
//
// [1] https://www.w3.org/TR/css-writing-modes-3/#orthogonal-auto
void SetOrthogonalFallbackInlineSizeIfNeeded(const ComputedStyle& parent_style,
                                             const NGLayoutInputNode child,
                                             NGConstraintSpaceBuilder* builder);

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_NG_NG_SPACE_UTILS_H_
