// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_MEDIA_TESTING_MOCK_RESOURCE_FETCH_CONTEXT_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_MEDIA_TESTING_MOCK_RESOURCE_FETCH_CONTEXT_H_

#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/blink/public/platform/media/resource_fetch_context.h"

namespace media {

class MockResourceFetchContext : public ResourceFetchContext {
 public:
  MockResourceFetchContext();
  MockResourceFetchContext(const MockResourceFetchContext&) = delete;
  MockResourceFetchContext& operator=(const MockResourceFetchContext&) = delete;
  ~MockResourceFetchContext() override;

  MOCK_METHOD1(CreateUrlLoader,
               std::unique_ptr<blink::WebAssociatedURLLoader>(
                   const blink::WebAssociatedURLLoaderOptions&));
};

}  // namespace media

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_MEDIA_TESTING_MOCK_RESOURCE_FETCH_CONTEXT_H_
