// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/app_history/app_history_navigate_event.h"

#include "third_party/blink/renderer/bindings/core/v8/v8_app_history_navigate_event_init.h"
#include "third_party/blink/renderer/core/app_history/app_history_destination.h"
#include "third_party/blink/renderer/core/dom/dom_exception.h"
#include "third_party/blink/renderer/core/event_interface_names.h"
#include "third_party/blink/renderer/core/event_type_names.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/html/forms/form_data.h"

namespace blink {

AppHistoryNavigateEvent::AppHistoryNavigateEvent(
    ExecutionContext* context,
    const AtomicString& type,
    AppHistoryNavigateEventInit* init)
    : Event(type, init),
      ExecutionContextClient(context),
      can_respond_(init->canRespond()),
      user_initiated_(init->userInitiated()),
      hash_change_(init->hashChange()),
      destination_(init->destination()),
      form_data_(init->formData()),
      info_(init->hasInfo()
                ? init->info()
                : ScriptValue(context->GetIsolate(),
                              v8::Undefined(context->GetIsolate()))) {
  DCHECK(IsA<LocalDOMWindow>(context));
}

void AppHistoryNavigateEvent::respondWith(ScriptState* script_state,
                                          ScriptPromise newNavigationAction,
                                          ExceptionState& exception_state) {
  if (!DomWindow()) {
    exception_state.ThrowDOMException(DOMExceptionCode::kInvalidStateError,
                                      "respondWith may not be called in a "
                                      "detached window");
    return;
  }

  if (!isTrusted()) {
    exception_state.ThrowSecurityError(
        "respondWith may only be called on a "
        "trusted event during event dispatch");
    return;
  }

  if (!can_respond_) {
    exception_state.ThrowSecurityError(
        "A navigation with URL '" + url_.ElidedString() +
        "' cannot be intercepted by respondWith in a window with origin '" +
        DomWindow()->GetSecurityOrigin()->ToString() + "' and URL '" +
        DomWindow()->Url().ElidedString() + "'.");
    return;
  }

  if (!IsBeingDispatched() || defaultPrevented()) {
    exception_state.ThrowDOMException(DOMExceptionCode::kInvalidStateError,
                                      "respondWith may only be called during "
                                      "the first dispatch of this event");
    return;
  }

  preventDefault();
  navigation_action_promise_ = newNavigationAction;
}

void AppHistoryNavigateEvent::ClearNavigationActionPromise() {
  navigation_action_promise_ = ScriptPromise();
}

const AtomicString& AppHistoryNavigateEvent::InterfaceName() const {
  return event_interface_names::kAppHistoryNavigateEvent;
}

void AppHistoryNavigateEvent::Trace(Visitor* visitor) const {
  Event::Trace(visitor);
  ExecutionContextClient::Trace(visitor);
  visitor->Trace(destination_);
  visitor->Trace(form_data_);
  visitor->Trace(info_);
  visitor->Trace(navigation_action_promise_);
}

}  // namespace blink
