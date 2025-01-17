// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_SCHEDULER_DOM_TASK_CONTROLLER_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_SCHEDULER_DOM_TASK_CONTROLLER_H_

#include "third_party/blink/renderer/core/dom/abort_controller.h"
#include "third_party/blink/renderer/modules/modules_export.h"
#include "third_party/blink/renderer/platform/scheduler/public/web_scheduling_priority.h"

namespace blink {
class ExceptionState;
class ExecutionContext;

class MODULES_EXPORT DOMTaskController final : public AbortController {
  DEFINE_WRAPPERTYPEINFO();

 public:
  static DOMTaskController* Create(ExecutionContext*,
                                   const AtomicString& priority);
  DOMTaskController(ExecutionContext*, const AtomicString& priority);

  void setPriority(const AtomicString& priority, ExceptionState&);
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_SCHEDULER_DOM_TASK_CONTROLLER_H_
