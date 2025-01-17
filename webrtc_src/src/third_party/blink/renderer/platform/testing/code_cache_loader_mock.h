// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_TESTING_CODE_CACHE_LOADER_MOCK_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_TESTING_CODE_CACHE_LOADER_MOCK_H_

#include "base/memory/weak_ptr.h"
#include "third_party/blink/public/platform/web_code_cache_loader.h"
#include "url/gurl.h"

namespace blink {

// A simple class for mocking WebCodeCacheLoader.
class CodeCacheLoaderMock : public WebCodeCacheLoader {
 public:
  CodeCacheLoaderMock() {}
  CodeCacheLoaderMock(const CodeCacheLoaderMock&) = delete;
  CodeCacheLoaderMock& operator=(const CodeCacheLoaderMock&) = delete;
  ~CodeCacheLoaderMock() override = default;

  // CodeCacheLoader methods:
  void FetchFromCodeCacheSynchronously(
      const WebURL& url,
      base::Time* response_time_out,
      mojo_base::BigBuffer* buffer_out) override;
  void FetchFromCodeCache(
      blink::mojom::CodeCacheType cache_type,
      const WebURL& url,
      WebCodeCacheLoader::FetchCodeCacheCallback callback) override;

  base::WeakPtr<CodeCacheLoaderMock> GetWeakPtr();

 private:
  base::WeakPtrFactory<CodeCacheLoaderMock> weak_ptr_factory_{this};
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_TESTING_CODE_CACHE_LOADER_MOCK_H_
