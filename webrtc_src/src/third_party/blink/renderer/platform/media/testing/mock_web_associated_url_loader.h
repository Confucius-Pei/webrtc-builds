// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_MEDIA_TESTING_MOCK_WEB_ASSOCIATED_URL_LOADER_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_MEDIA_TESTING_MOCK_WEB_ASSOCIATED_URL_LOADER_H_

#include "base/single_thread_task_runner.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/blink/public/web/web_associated_url_loader.h"

namespace media {

class MockWebAssociatedURLLoader : public blink::WebAssociatedURLLoader {
 public:
  MockWebAssociatedURLLoader();
  MockWebAssociatedURLLoader(const MockWebAssociatedURLLoader&) = delete;
  MockWebAssociatedURLLoader& operator=(const MockWebAssociatedURLLoader&) =
      delete;
  ~MockWebAssociatedURLLoader() override;

  MOCK_METHOD2(LoadAsynchronously,
               void(const blink::WebURLRequest& request,
                    blink::WebAssociatedURLLoaderClient* client));
  MOCK_METHOD0(Cancel, void());
  MOCK_METHOD1(SetDefersLoading, void(bool value));
  MOCK_METHOD1(SetLoadingTaskRunner,
               void(base::SingleThreadTaskRunner* task_runner));
};

}  // namespace media

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_MEDIA_TESTING_MOCK_WEB_ASSOCIATED_URL_LOADER_H_
