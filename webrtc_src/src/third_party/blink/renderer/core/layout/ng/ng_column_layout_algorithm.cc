// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/ng/ng_column_layout_algorithm.h"

#include <algorithm>
#include "third_party/blink/renderer/core/layout/geometry/logical_size.h"
#include "third_party/blink/renderer/core/layout/geometry/writing_mode_converter.h"
#include "third_party/blink/renderer/core/layout/ng/geometry/ng_fragment_geometry.h"
#include "third_party/blink/renderer/core/layout/ng/geometry/ng_margin_strut.h"
#include "third_party/blink/renderer/core/layout/ng/list/ng_unpositioned_list_marker.h"
#include "third_party/blink/renderer/core/layout/ng/ng_block_layout_algorithm.h"
#include "third_party/blink/renderer/core/layout/ng/ng_box_fragment.h"
#include "third_party/blink/renderer/core/layout/ng/ng_constraint_space_builder.h"
#include "third_party/blink/renderer/core/layout/ng/ng_fragmentation_utils.h"
#include "third_party/blink/renderer/core/layout/ng/ng_length_utils.h"
#include "third_party/blink/renderer/core/layout/ng/ng_out_of_flow_layout_part.h"
#include "third_party/blink/renderer/core/layout/ng/ng_physical_box_fragment.h"
#include "third_party/blink/renderer/core/style/computed_style.h"

namespace blink {

namespace {

LayoutUnit CalculateColumnContentBlockSize(
    const NGPhysicalFragment& fragment,
    WritingDirectionMode writing_direction) {
  WritingModeConverter converter(writing_direction, fragment.Size());
  // Note that what we're doing here is almost the same as what we do when
  // calculating overflow, with at least one important difference: If the
  // inline-size of a fragment is 0, the overflow rectangle becomes empty, even
  // if the fragment's block-size is non-zero. This is correct for overflow
  // handling, but it would be wrong for column balancing.
  LayoutUnit total_size;
  for (const auto& child : fragment.Children()) {
    LayoutUnit size = converter.ToLogical(child->Size()).block_size;
    LayoutUnit offset =
        converter.ToLogical(child.offset, child->Size()).block_offset;
    // TODO(mstensho): Need to detect whether we're actually clipping in the
    // block direction. The combination of overflow-x:clip and
    // overflow-y:visible should enter children here.
    if (child->IsContainer() && !child->HasNonVisibleOverflow()) {
      LayoutUnit children_size =
          CalculateColumnContentBlockSize(*child, writing_direction);
      if (size < children_size)
        size = children_size;
    }
    LayoutUnit block_end = offset + size;
    if (total_size < block_end)
      total_size = block_end;
  }
  return total_size;
}

// An itinerary of multicol container parts to walk separately for layout. A
// part is either a chunk of regular column content, or a column spanner.
class MulticolPartWalker {
  STACK_ALLOCATED();

 public:
  // What to lay out or process next.
  struct Entry {
    STACK_ALLOCATED();

   public:
    Entry() = default;
    Entry(const NGBlockBreakToken* token, NGBlockNode spanner)
        : break_token(token), spanner(spanner) {}

    // The incoming break token for the content to process, or null if we're at
    // the start.
    const NGBlockBreakToken* break_token = nullptr;

    // The column spanner node to process, or null if we're dealing with regular
    // column content.
    NGBlockNode spanner = nullptr;
  };

  MulticolPartWalker(NGBlockNode multicol_container,
                     const NGBlockBreakToken* break_token)
      : multicol_container_(multicol_container),
        parent_break_token_(break_token),
        child_token_idx_(0) {
    UpdateCurrent();
    // The first entry in the first multicol fragment may be empty (that just
    // means that we haven't started yet), but if this happens anywhere else, it
    // means that we're finished. Nothing inside this multicol container left to
    // process.
    if (IsResumingLayout(parent_break_token_) && !current_.break_token &&
        parent_break_token_->HasSeenAllChildren())
      is_finished_ = true;
  }

  Entry Current() const {
    DCHECK(!is_finished_);
    return current_;
  }

  bool IsFinished() const { return is_finished_; }

  // Move to the next part.
  void Next();

  // Move over to the specified spanner, and take it from there.
  void MoveToSpanner(NGBlockNode spanner,
                     const NGBlockBreakToken* next_column_token);

  // Push a break token for the column content to resume at.
  void AddNextColumnBreakToken(const NGBlockBreakToken& next_column_token);

 private:
  void MoveToNext();
  void UpdateCurrent();

  Entry current_;
  NGBlockNode spanner_ = nullptr;
  NGBlockNode multicol_container_;
  const NGBlockBreakToken* parent_break_token_;
  scoped_refptr<const NGBlockBreakToken> next_column_token_;

  // An index into parent_break_token_'s ChildBreakTokens() vector. Used for
  // keeping track of the next child break token to inspect.
  wtf_size_t child_token_idx_;

  bool is_finished_ = false;
};

void MulticolPartWalker::Next() {
  if (is_finished_)
    return;
  MoveToNext();
  if (!is_finished_)
    UpdateCurrent();
}

void MulticolPartWalker::MoveToSpanner(
    NGBlockNode spanner,
    const NGBlockBreakToken* next_column_token) {
  *this = MulticolPartWalker(multicol_container_, nullptr);
  DCHECK(spanner.IsColumnSpanAll());
  spanner_ = spanner;
  next_column_token_ = next_column_token;
  UpdateCurrent();
}

void MulticolPartWalker::AddNextColumnBreakToken(
    const NGBlockBreakToken& next_column_token) {
  *this = MulticolPartWalker(multicol_container_, nullptr);
  next_column_token_ = &next_column_token;
  UpdateCurrent();
}

void MulticolPartWalker::UpdateCurrent() {
  DCHECK(!is_finished_);
  if (parent_break_token_) {
    const auto& child_break_tokens = parent_break_token_->ChildBreakTokens();
    if (child_token_idx_ < child_break_tokens.size()) {
      const auto* child_break_token =
          To<NGBlockBreakToken>(child_break_tokens[child_token_idx_]);
      if (child_break_token->InputNode() == multicol_container_) {
        current_.spanner = nullptr;
      } else {
        current_.spanner = To<NGBlockNode>(child_break_token->InputNode());
        DCHECK(current_.spanner.IsColumnSpanAll());
      }
      current_.break_token = child_break_token;
      return;
    }
  }

  if (spanner_) {
    current_ = Entry(/* break_token */ nullptr, spanner_);
    return;
  }

  if (next_column_token_) {
    current_ = Entry(next_column_token_.get(), /* spanner */ nullptr);
    return;
  }

  // The current entry is empty. That's only the case when we're at the very
  // start of the multicol container, or if we're past all children.
  DCHECK(!is_finished_);
  DCHECK(!current_.spanner);
  DCHECK(!current_.break_token);
}

void MulticolPartWalker::MoveToNext() {
  if (parent_break_token_) {
    const auto& child_break_tokens = parent_break_token_->ChildBreakTokens();
    if (child_token_idx_ < child_break_tokens.size()) {
      child_token_idx_++;
      // If we have more incoming break tokens, we'll use that.
      if (child_token_idx_ < child_break_tokens.size())
        return;
      // We just ran out of break tokens. Fall through.
    }
  }

  if (spanner_) {
    NGLayoutInputNode next = spanner_.NextSibling();
    // Otherwise, if there's a next spanner, we'll use that.
    if (next && next.IsColumnSpanAll()) {
      spanner_ = To<NGBlockNode>(next);
      return;
    }
    spanner_ = nullptr;

    // Otherwise, if we have column content to resume at, use that.
    if (next_column_token_)
      return;
  }

  // Otherwise, we're done.
  is_finished_ = true;
}

}  // namespace

NGColumnLayoutAlgorithm::NGColumnLayoutAlgorithm(
    const NGLayoutAlgorithmParams& params)
    : NGLayoutAlgorithm(params) {
  // When a list item has multicol, |NGColumnLayoutAlgorithm| needs to keep
  // track of the list marker instead of the child layout algorithm. See
  // |NGBlockLayoutAlgorithm|.
  if (const NGBlockNode marker_node = Node().ListMarkerBlockNodeIfListItem()) {
    if (!marker_node.ListMarkerOccupiesWholeLine() &&
        (!BreakToken() || BreakToken()->HasUnpositionedListMarker())) {
      container_builder_.SetUnpositionedListMarker(
          NGUnpositionedListMarker(marker_node));
    }
  }
}

scoped_refptr<const NGLayoutResult> NGColumnLayoutAlgorithm::Layout() {
  const LogicalSize border_box_size = container_builder_.InitialBorderBoxSize();
  // TODO(mstensho): This isn't the content-box size, as
  // |BorderScrollbarPadding()| has been adjusted for fragmentation. Verify
  // that this is the correct size.
  column_block_size_ =
      ShrinkLogicalSize(border_box_size, BorderScrollbarPadding()).block_size;

  DCHECK_GE(ChildAvailableSize().inline_size, LayoutUnit());
  column_inline_size_ =
      ResolveUsedColumnInlineSize(ChildAvailableSize().inline_size, Style());

  column_inline_progression_ =
      column_inline_size_ +
      ResolveUsedColumnGap(ChildAvailableSize().inline_size, Style());
  used_column_count_ =
      ResolveUsedColumnCount(ChildAvailableSize().inline_size, Style());

  // If we know the block-size of the fragmentainers in an outer fragmentation
  // context (if any), our columns may be constrained by that, meaning that we
  // may have to fragment earlier than what we would have otherwise, and, if
  // that's the case, that we may also not create overflowing columns (in the
  // inline axis), but rather finish the row and resume in the next row in the
  // next outer fragmentainer. Note that it is possible to be nested inside a
  // fragmentation context that doesn't know the block-size of its
  // fragmentainers. This would be in the first layout pass of an outer multicol
  // container, before any tentative column block-size has been calculated.
  is_constrained_by_outer_fragmentation_context_ =
      ConstraintSpace().HasKnownFragmentainerBlockSize();

  container_builder_.SetIsBlockFragmentationContextRoot();

  intrinsic_block_size_ = BorderScrollbarPadding().block_start;

  NGBreakStatus break_status = LayoutChildren();
  if (break_status == NGBreakStatus::kNeedsEarlierBreak) {
    // We need to discard this layout and do it again. We found an earlier break
    // point that's more appealing than the one we ran out of space at.
    return RelayoutAndBreakEarlier<NGColumnLayoutAlgorithm>(
        container_builder_.EarlyBreak());
  } else if (break_status == NGBreakStatus::kBrokeBefore) {
    // If we want to break before, make sure that we're actually at the start.
    DCHECK(!IsResumingLayout(BreakToken()));

    return container_builder_.Abort(NGLayoutResult::kOutOfFragmentainerSpace);
  }

  intrinsic_block_size_ += BorderScrollbarPadding().block_end;

  // Figure out how much space we've already been able to process in previous
  // fragments, if this multicol container participates in an outer
  // fragmentation context.
  LayoutUnit previously_consumed_block_size;
  if (const auto* token = BreakToken())
    previously_consumed_block_size = token->ConsumedBlockSize();

  intrinsic_block_size_ =
      ClampIntrinsicBlockSize(ConstraintSpace(), Node(),
                              BorderScrollbarPadding(), intrinsic_block_size_);

  LayoutUnit block_size = ComputeBlockSizeForFragment(
      ConstraintSpace(), Style(), BorderPadding(),
      previously_consumed_block_size + intrinsic_block_size_,
      border_box_size.inline_size);

  container_builder_.SetFragmentsTotalBlockSize(block_size);
  container_builder_.SetIntrinsicBlockSize(intrinsic_block_size_);
  container_builder_.SetBlockOffsetForAdditionalColumns(
      CurrentContentBlockOffset());

  PositionAnyUnclaimedListMarker();

  if (ConstraintSpace().HasBlockFragmentation()) {
    // In addition to establishing one, we're nested inside another
    // fragmentation context.
    FinishFragmentation(Node(), ConstraintSpace(), BorderPadding().block_end,
                        FragmentainerSpaceAtBfcStart(ConstraintSpace()),
                        &container_builder_);

    // OOF positioned elements inside a nested fragmentation context are laid
    // out at the outermost context. If this multicol has OOF positioned
    // elements pending layout, store its node for later use.
    if (container_builder_.HasOutOfFlowFragmentainerDescendants()) {
      container_builder_.AddMulticolWithPendingOOFs(Node());
    }
  }

  NGOutOfFlowLayoutPart(Node(), ConstraintSpace(), &container_builder_).Run();

  return container_builder_.ToBoxFragment();
}

MinMaxSizesResult NGColumnLayoutAlgorithm::ComputeMinMaxSizes(
    const MinMaxSizesFloatInput&) const {
  // First calculate the min/max sizes of columns.
  NGConstraintSpace space = CreateConstraintSpaceForMinMax();
  NGFragmentGeometry fragment_geometry =
      CalculateInitialFragmentGeometry(space, Node(), /* is_intrinsic */ true);
  NGBlockLayoutAlgorithm algorithm({Node(), fragment_geometry, space});
  MinMaxSizesResult result =
      algorithm.ComputeMinMaxSizes(MinMaxSizesFloatInput());

  // How column-width affects min/max sizes is currently not defined in any
  // spec, but there used to be a definition, which everyone still follows to
  // some extent:
  // https://www.w3.org/TR/2016/WD-css-sizing-3-20160510/#multicol-intrinsic
  //
  // GitHub issue for getting this back into some spec:
  // https://github.com/w3c/csswg-drafts/issues/1742
  if (!Style().HasAutoColumnWidth()) {
    // One peculiarity in the (old and only) spec is that column-width may
    // shrink min intrinsic inline-size to become less than what the contents
    // require:
    //
    // "The min-content inline size of a multi-column element with a computed
    // column-width not auto is the smaller of its column-width and the largest
    // min-content inline-size contribution of its contents."
    const LayoutUnit column_width(Style().ColumnWidth());
    result.sizes.min_size = std::min(result.sizes.min_size, column_width);
    result.sizes.max_size = std::max(result.sizes.max_size, column_width);
    result.sizes.max_size =
        std::max(result.sizes.max_size, result.sizes.min_size);
  }

  // Now convert those column min/max values to multicol container min/max
  // values. We typically have multiple columns and also gaps between them.
  int column_count = Style().ColumnCount();
  DCHECK_GE(column_count, 1);
  LayoutUnit column_gap = ResolveUsedColumnGap(LayoutUnit(), Style());
  LayoutUnit gap_extra = column_gap * (column_count - 1);

  // Another peculiarity in the (old and only) spec (see above) is that
  // column-count (and therefore also column-gap) is ignored in intrinsic min
  // inline-size calculation, if column-width is specified.
  if (Style().HasAutoColumnWidth()) {
    result.sizes.min_size *= column_count;
    result.sizes.min_size += gap_extra;
  }
  result.sizes.max_size *= column_count;
  result.sizes.max_size += gap_extra;

  // The block layout algorithm skips spanners for min/max calculation (since
  // they shouldn't be part of the column-count multiplication above). Calculate
  // min/max inline-size for spanners now.
  result.sizes.Encompass(ComputeSpannersMinMaxSizes(Node()).sizes);

  result.sizes += BorderScrollbarPadding().InlineSum();
  return result;
}

MinMaxSizesResult NGColumnLayoutAlgorithm::ComputeSpannersMinMaxSizes(
    const NGBlockNode& search_parent) const {
  MinMaxSizesResult result;
  for (NGLayoutInputNode child = search_parent.FirstChild(); child;
       child = child.NextSibling()) {
    const NGBlockNode* child_block = DynamicTo<NGBlockNode>(&child);
    if (!child_block)
      continue;
    MinMaxSizesResult child_result;
    if (!child_block->IsColumnSpanAll()) {
      // Spanners don't need to be a direct child of the multicol container, but
      // they need to be in its formatting context.
      if (child_block->CreatesNewFormattingContext())
        continue;
      child_result = ComputeSpannersMinMaxSizes(*child_block);
    } else {
      NGMinMaxConstraintSpaceBuilder builder(
          ConstraintSpace(), Style(), *child_block, /* is_new_fc */ true);
      builder.SetAvailableBlockSize(ChildAvailableSize().block_size);
      const NGConstraintSpace child_space = builder.ToConstraintSpace();
      child_result = ComputeMinAndMaxContentContribution(Style(), *child_block,
                                                         child_space);
    }
    result.sizes.Encompass(child_result.sizes);
  }
  return result;
}

NGBreakStatus NGColumnLayoutAlgorithm::LayoutChildren() {
  NGMarginStrut margin_strut;
  MulticolPartWalker walker(Node(), BreakToken());
  while (!walker.IsFinished()) {
    auto entry = walker.Current();
    const auto* child_break_token = To<NGBlockBreakToken>(entry.break_token);

    // If this is regular column content (i.e. not a spanner), or we're at the
    // very start, perform column layout. If we're at the very start, and even
    // if the child is a spanner (which means that we won't be able to lay out
    // any column content at all), we still need to enter here, because that's
    // how we create a break token for the column content to resume at. With no
    // break token, we wouldn't be able to resume layout after the any initial
    // spanners.
    if (!entry.spanner) {
      scoped_refptr<const NGLayoutResult> result =
          LayoutRow(child_break_token, &margin_strut);

      if (!result) {
        // Not enough outer fragmentainer space to produce any columns at all.

        if (intrinsic_block_size_) {
          // We have preceding initial border/padding, or a column spanner
          // (possibly preceded by other spanners or even column content). So we
          // need to break inside the multicol container. Stop walking the
          // children, but "continue" layout, so that we produce a
          // fragment. Note that we normally don't want to break right after
          // initial border/padding, but will do so as a last resort. It's up to
          // our containing block to decide what's best. In case there is no
          // break token inside, we need to manually mark that we broke.
          container_builder_.SetDidBreakSelf();

          break;
        }
        // Otherwise we have nothing here, and need to break before the multicol
        // container. No fragment will be produced.
        DCHECK(!BreakToken());
        return NGBreakStatus::kBrokeBefore;
      }

      walker.Next();

      const auto* next_column_token =
          To<NGBlockBreakToken>(result->PhysicalFragment().BreakToken());

      if (NGBlockNode spanner_node = result->ColumnSpanner()) {
        // We found a spanner, and if there's column content to resume at after
        // it, |next_column_token| will be set. Move the walker to the
        // spanner. We'll now walk that spanner and any sibling spanners, before
        // resuming at |next_column_token|.
        walker.MoveToSpanner(spanner_node, next_column_token);
        continue;
      }

      // If we didn't find a spanner, it either means that we're through
      // everything, or that column layout needs to continue from the next outer
      // fragmentainer.
      if (next_column_token)
        walker.AddNextColumnBreakToken(*next_column_token);

      break;
    }

    // Attempt to lay out one column spanner.

    NGBlockNode spanner_node = entry.spanner;

    // If this is the child we had previously determined to break before, do so
    // now and finish layout.
    if (early_break_ &&
        IsEarlyBreakTarget(*early_break_, container_builder_, spanner_node))
      break;

    NGBreakStatus break_status =
        LayoutSpanner(spanner_node, child_break_token, &margin_strut);

    walker.Next();

    if (break_status == NGBreakStatus::kNeedsEarlierBreak)
      return break_status;
    if (break_status == NGBreakStatus::kBrokeBefore ||
        container_builder_.HasInflowChildBreakInside()) {
      break;
    }
  }

  if (!walker.IsFinished() || container_builder_.HasInflowChildBreakInside()) {
    // We broke in the main flow. Let this multicol container take up any
    // remaining space.
    intrinsic_block_size_ = FragmentainerSpaceAtBfcStart(ConstraintSpace());

    // Go through any remaining parts that we didn't get to, and push them as
    // break tokens for the next (outer) fragmentainer to handle.
    for (; !walker.IsFinished(); walker.Next()) {
      auto entry = walker.Current();
      if (entry.break_token) {
        // Copy unhandled incoming break tokens, for the next (outer)
        // fragmentainer.
        container_builder_.AddBreakToken(entry.break_token);
      } else if (entry.spanner) {
        // Create break tokens for the spanners that were discovered (but not
        // handled) while laying out this (outer) fragmentainer, so that they
        // get resumed in the next one (or pushed again, if it won't fit there
        // either).
        container_builder_.AddBreakBeforeChild(
            entry.spanner, kBreakAppealPerfect, /* is_forced_break */ false);
      }
    }
  } else {
    // We've gone through all the content. This doesn't necessarily mean that
    // we're done fragmenting, since the multicol container may be taller than
    // what the content requires, which means that we might create more
    // (childless) fragments, if we're nested inside another fragmentation
    // context. In that case we must make sure to skip the contents when
    // resuming.
    container_builder_.SetHasSeenAllChildren();

    // TODO(mstensho): Truncate the child margin if it overflows the
    // fragmentainer, by using AdjustedMarginAfterFinalChildFragment().

    intrinsic_block_size_ += margin_strut.Sum();
  }

  return NGBreakStatus::kContinue;
}

scoped_refptr<const NGLayoutResult> NGColumnLayoutAlgorithm::LayoutRow(
    const NGBlockBreakToken* next_column_token,
    NGMarginStrut* margin_strut) {
  LogicalSize column_size(column_inline_size_, column_block_size_);

  // We're adding a row. Incorporate the trailing margin from any preceding
  // column spanner into the layout position.
  intrinsic_block_size_ += margin_strut->Sum();
  *margin_strut = NGMarginStrut();

  // If block-size is non-auto, subtract the space for content we've consumed in
  // previous fragments. This is necessary when we're nested inside another
  // fragmentation context.
  if (column_size.block_size != kIndefiniteSize) {
    if (BreakToken() && is_constrained_by_outer_fragmentation_context_)
      column_size.block_size -= BreakToken()->ConsumedBlockSize();

    // Subtract the space already taken in the current fragment (spanners and
    // earlier column rows).
    column_size.block_size -= CurrentContentBlockOffset();

    column_size.block_size = column_size.block_size.ClampNegativeToZero();
  }

  bool may_resume_in_next_outer_fragmentainer = false;
  bool zero_outer_space_left = false;
  LayoutUnit available_outer_space = kIndefiniteSize;
  if (is_constrained_by_outer_fragmentation_context_) {
    available_outer_space =
        FragmentainerSpaceAtBfcStart(ConstraintSpace()) - intrinsic_block_size_;

    if (available_outer_space <= LayoutUnit()) {
      if (available_outer_space < LayoutUnit()) {
        // We're past the end of the outer fragmentainer (typically due to a
        // margin). Nothing will fit here, not even zero-size content.
        return nullptr;
      }

      // We are out of space, but we're exactly at the end of the outer
      // fragmentainer. If none of our contents take up space, we're going to
      // fit, otherwise not. Lay out and find out.
      zero_outer_space_left = true;
    }

    // Determine if we should resume layout in the next outer fragmentation
    // context if we run out of space in the current one. This is always the
    // thing to do except when block-size is non-auto and short enough to fit in
    // the current outer fragmentainer. In such cases we'll allow inner columns
    // to overflow its outer fragmentainer (since the inner multicol is too
    // short to reach the outer fragmentation line).
    if (column_size.block_size == kIndefiniteSize ||
        column_size.block_size > available_outer_space)
      may_resume_in_next_outer_fragmentainer = true;
  }

  // We balance if block-size is unconstrained, or when we're explicitly told
  // to. Note that the block-size may be constrained by outer fragmentation
  // contexts, not just by a block-size specified on this multicol container.
  bool balance_columns = Style().GetColumnFill() == EColumnFill::kBalance ||
                         (column_size.block_size == kIndefiniteSize &&
                          !is_constrained_by_outer_fragmentation_context_);

  if (balance_columns) {
    column_size.block_size =
        CalculateBalancedColumnBlockSize(column_size, next_column_token);
  } else if (available_outer_space != kIndefiniteSize) {
    // Finally, resolve any remaining auto block-size, and make sure that we
    // don't take up more space than there's room for in the outer fragmentation
    // context.
    if (column_size.block_size > available_outer_space ||
        column_size.block_size == kIndefiniteSize)
      column_size.block_size = available_outer_space;
  }

  DCHECK_GE(column_size.block_size, LayoutUnit());

  // New column fragments won't be added to the fragment builder right away,
  // since we may need to delete them and try again with a different block-size
  // (colum balancing). Keep them in this list, and add them to the fragment
  // builder when we have the final column fragments. Or clear the list and
  // retry otherwise.
  struct ResultWithOffset {
    scoped_refptr<const NGLayoutResult> result;
    LogicalOffset offset;

    ResultWithOffset(scoped_refptr<const NGLayoutResult> result,
                     LogicalOffset offset)
        : result(result), offset(offset) {}

    const NGPhysicalBoxFragment& Fragment() const {
      return To<NGPhysicalBoxFragment>(result->PhysicalFragment());
    }
  };
  Vector<ResultWithOffset, 16> new_columns;

  scoped_refptr<const NGLayoutResult> result;

  do {
    scoped_refptr<const NGBlockBreakToken> column_break_token =
        next_column_token;

    bool allow_discard_start_margin =
        column_break_token && !column_break_token->IsCausedByColumnSpanner();
    bool has_violating_break = false;

    LayoutUnit column_inline_offset(BorderScrollbarPadding().inline_start);
    int actual_column_count = 0;
    int forced_break_count = 0;

    // Each column should calculate their own minimal space shortage. Find the
    // lowest value of those. This will serve as the column stretch amount, if
    // we determine that stretching them is necessary and possible (column
    // balancing).
    LayoutUnit minimal_space_shortage(LayoutUnit::Max());

    do {
      // Lay out one column. Each column will become a fragment.
      NGConstraintSpace child_space = CreateConstraintSpaceForColumns(
          ConstraintSpace(), column_size, ColumnPercentageResolutionSize(),
          allow_discard_start_margin, balance_columns);

      NGFragmentGeometry fragment_geometry =
          CalculateInitialFragmentGeometry(child_space, Node());

      NGBlockLayoutAlgorithm child_algorithm(
          {Node(), fragment_geometry, child_space, column_break_token.get()});
      child_algorithm.SetBoxType(NGPhysicalFragment::kColumnBox);
      result = child_algorithm.Layout();
      const auto& column = result->PhysicalFragment();

      // Add the new column fragment to the list, but don't commit anything to
      // the fragment builder until we know whether these are the final columns.
      LogicalOffset logical_offset(column_inline_offset, intrinsic_block_size_);
      new_columns.emplace_back(result, logical_offset);

      LayoutUnit space_shortage = result->MinimalSpaceShortage();
      if (space_shortage > LayoutUnit()) {
        minimal_space_shortage =
            std::min(minimal_space_shortage, space_shortage);
      }
      actual_column_count++;
      if (result->HasForcedBreak())
        forced_break_count++;

      has_violating_break |= result->HasViolatingBreak();
      column_inline_offset += column_inline_progression_;

      if (result->ColumnSpanner())
        break;

      column_break_token = To<NGBlockBreakToken>(column.BreakToken());

      // If we're participating in an outer fragmentation context, we'll only
      // allow as many columns as the used value of column-count, so that we
      // don't overflow in the inline direction. There's one important
      // exception: If we have determined that this is going to be the last
      // fragment for this multicol container in the outer fragmentation
      // context, we'll just allow as many columns as needed (and let them
      // overflow in the inline direction, if necessary). We're not going to
      // progress into a next outer fragmentainer if the (remaining part of the)
      // multicol container fits block-wise in the current outer fragmentainer.
      if (ConstraintSpace().HasBlockFragmentation() && column_break_token &&
          actual_column_count >= used_column_count_ &&
          may_resume_in_next_outer_fragmentainer) {
        // We cannot keep any of this if we have zero space left. Then we need
        // to resume in the next outer fragmentainer.
        if (zero_outer_space_left)
          return nullptr;

        container_builder_.SetBreakAppeal(kBreakAppealPerfect);
        break;
      }

      allow_discard_start_margin = true;
    } while (column_break_token);

    if (!balance_columns) {
      if (result->ColumnSpanner()) {
        // We always have to balance columns preceding a spanner, so if we
        // didn't do that initially, switch over to column balancing mode now,
        // and lay out again.
        balance_columns = true;
        new_columns.clear();
        column_size.block_size =
            CalculateBalancedColumnBlockSize(column_size, next_column_token);
        continue;
      }

      // Balancing not enabled. We're done.
      break;
    }

    // We're balancing columns. Check if the column block-size that we laid out
    // with was satisfactory. If not, stretch and retry, if possible.
    //
    // If we didn't break at any undesirable location and actual column count
    // wasn't larger than what we have room for, we're done IF we're also out of
    // content (no break token; in nested multicol situations there are cases
    // where we only allow as many columns as we have room for, as additional
    // columns normally need to continue in the next outer fragmentainer). If we
    // have made the columns tall enough to bump into a spanner, it also means
    // we need to stop to lay out the spanner(s), and resume column layout
    // afterwards.
    if (!has_violating_break && actual_column_count <= used_column_count_ &&
        (!column_break_token || result->ColumnSpanner()))
      break;

    // We're in a situation where we'd like to stretch the columns, but then we
    // need to know the stretch amount (minimal space shortage).
    if (minimal_space_shortage == LayoutUnit::Max())
      break;

    // We also need at least one soft break opportunity. If forced breaks cause
    // too many breaks, there's no stretch amount that could prevent the columns
    // from overflowing.
    if (actual_column_count <= forced_break_count + 1)
      break;

    LayoutUnit new_column_block_size =
        StretchColumnBlockSize(minimal_space_shortage, column_size.block_size);

    // Give up if we cannot get taller columns. The multicol container may have
    // a specified block-size preventing taller columns, for instance.
    DCHECK_GE(new_column_block_size, column_size.block_size);
    if (new_column_block_size <= column_size.block_size) {
      if (ConstraintSpace().IsInsideBalancedColumns()) {
        // If we're doing nested column balancing, propagate any space shortage
        // to the outer multicol container, so that the outer multicol container
        // can attempt to stretch, so that this inner one may fit as well.
        if (!container_builder_.IsInitialColumnBalancingPass())
          container_builder_.PropagateSpaceShortage(minimal_space_shortage);
      }
      break;
    }

    // Remove column fragments and re-attempt layout with taller columns.
    new_columns.clear();
    column_size.block_size = new_column_block_size;
  } while (true);

  // If we just have one empty fragmentainer, we need to keep the trailing
  // margin from any previous column spanner, and also make sure that we don't
  // incorrectly consider this to be a class A breakpoint. A fragmentainer may
  // end up empty if there's no in-flow content at all inside the multicol
  // container, or if the multicol container starts with a spanner.
  bool is_empty =
      new_columns.size() == 1 && new_columns[0].Fragment().Children().empty();

  if (!is_empty) {
    has_processed_first_child_ = true;
    container_builder_.SetPreviousBreakAfter(EBreakBetween::kAuto);

    const auto& first_column =
        To<NGPhysicalBoxFragment>(new_columns[0].Fragment());
    if (!has_processed_first_column_) {
      has_processed_first_column_ = true;

      // According to the spec, we should only look for a baseline in the first
      // column.
      //
      // TODO(layout-dev): It might make sense to look for baselines inside
      // every column that's first in a row, not just the first column in the
      // multicol container.
      PropagateBaselineFromChild(first_column, intrinsic_block_size_);
    }

    // Only the first column in a row may attempt to place any unpositioned
    // list-item. This matches the behavior in Gecko, and also to some extent
    // with how baselines are propagated inside a multicol container.
    AttemptToPositionListMarker(first_column, intrinsic_block_size_);
  }

  intrinsic_block_size_ += column_size.block_size;

  // Commit all column fragments to the fragment builder.
  const NGBlockBreakToken* incoming_column_token = next_column_token;
  for (auto result_with_offset : new_columns) {
    const NGPhysicalBoxFragment& fragment = result_with_offset.Fragment();
    container_builder_.AddChild(fragment, result_with_offset.offset);
    Node().AddColumnResult(result_with_offset.result, incoming_column_token);
    incoming_column_token = To<NGBlockBreakToken>(fragment.BreakToken());
  }

  return result;
}

NGBreakStatus NGColumnLayoutAlgorithm::LayoutSpanner(
    NGBlockNode spanner_node,
    const NGBlockBreakToken* break_token,
    NGMarginStrut* margin_strut) {
  const ComputedStyle& spanner_style = spanner_node.Style();
  NGBoxStrut margins =
      ComputeMarginsFor(spanner_style, ChildAvailableSize().inline_size,
                        ConstraintSpace().GetWritingDirection());
  AdjustMarginsForFragmentation(break_token, &margins);

  // Collapse the block-start margin of this spanner with the block-end margin
  // of an immediately preceding spanner, if any.
  margin_strut->Append(margins.block_start, /* is_quirky */ false);

  LayoutUnit block_offset = intrinsic_block_size_ + margin_strut->Sum();
  auto spanner_space =
      CreateConstraintSpaceForSpanner(spanner_node, block_offset);

  const NGEarlyBreak* early_break_in_child = nullptr;
  if (UNLIKELY(early_break_))
    early_break_in_child = EnterEarlyBreakInChild(spanner_node, *early_break_);

  auto result =
      spanner_node.Layout(spanner_space, break_token, early_break_in_child);

  if (ConstraintSpace().HasBlockFragmentation() && !early_break_) {
    // We're nested inside another fragmentation context. Examine this break
    // point, and determine whether we should break.

    LayoutUnit fragmentainer_block_offset =
        ConstraintSpace().FragmentainerOffsetAtBfc() + block_offset;

    NGBreakStatus break_status = BreakBeforeChildIfNeeded(
        ConstraintSpace(), spanner_node, *result.get(),
        fragmentainer_block_offset, has_processed_first_child_,
        &container_builder_);

    if (break_status != NGBreakStatus::kContinue) {
      // We need to break, either before the spanner, or even earlier.
      return break_status;
    }
  }

  const auto& spanner_fragment =
      To<NGPhysicalBoxFragment>(result->PhysicalFragment());
  NGFragment logical_fragment(ConstraintSpace().GetWritingDirection(),
                              spanner_fragment);

  ResolveInlineMargins(spanner_style, Style(), ChildAvailableSize().inline_size,
                       logical_fragment.InlineSize(), &margins);

  LogicalOffset offset(
      BorderScrollbarPadding().inline_start + margins.inline_start,
      block_offset);
  container_builder_.AddResult(*result, offset);

  // According to the spec, the first spanner that has a baseline contributes
  // with its baseline to the multicol container. This is in contrast to column
  // content, where only the first column may contribute with a baseline.
  PropagateBaselineFromChild(spanner_fragment, offset.block_offset);

  AttemptToPositionListMarker(spanner_fragment, block_offset);

  *margin_strut = NGMarginStrut();
  margin_strut->Append(margins.block_end, /* is_quirky */ false);

  intrinsic_block_size_ = offset.block_offset + logical_fragment.BlockSize();
  has_processed_first_child_ = true;

  return NGBreakStatus::kContinue;
}

void NGColumnLayoutAlgorithm::AttemptToPositionListMarker(
    const NGPhysicalBoxFragment& child_fragment,
    LayoutUnit block_offset) {
  const auto marker = container_builder_.UnpositionedListMarker();
  if (!marker)
    return;
  DCHECK(Node().IsListItem());

  FontBaseline baseline_type = Style().GetFontBaseline();
  auto baseline = marker.ContentAlignmentBaseline(
      ConstraintSpace(), baseline_type, child_fragment);
  if (!baseline)
    return;

  scoped_refptr<const NGLayoutResult> layout_result = marker.Layout(
      ConstraintSpace(), container_builder_.Style(), baseline_type);
  DCHECK(layout_result);

  // TODO(layout-dev): AddToBox() may increase the specified block-offset, which
  // is bad, since it means that we may need to refragment. For now we'll just
  // ignore the adjustment (which is also bad, of course).
  marker.AddToBox(ConstraintSpace(), baseline_type, child_fragment,
                  BorderScrollbarPadding(), *layout_result, *baseline,
                  &block_offset, &container_builder_);

  container_builder_.ClearUnpositionedListMarker();
}

void NGColumnLayoutAlgorithm::PositionAnyUnclaimedListMarker() {
  if (!Node().IsListItem())
    return;
  const auto marker = container_builder_.UnpositionedListMarker();
  if (!marker)
    return;

  // Lay out the list marker.
  FontBaseline baseline_type = Style().GetFontBaseline();
  scoped_refptr<const NGLayoutResult> layout_result =
      marker.Layout(ConstraintSpace(), Style(), baseline_type);
  DCHECK(layout_result);
  // Position the list marker without aligning with line boxes.
  marker.AddToBoxWithoutLineBoxes(ConstraintSpace(), baseline_type,
                                  *layout_result, &container_builder_,
                                  &intrinsic_block_size_);
  container_builder_.ClearUnpositionedListMarker();
}

void NGColumnLayoutAlgorithm::PropagateBaselineFromChild(
    const NGPhysicalBoxFragment& child,
    LayoutUnit block_offset) {
  // Bail if a baseline was already found.
  if (container_builder_.Baseline())
    return;

  // According to the spec, multicol containers have no "last baseline set", so,
  // unless we're looking for a "first baseline set", we have no work to do.
  if (ConstraintSpace().BaselineAlgorithmType() !=
      NGBaselineAlgorithmType::kFirstLine)
    return;

  NGBoxFragment logical_fragment(ConstraintSpace().GetWritingDirection(),
                                 child);

  if (auto baseline = logical_fragment.FirstBaseline())
    container_builder_.SetBaseline(block_offset + *baseline);
}

// Distribute as many implicit breaks into the content runs as we need.
LayoutUnit NGColumnLayoutAlgorithm::CalculateBalancedColumnBlockSize(
    const LogicalSize& column_size,
    const NGBlockBreakToken* child_break_token) {
  // To calculate a balanced column size for one row of columns, we need to
  // figure out how tall our content is. To do that we need to lay out. Create a
  // special constraint space for column balancing, without allowing soft
  // breaks. It will make us lay out all the multicol content as one single tall
  // strip (unless there are forced breaks). When we're done with this layout
  // pass, we can examine the result and calculate an ideal column block-size.
  NGConstraintSpace space = CreateConstraintSpaceForBalancing(column_size);
  NGFragmentGeometry fragment_geometry =
      CalculateInitialFragmentGeometry(space, Node());

  // A run of content without explicit (forced) breaks; i.e. the content portion
  // between two explicit breaks, between fragmentation context start and an
  // explicit break, between an explicit break and fragmentation context end,
  // or, in cases when there are no explicit breaks at all: between
  // fragmentation context start and end. We need to know where the explicit
  // breaks are, in order to figure out where the implicit breaks will end up,
  // so that we get the columns properly balanced. A content run starts out as
  // representing one single column, and we'll add as many additional implicit
  // breaks as needed into the content runs that are the tallest ones
  // (ColumnBlockSize()).
  struct ContentRun {
    ContentRun(LayoutUnit content_block_size)
        : content_block_size(content_block_size) {}

    // Return the column block-size that this content run would require,
    // considering the implicit breaks we have assumed so far.
    LayoutUnit ColumnBlockSize() const {
      // Some extra care is required for the division here. We want the
      // resulting LayoutUnit value to be large enough to prevent overflowing
      // columns. Use floating point to get higher precision than
      // LayoutUnit. Then convert it to a LayoutUnit, but round it up to the
      // nearest value that LayoutUnit is able to represent.
      return LayoutUnit::FromFloatCeil(
          float(content_block_size) / float(implicit_breaks_assumed_count + 1));
    }

    LayoutUnit content_block_size;

    // The number of implicit breaks assumed to exist in this content run.
    int implicit_breaks_assumed_count = 0;
  };

  class ContentRuns final {
   public:
    // When we have "inserted" (assumed) enough implicit column breaks, this
    // method returns the block-size of the tallest column.
    LayoutUnit TallestColumnBlockSize() const {
      return TallestRun()->ColumnBlockSize();
    }

    LayoutUnit TallestContentBlockSize() const {
      return tallest_content_block_size_;
    }

    void AddRun(LayoutUnit content_block_size) {
      runs_.emplace_back(content_block_size);
      tallest_content_block_size_ =
          std::max(tallest_content_block_size_, content_block_size);
    }

    void DistributeImplicitBreaks(int used_column_count) {
      for (int columns_found = runs_.size(); columns_found < used_column_count;
           ++columns_found) {
        // The tallest content run (with all assumed implicit breaks added so
        // far taken into account) is where we assume the next implicit break.
        ++TallestRun()->implicit_breaks_assumed_count;
      }
    }

   private:
    ContentRun* TallestRun() const {
      DCHECK(!runs_.IsEmpty());
      auto* const it = std::max_element(
          runs_.begin(), runs_.end(),
          [](const ContentRun& run1, const ContentRun& run2) {
            return run1.ColumnBlockSize() < run2.ColumnBlockSize();
          });
      DCHECK(it != runs_.end());
      return const_cast<ContentRun*>(it);
    }

    Vector<ContentRun, 1> runs_;
    LayoutUnit tallest_content_block_size_;
  };

  // First split into content runs at explicit (forced) breaks.
  ContentRuns content_runs;
  scoped_refptr<const NGBlockBreakToken> break_token = child_break_token;
  tallest_unbreakable_block_size_ = LayoutUnit();
  do {
    NGBlockLayoutAlgorithm balancing_algorithm(
        {Node(), fragment_geometry, space, break_token.get()});
    balancing_algorithm.SetBoxType(NGPhysicalFragment::kColumnBox);
    scoped_refptr<const NGLayoutResult> result = balancing_algorithm.Layout();

    // This algorithm should never abort.
    DCHECK_EQ(result->Status(), NGLayoutResult::kSuccess);

    const NGPhysicalBoxFragment& fragment =
        To<NGPhysicalBoxFragment>(result->PhysicalFragment());
    LayoutUnit column_block_size =
        CalculateColumnContentBlockSize(fragment, space.GetWritingDirection());

    // Encompass the block-size of the (single-strip column) fragment, to
    // account for any trailing margins. We let them affect the column
    // block-size, for compatibility reasons, if nothing else. The initial
    // column balancing pass (i.e. here) is our opportunity to do that fairly
    // easily. But note that this doesn't guarantee that no margins will ever
    // get truncated. To avoid that we'd need to add some sort of mechanism that
    // is invoked in *every* column balancing layout pass, where we'd
    // essentially have to treat every margin as unbreakable (which kind of
    // sounds both bad and difficult).
    //
    // We might want to revisit this approach, if it's worth it: Maybe it's
    // better to not make any room at all for margins that might end up getting
    // truncated. After all, they don't really require any space, so what we're
    // doing currently might be seen as unnecessary (and slightly unpredictable)
    // column over-stretching.
    NGFragment logical_fragment(ConstraintSpace().GetWritingDirection(),
                                fragment);
    column_block_size =
        std::max(column_block_size, logical_fragment.BlockSize());
    content_runs.AddRun(column_block_size);

    tallest_unbreakable_block_size_ = std::max(
        tallest_unbreakable_block_size_, result->TallestUnbreakableBlockSize());

    // Stop when we reach a spanner. That's where this row of columns will end.
    if (result->ColumnSpanner())
      break;

    break_token = To<NGBlockBreakToken>(fragment.BreakToken());
  } while (break_token);

  if (ConstraintSpace().IsInitialColumnBalancingPass()) {
    // Nested column balancing. Our outer fragmentation context is in its
    // initial balancing pass, so it also wants to know the largest unbreakable
    // block-size.
    container_builder_.PropagateTallestUnbreakableBlockSize(
        tallest_unbreakable_block_size_);
  }

  // We now have an estimated minimal block-size for the columns. Roughly
  // speaking, this is the block-size that the columns will need if we are
  // allowed to break freely at any offset. This is normally not the case,
  // though, since there will typically be unbreakable pieces of content, such
  // as replaced content, lines of text, and other things. We need to actually
  // lay out into columns to figure out if they are tall enough or not (and
  // stretch and retry if not). Also honor {,min-,max-}block-size properties
  // before returning, and also try to not become shorter than the tallest piece
  // of unbreakable content.
  if (tallest_unbreakable_block_size_ >= content_runs.TallestContentBlockSize())
    return ConstrainColumnBlockSize(tallest_unbreakable_block_size_);

  content_runs.DistributeImplicitBreaks(
      ResolveUsedColumnCount(ChildAvailableSize().inline_size, Style()));
  return ConstrainColumnBlockSize(content_runs.TallestColumnBlockSize());
}

LayoutUnit NGColumnLayoutAlgorithm::StretchColumnBlockSize(
    LayoutUnit minimal_space_shortage,
    LayoutUnit current_column_size) const {
  LayoutUnit length = current_column_size + minimal_space_shortage;
  // Honor {,min-,max-}{height,width} properties.
  return ConstrainColumnBlockSize(length);
}

// Constrain a balanced column block size to not overflow the multicol
// container.
LayoutUnit NGColumnLayoutAlgorithm::ConstrainColumnBlockSize(
    LayoutUnit size) const {
  if (is_constrained_by_outer_fragmentation_context_) {
    // Don't become too tall to fit in the outer fragmentation context.
    LayoutUnit available_outer_space =
        FragmentainerSpaceAtBfcStart(ConstraintSpace()) - intrinsic_block_size_;
    DCHECK_GE(available_outer_space, LayoutUnit());
    size = std::min(size, available_outer_space);
  }

  // But avoid becoming shorter than the tallest piece of unbreakable content.
  size = std::max(size, tallest_unbreakable_block_size_);

  // The {,min-,max-}block-size properties are specified on the multicol
  // container, but here we're calculating the column block sizes inside the
  // multicol container, which isn't exactly the same. We may shrink the column
  // block size here, but we'll never stretch them, because the value passed is
  // the perfect balanced block size. Making it taller would only disrupt the
  // balanced output, for no reason. The only thing we need to worry about here
  // is to not overflow the multicol container.
  //
  // First of all we need to convert the size to a value that can be compared
  // against the resolved properties on the multicol container. That means that
  // we have to convert the value from content-box to border-box.
  LayoutUnit extra = BorderScrollbarPadding().BlockSum();
  size += extra;

  const ComputedStyle& style = Style();
  LayoutUnit max = ResolveMaxBlockLength(
      ConstraintSpace(), style, BorderPadding(), style.LogicalMaxHeight());
  LayoutUnit extent = kIndefiniteSize;
  if (!style.LogicalHeight().IsAuto()) {
    extent = ResolveMainBlockLength(ConstraintSpace(), style, BorderPadding(),
                                    style.LogicalHeight(), kIndefiniteSize);
    // A specified block-size will just constrain the maximum length.
    if (extent != kIndefiniteSize)
      max = std::min(max, extent);
  }

  // A specified min-block-size may increase the maximum length.
  LayoutUnit min = ResolveMinBlockLength(
      ConstraintSpace(), style, BorderPadding(), style.LogicalMinHeight());
  max = std::max(max, min);

  // If this multicol container is nested inside another fragmentation
  // context, we need to subtract the space consumed in previous fragments.
  if (BreakToken())
    max -= BreakToken()->ConsumedBlockSize();

  // We may already have used some of the available space in earlier column rows
  // or spanners.
  max -= CurrentContentBlockOffset();

  // Constrain and convert the value back to content-box.
  size = std::min(size, max);
  return (size - extra).ClampNegativeToZero();
}

NGConstraintSpace NGColumnLayoutAlgorithm::CreateConstraintSpaceForBalancing(
    const LogicalSize& column_size) const {
  NGConstraintSpaceBuilder space_builder(
      ConstraintSpace(), Style().GetWritingDirection(), /* is_new_fc */ true);
  space_builder.SetFragmentationType(kFragmentColumn);
  space_builder.SetAvailableSize({column_size.inline_size, kIndefiniteSize});
  space_builder.SetInlineAutoBehavior(NGAutoBehavior::kStretchImplicit);
  space_builder.SetPercentageResolutionSize(ColumnPercentageResolutionSize());
  space_builder.SetIsAnonymous(true);
  space_builder.SetIsInColumnBfc();
  space_builder.SetIsInsideBalancedColumns();

  return space_builder.ToConstraintSpace();
}

NGConstraintSpace NGColumnLayoutAlgorithm::CreateConstraintSpaceForSpanner(
    const NGBlockNode& spanner,
    LayoutUnit block_offset) const {
  NGConstraintSpaceBuilder space_builder(
      ConstraintSpace(), Style().GetWritingDirection(), /* is_new_fc */ true);
  space_builder.SetAvailableSize(ChildAvailableSize());
  space_builder.SetInlineAutoBehavior(NGAutoBehavior::kStretchImplicit);
  space_builder.SetPercentageResolutionSize(ChildAvailableSize());

  space_builder.SetBaselineAlgorithmType(
      ConstraintSpace().BaselineAlgorithmType());

  if (ConstraintSpace().HasBlockFragmentation()) {
    SetupSpaceBuilderForFragmentation(ConstraintSpace(), spanner, block_offset,
                                      &space_builder, /* is_new_fc */ true);
  }

  return space_builder.ToConstraintSpace();
}

NGConstraintSpace NGColumnLayoutAlgorithm::CreateConstraintSpaceForMinMax()
    const {
  NGConstraintSpaceBuilder space_builder(
      ConstraintSpace(), Style().GetWritingDirection(), /* is_new_fc */ true);
  space_builder.SetIsAnonymous(true);
  space_builder.SetIsInColumnBfc();

  return space_builder.ToConstraintSpace();
}

}  // namespace blink
