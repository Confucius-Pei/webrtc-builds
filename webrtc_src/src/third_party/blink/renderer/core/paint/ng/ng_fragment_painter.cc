// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/paint/ng/ng_fragment_painter.h"

#include "third_party/blink/renderer/core/layout/ng/ng_outline_utils.h"
#include "third_party/blink/renderer/core/paint/paint_info.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context_state_saver.h"
#include "third_party/blink/renderer/platform/graphics/paint/drawing_recorder.h"

namespace blink {

void NGFragmentPainter::PaintOutline(const PaintInfo& paint_info,
                                     const PhysicalOffset& paint_offset,
                                     const ComputedStyle& style_to_use) {
  const NGPhysicalBoxFragment& fragment = PhysicalFragment();
  DCHECK(NGOutlineUtils::HasPaintedOutline(style_to_use, fragment.GetNode()));
  Vector<PhysicalRect> outline_rects;
  fragment.AddSelfOutlineRects(
      paint_offset, style_to_use.OutlineRectsShouldIncludeBlockVisualOverflow(),
      &outline_rects);

  if (outline_rects.IsEmpty())
    return;

  const DisplayItemClient& display_item_client = GetDisplayItemClient();
  if (DrawingRecorder::UseCachedDrawingIfPossible(
          paint_info.context, display_item_client, paint_info.phase))
    return;

  IntRect visual_rect =
      PixelSnappedIntRect(UnionRectEvenIfEmpty(outline_rects));
  visual_rect.Inflate(style_to_use.OutlineOutsetExtent());
  DrawingRecorder recorder(paint_info.context, display_item_client,
                           paint_info.phase, visual_rect);
  PaintOutlineRects(paint_info, outline_rects, style_to_use);
}

void NGFragmentPainter::AddURLRectIfNeeded(const PaintInfo& paint_info,
                                           const PhysicalOffset& paint_offset) {
  DCHECK(paint_info.ShouldAddUrlMetadata());

  // TODO(layout-dev): Should use break token when NG has its own tree building.
  const NGPhysicalBoxFragment& fragment = PhysicalFragment();
  if (fragment.GetLayoutObject()->IsElementContinuation() ||
      fragment.Style().Visibility() != EVisibility::kVisible)
    return;

  Node* node = fragment.GetNode();
  if (!node || !node->IsLink())
    return;

  KURL url = To<Element>(node)->HrefURL();
  if (!url.IsValid())
    return;

  auto outline_rects = fragment.GetLayoutObject()->OutlineRects(
      paint_offset, NGOutlineType::kIncludeBlockVisualOverflow);
  IntRect rect = PixelSnappedIntRect(UnionRect(outline_rects));
  if (rect.IsEmpty())
    return;

  const DisplayItemClient& display_item_client = GetDisplayItemClient();
  if (DrawingRecorder::UseCachedDrawingIfPossible(
          paint_info.context, display_item_client,
          DisplayItem::kPrintedContentPDFURLRect))
    return;

  DrawingRecorder recorder(paint_info.context, display_item_client,
                           DisplayItem::kPrintedContentPDFURLRect);

  Document& document = fragment.GetLayoutObject()->GetDocument();
  if (url.HasFragmentIdentifier() &&
      EqualIgnoringFragmentIdentifier(url, document.BaseURL())) {
    String fragment_name = url.FragmentIdentifier();
    if (document.FindAnchor(fragment_name))
      paint_info.context.SetURLFragmentForRect(fragment_name, rect);
    return;
  }
  paint_info.context.SetURLForRect(url, rect);
}

}  // namespace blink
