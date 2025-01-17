// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/editing/frame_caret.h"

#include "third_party/blink/renderer/core/editing/commands/typing_command.h"
#include "third_party/blink/renderer/core/editing/frame_selection.h"
#include "third_party/blink/renderer/core/editing/selection_template.h"
#include "third_party/blink/renderer/core/editing/testing/editing_test_base.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/layout/layout_theme.h"
#include "third_party/blink/renderer/core/page/focus_controller.h"
#include "third_party/blink/renderer/platform/scheduler/test/fake_task_runner.h"
#include "third_party/blink/renderer/platform/web_test_support.h"

namespace blink {

class FrameCaretTest : public EditingTestBase {
 public:
  static bool ShouldShowCaret(const FrameCaret& caret) {
    return caret.ShouldShowCaret();
  }

 private:
  // The caret blink timer doesn't work if IsRunningWebTest() because
  // LayoutTheme::CaretBlinkInterval() returns 0.
  ScopedWebTestMode web_test_mode_{false};
};

TEST_F(FrameCaretTest, BlinkAfterTyping) {
  FrameCaret& caret = Selection().FrameCaretForTesting();
  scoped_refptr<scheduler::FakeTaskRunner> task_runner =
      base::MakeRefCounted<scheduler::FakeTaskRunner>();
  task_runner->SetTime(0);
  caret.RecreateCaretBlinkTimerForTesting(task_runner.get());
  const double kInterval = 10;
  LayoutTheme::GetTheme().SetCaretBlinkInterval(
      base::TimeDelta::FromSecondsD(kInterval));
  GetDocument().GetPage()->GetFocusController().SetActive(true);
  GetDocument().GetPage()->GetFocusController().SetFocused(true);
  GetDocument().body()->setInnerHTML("<textarea>");
  auto* editor = To<Element>(GetDocument().body()->firstChild());
  editor->focus();
  UpdateAllLifecyclePhasesForTest();

  EXPECT_TRUE(caret.IsActive());
  EXPECT_TRUE(caret.IsVisibleIfActiveForTesting())
      << "Initially a caret should be in visible cycle.";

  task_runner->AdvanceTimeAndRun(kInterval);
  EXPECT_FALSE(caret.IsVisibleIfActiveForTesting())
      << "The caret blinks normally.";

  TypingCommand::InsertLineBreak(GetDocument());
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(caret.IsVisibleIfActiveForTesting())
      << "The caret should be in visible cycle just after a typing command.";

  task_runner->AdvanceTimeAndRun(kInterval - 1);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(caret.IsVisibleIfActiveForTesting())
      << "The typing command reset the timer. The caret is still visible.";

  task_runner->AdvanceTimeAndRun(1);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(caret.IsVisibleIfActiveForTesting())
      << "The caret should blink after the typing command.";
}

TEST_F(FrameCaretTest, ShouldNotBlinkWhenSelectionLooseFocus) {
  FrameCaret& caret = Selection().FrameCaretForTesting();
  GetDocument().GetPage()->GetFocusController().SetActive(true);
  GetDocument().GetPage()->GetFocusController().SetFocused(true);
  GetDocument().body()->setInnerHTML(
      "<div id='outer' tabindex='-1'>"
      "<div id='input' contenteditable>foo</div>"
      "</div>");
  Element* input = GetDocument().QuerySelector("#input");
  input->focus();
  Element* outer = GetDocument().QuerySelector("#outer");
  outer->focus();
  UpdateAllLifecyclePhasesForTest();
  const SelectionInDOMTree& selection = Selection().GetSelectionInDOMTree();
  EXPECT_EQ(selection.Base(), Position::FirstPositionInNode(*input));
  EXPECT_FALSE(ShouldShowCaret(caret));
}

TEST_F(FrameCaretTest, ShouldBlinkCaretWhileCaretBrowsing) {
  FrameCaret& caret = Selection().FrameCaretForTesting();
  Selection().SetSelection(SetSelectionTextToBody("<div>a|b</div>"),
                           SetSelectionOptions());
  Selection().SetCaretEnabled(true);
  EXPECT_FALSE(ShouldShowCaret(caret));
  GetDocument().GetFrame()->GetSettings()->SetCaretBrowsingEnabled(true);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(ShouldShowCaret(caret));
}

}  // namespace blink
