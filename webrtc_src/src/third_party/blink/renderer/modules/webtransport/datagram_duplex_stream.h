// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_WEBTRANSPORT_DATAGRAM_DUPLEX_STREAM_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_WEBTRANSPORT_DATAGRAM_DUPLEX_STREAM_H_

#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/renderer/modules/modules_export.h"
#include "third_party/blink/renderer/modules/webtransport/web_transport.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "third_party/blink/renderer/platform/heap/visitor.h"

namespace blink {

class ReadableStream;
class WritableStream;

constexpr int32_t kDefaultIncomingHighWaterMark = 1;
constexpr int32_t kDefaultOutgoingHighWaterMark = 1;

class MODULES_EXPORT DatagramDuplexStream : public ScriptWrappable {
  DEFINE_WRAPPERTYPEINFO();

 public:
  // Currently we delegate to the WebTransport object rather than store the
  // readable and writable separately.
  // TODO(ricea): Once the legacy getters are removed from WebTransport, store
  // the readable and writable in this object.
  explicit DatagramDuplexStream(WebTransport* web_transport)
      : web_transport_(web_transport) {}

  ReadableStream* readable() const {
    return web_transport_->datagramReadable();
  }

  WritableStream* writable() const {
    return web_transport_->datagramWritable();
  }

  absl::optional<double> incomingMaxAge() const { return incoming_max_age_; }
  void setIncomingMaxAge(absl::optional<double> max_age);

  absl::optional<double> outgoingMaxAge() const { return outgoing_max_age_; }
  void setOutgoingMaxAge(absl::optional<double> max_age);

  int32_t incomingHighWaterMark() const { return incoming_high_water_mark_; }
  void setIncomingHighWaterMark(int32_t high_water_mark);

  int32_t outgoingHighWaterMark() const { return outgoing_high_water_mark_; }
  void setOutgoingHighWaterMark(int32_t high_water_mark);

  void Trace(Visitor* visitor) const override {
    visitor->Trace(web_transport_);
    ScriptWrappable::Trace(visitor);
  }

 private:
  const Member<WebTransport> web_transport_;

  absl::optional<double> incoming_max_age_;
  absl::optional<double> outgoing_max_age_;
  int32_t incoming_high_water_mark_ = kDefaultIncomingHighWaterMark;
  int32_t outgoing_high_water_mark_ = kDefaultOutgoingHighWaterMark;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_WEBTRANSPORT_DATAGRAM_DUPLEX_STREAM_H_
