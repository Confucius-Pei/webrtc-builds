// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/accessibility/ax_media_element.h"

#include "third_party/blink/public/strings/grit/blink_strings.h"
#include "third_party/blink/renderer/core/html/media/html_media_element.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/platform/text/platform_locale.h"

namespace blink {

AXObject* AccessibilityMediaElement::Create(
    LayoutObject* layout_object,
    AXObjectCacheImpl& ax_object_cache) {
  DCHECK(layout_object->GetNode());
  DCHECK(IsA<HTMLMediaElement>(layout_object->GetNode()));
  return MakeGarbageCollected<AccessibilityMediaElement>(layout_object,
                                                         ax_object_cache);
}

AccessibilityMediaElement::AccessibilityMediaElement(
    LayoutObject* layout_object,
    AXObjectCacheImpl& ax_object_cache)
    : AXLayoutObject(layout_object, ax_object_cache) {}

String AccessibilityMediaElement::TextAlternative(
    bool recursive,
    const AXObject* aria_label_or_description_root,
    AXObjectSet& visited,
    ax::mojom::NameFrom& name_from,
    AXRelatedObjectVector* related_objects,
    NameSources* name_sources) const {
  if (IsDetached())
    return String();

  if (IsUnplayable()) {
    HTMLMediaElement* element =
        static_cast<HTMLMediaElement*>(layout_object_->GetNode());
    return element->GetLocale().QueryString(IDS_MEDIA_PLAYBACK_ERROR);
  }
  return AXLayoutObject::TextAlternative(
      recursive, aria_label_or_description_root, visited, name_from,
      related_objects, name_sources);
}

bool AccessibilityMediaElement::CanHaveChildren() const {
  return true;
}

bool AccessibilityMediaElement::ComputeAccessibilityIsIgnored(
    IgnoredReasons* ignored_reasons) const {
  return false;
}

AXRestriction AccessibilityMediaElement::Restriction() const {
  if (IsUnplayable())
    return kRestrictionDisabled;

  return AXNodeObject::Restriction();
}

bool AccessibilityMediaElement::HasControls() const {
  if (IsDetached())
    return false;
  if (!IsA<HTMLMediaElement>(GetNode()) || !GetNode()->isConnected()) {
    NOTREACHED() << "Accessible media element not ready: " << GetNode()
                 << "  isConnected? " << GetNode()->isConnected();
    return false;
  }
  return To<HTMLMediaElement>(GetNode())->ShouldShowControls();
}

bool AccessibilityMediaElement::HasEmptySource() const {
  if (IsDetached())
    return false;
  return To<HTMLMediaElement>(GetNode())->getNetworkState() ==
         HTMLMediaElement::kNetworkEmpty;
}

bool AccessibilityMediaElement::IsUnplayable() const {
  if (IsDetached())
    return true;
  HTMLMediaElement* element =
      static_cast<HTMLMediaElement*>(layout_object_->GetNode());
  HTMLMediaElement::NetworkState network_state = element->getNetworkState();
  return (element->error() ||
          network_state == HTMLMediaElement::kNetworkEmpty ||
          network_state == HTMLMediaElement::kNetworkNoSource);
}

}  // namespace blink
