// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/screen_orientation/screen_orientation_controller.h"

#include <memory>
#include <utility>
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/common/privacy_budget/identifiability_metric_builder.h"
#include "third_party/blink/public/common/privacy_budget/identifiability_study_settings.h"
#include "third_party/blink/public/common/widget/screen_info.h"
#include "third_party/blink/public/platform/task_type.h"
#include "third_party/blink/renderer/core/dom/events/event.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/fullscreen/scoped_allow_fullscreen.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/modules/screen_orientation/screen_orientation.h"
#include "third_party/blink/renderer/platform/web_test_support.h"

namespace blink {

ScreenOrientationController::~ScreenOrientationController() = default;

const char ScreenOrientationController::kSupplementName[] =
    "ScreenOrientationController";

ScreenOrientationController* ScreenOrientationController::From(
    LocalDOMWindow& window) {
  auto* controller = FromIfExists(window);
  if (!controller) {
    controller = MakeGarbageCollected<ScreenOrientationController>(window);
    Supplement<LocalDOMWindow>::ProvideTo(window, controller);
  }
  return controller;
}

ScreenOrientationController* ScreenOrientationController::FromIfExists(
    LocalDOMWindow& window) {
  return Supplement<LocalDOMWindow>::From<ScreenOrientationController>(window);
}

ScreenOrientationController::ScreenOrientationController(LocalDOMWindow& window)
    : ExecutionContextLifecycleObserver(&window),
      PageVisibilityObserver(window.GetFrame()->GetPage()),
      Supplement<LocalDOMWindow>(window),
      screen_orientation_service_(&window) {
  AssociatedInterfaceProvider* provider =
      window.GetFrame()->GetRemoteNavigationAssociatedInterfaces();
  if (provider) {
    provider->GetInterface(
        screen_orientation_service_.BindNewEndpointAndPassReceiver(
            window.GetTaskRunner(TaskType::kMiscPlatformAPI)));
  }
}

// Compute the screen orientation using the orientation angle and the screen
// width / height.
mojom::blink::ScreenOrientation ScreenOrientationController::ComputeOrientation(
    const gfx::Rect& rect,
    uint16_t rotation) {
  // Bypass orientation detection in web tests to get consistent results.
  // FIXME: The screen dimension should be fixed when running the web tests
  // to avoid such issues.
  if (WebTestSupport::IsRunningWebTest())
    return mojom::blink::ScreenOrientation::kPortraitPrimary;

  bool is_tall_display = rotation % 180 ? rect.height() < rect.width()
                                        : rect.height() > rect.width();

  // https://w3c.github.io/screen-orientation/#dfn-current-orientation-angle
  // allows the UA to associate *-primary and *-secondary values at will. Blink
  // arbitrarily chooses rotation 0 to always be portrait-primary or
  // landscape-primary, and portrait-primary + 90 to be landscape-primary, which
  // together fully determine the relationship.
  switch (rotation) {
    case 0:
      return is_tall_display
                 ? mojom::blink::ScreenOrientation::kPortraitPrimary
                 : mojom::blink::ScreenOrientation::kLandscapePrimary;
    case 90:
      return is_tall_display
                 ? mojom::blink::ScreenOrientation::kLandscapePrimary
                 : mojom::blink::ScreenOrientation::kPortraitSecondary;
    case 180:
      return is_tall_display
                 ? mojom::blink::ScreenOrientation::kPortraitSecondary
                 : mojom::blink::ScreenOrientation::kLandscapeSecondary;
    case 270:
      return is_tall_display
                 ? mojom::blink::ScreenOrientation::kLandscapeSecondary
                 : mojom::blink::ScreenOrientation::kPortraitPrimary;
    default:
      NOTREACHED();
      return mojom::blink::ScreenOrientation::kPortraitPrimary;
  }
}

void ScreenOrientationController::UpdateOrientation() {
  DCHECK(orientation_);
  DCHECK(GetPage());
  ChromeClient& chrome_client = GetPage()->GetChromeClient();
  LocalFrame& frame = *DomWindow()->GetFrame();
  const ScreenInfo& screen_info = chrome_client.GetScreenInfo(frame);
  mojom::blink::ScreenOrientation orientation_type =
      screen_info.orientation_type;
  if (orientation_type == mojom::blink::ScreenOrientation::kUndefined) {
    // The embedder could not provide us with an orientation, deduce it
    // ourselves.
    orientation_type =
        ComputeOrientation(screen_info.rect, screen_info.orientation_angle);
  }
  DCHECK(orientation_type != mojom::blink::ScreenOrientation::kUndefined);

  orientation_->SetType(orientation_type);
  orientation_->SetAngle(screen_info.orientation_angle);
}

bool ScreenOrientationController::IsActiveAndVisible() const {
  return orientation_ && screen_orientation_service_.is_bound() && GetPage() &&
         GetPage()->IsPageVisible();
}

void ScreenOrientationController::PageVisibilityChanged() {
  if (!IsActiveAndVisible())
    return;

  DCHECK(GetPage());

  // The orientation type and angle are tied in a way that if the angle has
  // changed, the type must have changed.
  LocalFrame& frame = *DomWindow()->GetFrame();
  uint16_t current_angle =
      GetPage()->GetChromeClient().GetScreenInfo(frame).orientation_angle;

  // FIXME: sendOrientationChangeEvent() currently send an event all the
  // children of the frame, so it should only be called on the frame on
  // top of the tree. We would need the embedder to call
  // sendOrientationChangeEvent on every WebFrame part of a WebView to be
  // able to remove this.
  if (&frame == frame.LocalFrameRoot() &&
      orientation_->angle() != current_angle)
    NotifyOrientationChanged();
}

void ScreenOrientationController::NotifyOrientationChanged() {
  // TODO(dcheng): Update this code to better handle instances when v8 memory
  // is forcibly purged.
  if (!DomWindow()) {
    return;
  }

  // Keep track of the frames that need to be notified before notifying the
  // current frame as it will prevent side effects from the change event
  // handlers.
  HeapVector<Member<LocalFrame>> frames;
  for (Frame* frame = DomWindow()->GetFrame(); frame;
       frame = frame->Tree().TraverseNext(DomWindow()->GetFrame())) {
    if (auto* local_frame = DynamicTo<LocalFrame>(frame))
      frames.push_back(local_frame);
  }
  for (LocalFrame* frame : frames) {
    if (auto* controller = FromIfExists(*frame->DomWindow()))
      controller->NotifyOrientationChangedInternal();
  }
}

void ScreenOrientationController::NotifyOrientationChangedInternal() {
  if (!IsActiveAndVisible())
    return;

  UpdateOrientation();
  GetExecutionContext()
      ->GetTaskRunner(TaskType::kMiscPlatformAPI)
      ->PostTask(FROM_HERE,
                 WTF::Bind(
                     [](ScreenOrientation* orientation) {
                       ScopedAllowFullscreen allow_fullscreen(
                           ScopedAllowFullscreen::kOrientationChange);
                       orientation->DispatchEvent(
                           *Event::Create(event_type_names::kChange));
                     },
                     WrapPersistent(orientation_.Get())));
}

void ScreenOrientationController::SetOrientation(
    ScreenOrientation* orientation) {
  orientation_ = orientation;
  if (orientation_)
    UpdateOrientation();
}

void ScreenOrientationController::lock(
    device::mojom::blink::ScreenOrientationLockType orientation,
    std::unique_ptr<WebLockOrientationCallback> callback) {
  // When detached, the |screen_orientation_service_| is no longer valid.
  if (!screen_orientation_service_.is_bound())
    return;

  // https://jeremyroman.github.io/alternate-loading-modes/#patch-orientation-lock
  // Step 7.3.10. Screen Orientation API.
  // Defer to lock with |orientation| until the prerendering page is activated
  // via appending lock operation to the post-prerendering activation steps
  // list.
  if (DomWindow()->document()->IsPrerendering()) {
    DomWindow()->document()->AddPostPrerenderingActivationStep(
        WTF::Bind(&ScreenOrientationController::LockOrientationInternal,
                  WrapWeakPersistent(this), orientation, std::move(callback)));
    return;
  }

  LockOrientationInternal(orientation, std::move(callback));
}

void ScreenOrientationController::unlock() {
  // When detached, the |screen_orientation_service_| is no longer valid.
  if (!screen_orientation_service_.is_bound())
    return;

  // https://jeremyroman.github.io/alternate-loading-modes/#patch-orientation-lock
  // Step 7.3.10. Screen Orientation API.
  // Defer to unlock with |orientation| until the prerendering page is activated
  // via appending unlock operation to the post-prerendering activation steps
  // list.
  if (DomWindow()->document()->IsPrerendering()) {
    DomWindow()->document()->AddPostPrerenderingActivationStep(
        WTF::Bind(&ScreenOrientationController::UnlockOrientationInternal,
                  WrapWeakPersistent(this)));
    return;
  }

  UnlockOrientationInternal();
}

bool ScreenOrientationController::MaybeHasActiveLock() const {
  return active_lock_;
}

void ScreenOrientationController::ContextDestroyed() {
  active_lock_ = false;
}

void ScreenOrientationController::Trace(Visitor* visitor) const {
  visitor->Trace(orientation_);
  visitor->Trace(screen_orientation_service_);
  ExecutionContextLifecycleObserver::Trace(visitor);
  PageVisibilityObserver::Trace(visitor);
  Supplement<LocalDOMWindow>::Trace(visitor);
}

void ScreenOrientationController::SetScreenOrientationAssociatedRemoteForTests(
    HeapMojoAssociatedRemote<device::mojom::blink::ScreenOrientation> remote) {
  screen_orientation_service_ = std::move(remote);
}

void ScreenOrientationController::OnLockOrientationResult(
    int request_id,
    ScreenOrientationLockResult result) {
  if (!pending_callback_ || request_id != request_id_)
    return;

  if (IdentifiabilityStudySettings::Get()->ShouldSample(
          IdentifiableSurface::FromTypeAndToken(
              IdentifiableSurface::Type::kWebFeature,
              WebFeature::kScreenOrientationLock))) {
    auto* context = GetExecutionContext();
    IdentifiabilityMetricBuilder(context->UkmSourceID())
        .SetWebfeature(WebFeature::kScreenOrientationLock,
                       result == ScreenOrientationLockResult::
                                     SCREEN_ORIENTATION_LOCK_RESULT_SUCCESS)
        .Record(context->UkmRecorder());
  }

  switch (result) {
    case ScreenOrientationLockResult::SCREEN_ORIENTATION_LOCK_RESULT_SUCCESS:
      pending_callback_->OnSuccess();
      break;
    case ScreenOrientationLockResult::
        SCREEN_ORIENTATION_LOCK_RESULT_ERROR_NOT_AVAILABLE:
      pending_callback_->OnError(kWebLockOrientationErrorNotAvailable);
      break;
    case ScreenOrientationLockResult::
        SCREEN_ORIENTATION_LOCK_RESULT_ERROR_FULLSCREEN_REQUIRED:
      pending_callback_->OnError(kWebLockOrientationErrorFullscreenRequired);
      break;
    case ScreenOrientationLockResult::
        SCREEN_ORIENTATION_LOCK_RESULT_ERROR_CANCELED:
      pending_callback_->OnError(kWebLockOrientationErrorCanceled);
      break;
    default:
      NOTREACHED();
      break;
  }

  pending_callback_.reset();
}

void ScreenOrientationController::CancelPendingLocks() {
  if (!pending_callback_)
    return;

  pending_callback_->OnError(kWebLockOrientationErrorCanceled);
  pending_callback_.reset();
}

int ScreenOrientationController::GetRequestIdForTests() {
  return pending_callback_ ? request_id_ : -1;
}

void ScreenOrientationController::LockOrientationInternal(
    device::mojom::blink::ScreenOrientationLockType orientation,
    std::unique_ptr<WebLockOrientationCallback> callback) {
  CancelPendingLocks();
  pending_callback_ = std::move(callback);
  screen_orientation_service_->LockOrientation(
      orientation,
      WTF::Bind(&ScreenOrientationController::OnLockOrientationResult,
                WrapWeakPersistent(this), ++request_id_));

  active_lock_ = true;
}

void ScreenOrientationController::UnlockOrientationInternal() {
  CancelPendingLocks();
  screen_orientation_service_->UnlockOrientation();
  active_lock_ = false;
}

}  // namespace blink
