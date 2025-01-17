// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_STORAGE_BLINK_STORAGE_KEY_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_STORAGE_BLINK_STORAGE_KEY_H_

#include <iosfwd>

#include "base/component_export.h"
#include "base/memory/scoped_refptr.h"
#include "third_party/blink/renderer/platform/weborigin/security_origin.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

// This class represents the key by which DOM Storage keys its
// CachedStorageAreas.
// It is typemapped to blink.mojom.StorageKey, and should stay in sync with
// blink::StorageKey (third_party/blink/public/common/storage_key/storage_key.h)
class COMPONENT_EXPORT(MODULES_STORAGE_BLINK_STORAGE_KEY) BlinkStorageKey {
  DISALLOW_NEW();

 public:
  // Creates a BlinkStorageKey with a unique opaque origin.
  BlinkStorageKey();

  // Creates a BlinkStorageKey with the given origin. `origin` must not be null.
  // `origin` can be opaque.
  explicit BlinkStorageKey(scoped_refptr<const SecurityOrigin> origin);

  ~BlinkStorageKey() = default;

  BlinkStorageKey(const BlinkStorageKey& other) = default;
  BlinkStorageKey& operator=(const BlinkStorageKey& other) = default;
  BlinkStorageKey(BlinkStorageKey&& other) = default;
  BlinkStorageKey& operator=(BlinkStorageKey&& other) = default;

  const scoped_refptr<const SecurityOrigin>& GetSecurityOrigin() const {
    return origin_;
  }

  String ToDebugString() const;

 private:
  scoped_refptr<const SecurityOrigin> origin_;
};

COMPONENT_EXPORT(MODULES_STORAGE_BLINK_STORAGE_KEY)
bool operator==(const BlinkStorageKey&, const BlinkStorageKey&);
COMPONENT_EXPORT(MODULES_STORAGE_BLINK_STORAGE_KEY)
bool operator!=(const BlinkStorageKey&, const BlinkStorageKey&);
COMPONENT_EXPORT(MODULES_STORAGE_BLINK_STORAGE_KEY)
std::ostream& operator<<(std::ostream&, const BlinkStorageKey&);

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_STORAGE_BLINK_STORAGE_KEY_H_
