// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/xr/xr_image_tracking_result.h"

#include "third_party/blink/renderer/core/html/html_image_element.h"
#include "third_party/blink/renderer/modules/xr/xr_object_space.h"
#include "third_party/blink/renderer/modules/xr/xr_pose.h"
#include "third_party/blink/renderer/modules/xr/xr_session.h"
#include "third_party/blink/renderer/modules/xr/xr_space.h"
#include "third_party/blink/renderer/platform/transforms/transformation_matrix.h"

namespace blink {

XRImageTrackingResult::XRImageTrackingResult(
    XRSession* session,
    const device::mojom::blink::XRTrackedImageData& result)
    : session_(session),
      index_(result.index),
      mojo_from_this_(result.mojo_from_image),
      width_in_meters_(result.width_in_meters) {
  DVLOG(2) << __func__ << ": image index=" << index_;
  if (result.actively_tracked) {
    tracking_state_string_ = "tracked";
  } else {
    tracking_state_string_ = "emulated";
  }
}

absl::optional<TransformationMatrix> XRImageTrackingResult::MojoFromObject()
    const {
  if (!mojo_from_this_) {
    return absl::nullopt;
  }

  return TransformationMatrix(mojo_from_this_->ToTransform().matrix());
}

XRSpace* XRImageTrackingResult::imageSpace() const {
  if (!image_space_) {
    image_space_ = MakeGarbageCollected<XRObjectSpace<XRImageTrackingResult>>(
        session_, this);
  }

  return image_space_;
}

device::mojom::blink::XRNativeOriginInformationPtr
XRImageTrackingResult::NativeOrigin() const {
  // TODO(https://crbug.com/1143575): We'll want these to correspond to an
  // actual, independent space eventually, but at the moment it's sufficient for
  // the ARCore implementation to have it be equivalent to the local reference
  // space.
  return device::mojom::blink::XRNativeOriginInformation::NewReferenceSpaceType(
      device::mojom::XRReferenceSpaceType::kLocal);
}

void XRImageTrackingResult::Trace(Visitor* visitor) const {
  visitor->Trace(session_);
  visitor->Trace(image_space_);
  ScriptWrappable::Trace(visitor);
}

}  // namespace blink
