// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/ng/ng_fieldset_layout_algorithm.h"

#include "third_party/blink/renderer/core/layout/layout_fieldset.h"
#include "third_party/blink/renderer/core/layout/ng/ng_block_break_token.h"
#include "third_party/blink/renderer/core/layout/ng/ng_block_layout_algorithm.h"
#include "third_party/blink/renderer/core/layout/ng/ng_constraint_space.h"
#include "third_party/blink/renderer/core/layout/ng/ng_constraint_space_builder.h"
#include "third_party/blink/renderer/core/layout/ng/ng_fragment.h"
#include "third_party/blink/renderer/core/layout/ng/ng_fragmentation_utils.h"
#include "third_party/blink/renderer/core/layout/ng/ng_layout_result.h"
#include "third_party/blink/renderer/core/layout/ng/ng_length_utils.h"
#include "third_party/blink/renderer/core/layout/ng/ng_out_of_flow_layout_part.h"
#include "third_party/blink/renderer/core/layout/ng/ng_physical_box_fragment.h"
#include "third_party/blink/renderer/core/layout/ng/ng_space_utils.h"

namespace blink {

namespace {

enum class LegendBlockAlignment {
  kStart,
  kCenter,
  kEnd,
};

// This function is very similar to BlockAlignment() in ng_length_utils.cc, but
// it supports text-align:left/center/right.
inline LegendBlockAlignment ComputeLegendBlockAlignment(
    const ComputedStyle& legend_style,
    const ComputedStyle& fieldset_style) {
  bool start_auto = legend_style.MarginStartUsing(fieldset_style).IsAuto();
  bool end_auto = legend_style.MarginEndUsing(fieldset_style).IsAuto();
  if (start_auto || end_auto) {
    if (start_auto) {
      return end_auto ? LegendBlockAlignment::kCenter
                      : LegendBlockAlignment::kEnd;
    }
    return LegendBlockAlignment::kStart;
  }
  const bool is_ltr = fieldset_style.IsLeftToRightDirection();
  switch (legend_style.GetTextAlign()) {
    case ETextAlign::kLeft:
      return is_ltr ? LegendBlockAlignment::kStart : LegendBlockAlignment::kEnd;
    case ETextAlign::kRight:
      return is_ltr ? LegendBlockAlignment::kEnd : LegendBlockAlignment::kStart;
    case ETextAlign::kCenter:
      return LegendBlockAlignment::kCenter;
    default:
      return LegendBlockAlignment::kStart;
  }
}

}  // namespace

NGFieldsetLayoutAlgorithm::NGFieldsetLayoutAlgorithm(
    const NGLayoutAlgorithmParams& params)
    : NGLayoutAlgorithm(params),
      writing_direction_(ConstraintSpace().GetWritingDirection()),
      consumed_block_size_(BreakToken() ? BreakToken()->ConsumedBlockSize()
                                        : LayoutUnit()) {
  DCHECK(params.fragment_geometry.scrollbar.IsEmpty());

  borders_ = container_builder_.Borders();
  padding_ = container_builder_.Padding();
  border_box_size_ = container_builder_.InitialBorderBoxSize();
}

scoped_refptr<const NGLayoutResult> NGFieldsetLayoutAlgorithm::Layout() {
  // Layout of a fieldset container consists of two parts: Create a child
  // fragment for the rendered legend (if any), and create a child fragment for
  // the fieldset contents anonymous box (if any).
  // Fieldset scrollbars and padding will not be applied to the fieldset
  // container itself, but rather to the fieldset contents anonymous child box.
  // The reason for this is that the rendered legend shouldn't be part of the
  // scrollport; the legend is essentially a part of the block-start border,
  // and should not scroll along with the actual fieldset contents. Since
  // scrollbars are handled by the anonymous child box, and since padding is
  // inside the scrollport, padding also needs to be handled by the anonymous
  // child.
  intrinsic_block_size_ =
      IsResumingLayout(BreakToken()) ? LayoutUnit() : borders_.block_start;

  NGBreakStatus break_status = LayoutChildren();
  if (break_status == NGBreakStatus::kNeedsEarlierBreak) {
    // We need to abort the layout. No fragment will be generated.
    return container_builder_.Abort(NGLayoutResult::kNeedsEarlierBreak);
  }

  intrinsic_block_size_ = ClampIntrinsicBlockSize(
      ConstraintSpace(), Node(), BorderScrollbarPadding(),
      intrinsic_block_size_ + borders_.block_end);

  // Recompute the block-axis size now that we know our content size.
  border_box_size_.block_size =
      ComputeBlockSizeForFragment(ConstraintSpace(), Style(), BorderPadding(),
                                  intrinsic_block_size_ + consumed_block_size_,
                                  border_box_size_.inline_size);

  // The above computation utility knows nothing about fieldset weirdness. The
  // legend may eat from the available content box block size. Make room for
  // that if necessary.
  // Note that in size containment, we have to consider sizing as if we have no
  // contents, with the conjecture being that legend is part of the contents.
  // Thus, only do this adjustment if we do not contain size.
  if (!Node().ShouldApplyBlockSizeContainment()) {
    // Similar to how we add the consumed block size to the intrinsic
    // block size when calculating border_box_size_.block_size, we also need to
    // do so when the fieldset is adjusted to encompass the legend.
    border_box_size_.block_size =
        std::max(border_box_size_.block_size,
                 minimum_border_box_block_size_ + consumed_block_size_);
  }

  // TODO(almaher): end border and padding may overflow the parent
  // fragmentainer, and we should avoid that.
  LayoutUnit all_fragments_block_size = border_box_size_.block_size;

  container_builder_.SetIntrinsicBlockSize(intrinsic_block_size_);
  container_builder_.SetFragmentsTotalBlockSize(all_fragments_block_size);
  container_builder_.SetIsFieldsetContainer();

  if (ConstraintSpace().HasBlockFragmentation()) {
    FinishFragmentation(Node(), ConstraintSpace(), borders_.block_end,
                        FragmentainerSpaceAtBfcStart(ConstraintSpace()),
                        &container_builder_);
  }

  NGOutOfFlowLayoutPart(Node(), ConstraintSpace(), &container_builder_).Run();

  const auto& style = Style();
  if (style.LogicalHeight().IsPercentOrCalc() ||
      style.LogicalMinHeight().IsPercentOrCalc() ||
      style.LogicalMaxHeight().IsPercentOrCalc()) {
    // The height of the fieldset content box depends on the percent-height of
    // the fieldset. So we should assume the fieldset has a percent-height
    // descendant.
    container_builder_.SetHasDescendantThatDependsOnPercentageBlockSize();
  }

  return container_builder_.ToBoxFragment();
}

NGBreakStatus NGFieldsetLayoutAlgorithm::LayoutChildren() {
  scoped_refptr<const NGBlockBreakToken> content_break_token;
  bool has_seen_all_children = false;
  if (const auto* token = BreakToken()) {
    const auto child_tokens = token->ChildBreakTokens();
    if (wtf_size_t break_token_count = child_tokens.size()) {
      scoped_refptr<const NGBlockBreakToken> child_token =
          To<NGBlockBreakToken>(child_tokens[0]);
      if (child_token) {
        DCHECK(!child_token->InputNode().IsRenderedLegend());
        content_break_token = child_token;
      }
      // There shouldn't be any additional break tokens.
      DCHECK_EQ(child_tokens.size(), 1u);
    }
    if (token->HasSeenAllChildren()) {
      container_builder_.SetHasSeenAllChildren();
      has_seen_all_children = true;
    }
  }

  LogicalSize adjusted_padding_box_size =
      ShrinkLogicalSize(border_box_size_, borders_);

  NGBlockNode legend = Node().GetRenderedLegend();
  if (legend) {
    if (!IsResumingLayout(BreakToken()))
      LayoutLegend(legend);
    // The legend may eat from the available content box block size. Calculate
    // the minimum block size needed to encompass the legend.
    if (!Node().ShouldApplyBlockSizeContainment() &&
        !IsResumingLayout(content_break_token.get())) {
      minimum_border_box_block_size_ =
          intrinsic_block_size_ + padding_.BlockSum() + borders_.block_end;
    }

    if (adjusted_padding_box_size.block_size != kIndefiniteSize) {
      DCHECK_NE(border_box_size_.block_size, kIndefiniteSize);
      LayoutUnit legend_size_contribution;
      if (IsResumingLayout(BreakToken())) {
        // The legend has been laid out in previous fragments, and
        // adjusted_padding_box_size will need to be adjusted further to account
        // for block size taken up by the legend.
        //
        // To calculate its size contribution to the border block-start area,
        // take the difference between the previously consumed block-size of the
        // fieldset excluding its specified block-start border, and the consumed
        // block-size of the contents wrapper.
        LayoutUnit content_consumed_block_size =
            content_break_token ? content_break_token->ConsumedBlockSize()
                                : LayoutUnit();
        legend_size_contribution = consumed_block_size_ - borders_.block_start -
                                   content_consumed_block_size;
      } else {
        // We're at the first fragment. The current layout position
        // (intrinsic_block_size_) is at the outer block-end edge of the legend
        // or just after the block-start border, whichever is larger.
        legend_size_contribution = intrinsic_block_size_ - borders_.block_start;
      }

      adjusted_padding_box_size.block_size = std::max(
          adjusted_padding_box_size.block_size - legend_size_contribution,
          padding_.BlockSum());
    }
  }

  // Proceed with normal fieldset children (excluding the rendered legend). They
  // all live inside an anonymous child box of the fieldset container.
  auto fieldset_content = Node().GetFieldsetContent();
  if (fieldset_content && (content_break_token || !has_seen_all_children)) {
    NGBreakStatus break_status =
        LayoutFieldsetContent(fieldset_content, content_break_token,
                              adjusted_padding_box_size, !!legend);
    if (break_status == NGBreakStatus::kNeedsEarlierBreak)
      return break_status;
  }

  if (!fieldset_content) {
    container_builder_.SetHasSeenAllChildren();
    // There was no anonymous child to provide the padding, so we have to add it
    // ourselves.
    intrinsic_block_size_ += padding_.BlockSum();
  }

  return NGBreakStatus::kContinue;
}

void NGFieldsetLayoutAlgorithm::LayoutLegend(NGBlockNode& legend) {
  // Lay out the legend. While the fieldset container normally ignores its
  // padding, the legend is laid out within what would have been the content
  // box had the fieldset been a regular block with no weirdness.
  LogicalSize percentage_size = CalculateChildPercentageSize(
      ConstraintSpace(), Node(), ChildAvailableSize());
  NGBoxStrut legend_margins =
      ComputeMarginsFor(legend.Style(), percentage_size.inline_size,
                        ConstraintSpace().GetWritingDirection());

  auto legend_space = CreateConstraintSpaceForLegend(
      legend, ChildAvailableSize(), percentage_size);
  scoped_refptr<const NGLayoutResult> result =
      legend.Layout(legend_space, BreakToken());

  // Legends are monolithic, so abortions are not expected.
  DCHECK_EQ(result->Status(), NGLayoutResult::kSuccess);

  const auto& physical_fragment = result->PhysicalFragment();

  LayoutUnit legend_border_box_block_size =
      NGFragment(writing_direction_, physical_fragment).BlockSize();
  LayoutUnit legend_margin_box_block_size = legend_margins.block_start +
                                            legend_border_box_block_size +
                                            legend_margins.block_end;

  LayoutUnit space_left = borders_.block_start - legend_border_box_block_size;
  LayoutUnit block_offset;
  if (space_left > LayoutUnit()) {
    // https://html.spec.whatwg.org/C/#the-fieldset-and-legend-elements
    // * The element is expected to be positioned in the block-flow direction
    //   such that its border box is centered over the border on the
    //   block-start side of the fieldset element.
    block_offset += space_left / 2;
  }
  // If the border is smaller than the block end offset of the legend margin
  // box, intrinsic_block_size_ should now be based on the the block end
  // offset of the legend margin box instead of the border.
  LayoutUnit legend_margin_end_offset =
      block_offset + legend_margin_box_block_size - legend_margins.block_start;
  if (legend_margin_end_offset > borders_.block_start) {
    intrinsic_block_size_ = legend_margin_end_offset;

    is_legend_past_border_ = true;
  }

  // If the margin box of the legend is at least as tall as the fieldset
  // block-start border width, it will start at the block-start border edge
  // of the fieldset. As a paint effect, the block-start border will be
  // pushed so that the center of the border will be flush with the center
  // of the border-box of the legend.

  LayoutUnit legend_inline_start = ComputeLegendInlineOffset(
      legend.Style(),
      NGFragment(writing_direction_, result->PhysicalFragment()).InlineSize(),
      legend_margins, Style(), BorderScrollbarPadding().inline_start,
      ChildAvailableSize().inline_size);
  LogicalOffset legend_offset = {legend_inline_start, block_offset};

  container_builder_.AddResult(*result, legend_offset);
}

LayoutUnit NGFieldsetLayoutAlgorithm::ComputeLegendInlineOffset(
    const ComputedStyle& legend_style,
    LayoutUnit legend_border_box_inline_size,
    const NGBoxStrut& legend_margins,
    const ComputedStyle& fieldset_style,
    LayoutUnit fieldset_border_padding_inline_start,
    LayoutUnit fieldset_content_inline_size) {
  LayoutUnit legend_inline_start =
      fieldset_border_padding_inline_start + legend_margins.inline_start;
  // The following logic is very similar to ResolveInlineMargins(), but it uses
  // ComputeLegendBlockAlignment().
  const LayoutUnit available_space =
      fieldset_content_inline_size - legend_border_box_inline_size;
  if (available_space > LayoutUnit()) {
    auto alignment = ComputeLegendBlockAlignment(legend_style, fieldset_style);
    if (alignment == LegendBlockAlignment::kCenter)
      legend_inline_start += available_space / 2;
    else if (alignment == LegendBlockAlignment::kEnd)
      legend_inline_start += available_space - legend_margins.inline_end;
  }
  return legend_inline_start;
}

NGBreakStatus NGFieldsetLayoutAlgorithm::LayoutFieldsetContent(
    NGBlockNode& fieldset_content,
    scoped_refptr<const NGBlockBreakToken> content_break_token,
    LogicalSize adjusted_padding_box_size,
    bool has_legend) {
  // If the following conditions meet, the content should be laid out with
  // a block-size limitation:
  // - The FIELDSET block-size is indefinite.
  // - It has max-block-size.
  // - The intrinsic block-size of the content is larger than the
  //   max-block-size.
  if (adjusted_padding_box_size.block_size == kIndefiniteSize) {
    LayoutUnit max_content_block_size =
        ResolveMaxBlockLength(ConstraintSpace(), Style(), BorderPadding(),
                              Style().LogicalMaxHeight());
    if (max_content_block_size != LayoutUnit::Max()) {
      max_content_block_size -= BorderPadding().BlockSum();

      auto child_measure_space = CreateConstraintSpaceForFieldsetContent(
          fieldset_content, adjusted_padding_box_size, intrinsic_block_size_,
          NGCacheSlot::kMeasure);
      LayoutUnit intrinsic_content_block_size =
          fieldset_content
              .Layout(child_measure_space, content_break_token.get())
              ->IntrinsicBlockSize();
      if (intrinsic_content_block_size > max_content_block_size)
        adjusted_padding_box_size.block_size = max_content_block_size;
    }
  }
  auto child_space = CreateConstraintSpaceForFieldsetContent(
      fieldset_content, adjusted_padding_box_size, intrinsic_block_size_,
      NGCacheSlot::kLayout);
  auto result = fieldset_content.Layout(child_space, content_break_token.get());

  NGBreakStatus break_status = NGBreakStatus::kContinue;
  if (ConstraintSpace().HasBlockFragmentation()) {
    bool has_container_separation = is_legend_past_border_;
    // TODO(almaher): The legend should be treated as out-of-flow.
    break_status = BreakBeforeChildIfNeeded(
        ConstraintSpace(), fieldset_content, *result.get(),
        ConstraintSpace().FragmentainerOffsetAtBfc() + intrinsic_block_size_,
        has_container_separation, &container_builder_);
  }

  if (break_status == NGBreakStatus::kContinue) {
    DCHECK_EQ(result->Status(), NGLayoutResult::kSuccess);
    LogicalOffset offset(borders_.inline_start, intrinsic_block_size_);
    container_builder_.AddResult(*result, offset);
    intrinsic_block_size_ +=
        NGFragment(writing_direction_, result->PhysicalFragment()).BlockSize();
    container_builder_.SetHasSeenAllChildren();
  }

  return break_status;
}

bool NGFieldsetLayoutAlgorithm::IsFragmentainerOutOfSpace(
    LayoutUnit block_offset) const {
  if (!ConstraintSpace().HasKnownFragmentainerBlockSize())
    return false;
  return block_offset >= FragmentainerSpaceAtBfcStart(ConstraintSpace());
}

MinMaxSizesResult NGFieldsetLayoutAlgorithm::ComputeMinMaxSizes(
    const MinMaxSizesFloatInput&) const {
  MinMaxSizesResult result;

  bool has_inline_size_containment = Node().ShouldApplyInlineSizeContainment();
  if (has_inline_size_containment) {
    // Size containment does not consider the legend for sizing.
    absl::optional<MinMaxSizesResult> result_without_children =
        CalculateMinMaxSizesIgnoringChildren(Node(), BorderScrollbarPadding());
    if (result_without_children)
      return *result_without_children;
  } else {
    if (NGBlockNode legend = Node().GetRenderedLegend()) {
      NGMinMaxConstraintSpaceBuilder builder(ConstraintSpace(), Style(), legend,
                                             /* is_new_fc */ true);
      builder.SetAvailableBlockSize(kIndefiniteSize);
      const auto space = builder.ToConstraintSpace();

      result = ComputeMinAndMaxContentContribution(Style(), legend, space);
      result.sizes += ComputeMinMaxMargins(Style(), legend).InlineSum();
    }
  }

  // The fieldset content includes the fieldset padding (and any scrollbars),
  // while the legend is a regular child and doesn't. We may have a fieldset
  // without any content or legend, so add the padding here, on the outside.
  result.sizes += ComputePadding(ConstraintSpace(), Style()).InlineSum();

  // Size containment does not consider the content for sizing.
  if (!has_inline_size_containment) {
    if (NGBlockNode content = Node().GetFieldsetContent()) {
      NGMinMaxConstraintSpaceBuilder builder(ConstraintSpace(), Style(),
                                             content,
                                             /* is_new_fc */ true);
      builder.SetAvailableBlockSize(kIndefiniteSize);
      const auto space = builder.ToConstraintSpace();

      MinMaxSizesResult content_result =
          ComputeMinAndMaxContentContribution(Style(), content, space);
      content_result.sizes +=
          ComputeMinMaxMargins(Style(), content).InlineSum();
      result.sizes.Encompass(content_result.sizes);
      result.depends_on_block_constraints |=
          content_result.depends_on_block_constraints;
    }
  }

  result.sizes += ComputeBorders(ConstraintSpace(), Node()).InlineSum();
  return result;
}

const NGConstraintSpace
NGFieldsetLayoutAlgorithm::CreateConstraintSpaceForLegend(
    NGBlockNode legend,
    LogicalSize available_size,
    LogicalSize percentage_size) {
  NGConstraintSpaceBuilder builder(ConstraintSpace(),
                                   legend.Style().GetWritingDirection(),
                                   /* is_new_fc */ true);
  SetOrthogonalFallbackInlineSizeIfNeeded(Style(), legend, &builder);

  builder.SetAvailableSize(available_size);
  builder.SetPercentageResolutionSize(percentage_size);
  return builder.ToConstraintSpace();
}

const NGConstraintSpace
NGFieldsetLayoutAlgorithm::CreateConstraintSpaceForFieldsetContent(
    NGBlockNode fieldset_content,
    LogicalSize padding_box_size,
    LayoutUnit block_offset,
    NGCacheSlot slot) {
  DCHECK(fieldset_content.CreatesNewFormattingContext());
  NGConstraintSpaceBuilder builder(
      ConstraintSpace(), fieldset_content.Style().GetWritingDirection(),
      /* is_new_fc */ true);
  builder.SetCacheSlot(slot);
  builder.SetAvailableSize(padding_box_size);
  builder.SetInlineAutoBehavior(NGAutoBehavior::kStretchImplicit);
  // We pass the container's PercentageResolutionSize because percentage
  // padding for the fieldset content should be computed as they are in
  // the container.
  //
  // https://html.spec.whatwg.org/C/#anonymous-fieldset-content-box
  // > * For the purpose of calculating percentage padding, act as if the
  // >   padding was calculated for the fieldset element.
  builder.SetPercentageResolutionSize(
      ConstraintSpace().PercentageResolutionSize());
  builder.SetIsFixedBlockSize(padding_box_size.block_size != kIndefiniteSize);

  if (ConstraintSpace().HasBlockFragmentation()) {
    SetupSpaceBuilderForFragmentation(ConstraintSpace(), fieldset_content,
                                      block_offset, &builder,
                                      /* is_new_fc */ true);
  }
  return builder.ToConstraintSpace();
}

}  // namespace blink
