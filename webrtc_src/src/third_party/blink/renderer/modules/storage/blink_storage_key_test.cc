// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/storage/blink_storage_key.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/platform/weborigin/security_origin.h"

namespace blink {

TEST(BlinkStorageKeyTest, OpaqueOriginsDistinct) {
  // Test that two opaque origins give distinct BlinkStorageKeys.
  BlinkStorageKey unique_opaque1;
  EXPECT_TRUE(unique_opaque1.GetSecurityOrigin());
  EXPECT_TRUE(unique_opaque1.GetSecurityOrigin()->IsOpaque());
  BlinkStorageKey unique_opaque2;
  EXPECT_FALSE(unique_opaque2.GetSecurityOrigin()->IsSameOriginWith(
      unique_opaque1.GetSecurityOrigin().get()));
  EXPECT_NE(unique_opaque1, unique_opaque2);
}

TEST(BlinkStorageKeyTest, OpaqueOriginRetained) {
  // Test that a StorageKey made from an opaque origin retains the origin.
  scoped_refptr<const SecurityOrigin> opaque_origin =
      SecurityOrigin::CreateUniqueOpaque();
  scoped_refptr<const SecurityOrigin> opaque_copied =
      opaque_origin->IsolatedCopy();
  BlinkStorageKey from_opaque(std::move(opaque_origin));
  EXPECT_TRUE(
      from_opaque.GetSecurityOrigin()->IsSameOriginWith(opaque_copied.get()));
}

TEST(BlinkStorageKeyTest, CreateFromNonOpaqueOrigin) {
  struct {
    const char* origin;
  } kTestCases[] = {
      {"http://example.site"},
      {"https://example.site"},
      {"file:///path/to/file"},
  };

  for (const auto& test : kTestCases) {
    scoped_refptr<const SecurityOrigin> origin =
        SecurityOrigin::CreateFromString(test.origin);
    ASSERT_FALSE(origin->IsOpaque());
    scoped_refptr<const SecurityOrigin> copied = origin->IsolatedCopy();

    // Test that the origin is retained.
    BlinkStorageKey storage_key(std::move(origin));
    EXPECT_TRUE(
        storage_key.GetSecurityOrigin()->IsSameOriginWith(copied.get()));

    // Test that two StorageKeys from the same origin are the same.
    BlinkStorageKey storage_key_from_copy(std::move(copied));
    EXPECT_EQ(storage_key, storage_key_from_copy);
  }
}

}  // namespace blink
