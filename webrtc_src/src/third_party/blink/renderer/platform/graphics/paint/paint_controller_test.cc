// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/graphics/paint/paint_controller_test.h"

#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context.h"
#include "third_party/blink/renderer/platform/graphics/paint/display_item_cache_skipper.h"
#include "third_party/blink/renderer/platform/graphics/paint/drawing_display_item.h"
#include "third_party/blink/renderer/platform/graphics/paint/drawing_recorder.h"
#include "third_party/blink/renderer/platform/graphics/paint/scoped_display_item_fragment.h"
#include "third_party/blink/renderer/platform/graphics/paint/scoped_paint_chunk_properties.h"
#include "third_party/blink/renderer/platform/graphics/paint/subsequence_recorder.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/testing/paint_test_configurations.h"

using testing::ElementsAre;

namespace blink {

// Tests using this class will be tested with under-invalidation-checking
// enabled and disabled.
class PaintControllerTest : public PaintTestConfigurations,
                            public PaintControllerTestBase {
};

#define EXPECT_DEFAULT_ROOT_CHUNK(size)                               \
  EXPECT_THAT(GetPaintController().PaintChunks(),                     \
              ElementsAre(IsPaintChunk(0, size, DefaultRootChunkId(), \
                                       DefaultPaintChunkProperties())))

INSTANTIATE_TEST_SUITE_P(
    All,
    PaintControllerTest,
    testing::Values(0,
                    kCompositeAfterPaint,
                    kUnderInvalidationChecking,
                    kCompositeAfterPaint | kUnderInvalidationChecking));

TEST_P(PaintControllerTest, NestedRecorders) {
  GraphicsContext context(GetPaintController());
  FakeDisplayItemClient client("client");
  InitRootChunk();

  DrawRect(context, client, kBackgroundType, IntRect(100, 100, 200, 200));
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&client, kBackgroundType)));
  EXPECT_DEFAULT_ROOT_CHUNK(1);
}

TEST_P(PaintControllerTest, UpdateBasic) {
  FakeDisplayItemClient first("first");
  FakeDisplayItemClient second("second");
  GraphicsContext context(GetPaintController());
  InitRootChunk();

  DrawRect(context, first, kBackgroundType, IntRect(100, 100, 300, 300));
  DrawRect(context, second, kBackgroundType, IntRect(100, 100, 200, 200));
  DrawRect(context, first, kForegroundType, IntRect(100, 100, 300, 300));

  EXPECT_EQ(0u, NumCachedNewItems());
  EXPECT_EQ(0u, NumCachedNewSubsequences());

  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&first, kBackgroundType),
                          IsSameId(&second, kBackgroundType),
                          IsSameId(&first, kForegroundType)));
  EXPECT_DEFAULT_ROOT_CHUNK(3);

  InitRootChunk();
  DrawRect(context, first, kBackgroundType, IntRect(100, 100, 300, 300));
  DrawRect(context, first, kForegroundType, IntRect(100, 100, 300, 300));

  EXPECT_EQ(2u, NumCachedNewItems());
  EXPECT_EQ(0u, NumCachedNewSubsequences());
#if DCHECK_IS_ON()
  EXPECT_EQ(1u, NumIndexedItems());
  EXPECT_EQ(2u, NumSequentialMatches());
  EXPECT_EQ(0u, NumOutOfOrderMatches());
#endif

  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&first, kBackgroundType),
                          IsSameId(&first, kForegroundType)));
  EXPECT_DEFAULT_ROOT_CHUNK(2);
}

TEST_P(PaintControllerTest, UpdateSwapOrder) {
  FakeDisplayItemClient first("first");
  FakeDisplayItemClient second("second");
  FakeDisplayItemClient unaffected("unaffected");
  GraphicsContext context(GetPaintController());
  InitRootChunk();

  DrawRect(context, first, kBackgroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, first, kForegroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, second, kBackgroundType, IntRect(100, 100, 50, 200));
  DrawRect(context, second, kForegroundType, IntRect(100, 100, 50, 200));
  DrawRect(context, unaffected, kBackgroundType, IntRect(300, 300, 10, 10));
  DrawRect(context, unaffected, kForegroundType, IntRect(300, 300, 10, 10));
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&first, kBackgroundType),
                          IsSameId(&first, kForegroundType),
                          IsSameId(&second, kBackgroundType),
                          IsSameId(&second, kForegroundType),
                          IsSameId(&unaffected, kBackgroundType),
                          IsSameId(&unaffected, kForegroundType)));

  InitRootChunk();
  DrawRect(context, second, kBackgroundType, IntRect(100, 100, 50, 200));
  DrawRect(context, second, kForegroundType, IntRect(100, 100, 50, 200));
  DrawRect(context, first, kBackgroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, first, kForegroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, unaffected, kBackgroundType, IntRect(300, 300, 10, 10));
  DrawRect(context, unaffected, kForegroundType, IntRect(300, 300, 10, 10));

  EXPECT_EQ(6u, NumCachedNewItems());
  EXPECT_EQ(0u, NumCachedNewSubsequences());
#if DCHECK_IS_ON()
  EXPECT_EQ(2u, NumIndexedItems());  // first
  EXPECT_EQ(5u,
            NumSequentialMatches());  // second, first foreground, unaffected
  EXPECT_EQ(1u, NumOutOfOrderMatches());  // first
#endif

  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&second, kBackgroundType),
                          IsSameId(&second, kForegroundType),
                          IsSameId(&first, kBackgroundType),
                          IsSameId(&first, kForegroundType),
                          IsSameId(&unaffected, kBackgroundType),
                          IsSameId(&unaffected, kForegroundType)));
  EXPECT_DEFAULT_ROOT_CHUNK(6);
}

TEST_P(PaintControllerTest, UpdateSwapOrderWithInvalidation) {
  FakeDisplayItemClient first("first");
  FakeDisplayItemClient second("second");
  FakeDisplayItemClient unaffected("unaffected");
  GraphicsContext context(GetPaintController());
  InitRootChunk();

  DrawRect(context, first, kBackgroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, first, kForegroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, second, kBackgroundType, IntRect(100, 100, 50, 200));
  DrawRect(context, second, kForegroundType, IntRect(100, 100, 50, 200));
  DrawRect(context, unaffected, kBackgroundType, IntRect(300, 300, 10, 10));
  DrawRect(context, unaffected, kForegroundType, IntRect(300, 300, 10, 10));
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&first, kBackgroundType),
                          IsSameId(&first, kForegroundType),
                          IsSameId(&second, kBackgroundType),
                          IsSameId(&second, kForegroundType),
                          IsSameId(&unaffected, kBackgroundType),
                          IsSameId(&unaffected, kForegroundType)));

  InitRootChunk();
  first.Invalidate();
  DrawRect(context, second, kBackgroundType, IntRect(100, 100, 50, 200));
  DrawRect(context, second, kForegroundType, IntRect(100, 100, 50, 200));
  DrawRect(context, first, kBackgroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, first, kForegroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, unaffected, kBackgroundType, IntRect(300, 300, 10, 10));
  DrawRect(context, unaffected, kForegroundType, IntRect(300, 300, 10, 10));

  EXPECT_EQ(4u, NumCachedNewItems());
  EXPECT_EQ(0u, NumCachedNewSubsequences());
#if DCHECK_IS_ON()
  EXPECT_EQ(2u, NumIndexedItems());
  EXPECT_EQ(4u, NumSequentialMatches());  // second, unaffected
  EXPECT_EQ(0u, NumOutOfOrderMatches());
#endif

  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&second, kBackgroundType),
                          IsSameId(&second, kForegroundType),
                          IsSameId(&first, kBackgroundType),
                          IsSameId(&first, kForegroundType),
                          IsSameId(&unaffected, kBackgroundType),
                          IsSameId(&unaffected, kForegroundType)));
  EXPECT_DEFAULT_ROOT_CHUNK(6);
}

TEST_P(PaintControllerTest, UpdateNewItemInMiddle) {
  FakeDisplayItemClient first("first");
  FakeDisplayItemClient second("second");
  FakeDisplayItemClient third("third");
  GraphicsContext context(GetPaintController());
  InitRootChunk();

  DrawRect(context, first, kBackgroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, second, kBackgroundType, IntRect(100, 100, 50, 200));
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&first, kBackgroundType),
                          IsSameId(&second, kBackgroundType)));

  InitRootChunk();

  DrawRect(context, first, kBackgroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, third, kBackgroundType, IntRect(125, 100, 200, 50));
  DrawRect(context, second, kBackgroundType, IntRect(100, 100, 50, 200));

  EXPECT_EQ(2u, NumCachedNewItems());
  EXPECT_EQ(0u, NumCachedNewSubsequences());
#if DCHECK_IS_ON()
  EXPECT_EQ(0u, NumIndexedItems());
  EXPECT_EQ(2u, NumSequentialMatches());  // first, second
  EXPECT_EQ(0u, NumOutOfOrderMatches());
#endif

  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&first, kBackgroundType),
                          IsSameId(&third, kBackgroundType),
                          IsSameId(&second, kBackgroundType)));
  EXPECT_DEFAULT_ROOT_CHUNK(3);
}

TEST_P(PaintControllerTest, UpdateInvalidationWithPhases) {
  FakeDisplayItemClient first("first");
  FakeDisplayItemClient second("second");
  FakeDisplayItemClient third("third");
  GraphicsContext context(GetPaintController());
  InitRootChunk();

  DrawRect(context, first, kBackgroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, second, kBackgroundType, IntRect(100, 100, 50, 200));
  DrawRect(context, third, kBackgroundType, IntRect(300, 100, 50, 50));
  DrawRect(context, first, kForegroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, second, kForegroundType, IntRect(100, 100, 50, 200));
  DrawRect(context, third, kForegroundType, IntRect(300, 100, 50, 50));
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&first, kBackgroundType),
                          IsSameId(&second, kBackgroundType),
                          IsSameId(&third, kBackgroundType),
                          IsSameId(&first, kForegroundType),
                          IsSameId(&second, kForegroundType),
                          IsSameId(&third, kForegroundType)));

  InitRootChunk();

  second.Invalidate();
  DrawRect(context, first, kBackgroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, second, kBackgroundType, IntRect(100, 100, 50, 200));
  DrawRect(context, third, kBackgroundType, IntRect(300, 100, 50, 50));
  DrawRect(context, first, kForegroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, second, kForegroundType, IntRect(100, 100, 50, 200));
  DrawRect(context, third, kForegroundType, IntRect(300, 100, 50, 50));

  EXPECT_EQ(4u, NumCachedNewItems());
  EXPECT_EQ(0u, NumCachedNewSubsequences());
#if DCHECK_IS_ON()
  EXPECT_EQ(2u, NumIndexedItems());
  EXPECT_EQ(4u, NumSequentialMatches());
  EXPECT_EQ(0u, NumOutOfOrderMatches());
#endif

  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&first, kBackgroundType),
                          IsSameId(&second, kBackgroundType),
                          IsSameId(&third, kBackgroundType),
                          IsSameId(&first, kForegroundType),
                          IsSameId(&second, kForegroundType),
                          IsSameId(&third, kForegroundType)));
  EXPECT_DEFAULT_ROOT_CHUNK(6);
}

TEST_P(PaintControllerTest, UpdateAddFirstOverlap) {
  FakeDisplayItemClient first("first");
  FakeDisplayItemClient second("second");
  GraphicsContext context(GetPaintController());
  InitRootChunk();

  DrawRect(context, second, kBackgroundType, IntRect(200, 200, 50, 50));
  DrawRect(context, second, kForegroundType, IntRect(200, 200, 50, 50));
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&second, kBackgroundType),
                          IsSameId(&second, kForegroundType)));

  InitRootChunk();

  first.Invalidate();
  second.Invalidate();
  DrawRect(context, first, kBackgroundType, IntRect(100, 100, 150, 150));
  DrawRect(context, first, kForegroundType, IntRect(100, 100, 150, 150));
  DrawRect(context, second, kBackgroundType, IntRect(150, 250, 100, 100));
  DrawRect(context, second, kForegroundType, IntRect(150, 250, 100, 100));
  EXPECT_EQ(0u, NumCachedNewItems());
  EXPECT_EQ(0u, NumCachedNewSubsequences());
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&first, kBackgroundType),
                          IsSameId(&first, kForegroundType),
                          IsSameId(&second, kBackgroundType),
                          IsSameId(&second, kForegroundType)));
  EXPECT_DEFAULT_ROOT_CHUNK(4);

  InitRootChunk();
  DrawRect(context, second, kBackgroundType, IntRect(150, 250, 100, 100));
  DrawRect(context, second, kForegroundType, IntRect(150, 250, 100, 100));

  EXPECT_EQ(2u, NumCachedNewItems());
  EXPECT_EQ(0u, NumCachedNewSubsequences());
#if DCHECK_IS_ON()
  EXPECT_EQ(2u, NumIndexedItems());
  EXPECT_EQ(2u, NumSequentialMatches());
  EXPECT_EQ(0u, NumOutOfOrderMatches());
#endif

  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&second, kBackgroundType),
                          IsSameId(&second, kForegroundType)));
  EXPECT_DEFAULT_ROOT_CHUNK(2);
}

TEST_P(PaintControllerTest, UpdateAddLastOverlap) {
  FakeDisplayItemClient first("first");
  FakeDisplayItemClient second("second");
  GraphicsContext context(GetPaintController());
  InitRootChunk();

  DrawRect(context, first, kBackgroundType, IntRect(100, 100, 150, 150));
  DrawRect(context, first, kForegroundType, IntRect(100, 100, 150, 150));
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&first, kBackgroundType),
                          IsSameId(&first, kForegroundType)));

  InitRootChunk();

  first.Invalidate();
  second.Invalidate();
  DrawRect(context, first, kBackgroundType, IntRect(150, 150, 100, 100));
  DrawRect(context, first, kForegroundType, IntRect(150, 150, 100, 100));
  DrawRect(context, second, kBackgroundType, IntRect(200, 200, 50, 50));
  DrawRect(context, second, kForegroundType, IntRect(200, 200, 50, 50));
  EXPECT_EQ(0u, NumCachedNewItems());
  EXPECT_EQ(0u, NumCachedNewSubsequences());
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&first, kBackgroundType),
                          IsSameId(&first, kForegroundType),
                          IsSameId(&second, kBackgroundType),
                          IsSameId(&second, kForegroundType)));
  EXPECT_DEFAULT_ROOT_CHUNK(4);

  InitRootChunk();
  first.Invalidate();
  second.Invalidate();
  DrawRect(context, first, kBackgroundType, IntRect(100, 100, 150, 150));
  DrawRect(context, first, kForegroundType, IntRect(100, 100, 150, 150));
  EXPECT_EQ(0u, NumCachedNewItems());
  EXPECT_EQ(0u, NumCachedNewSubsequences());
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&first, kBackgroundType),
                          IsSameId(&first, kForegroundType)));
  EXPECT_DEFAULT_ROOT_CHUNK(2);
}

TEST_P(PaintControllerTest, CachedDisplayItems) {
  FakeDisplayItemClient first("first");
  FakeDisplayItemClient second("second");
  GraphicsContext context(GetPaintController());
  InitRootChunk();

  DrawRect(context, first, kBackgroundType, IntRect(100, 100, 150, 150));
  DrawRect(context, second, kBackgroundType, IntRect(100, 100, 150, 150));
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&first, kBackgroundType),
                          IsSameId(&second, kBackgroundType)));
  EXPECT_TRUE(ClientCacheIsValid(first));
  EXPECT_TRUE(ClientCacheIsValid(second));
  sk_sp<const PaintRecord> first_paint_record =
      To<DrawingDisplayItem>(GetPaintController().GetDisplayItemList()[0])
          .GetPaintRecord();
  sk_sp<const PaintRecord> second_paint_record =
      To<DrawingDisplayItem>(GetPaintController().GetDisplayItemList()[1])
          .GetPaintRecord();

  first.Invalidate();
  EXPECT_FALSE(ClientCacheIsValid(first));
  EXPECT_TRUE(ClientCacheIsValid(second));

  InitRootChunk();
  DrawRect(context, first, kBackgroundType, IntRect(100, 100, 150, 150));
  DrawRect(context, second, kBackgroundType, IntRect(100, 100, 150, 150));
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&first, kBackgroundType),
                          IsSameId(&second, kBackgroundType)));
  // The first display item should be updated.
  EXPECT_NE(first_paint_record,
            To<DrawingDisplayItem>(GetPaintController().GetDisplayItemList()[0])
                .GetPaintRecord());
  // The second display item should be cached.
  if (!RuntimeEnabledFeatures::PaintUnderInvalidationCheckingEnabled()) {
    EXPECT_EQ(
        second_paint_record,
        To<DrawingDisplayItem>(GetPaintController().GetDisplayItemList()[1])
            .GetPaintRecord());
  }
  EXPECT_TRUE(ClientCacheIsValid(first));
  EXPECT_TRUE(ClientCacheIsValid(second));

  InvalidateAll();
  EXPECT_FALSE(ClientCacheIsValid(first));
  EXPECT_FALSE(ClientCacheIsValid(second));
}

TEST_P(PaintControllerTest, UpdateSwapOrderWithChildren) {
  FakeDisplayItemClient container1("container1");
  FakeDisplayItemClient content1("content1");
  FakeDisplayItemClient container2("container2");
  FakeDisplayItemClient content2("content2");
  GraphicsContext context(GetPaintController());
  InitRootChunk();

  DrawRect(context, container1, kBackgroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, content1, kBackgroundType, IntRect(100, 100, 50, 200));
  DrawRect(context, content1, kForegroundType, IntRect(100, 100, 50, 200));
  DrawRect(context, container1, kForegroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, container2, kBackgroundType, IntRect(100, 200, 100, 100));
  DrawRect(context, content2, kBackgroundType, IntRect(100, 200, 50, 200));
  DrawRect(context, content2, kForegroundType, IntRect(100, 200, 50, 200));
  DrawRect(context, container2, kForegroundType, IntRect(100, 200, 100, 100));
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&container1, kBackgroundType),
                          IsSameId(&content1, kBackgroundType),
                          IsSameId(&content1, kForegroundType),
                          IsSameId(&container1, kForegroundType),
                          IsSameId(&container2, kBackgroundType),
                          IsSameId(&content2, kBackgroundType),
                          IsSameId(&content2, kForegroundType),
                          IsSameId(&container2, kForegroundType)));

  InitRootChunk();

  // Simulate the situation when |container1| gets a z-index that is greater
  // than that of |container2|.
  DrawRect(context, container2, kBackgroundType, IntRect(100, 200, 100, 100));
  DrawRect(context, content2, kBackgroundType, IntRect(100, 200, 50, 200));
  DrawRect(context, content2, kForegroundType, IntRect(100, 200, 50, 200));
  DrawRect(context, container2, kForegroundType, IntRect(100, 200, 100, 100));
  DrawRect(context, container1, kBackgroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, content1, kBackgroundType, IntRect(100, 100, 50, 200));
  DrawRect(context, content1, kForegroundType, IntRect(100, 100, 50, 200));
  DrawRect(context, container1, kForegroundType, IntRect(100, 100, 100, 100));
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&container2, kBackgroundType),
                          IsSameId(&content2, kBackgroundType),
                          IsSameId(&content2, kForegroundType),
                          IsSameId(&container2, kForegroundType),
                          IsSameId(&container1, kBackgroundType),
                          IsSameId(&content1, kBackgroundType),
                          IsSameId(&content1, kForegroundType),
                          IsSameId(&container1, kForegroundType)));
  EXPECT_DEFAULT_ROOT_CHUNK(8);
}

TEST_P(PaintControllerTest, UpdateSwapOrderWithChildrenAndInvalidation) {
  FakeDisplayItemClient container1("container1");
  FakeDisplayItemClient content1("content1");
  FakeDisplayItemClient container2("container2");
  FakeDisplayItemClient content2("content2");
  GraphicsContext context(GetPaintController());
  InitRootChunk();

  DrawRect(context, container1, kBackgroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, content1, kBackgroundType, IntRect(100, 100, 50, 200));
  DrawRect(context, content1, kForegroundType, IntRect(100, 100, 50, 200));
  DrawRect(context, container1, kForegroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, container2, kBackgroundType, IntRect(100, 200, 100, 100));
  DrawRect(context, content2, kBackgroundType, IntRect(100, 200, 50, 200));
  DrawRect(context, content2, kForegroundType, IntRect(100, 200, 50, 200));
  DrawRect(context, container2, kForegroundType, IntRect(100, 200, 100, 100));
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&container1, kBackgroundType),
                          IsSameId(&content1, kBackgroundType),
                          IsSameId(&content1, kForegroundType),
                          IsSameId(&container1, kForegroundType),
                          IsSameId(&container2, kBackgroundType),
                          IsSameId(&content2, kBackgroundType),
                          IsSameId(&content2, kForegroundType),
                          IsSameId(&container2, kForegroundType)));

  InitRootChunk();

  // Simulate the situation when |container1| gets a z-index that is greater
  // than that of |container2|, and |container1| is invalidated.
  container1.Invalidate();
  DrawRect(context, container2, kBackgroundType, IntRect(100, 200, 100, 100));
  DrawRect(context, content2, kBackgroundType, IntRect(100, 200, 50, 200));
  DrawRect(context, content2, kForegroundType, IntRect(100, 200, 50, 200));
  DrawRect(context, container2, kForegroundType, IntRect(100, 200, 100, 100));
  DrawRect(context, container1, kBackgroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, content1, kBackgroundType, IntRect(100, 100, 50, 200));
  DrawRect(context, content1, kForegroundType, IntRect(100, 100, 50, 200));
  DrawRect(context, container1, kForegroundType, IntRect(100, 100, 100, 100));
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&container2, kBackgroundType),
                          IsSameId(&content2, kBackgroundType),
                          IsSameId(&content2, kForegroundType),
                          IsSameId(&container2, kForegroundType),
                          IsSameId(&container1, kBackgroundType),
                          IsSameId(&content1, kBackgroundType),
                          IsSameId(&content1, kForegroundType),
                          IsSameId(&container1, kForegroundType)));
  EXPECT_DEFAULT_ROOT_CHUNK(8);
}

TEST_P(PaintControllerTest, CachedSubsequenceForcePaintChunk) {
  if (RuntimeEnabledFeatures::PaintUnderInvalidationCheckingEnabled())
    return;

  GraphicsContext context(GetPaintController());

  FakeDisplayItemClient root("root");
  auto root_properties = DefaultPaintChunkProperties();
  PaintChunk::Id root_id(root, DisplayItem::kCaret);
  GetPaintController().UpdateCurrentPaintChunkProperties(&root_id,
                                                         root_properties);
  DrawRect(context, root, kBackgroundType, IntRect(100, 100, 100, 100));

  FakeDisplayItemClient container("container");
  auto container_properties = DefaultPaintChunkProperties();
  PaintChunk::Id container_id(container, DisplayItem::kCaret);
  {
    SubsequenceRecorder r(context, container);
    GetPaintController().UpdateCurrentPaintChunkProperties(
        &container_id, container_properties);
    DrawRect(context, container, kBackgroundType, IntRect(100, 100, 100, 100));
    DrawRect(context, container, kForegroundType, IntRect(100, 100, 100, 100));
  }

  DrawRect(context, root, kForegroundType, IntRect(100, 100, 100, 100));

  CommitAndFinishCycle();

  // Even though the paint properties match, |container| should receive its
  // own PaintChunk because it created a subsequence.
  EXPECT_THAT(
      GetPaintController().PaintChunks(),
      ElementsAre(IsPaintChunk(0, 1, root_id, root_properties),
                  IsPaintChunk(1, 3, container_id, container_properties),
                  IsPaintChunk(3, 4, PaintChunk::Id(root, kForegroundType),
                               root_properties)));

  GetPaintController().UpdateCurrentPaintChunkProperties(&root_id,
                                                         root_properties);
  DrawRect(context, root, kBackgroundType, IntRect(100, 100, 100, 100));
  EXPECT_TRUE(GetPaintController().UseCachedSubsequenceIfPossible(container));
  DrawRect(context, root, kForegroundType, IntRect(100, 100, 100, 100));
  CommitAndFinishCycle();

  // |container| should still receive its own PaintChunk because it is a cached
  // subsequence.
  EXPECT_THAT(
      GetPaintController().PaintChunks(),
      ElementsAre(IsPaintChunk(0, 1, root_id, root_properties),
                  IsPaintChunk(1, 3, container_id, container_properties),
                  IsPaintChunk(3, 4, PaintChunk::Id(root, kForegroundType),
                               container_properties)));
}

TEST_P(PaintControllerTest, CachedSubsequenceSwapOrder) {
  FakeDisplayItemClient container1("container1");
  FakeDisplayItemClient content1("content1");
  FakeDisplayItemClient container2("container2");
  FakeDisplayItemClient content2("content2");
  GraphicsContext context(GetPaintController());

  PaintChunk::Id container1_id(container1, kBackgroundType);
  auto container1_effect = CreateOpacityEffect(e0(), 0.5);
  auto container1_properties = DefaultPaintChunkProperties();
  container1_properties.SetEffect(*container1_effect);

  PaintChunk::Id container2_id(container2, kBackgroundType);
  auto container2_effect = CreateOpacityEffect(e0(), 0.5);
  auto container2_properties = DefaultPaintChunkProperties();
  container2_properties.SetEffect(*container2_effect);

  {
    GetPaintController().UpdateCurrentPaintChunkProperties(
        &container1_id, container1_properties);

    SubsequenceRecorder r(context, container1);
    DrawRect(context, container1, kBackgroundType, IntRect(100, 100, 100, 100));
    DrawRect(context, content1, kBackgroundType, IntRect(100, 100, 50, 200));
    DrawRect(context, content1, kForegroundType, IntRect(100, 100, 50, 200));
    DrawRect(context, container1, kForegroundType, IntRect(100, 100, 100, 100));
  }
  {
    GetPaintController().UpdateCurrentPaintChunkProperties(
        &container2_id, container2_properties);

    SubsequenceRecorder r(context, container2);
    DrawRect(context, container2, kBackgroundType, IntRect(100, 200, 100, 100));
    DrawRect(context, content2, kBackgroundType, IntRect(100, 200, 50, 200));
    DrawRect(context, content2, kForegroundType, IntRect(100, 200, 50, 200));
    DrawRect(context, container2, kForegroundType, IntRect(100, 200, 100, 100));
  }
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&container1, kBackgroundType),
                          IsSameId(&content1, kBackgroundType),
                          IsSameId(&content1, kForegroundType),
                          IsSameId(&container1, kForegroundType),

                          IsSameId(&container2, kBackgroundType),
                          IsSameId(&content2, kBackgroundType),
                          IsSameId(&content2, kForegroundType),
                          IsSameId(&container2, kForegroundType)));

  EXPECT_SUBSEQUENCE(container1, 0, 1);
  EXPECT_NO_SUBSEQUENCE(content1);
  EXPECT_SUBSEQUENCE(container2, 1, 2);
  EXPECT_NO_SUBSEQUENCE(content2);

  EXPECT_THAT(
      GetPaintController().PaintChunks(),
      ElementsAre(IsPaintChunk(0, 4, container1_id, container1_properties),
                  IsPaintChunk(4, 8, container2_id, container2_properties)));

  // Simulate the situation when |container1| gets a z-index that is greater
  // than that of |container2|.
  if (RuntimeEnabledFeatures::PaintUnderInvalidationCheckingEnabled()) {
    // When under-invalidation-checking is enabled,
    // UseCachedSubsequenceIfPossible is forced off, and the client is expected
    // to create the same painting as in the previous paint.
    EXPECT_FALSE(SubsequenceRecorder::UseCachedSubsequenceIfPossible(
        context, container2));
    {
      GetPaintController().UpdateCurrentPaintChunkProperties(
          &container2_id, container2_properties);

      SubsequenceRecorder r(context, container2);
      DrawRect(context, container2, kBackgroundType,
               IntRect(100, 200, 100, 100));
      DrawRect(context, content2, kBackgroundType, IntRect(100, 200, 50, 200));
      DrawRect(context, content2, kForegroundType, IntRect(100, 200, 50, 200));
      DrawRect(context, container2, kForegroundType,
               IntRect(100, 200, 100, 100));
    }
    EXPECT_FALSE(SubsequenceRecorder::UseCachedSubsequenceIfPossible(
        context, container1));
    {
      GetPaintController().UpdateCurrentPaintChunkProperties(
          &container1_id, container1_properties);

      SubsequenceRecorder r(context, container1);
      DrawRect(context, container1, kBackgroundType,
               IntRect(100, 100, 100, 100));
      DrawRect(context, content1, kBackgroundType, IntRect(100, 100, 50, 200));
      DrawRect(context, content1, kForegroundType, IntRect(100, 100, 50, 200));
      DrawRect(context, container1, kForegroundType,
               IntRect(100, 100, 100, 100));
    }
  } else {
    EXPECT_TRUE(SubsequenceRecorder::UseCachedSubsequenceIfPossible(
        context, container2));
    EXPECT_TRUE(SubsequenceRecorder::UseCachedSubsequenceIfPossible(
        context, container1));
  }

  EXPECT_EQ(8u, NumCachedNewItems());
  EXPECT_EQ(2u, NumCachedNewSubsequences());
#if DCHECK_IS_ON()
  EXPECT_EQ(0u, NumIndexedItems());
  EXPECT_EQ(0u, NumSequentialMatches());
  EXPECT_EQ(0u, NumOutOfOrderMatches());
#endif

  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&container2, kBackgroundType),
                          IsSameId(&content2, kBackgroundType),
                          IsSameId(&content2, kForegroundType),
                          IsSameId(&container2, kForegroundType),
                          IsSameId(&container1, kBackgroundType),
                          IsSameId(&content1, kBackgroundType),
                          IsSameId(&content1, kForegroundType),
                          IsSameId(&container1, kForegroundType)));

  EXPECT_SUBSEQUENCE(container1, 1, 2);
  EXPECT_NO_SUBSEQUENCE(content1);
  EXPECT_SUBSEQUENCE(container2, 0, 1);
  EXPECT_NO_SUBSEQUENCE(content2);

  EXPECT_THAT(
      GetPaintController().PaintChunks(),
      ElementsAre(IsPaintChunk(0, 4, container2_id, container2_properties),
                  IsPaintChunk(4, 8, container1_id, container1_properties)));
}

TEST_P(PaintControllerTest, CachedSubsequenceAndDisplayItemsSwapOrder) {
  FakeDisplayItemClient content1("content1");
  FakeDisplayItemClient container2("container2");
  FakeDisplayItemClient content2("content2");
  GraphicsContext context(GetPaintController());

  PaintChunk::Id content1_id(content1, kBackgroundType);
  PaintChunk::Id container2_id(container2, kBackgroundType);
  PaintChunk::Id content2_id(content2, kBackgroundType);

  InitRootChunk();

  DrawRect(context, content1, kBackgroundType, IntRect(100, 100, 50, 200));
  {
    SubsequenceRecorder r(context, container2);
    DrawRect(context, container2, kBackgroundType, IntRect(100, 200, 100, 100));
    DrawRect(context, content2, kBackgroundType, IntRect(100, 200, 50, 200));
    DrawRect(context, content2, kForegroundType, IntRect(100, 200, 50, 200));
    DrawRect(context, container2, kForegroundType, IntRect(100, 200, 100, 100));
  }
  DrawRect(context, content1, kForegroundType, IntRect(100, 100, 50, 200));
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&content1, kBackgroundType),
                          IsSameId(&container2, kBackgroundType),
                          IsSameId(&content2, kBackgroundType),
                          IsSameId(&content2, kForegroundType),
                          IsSameId(&container2, kForegroundType),
                          IsSameId(&content1, kForegroundType)));

  EXPECT_NO_SUBSEQUENCE(content1);
  EXPECT_SUBSEQUENCE(container2, 1, 2);
  EXPECT_NO_SUBSEQUENCE(content2);

  EXPECT_THAT(
      GetPaintController().PaintChunks(),
      ElementsAre(
          IsPaintChunk(0, 1, DefaultRootChunkId(),
                       DefaultPaintChunkProperties()),
          IsPaintChunk(1, 5, PaintChunk::Id(container2, kBackgroundType),
                       DefaultPaintChunkProperties()),
          IsPaintChunk(5, 6, PaintChunk::Id(content1, kForegroundType),
                       DefaultPaintChunkProperties())));

  // Simulate the situation when |container2| gets a z-index that is smaller
  // than that of |content1|.
  InitRootChunk();
  if (RuntimeEnabledFeatures::PaintUnderInvalidationCheckingEnabled()) {
    // When under-invalidation-checking is enabled,
    // UseCachedSubsequenceIfPossible is forced off, and the client is expected
    // to create the same painting as in the previous paint.
    EXPECT_FALSE(SubsequenceRecorder::UseCachedSubsequenceIfPossible(
        context, container2));
    {
      SubsequenceRecorder r(context, container2);
      DrawRect(context, container2, kBackgroundType,
               IntRect(100, 200, 100, 100));
      DrawRect(context, content2, kBackgroundType, IntRect(100, 200, 50, 200));
      DrawRect(context, content2, kForegroundType, IntRect(100, 200, 50, 200));
      DrawRect(context, container2, kForegroundType,
               IntRect(100, 200, 100, 100));
    }
    DrawRect(context, content1, kBackgroundType, IntRect(100, 100, 50, 200));
    DrawRect(context, content1, kForegroundType, IntRect(100, 100, 50, 200));
  } else {
    EXPECT_TRUE(SubsequenceRecorder::UseCachedSubsequenceIfPossible(
        context, container2));
    EXPECT_TRUE(DrawingRecorder::UseCachedDrawingIfPossible(context, content1,
                                                            kBackgroundType));
    EXPECT_TRUE(DrawingRecorder::UseCachedDrawingIfPossible(context, content1,
                                                            kForegroundType));
  }

  EXPECT_EQ(6u, NumCachedNewItems());
  EXPECT_EQ(1u, NumCachedNewSubsequences());
#if DCHECK_IS_ON()
  EXPECT_EQ(0u, NumIndexedItems());
  EXPECT_EQ(2u, NumSequentialMatches());
  EXPECT_EQ(0u, NumOutOfOrderMatches());
#endif

  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&container2, kBackgroundType),
                          IsSameId(&content2, kBackgroundType),
                          IsSameId(&content2, kForegroundType),
                          IsSameId(&container2, kForegroundType),
                          IsSameId(&content1, kBackgroundType),
                          IsSameId(&content1, kForegroundType)));

  EXPECT_NO_SUBSEQUENCE(content1);
  EXPECT_SUBSEQUENCE(container2, 0, 1);
  EXPECT_NO_SUBSEQUENCE(content2);

  EXPECT_THAT(
      GetPaintController().PaintChunks(),
      ElementsAre(
          IsPaintChunk(0, 4, PaintChunk::Id(container2, kBackgroundType),
                       DefaultPaintChunkProperties()),
          IsPaintChunk(4, 6, PaintChunk::Id(content1, kBackgroundType),
                       DefaultPaintChunkProperties())));
}

TEST_P(PaintControllerTest, DisplayItemSwapOrderBeforeCachedSubsequence) {
  FakeDisplayItemClient content1a("content1a");
  FakeDisplayItemClient content1b("content1b");
  FakeDisplayItemClient container2("container2");
  FakeDisplayItemClient content3("content3");
  GraphicsContext context(GetPaintController());

  PaintChunk::Id content1a_id(content1a, kBackgroundType);
  PaintChunk::Id content1b_id(content1b, kBackgroundType);
  PaintChunk::Id container2_id(container2, kBackgroundType);
  PaintChunk::Id content3_id(content3, kBackgroundType);
  IntRect rect(100, 100, 50, 200);

  InitRootChunk();

  DrawRect(context, content1a, kBackgroundType, rect);
  DrawRect(context, content1b, kBackgroundType, rect);
  {
    SubsequenceRecorder r(context, container2);
    DrawRect(context, container2, kBackgroundType, rect);
  }
  DrawRect(context, content3, kBackgroundType, rect);
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&content1a, kBackgroundType),
                          IsSameId(&content1b, kBackgroundType),
                          IsSameId(&container2, kBackgroundType),
                          IsSameId(&content3, kBackgroundType)));

  // New paint order:
  // Subsequence(container1): container1, content1b(cached), content1a(cached).
  // Subsequence(container2): cached
  // Subsequence(contaienr3): container3, content3
  InitRootChunk();
  if (RuntimeEnabledFeatures::PaintUnderInvalidationCheckingEnabled()) {
    EXPECT_FALSE(DrawingRecorder::UseCachedDrawingIfPossible(context, content1b,
                                                             kBackgroundType));
    DrawRect(context, content1b, kBackgroundType, rect);
    EXPECT_FALSE(DrawingRecorder::UseCachedDrawingIfPossible(context, content1a,
                                                             kBackgroundType));
    DrawRect(context, content1a, kBackgroundType, rect);
    {
      EXPECT_FALSE(SubsequenceRecorder::UseCachedSubsequenceIfPossible(
          context, container2));
      SubsequenceRecorder r(context, container2);
      DrawRect(context, container2, kBackgroundType, rect);
    }
    EXPECT_FALSE(DrawingRecorder::UseCachedDrawingIfPossible(context, content3,
                                                             kBackgroundType));
    DrawRect(context, content3, kBackgroundType, rect);
  } else {
    EXPECT_TRUE(DrawingRecorder::UseCachedDrawingIfPossible(context, content1b,
                                                            kBackgroundType));
    EXPECT_TRUE(DrawingRecorder::UseCachedDrawingIfPossible(context, content1a,
                                                            kBackgroundType));
    EXPECT_TRUE(SubsequenceRecorder::UseCachedSubsequenceIfPossible(
        context, container2));
    EXPECT_TRUE(DrawingRecorder::UseCachedDrawingIfPossible(context, content3,
                                                            kBackgroundType));
  }

  EXPECT_EQ(4u, NumCachedNewItems());
  EXPECT_EQ(1u, NumCachedNewSubsequences());
#if DCHECK_IS_ON()
  EXPECT_EQ(1u, NumIndexedItems());
  EXPECT_EQ(2u, NumSequentialMatches());
  EXPECT_EQ(1u, NumOutOfOrderMatches());
#endif

  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&content1b, kBackgroundType),
                          IsSameId(&content1a, kBackgroundType),
                          IsSameId(&container2, kBackgroundType),
                          IsSameId(&content3, kBackgroundType)));
}

TEST_P(PaintControllerTest, CachedSubsequenceContainingFragments) {
  GraphicsContext context(GetPaintController());
  FakeDisplayItemClient root("root");
  constexpr wtf_size_t kFragmentCount = 3;
  FakeDisplayItemClient container("container");

  // The first paint.
  auto paint_container = [this, &context, &container]() {
    SubsequenceRecorder r(context, container);
    for (wtf_size_t i = 0; i < kFragmentCount; ++i) {
      ScopedDisplayItemFragment scoped_fragment(context, i);
      ScopedPaintChunkProperties content_chunk_properties(
          GetPaintController(), DefaultPaintChunkProperties(), container,
          kBackgroundType);
      DrawRect(context, container, kBackgroundType,
               IntRect(100, 100, 100, 100));
    }
  };
  {
    ScopedPaintChunkProperties root_chunk_properties(
        GetPaintController(), DefaultPaintChunkProperties(), root,
        kBackgroundType);
    DrawRect(context, root, kBackgroundType, IntRect(100, 100, 100, 100));
    paint_container();
    DrawRect(context, root, kForegroundType, IntRect(100, 100, 100, 100));
  }
  CommitAndFinishCycle();

  auto check_paint_results = [this, &root, &container]() {
    EXPECT_THAT(
        GetPaintController().PaintChunks(),
        ElementsAre(
            IsPaintChunk(0, 1, PaintChunk::Id(root, kBackgroundType),
                         DefaultPaintChunkProperties()),
            // One chunk for all of the fragments because they have the
            // same properties.
            IsPaintChunk(1, 4, PaintChunk::Id(container, kBackgroundType),
                         DefaultPaintChunkProperties()),
            IsPaintChunk(4, 5, PaintChunk::Id(root, kForegroundType),
                         DefaultPaintChunkProperties())));
  };
  // Check results of the first paint.
  check_paint_results();

  // The second paint.
  {
    ScopedPaintChunkProperties root_chunk_properties(
        GetPaintController(), DefaultPaintChunkProperties(), root,
        kBackgroundType);
    DrawRect(context, root, kBackgroundType, IntRect(100, 100, 100, 100));

    if (RuntimeEnabledFeatures::PaintUnderInvalidationCheckingEnabled()) {
      EXPECT_FALSE(
          GetPaintController().UseCachedSubsequenceIfPossible(container));
      paint_container();
    } else {
      EXPECT_TRUE(
          GetPaintController().UseCachedSubsequenceIfPossible(container));
    }
    DrawRect(context, root, kForegroundType, IntRect(100, 100, 100, 100));
  }
  CommitAndFinishCycle();

  // The second paint should produce the exactly same results.
  check_paint_results();
}

TEST_P(PaintControllerTest, UpdateSwapOrderCrossingChunks) {
  FakeDisplayItemClient container1("container1");
  FakeDisplayItemClient content1("content1");
  FakeDisplayItemClient container2("container2");
  FakeDisplayItemClient content2("content2");
  GraphicsContext context(GetPaintController());

  PaintChunk::Id container1_id(container1, kBackgroundType);
  auto container1_effect = CreateOpacityEffect(e0(), 0.5);
  auto container1_properties = DefaultPaintChunkProperties();
  container1_properties.SetEffect(*container1_effect);

  PaintChunk::Id container2_id(container2, kBackgroundType);
  auto container2_effect = CreateOpacityEffect(e0(), 0.5);
  auto container2_properties = DefaultPaintChunkProperties();
  container2_properties.SetEffect(*container2_effect);

  GetPaintController().UpdateCurrentPaintChunkProperties(&container1_id,
                                                         container1_properties);
  DrawRect(context, container1, kBackgroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, content1, kBackgroundType, IntRect(100, 100, 50, 200));
  GetPaintController().UpdateCurrentPaintChunkProperties(&container2_id,
                                                         container2_properties);
  DrawRect(context, container2, kBackgroundType, IntRect(100, 200, 100, 100));
  DrawRect(context, content2, kBackgroundType, IntRect(100, 200, 50, 200));
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&container1, kBackgroundType),
                          IsSameId(&content1, kBackgroundType),
                          IsSameId(&container2, kBackgroundType),
                          IsSameId(&content2, kBackgroundType)));

  EXPECT_THAT(
      GetPaintController().PaintChunks(),
      ElementsAre(IsPaintChunk(0, 2, container1_id, container1_properties),
                  IsPaintChunk(2, 4, container2_id, container2_properties)));

  // Move content2 into container1, without invalidation.
  GetPaintController().UpdateCurrentPaintChunkProperties(&container1_id,
                                                         container1_properties);
  DrawRect(context, container1, kBackgroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, content1, kBackgroundType, IntRect(100, 100, 50, 200));
  DrawRect(context, content2, kBackgroundType, IntRect(100, 200, 50, 200));
  GetPaintController().UpdateCurrentPaintChunkProperties(&container2_id,
                                                         container2_properties);
  DrawRect(context, container2, kBackgroundType, IntRect(100, 200, 100, 100));

  EXPECT_EQ(4u, NumCachedNewItems());
  EXPECT_EQ(0u, NumCachedNewSubsequences());
#if DCHECK_IS_ON()
  EXPECT_EQ(1u, NumIndexedItems());
  EXPECT_EQ(3u, NumSequentialMatches());
  EXPECT_EQ(1u, NumOutOfOrderMatches());
#endif

  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&container1, kBackgroundType),
                          IsSameId(&content1, kBackgroundType),
                          IsSameId(&content2, kBackgroundType),
                          IsSameId(&container2, kBackgroundType)));

  EXPECT_THAT(
      GetPaintController().PaintChunks(),
      ElementsAre(IsPaintChunk(0, 3, container1_id, container1_properties),
                  IsPaintChunk(3, 4, container2_id, container2_properties)));
}

TEST_P(PaintControllerTest, OutOfOrderNoCrash) {
  FakeDisplayItemClient client("client");
  GraphicsContext context(GetPaintController());

  const DisplayItem::Type kType1 = DisplayItem::kDrawingFirst;
  const DisplayItem::Type kType2 =
      static_cast<DisplayItem::Type>(DisplayItem::kDrawingFirst + 1);
  const DisplayItem::Type kType3 =
      static_cast<DisplayItem::Type>(DisplayItem::kDrawingFirst + 2);
  const DisplayItem::Type kType4 =
      static_cast<DisplayItem::Type>(DisplayItem::kDrawingFirst + 3);

  InitRootChunk();
  DrawRect(context, client, kType1, IntRect(100, 100, 100, 100));
  DrawRect(context, client, kType2, IntRect(100, 100, 50, 200));
  DrawRect(context, client, kType3, IntRect(100, 100, 50, 200));
  DrawRect(context, client, kType4, IntRect(100, 100, 100, 100));

  CommitAndFinishCycle();

  InitRootChunk();
  DrawRect(context, client, kType2, IntRect(100, 100, 50, 200));
  DrawRect(context, client, kType3, IntRect(100, 100, 50, 200));
  DrawRect(context, client, kType1, IntRect(100, 100, 100, 100));
  DrawRect(context, client, kType4, IntRect(100, 100, 100, 100));

  CommitAndFinishCycle();
}

TEST_P(PaintControllerTest, CachedNestedSubsequenceUpdate) {
  FakeDisplayItemClient container1("container1");
  FakeDisplayItemClient content1("content1");
  FakeDisplayItemClient container2("container2");
  FakeDisplayItemClient content2("content2");
  GraphicsContext context(GetPaintController());

  PaintChunk::Id container1_background_id(container1, kBackgroundType);
  auto container1_effect = CreateOpacityEffect(e0(), 0.5);
  auto container1_background_properties = DefaultPaintChunkProperties();
  container1_background_properties.SetEffect(*container1_effect);
  PaintChunk::Id container1_foreground_id(container1, kForegroundType);
  auto container1_foreground_properties = DefaultPaintChunkProperties();
  container1_foreground_properties.SetEffect(*container1_effect);

  PaintChunk::Id content1_id(content1, kBackgroundType);
  auto content1_effect = CreateOpacityEffect(e0(), 0.6);
  auto content1_properties = DefaultPaintChunkProperties();
  content1_properties.SetEffect(*content1_effect);

  PaintChunk::Id container2_background_id(container2, kBackgroundType);
  auto container2_effect = CreateOpacityEffect(e0(), 0.7);
  auto container2_background_properties = DefaultPaintChunkProperties();
  container2_background_properties.SetEffect(*container2_effect);

  PaintChunk::Id content2_id(content2, kBackgroundType);
  auto content2_effect = CreateOpacityEffect(e0(), 0.8);
  auto content2_properties = DefaultPaintChunkProperties();
  content2_properties.SetEffect(*content2_effect);

  {
    SubsequenceRecorder r(context, container1);
    GetPaintController().UpdateCurrentPaintChunkProperties(
        &container1_background_id, container1_background_properties);
    DrawRect(context, container1, kBackgroundType, IntRect(100, 100, 100, 100));

    {
      SubsequenceRecorder r(context, content1);
      GetPaintController().UpdateCurrentPaintChunkProperties(
          &content1_id, content1_properties);
      DrawRect(context, content1, kBackgroundType, IntRect(100, 100, 50, 200));
      DrawRect(context, content1, kForegroundType, IntRect(100, 100, 50, 200));
    }
    GetPaintController().UpdateCurrentPaintChunkProperties(
        &container1_foreground_id, container1_foreground_properties);
    DrawRect(context, container1, kForegroundType, IntRect(100, 100, 100, 100));
  }
  {
    SubsequenceRecorder r(context, container2);
    GetPaintController().UpdateCurrentPaintChunkProperties(
        &container2_background_id, container2_background_properties);
    DrawRect(context, container2, kBackgroundType, IntRect(100, 200, 100, 100));
    {
      SubsequenceRecorder r(context, content2);
      GetPaintController().UpdateCurrentPaintChunkProperties(
          &content2_id, content2_properties);
      DrawRect(context, content2, kBackgroundType, IntRect(100, 200, 50, 200));
    }
  }
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&container1, kBackgroundType),
                          IsSameId(&content1, kBackgroundType),
                          IsSameId(&content1, kForegroundType),
                          IsSameId(&container1, kForegroundType),
                          IsSameId(&container2, kBackgroundType),
                          IsSameId(&content2, kBackgroundType)));

  EXPECT_SUBSEQUENCE(container1, 0, 3);
  EXPECT_SUBSEQUENCE(content1, 1, 2);
  EXPECT_SUBSEQUENCE(container2, 3, 5);
  EXPECT_SUBSEQUENCE(content2, 4, 5);

  EXPECT_THAT(
      GetPaintController().PaintChunks(),
      ElementsAre(IsPaintChunk(0, 1, container1_background_id,
                               container1_background_properties),
                  IsPaintChunk(1, 3, content1_id, content1_properties),
                  IsPaintChunk(3, 4, container1_foreground_id,
                               container1_foreground_properties),
                  IsPaintChunk(4, 5, container2_background_id,
                               container2_background_properties),
                  IsPaintChunk(5, 6, content2_id, content2_properties)));

  // Invalidate container1 but not content1.
  container1.Invalidate();
  // Container2 itself now becomes empty (but still has the 'content2' child),
  // and chooses not to output subsequence info.
  container2.Invalidate();
  content2.Invalidate();
  EXPECT_FALSE(
      SubsequenceRecorder::UseCachedSubsequenceIfPossible(context, container2));
  EXPECT_FALSE(
      SubsequenceRecorder::UseCachedSubsequenceIfPossible(context, content2));
  // Content2 now outputs foreground only.
  {
    SubsequenceRecorder r(context, content2);
    GetPaintController().UpdateCurrentPaintChunkProperties(&content2_id,
                                                           content2_properties);
    DrawRect(context, content2, kForegroundType, IntRect(100, 200, 50, 200));
  }
  // Repaint container1 with foreground only.
  {
    SubsequenceRecorder r(context, container1);
    EXPECT_FALSE(SubsequenceRecorder::UseCachedSubsequenceIfPossible(
        context, container1));
    // Use cached subsequence of content1.
    if (RuntimeEnabledFeatures::PaintUnderInvalidationCheckingEnabled()) {
      // When under-invalidation-checking is enabled,
      // UseCachedSubsequenceIfPossible is forced off, and the client is
      // expected to create the same painting as in the previous paint.
      EXPECT_FALSE(SubsequenceRecorder::UseCachedSubsequenceIfPossible(
          context, content1));
      SubsequenceRecorder r(context, content1);
      GetPaintController().UpdateCurrentPaintChunkProperties(
          &content1_id, content1_properties);
      DrawRect(context, content1, kBackgroundType, IntRect(100, 100, 50, 200));
      DrawRect(context, content1, kForegroundType, IntRect(100, 100, 50, 200));
    } else {
      EXPECT_TRUE(SubsequenceRecorder::UseCachedSubsequenceIfPossible(
          context, content1));
    }
    GetPaintController().UpdateCurrentPaintChunkProperties(
        &container1_foreground_id, container1_foreground_properties);
    DrawRect(context, container1, kForegroundType, IntRect(100, 100, 100, 100));
  }

  EXPECT_EQ(2u, NumCachedNewItems());
  EXPECT_EQ(1u, NumCachedNewSubsequences());
#if DCHECK_IS_ON()
  EXPECT_EQ(0u, NumIndexedItems());
  EXPECT_EQ(0u, NumSequentialMatches());
  EXPECT_EQ(0u, NumOutOfOrderMatches());
#endif

  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&content2, kForegroundType),
                          IsSameId(&content1, kBackgroundType),
                          IsSameId(&content1, kForegroundType),
                          IsSameId(&container1, kForegroundType)));

  EXPECT_NO_SUBSEQUENCE(container2);
  EXPECT_SUBSEQUENCE(content2, 0, 1);
  EXPECT_SUBSEQUENCE(container1, 1, 3);
  EXPECT_SUBSEQUENCE(content1, 1, 2);

  EXPECT_THAT(GetPaintController().PaintChunks(),
              ElementsAre(IsPaintChunk(0, 1, content2_id, content2_properties),
                          IsPaintChunk(1, 3, content1_id, content1_properties),
                          IsPaintChunk(3, 4, container1_foreground_id,
                                       container1_foreground_properties)));
}

TEST_P(PaintControllerTest, CachedNestedSubsequenceKeepingDescendants) {
  if (RuntimeEnabledFeatures::PaintUnderInvalidationCheckingEnabled())
    return;

  FakeDisplayItemClient root("root");
  auto properties = DefaultPaintChunkProperties();
  PaintChunk::Id root_id(root, DisplayItem::kLayerChunk);
  GraphicsContext context(GetPaintController());
  GetPaintController().UpdateCurrentPaintChunkProperties(&root_id, properties);

  FakeDisplayItemClient container1("container1");
  PaintChunk::Id container1_bg_id(container1, kBackgroundType);
  PaintChunk::Id container1_fg_id(container1, kForegroundType);
  FakeDisplayItemClient content1a("content1a");
  PaintChunk::Id content1a_id(content1a, kBackgroundType);
  FakeDisplayItemClient content1b("content1b");
  PaintChunk::Id content1b_id(content1b, kForegroundType);
  FakeDisplayItemClient container2("container2");
  PaintChunk::Id container2_id(container2, kBackgroundType);
  FakeDisplayItemClient content2a("content2a");
  PaintChunk::Id content2a_id(content2a, kBackgroundType);
  FakeDisplayItemClient content2b("content2b");
  PaintChunk::Id content2b_id(content2b, kForegroundType);

  {
    SubsequenceRecorder r(context, container1);
    DrawRect(context, container1, kBackgroundType, IntRect(100, 100, 100, 100));
    {
      SubsequenceRecorder r(context, content1a);
      DrawRect(context, content1a, kBackgroundType, IntRect(100, 100, 50, 200));
    }
    {
      SubsequenceRecorder r(context, content1b);
      DrawRect(context, content1b, kForegroundType, IntRect(100, 100, 50, 200));
    }
    DrawRect(context, container1, kForegroundType, IntRect(100, 100, 100, 100));
  }
  {
    SubsequenceRecorder r(context, container2);
    DrawRect(context, container2, kBackgroundType, IntRect(100, 200, 100, 100));
    {
      SubsequenceRecorder r(context, content2a);
      DrawRect(context, content2a, kBackgroundType, IntRect(100, 200, 50, 200));
    }
    {
      SubsequenceRecorder r(context, content2b);
      DrawRect(context, content2b, kForegroundType, IntRect(100, 200, 50, 200));
    }
  }

  EXPECT_EQ(0u, NumCachedNewItems());
  EXPECT_EQ(0u, NumCachedNewSubsequences());
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&container1, kBackgroundType),
                          IsSameId(&content1a, kBackgroundType),
                          IsSameId(&content1b, kForegroundType),
                          IsSameId(&container1, kForegroundType),
                          IsSameId(&container2, kBackgroundType),
                          IsSameId(&content2a, kBackgroundType),
                          IsSameId(&content2b, kForegroundType)));

  EXPECT_SUBSEQUENCE(container1, 0, 4);
  EXPECT_SUBSEQUENCE(content1a, 1, 2);
  EXPECT_SUBSEQUENCE(content1b, 2, 3);
  EXPECT_SUBSEQUENCE(container2, 4, 7);
  EXPECT_SUBSEQUENCE(content2a, 5, 6);
  EXPECT_SUBSEQUENCE(content2b, 6, 7);

  EXPECT_THAT(GetPaintController().PaintChunks(),
              ElementsAre(IsPaintChunk(0, 1, container1_bg_id, properties),
                          IsPaintChunk(1, 2, content1a_id, properties),
                          IsPaintChunk(2, 3, content1b_id, properties),
                          IsPaintChunk(3, 4, container1_fg_id, properties),
                          IsPaintChunk(4, 5, container2_id, properties),
                          IsPaintChunk(5, 6, content2a_id, properties),
                          IsPaintChunk(6, 7, content2b_id, properties)));

  // Nothing invalidated. Should keep all subsequences.
  EXPECT_TRUE(
      SubsequenceRecorder::UseCachedSubsequenceIfPossible(context, container1));
  EXPECT_TRUE(
      SubsequenceRecorder::UseCachedSubsequenceIfPossible(context, container2));

  EXPECT_EQ(7u, NumCachedNewItems());
  EXPECT_EQ(6u, NumCachedNewSubsequences());
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&container1, kBackgroundType),
                          IsSameId(&content1a, kBackgroundType),
                          IsSameId(&content1b, kForegroundType),
                          IsSameId(&container1, kForegroundType),
                          IsSameId(&container2, kBackgroundType),
                          IsSameId(&content2a, kBackgroundType),
                          IsSameId(&content2b, kForegroundType)));

  EXPECT_SUBSEQUENCE(container1, 0, 4);
  EXPECT_SUBSEQUENCE(content1a, 1, 2);
  EXPECT_SUBSEQUENCE(content1b, 2, 3);
  EXPECT_SUBSEQUENCE(container2, 4, 7);
  EXPECT_SUBSEQUENCE(content2a, 5, 6);
  EXPECT_SUBSEQUENCE(content2b, 6, 7);

  EXPECT_THAT(GetPaintController().PaintChunks(),
              ElementsAre(IsPaintChunk(0, 1, container1_bg_id, properties),
                          IsPaintChunk(1, 2, content1a_id, properties),
                          IsPaintChunk(2, 3, content1b_id, properties),
                          IsPaintChunk(3, 4, container1_fg_id, properties),
                          IsPaintChunk(4, 5, container2_id, properties),
                          IsPaintChunk(5, 6, content2a_id, properties),
                          IsPaintChunk(6, 7, content2b_id, properties)));

  // Swap order of the subsequences of container1 and container2.
  // Nothing invalidated. Should keep all subsequences.
  EXPECT_TRUE(
      SubsequenceRecorder::UseCachedSubsequenceIfPossible(context, container2));
  EXPECT_TRUE(
      SubsequenceRecorder::UseCachedSubsequenceIfPossible(context, container1));

  EXPECT_EQ(7u, NumCachedNewItems());
  EXPECT_EQ(6u, NumCachedNewSubsequences());
  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&container2, kBackgroundType),
                          IsSameId(&content2a, kBackgroundType),
                          IsSameId(&content2b, kForegroundType),
                          IsSameId(&container1, kBackgroundType),
                          IsSameId(&content1a, kBackgroundType),
                          IsSameId(&content1b, kForegroundType),
                          IsSameId(&container1, kForegroundType)));

  EXPECT_SUBSEQUENCE(container2, 0, 3);
  EXPECT_SUBSEQUENCE(content2a, 1, 2);
  EXPECT_SUBSEQUENCE(content2b, 2, 3);
  EXPECT_SUBSEQUENCE(container1, 3, 7);
  EXPECT_SUBSEQUENCE(content1a, 4, 5);
  EXPECT_SUBSEQUENCE(content1b, 5, 6);

  EXPECT_THAT(GetPaintController().PaintChunks(),
              ElementsAre(IsPaintChunk(0, 1, container2_id, properties),
                          IsPaintChunk(1, 2, content2a_id, properties),
                          IsPaintChunk(2, 3, content2b_id, properties),
                          IsPaintChunk(3, 4, container1_bg_id, properties),
                          IsPaintChunk(4, 5, content1a_id, properties),
                          IsPaintChunk(5, 6, content1b_id, properties),
                          IsPaintChunk(6, 7, container1_fg_id, properties)));
}

TEST_P(PaintControllerTest, SkipCache) {
  FakeDisplayItemClient multicol("multicol");
  FakeDisplayItemClient content("content");
  GraphicsContext context(GetPaintController());
  InitRootChunk();

  IntRect rect1(100, 100, 50, 50);
  IntRect rect2(150, 100, 50, 50);
  IntRect rect3(200, 100, 50, 50);

  DrawRect(context, multicol, kBackgroundType, IntRect(100, 200, 100, 100));

  GetPaintController().BeginSkippingCache();
  DrawRect(context, content, kForegroundType, rect1);
  DrawRect(context, content, kForegroundType, rect2);
  GetPaintController().EndSkippingCache();

  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&multicol, kBackgroundType),
                          IsSameId(&content, kForegroundType),
                          IsSameId(&content, kForegroundType)));
  sk_sp<const PaintRecord> record1 =
      To<DrawingDisplayItem>(GetPaintController().GetDisplayItemList()[1])
          .GetPaintRecord();
  sk_sp<const PaintRecord> record2 =
      To<DrawingDisplayItem>(GetPaintController().GetDisplayItemList()[2])
          .GetPaintRecord();
  EXPECT_NE(record1, record2);
  EXPECT_DEFAULT_ROOT_CHUNK(3);

  InitRootChunk();
  // Draw again with nothing invalidated.
  EXPECT_TRUE(ClientCacheIsValid(multicol));
  DrawRect(context, multicol, kBackgroundType, IntRect(100, 200, 100, 100));

  GetPaintController().BeginSkippingCache();
  DrawRect(context, content, kForegroundType, rect1);
  DrawRect(context, content, kForegroundType, rect2);
  GetPaintController().EndSkippingCache();

  EXPECT_EQ(1u, NumCachedNewItems());
  EXPECT_EQ(0u, NumCachedNewSubsequences());
#if DCHECK_IS_ON()
  EXPECT_EQ(0u, NumIndexedItems());
  EXPECT_EQ(1u, NumSequentialMatches());
  EXPECT_EQ(0u, NumOutOfOrderMatches());
#endif

  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&multicol, kBackgroundType),
                          IsSameId(&content, kForegroundType),
                          IsSameId(&content, kForegroundType)));
  EXPECT_NE(record1,
            To<DrawingDisplayItem>(GetPaintController().GetDisplayItemList()[1])
                .GetPaintRecord());
  EXPECT_NE(record2,
            To<DrawingDisplayItem>(GetPaintController().GetDisplayItemList()[2])
                .GetPaintRecord());
  EXPECT_DEFAULT_ROOT_CHUNK(3);

  InitRootChunk();
  // Now the multicol becomes 3 columns and repaints.
  multicol.Invalidate();
  DrawRect(context, multicol, kBackgroundType, IntRect(100, 100, 100, 100));

  GetPaintController().BeginSkippingCache();
  DrawRect(context, content, kForegroundType, rect1);
  DrawRect(context, content, kForegroundType, rect2);
  DrawRect(context, content, kForegroundType, rect3);
  GetPaintController().EndSkippingCache();

  // We should repaint everything on invalidation of the scope container.
  const auto& display_item_list =
      GetPaintController().GetNewPaintArtifactShared()->GetDisplayItemList();
  EXPECT_THAT(display_item_list,
              ElementsAre(IsSameId(&multicol, kBackgroundType),
                          IsSameId(&content, kForegroundType),
                          IsSameId(&content, kForegroundType),
                          IsSameId(&content, kForegroundType)));
  EXPECT_NE(record1,
            To<DrawingDisplayItem>(display_item_list[1]).GetPaintRecord());
  EXPECT_NE(record2,
            To<DrawingDisplayItem>(display_item_list[2]).GetPaintRecord());

  CommitAndFinishCycle();
  EXPECT_DEFAULT_ROOT_CHUNK(4);
}

TEST_P(PaintControllerTest, PartialSkipCache) {
  FakeDisplayItemClient content("content");
  GraphicsContext context(GetPaintController());

  IntRect rect1(100, 100, 50, 50);
  IntRect rect2(150, 100, 50, 50);
  IntRect rect3(200, 100, 50, 50);

  InitRootChunk();
  DrawRect(context, content, kBackgroundType, rect1);
  GetPaintController().BeginSkippingCache();
  DrawRect(context, content, kForegroundType, rect2);
  GetPaintController().EndSkippingCache();
  DrawRect(context, content, kForegroundType, rect3);

  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&content, kBackgroundType),
                          IsSameId(&content, kForegroundType),
                          IsSameId(&content, kForegroundType)));
  sk_sp<const PaintRecord> record0 =
      To<DrawingDisplayItem>(GetPaintController().GetDisplayItemList()[0])
          .GetPaintRecord();
  sk_sp<const PaintRecord> record1 =
      To<DrawingDisplayItem>(GetPaintController().GetDisplayItemList()[1])
          .GetPaintRecord();
  sk_sp<const PaintRecord> record2 =
      To<DrawingDisplayItem>(GetPaintController().GetDisplayItemList()[2])
          .GetPaintRecord();
  EXPECT_NE(record1, record2);

  // Content's cache is invalid because it has display items skipped cache.
  EXPECT_FALSE(ClientCacheIsValid(content));
  EXPECT_EQ(PaintInvalidationReason::kUncacheable,
            content.GetPaintInvalidationReason());

  InitRootChunk();
  // Draw again with nothing invalidated.
  DrawRect(context, content, kBackgroundType, rect1);
  GetPaintController().BeginSkippingCache();
  DrawRect(context, content, kForegroundType, rect2);
  GetPaintController().EndSkippingCache();
  DrawRect(context, content, kForegroundType, rect3);

  EXPECT_EQ(0u, NumCachedNewItems());
  EXPECT_EQ(0u, NumCachedNewSubsequences());
#if DCHECK_IS_ON()
  EXPECT_EQ(0u, NumIndexedItems());
  EXPECT_EQ(0u, NumSequentialMatches());
  EXPECT_EQ(0u, NumOutOfOrderMatches());
#endif

  CommitAndFinishCycle();

  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&content, kBackgroundType),
                          IsSameId(&content, kForegroundType),
                          IsSameId(&content, kForegroundType)));
  EXPECT_NE(record0,
            To<DrawingDisplayItem>(GetPaintController().GetDisplayItemList()[0])
                .GetPaintRecord());
  EXPECT_NE(record1,
            To<DrawingDisplayItem>(GetPaintController().GetDisplayItemList()[1])
                .GetPaintRecord());
  EXPECT_NE(record2,
            To<DrawingDisplayItem>(GetPaintController().GetDisplayItemList()[2])
                .GetPaintRecord());
}

TEST_P(PaintControllerTest, SkipCacheDuplicatedItemAndChunkIds) {
  FakeDisplayItemClient chunk_client("chunk client");
  FakeDisplayItemClient item_client("item client");
  auto properties = DefaultPaintChunkProperties();
  PaintChunk::Id chunk_id(chunk_client, DisplayItem::kLayerChunk);
  auto& paint_controller = GetPaintController();

  GraphicsContext context(paint_controller);
  paint_controller.BeginSkippingCache();
  paint_controller.SetWillForceNewChunk(true);
  paint_controller.UpdateCurrentPaintChunkProperties(&chunk_id, properties);
  DrawRect(context, item_client, kBackgroundType, IntRect(0, 0, 100, 100));
  paint_controller.SetWillForceNewChunk(true);
  paint_controller.UpdateCurrentPaintChunkProperties(&chunk_id, properties);
  DrawRect(context, item_client, kBackgroundType, IntRect(0, 0, 100, 100));
  paint_controller.EndSkippingCache();

  CommitAndFinishCycle();

  EXPECT_THAT(paint_controller.GetDisplayItemList(),
              ElementsAre(IsSameId(&item_client, kBackgroundType),
                          IsSameId(&item_client, kBackgroundType)));
  EXPECT_FALSE(paint_controller.GetDisplayItemList()[0].IsCacheable());
  EXPECT_FALSE(paint_controller.GetDisplayItemList()[1].IsCacheable());

  EXPECT_THAT(GetPaintController().PaintChunks(),
              ElementsAre(IsPaintChunk(0, 1, chunk_id, properties),
                          IsPaintChunk(1, 2, chunk_id, properties)));
  EXPECT_FALSE(paint_controller.PaintChunks()[0].is_cacheable);
  EXPECT_FALSE(paint_controller.PaintChunks()[1].is_cacheable);
}

TEST_P(PaintControllerTest, SmallPaintControllerHasOnePaintChunk) {
  FakeDisplayItemClient client("test client");

  InitRootChunk();
  GraphicsContext context(GetPaintController());
  DrawRect(context, client, kBackgroundType, IntRect(0, 0, 100, 100));

  CommitAndFinishCycle();
  EXPECT_THAT(GetPaintController().PaintChunks(),
              ElementsAre(IsPaintChunk(0, 1)));
}

void DrawPath(GraphicsContext& context,
              DisplayItemClient& client,
              DisplayItem::Type type,
              unsigned count) {
  if (DrawingRecorder::UseCachedDrawingIfPossible(context, client, type))
    return;

  DrawingRecorder recorder(context, client, type, IntRect(0, 0, 100, 100));
  SkPathBuilder builder;
  builder.moveTo(0, 0);
  builder.lineTo(0, 100);
  builder.lineTo(50, 50);
  builder.lineTo(100, 100);
  builder.lineTo(100, 0);
  builder.close();
  SkPath path = builder.detach();
  PaintFlags flags;
  flags.setAntiAlias(true);
  for (unsigned i = 0; i < count; i++)
    context.DrawPath(path, flags);
}

TEST_P(PaintControllerTest, BeginAndEndFrame) {
  class FakeFrame {};

  // PaintController should have one null frame in the stack since beginning.
  GetPaintController().SetFirstPainted();
  FrameFirstPaint result = GetPaintController().EndFrame(nullptr);
  EXPECT_TRUE(result.first_painted);
  EXPECT_FALSE(result.text_painted);
  EXPECT_FALSE(result.image_painted);
  // Readd the null frame.
  GetPaintController().BeginFrame(nullptr);

  std::unique_ptr<FakeFrame> frame1(new FakeFrame);
  GetPaintController().BeginFrame(frame1.get());
  GetPaintController().SetFirstPainted();
  GetPaintController().SetTextPainted();
  GetPaintController().SetImagePainted();

  result = GetPaintController().EndFrame(frame1.get());
  EXPECT_TRUE(result.first_painted);
  EXPECT_TRUE(result.text_painted);
  EXPECT_TRUE(result.image_painted);

  std::unique_ptr<FakeFrame> frame2(new FakeFrame);
  GetPaintController().BeginFrame(frame2.get());
  GetPaintController().SetFirstPainted();

  std::unique_ptr<FakeFrame> frame3(new FakeFrame);
  GetPaintController().BeginFrame(frame3.get());
  GetPaintController().SetTextPainted();
  GetPaintController().SetImagePainted();

  result = GetPaintController().EndFrame(frame3.get());
  EXPECT_FALSE(result.first_painted);
  EXPECT_TRUE(result.text_painted);
  EXPECT_TRUE(result.image_painted);

  result = GetPaintController().EndFrame(frame2.get());
  EXPECT_TRUE(result.first_painted);
  EXPECT_FALSE(result.text_painted);
  EXPECT_FALSE(result.image_painted);
}

TEST_P(PaintControllerTest, InsertValidItemInFront) {
  FakeDisplayItemClient first("first");
  FakeDisplayItemClient second("second");
  FakeDisplayItemClient third("third");
  FakeDisplayItemClient fourth("fourth");
  GraphicsContext context(GetPaintController());

  InitRootChunk();
  DrawRect(context, first, kBackgroundType, IntRect(100, 100, 300, 300));
  DrawRect(context, second, kBackgroundType, IntRect(100, 100, 200, 200));
  DrawRect(context, third, kBackgroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, fourth, kBackgroundType, IntRect(100, 100, 50, 50));

  EXPECT_EQ(0u, NumCachedNewItems());
  EXPECT_EQ(0u, NumCachedNewSubsequences());
  CommitAndFinishCycle();
  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&first, kBackgroundType),
                          IsSameId(&second, kBackgroundType),
                          IsSameId(&third, kBackgroundType),
                          IsSameId(&fourth, kBackgroundType)));
  EXPECT_TRUE(first.IsValid());
  EXPECT_TRUE(second.IsValid());
  EXPECT_TRUE(third.IsValid());
  EXPECT_TRUE(fourth.IsValid());

  // Simulate that a composited scrolling element is scrolled down, and "first"
  // and "second" are scrolled out of the interest rect.
  InitRootChunk();
  DrawRect(context, third, kBackgroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, fourth, kBackgroundType, IntRect(100, 100, 50, 50));

  EXPECT_EQ(2u, NumCachedNewItems());
  EXPECT_EQ(0u, NumCachedNewSubsequences());
#if DCHECK_IS_ON()
  // We indexed "first" and "second" when finding the cached item for "third".
  EXPECT_EQ(2u, NumIndexedItems());
  EXPECT_EQ(2u, NumSequentialMatches());
  EXPECT_EQ(0u, NumOutOfOrderMatches());
#endif

  CommitAndFinishCycle();
  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&third, kBackgroundType),
                          IsSameId(&fourth, kBackgroundType)));
  EXPECT_TRUE(first.IsValid());
  EXPECT_TRUE(second.IsValid());
  EXPECT_TRUE(third.IsValid());
  EXPECT_TRUE(fourth.IsValid());

  // Simulate "first" and "second" are scrolled back into the interest rect.
  InitRootChunk();
  DrawRect(context, first, kBackgroundType, IntRect(100, 100, 300, 300));
  DrawRect(context, second, kBackgroundType, IntRect(100, 100, 200, 200));
  DrawRect(context, third, kBackgroundType, IntRect(100, 100, 100, 100));
  DrawRect(context, fourth, kBackgroundType, IntRect(100, 100, 50, 50));

  EXPECT_EQ(2u, NumCachedNewItems());
  EXPECT_EQ(0u, NumCachedNewSubsequences());
#if DCHECK_IS_ON()
  // We indexed "third" and "fourth" when finding the cached item for "first".
  EXPECT_EQ(2u, NumIndexedItems());
  EXPECT_EQ(2u, NumSequentialMatches());
  EXPECT_EQ(0u, NumOutOfOrderMatches());
#endif

  CommitAndFinishCycle();
  EXPECT_THAT(GetPaintController().GetDisplayItemList(),
              ElementsAre(IsSameId(&first, kBackgroundType),
                          IsSameId(&second, kBackgroundType),
                          IsSameId(&third, kBackgroundType),
                          IsSameId(&fourth, kBackgroundType)));
  EXPECT_TRUE(first.IsValid());
  EXPECT_TRUE(second.IsValid());
  EXPECT_TRUE(third.IsValid());
  EXPECT_TRUE(fourth.IsValid());
}

TEST_P(PaintControllerTest, TransientPaintControllerIncompleteCycle) {
  auto paint_controller =
      std::make_unique<PaintController>(PaintController::kTransient);
  GraphicsContext context(*paint_controller);
  FakeDisplayItemClient client("client");
  InitRootChunk(*paint_controller);
  DrawRect(context, client, kBackgroundType, IntRect(100, 100, 50, 50));
  // The client of a transient paint controller can abort without
  // CommintNewDisplayItems() and FinishCycle(). This should not crash.
  paint_controller = nullptr;
}

TEST_P(PaintControllerTest, AllowDuplicatedIdForUncacheableItem) {
  if (RuntimeEnabledFeatures::PaintUnderInvalidationCheckingEnabled())
    return;

  IntRect r(100, 100, 300, 300);
  FakeDisplayItemClient cacheable("cacheable");
  FakeDisplayItemClient uncacheable("uncacheable");
  GraphicsContext context(GetPaintController());

  uncacheable.Invalidate(PaintInvalidationReason::kUncacheable);
  EXPECT_TRUE(cacheable.IsCacheable());
  EXPECT_FALSE(uncacheable.IsCacheable());

  InitRootChunk();
  {
    SubsequenceRecorder recorder(context, cacheable);
    DrawRect(context, cacheable, kBackgroundType, IntRect(r));
    DrawRect(context, uncacheable, kBackgroundType, IntRect(r));
    // This should not trigger the duplicated id assert.
    DrawRect(context, uncacheable, kBackgroundType, IntRect(r));
  }

  CommitAndFinishCycle();
  EXPECT_TRUE(GetPaintController().GetDisplayItemList()[0].IsCacheable());
  EXPECT_FALSE(GetPaintController().GetDisplayItemList()[1].IsCacheable());
  EXPECT_FALSE(GetPaintController().GetDisplayItemList()[2].IsCacheable());
  EXPECT_TRUE(cacheable.IsCacheable());
  EXPECT_FALSE(uncacheable.IsCacheable());

  InitRootChunk();
  EXPECT_TRUE(GetPaintController().UseCachedSubsequenceIfPossible(cacheable));
  CommitAndFinishCycle();
  EXPECT_TRUE(GetPaintController().GetDisplayItemList()[0].IsCacheable());
  EXPECT_FALSE(GetPaintController().GetDisplayItemList()[1].IsCacheable());
  EXPECT_FALSE(GetPaintController().GetDisplayItemList()[2].IsCacheable());
  EXPECT_TRUE(cacheable.IsCacheable());
  EXPECT_FALSE(uncacheable.IsCacheable());
}

// Death tests don't work properly on Android.
#if defined(GTEST_HAS_DEATH_TEST) && !defined(OS_ANDROID)

TEST_P(PaintControllerTest, DuplicatedSubsequences) {
  FakeDisplayItemClient client("test");
  GraphicsContext context(GetPaintController());

  auto paint_duplicated_subsequences = [&]() {
    InitRootChunk();
    {
      SubsequenceRecorder r(context, client);
      DrawRect(context, client, kBackgroundType, IntRect(100, 100, 100, 100));
    }
    {
      SubsequenceRecorder r(context, client);
      DrawRect(context, client, kForegroundType, IntRect(100, 100, 100, 100));
    }
    CommitAndFinishCycle();
  };

#if DCHECK_IS_ON()
  EXPECT_DEATH(paint_duplicated_subsequences(),
               "Multiple subsequences for client: \"test\"");
#else
  // The following is for non-DCHECK path. No security CHECK should trigger.
  paint_duplicated_subsequences();
  // Paint again.
  InitRootChunk();
  if (RuntimeEnabledFeatures::PaintUnderInvalidationCheckingEnabled()) {
    EXPECT_FALSE(GetPaintController().UseCachedSubsequenceIfPossible(client));
    SubsequenceRecorder r(context, client);
    DrawRect(context, client, kBackgroundType, IntRect(100, 100, 100, 100));
  } else {
    EXPECT_TRUE(GetPaintController().UseCachedSubsequenceIfPossible(client));
  }
  {
    // Should not use the cached duplicated subsequence.
    EXPECT_FALSE(GetPaintController().UseCachedSubsequenceIfPossible(client));
    SubsequenceRecorder r(context, client);
    DrawRect(context, client, kForegroundType, IntRect(100, 100, 100, 100));
  }
  CommitAndFinishCycle();
#endif
}

TEST_P(PaintControllerTest, DeletedClientInUnderInvaldiatedSubsequence) {
  if (RuntimeEnabledFeatures::PaintUnderInvalidationCheckingEnabled())
    return;

  FakeDisplayItemClient container("container");
  auto content = std::make_unique<FakeDisplayItemClient>("content");
  GraphicsContext context(GetPaintController());

  InitRootChunk();
  {
    SubsequenceRecorder r(context, container);
    DrawRect(context, *content, kBackgroundType, IntRect(100, 100, 300, 300));
  }
  CommitAndFinishCycle();

  content = nullptr;
  InitRootChunk();
  // Leave container not invalidated.
#if DCHECK_IS_ON()
  ASSERT_DEATH(
      SubsequenceRecorder::UseCachedSubsequenceIfPossible(context, container),
      "");
#else
  // This should not crash.
  EXPECT_TRUE(
      SubsequenceRecorder::UseCachedSubsequenceIfPossible(context, container));
  CommitAndFinishCycle();
#endif
}

#endif  // defined(GTEST_HAS_DEATH_TEST) && !defined(OS_ANDROID)

}  // namespace blink
