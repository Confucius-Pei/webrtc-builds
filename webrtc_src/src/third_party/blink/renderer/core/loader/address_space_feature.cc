/*
 * Copyright (C) 2020 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "third_party/blink/renderer/core/loader/address_space_feature.h"

#include <tuple>

#include "services/network/public/cpp/cors/cors_error_status.h"
#include "services/network/public/mojom/cors.mojom-forward.h"
#include "third_party/blink/public/mojom/web_feature/web_feature.mojom-forward.h"
#include "third_party/blink/renderer/core/frame/deprecation.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/platform/instrumentation/use_counter.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_error.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_response.h"

namespace blink {
namespace {

using AddressSpace = network::mojom::blink::IPAddressSpace;
using Feature = mojom::blink::WebFeature;

// A key in |kFeatureMap|.
//
// Mirrors the arguments to |AddressSpaceFeature()| except for |fetch_type|.
struct FeatureKey {
  AddressSpace client_address_space;
  bool client_is_secure_context;
  AddressSpace response_address_space;
};

// FeatureKey instances are comparable for equality.
bool operator==(const FeatureKey& lhs, const FeatureKey& rhs) {
  return std::tie(lhs.client_address_space, lhs.client_is_secure_context,
                  lhs.response_address_space) ==
         std::tie(rhs.client_address_space, rhs.client_is_secure_context,
                  rhs.response_address_space);
}

// An entry in |kFeatureMap|.
//
// A single key maps to features for all |fetch_type| values. We could instead
// have two maps, one for subresources and one for navigations, but they would
// have the exact same set of keys. Hence it is simpler to have a single map.
struct FeatureEntry {
  // The key to this entry.
  FeatureKey key;

  // The corresponding feature for |kSubresource| fetch types.
  Feature subresource_feature;

  // The corresponding feature for |kNavigation| fetch types.
  Feature navigation_feature;
};

constexpr bool kNonSecureContext = false;
constexpr bool kSecureContext = true;

constexpr struct FeatureEntry kFeatureMap[] = {
    {
        {AddressSpace::kPrivate, kNonSecureContext, AddressSpace::kLocal},
        Feature::kAddressSpacePrivateNonSecureContextEmbeddedLocal,
        Feature::kAddressSpacePrivateNonSecureContextNavigatedToLocal,
    },
    {
        {AddressSpace::kPrivate, kSecureContext, AddressSpace::kLocal},
        Feature::kAddressSpacePrivateSecureContextEmbeddedLocal,
        Feature::kAddressSpacePrivateSecureContextNavigatedToLocal,
    },
    {
        {AddressSpace::kPublic, kNonSecureContext, AddressSpace::kLocal},
        Feature::kAddressSpacePublicNonSecureContextEmbeddedLocal,
        Feature::kAddressSpacePublicNonSecureContextNavigatedToLocal,
    },
    {
        {AddressSpace::kPublic, kSecureContext, AddressSpace::kLocal},
        Feature::kAddressSpacePublicSecureContextEmbeddedLocal,
        Feature::kAddressSpacePublicSecureContextNavigatedToLocal,
    },
    {
        {AddressSpace::kPublic, kNonSecureContext, AddressSpace::kPrivate},
        Feature::kAddressSpacePublicNonSecureContextEmbeddedPrivate,
        Feature::kAddressSpacePublicNonSecureContextNavigatedToPrivate,
    },
    {
        {AddressSpace::kPublic, kSecureContext, AddressSpace::kPrivate},
        Feature::kAddressSpacePublicSecureContextEmbeddedPrivate,
        Feature::kAddressSpacePublicSecureContextNavigatedToPrivate,
    },
    {
        {AddressSpace::kUnknown, kNonSecureContext, AddressSpace::kLocal},
        Feature::kAddressSpaceUnknownNonSecureContextEmbeddedLocal,
        Feature::kAddressSpaceUnknownNonSecureContextNavigatedToLocal,
    },
    {
        {AddressSpace::kUnknown, kSecureContext, AddressSpace::kLocal},
        Feature::kAddressSpaceUnknownSecureContextEmbeddedLocal,
        Feature::kAddressSpaceUnknownSecureContextNavigatedToLocal,
    },
    {
        {AddressSpace::kUnknown, kNonSecureContext, AddressSpace::kPrivate},
        Feature::kAddressSpaceUnknownNonSecureContextEmbeddedPrivate,
        Feature::kAddressSpaceUnknownNonSecureContextNavigatedToPrivate,
    },
    {
        {AddressSpace::kUnknown, kSecureContext, AddressSpace::kPrivate},
        Feature::kAddressSpaceUnknownSecureContextEmbeddedPrivate,
        Feature::kAddressSpaceUnknownSecureContextNavigatedToPrivate,
    },
};

// Attempts to find an entry matching |key| in |kFeatureMap|.
// Returns a pointer to the entry if successful, nullptr otherwise.
const FeatureEntry* FindFeatureEntry(const FeatureKey& key) {
  for (const FeatureEntry& entry : kFeatureMap) {
    if (key == entry.key) {
      return &entry;
    }
  }
  return nullptr;
}

// The list of features which should be reported as deprecated.
constexpr Feature kDeprecatedFeatures[] = {
    Feature::kAddressSpacePublicNonSecureContextEmbeddedPrivate,
    Feature::kAddressSpacePublicNonSecureContextEmbeddedLocal,
    Feature::kAddressSpacePrivateNonSecureContextEmbeddedLocal,
};

// Returns whether |feature| is deprecated.
bool IsDeprecated(Feature feature) {
  for (Feature entry : kDeprecatedFeatures) {
    if (feature == entry) {
      return true;
    }
  }
  return false;
}

}  // namespace

absl::optional<Feature> AddressSpaceFeature(
    FetchType fetch_type,
    AddressSpace client_address_space,
    bool client_is_secure_context,
    AddressSpace response_address_space) {
  FeatureKey key;
  key.client_address_space = client_address_space;
  key.client_is_secure_context = client_is_secure_context;
  key.response_address_space = response_address_space;

  const FeatureEntry* entry = FindFeatureEntry(key);
  if (!entry) {
    return absl::nullopt;
  }

  switch (fetch_type) {
    case FetchType::kSubresource:
      return entry->subresource_feature;
    case FetchType::kNavigation:
      return entry->navigation_feature;
  }
}

void RecordAddressSpaceFeature(FetchType fetch_type,
                               LocalFrame* client_frame,
                               const ResourceResponse& response) {
  if (!client_frame) {
    return;
  }

  LocalDOMWindow* window = client_frame->DomWindow();
  absl::optional<WebFeature> feature =
      AddressSpaceFeature(fetch_type, window->AddressSpace(),
                          window->IsSecureContext(), response.AddressSpace());
  if (!feature.has_value()) {
    return;
  }

  // This WebFeature encompasses all private network requests.
  UseCounter::Count(window,
                    WebFeature::kMixedContentPrivateHostnameInPublicHostname);

  if (IsDeprecated(*feature)) {
    Deprecation::CountDeprecation(window, *feature);
  } else {
    UseCounter::Count(window, *feature);
  }
}

void RecordAddressSpaceFeature(FetchType fetch_type,
                               LocalFrame* client_frame,
                               const ResourceError& error) {
  if (!client_frame) {
    return;
  }

  absl::optional<network::CorsErrorStatus> status = error.CorsErrorStatus();
  if (!status.has_value() ||
      status->cors_error !=
          network::mojom::CorsError::kInsecurePrivateNetwork) {
    // Not the right kind of error, ignore.
    return;
  }

  LocalDOMWindow* window = client_frame->DomWindow();
  absl::optional<WebFeature> feature = AddressSpaceFeature(
      fetch_type, window->AddressSpace(), window->IsSecureContext(),
      status->resource_address_space);
  if (!feature.has_value()) {
    return;
  }

  // This WebFeature encompasses all private network requests.
  UseCounter::Count(window,
                    WebFeature::kMixedContentPrivateHostnameInPublicHostname);

  // Count the feature but do not log it as a deprecation, since its use is
  // forbidden and has resulted in the fetch failing. In other words, the
  // document only *attempted* to use a feature that is no longer available.
  UseCounter::Count(window, *feature);
}

}  // namespace blink
