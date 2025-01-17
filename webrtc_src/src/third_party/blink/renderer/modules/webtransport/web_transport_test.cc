// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/webtransport/web_transport.h"

#include <array>
#include <memory>
#include <utility>

#include "base/containers/span.h"
#include "base/memory/weak_ptr.h"
#include "base/test/mock_callback.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "services/network/public/mojom/web_transport.mojom-blink.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/browser_interface_broker_proxy.h"
#include "third_party/blink/public/mojom/webtransport/web_transport_connector.mojom-blink.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_tester.h"
#include "third_party/blink/renderer/bindings/core/v8/script_value.h"
#include "third_party/blink/renderer/bindings/core/v8/to_v8_for_core.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_array_buffer.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_binding_for_testing.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_dom_exception.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_gc_controller.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_iterator_result_value.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_uint8_array.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_bidirectional_stream.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_receive_stream.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_rtc_dtls_fingerprint.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_send_stream.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_web_transport_close_info.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_web_transport_options.h"
#include "third_party/blink/renderer/core/frame/csp/content_security_policy.h"
#include "third_party/blink/renderer/core/streams/readable_stream.h"
#include "third_party/blink/renderer/core/streams/readable_stream_default_reader.h"
#include "third_party/blink/renderer/core/streams/writable_stream.h"
#include "third_party/blink/renderer/core/streams/writable_stream_default_writer.h"
#include "third_party/blink/renderer/core/typed_arrays/dom_array_buffer.h"
#include "third_party/blink/renderer/core/typed_arrays/dom_typed_array.h"
#include "third_party/blink/renderer/modules/webtransport/datagram_duplex_stream.h"
#include "third_party/blink/renderer/modules/webtransport/receive_stream.h"
#include "third_party/blink/renderer/modules/webtransport/send_stream.h"
#include "third_party/blink/renderer/modules/webtransport/test_utils.h"
#include "third_party/blink/renderer/platform/bindings/exception_code.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/testing/unit_test_helpers.h"
#include "third_party/blink/renderer/platform/wtf/deque.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "v8/include/v8.h"

namespace blink {

namespace {

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::StrictMock;
using ::testing::Truly;
using ::testing::Unused;

class WebTransportConnector final : public mojom::blink::WebTransportConnector {
 public:
  struct ConnectArgs {
    ConnectArgs(
        const KURL& url,
        Vector<network::mojom::blink::WebTransportCertificateFingerprintPtr>
            fingerprints,
        mojo::PendingRemote<network::mojom::blink::WebTransportHandshakeClient>
            handshake_client)
        : url(url),
          fingerprints(std::move(fingerprints)),
          handshake_client(std::move(handshake_client)) {}

    KURL url;
    Vector<network::mojom::blink::WebTransportCertificateFingerprintPtr>
        fingerprints;
    mojo::PendingRemote<network::mojom::blink::WebTransportHandshakeClient>
        handshake_client;
  };

  void Connect(
      const KURL& url,
      Vector<network::mojom::blink::WebTransportCertificateFingerprintPtr>
          fingerprints,
      mojo::PendingRemote<network::mojom::blink::WebTransportHandshakeClient>
          handshake_client) override {
    connect_args_.push_back(
        ConnectArgs(url, std::move(fingerprints), std::move(handshake_client)));
  }

  Vector<ConnectArgs> TakeConnectArgs() { return std::move(connect_args_); }

  void Bind(
      mojo::PendingReceiver<mojom::blink::WebTransportConnector> receiver) {
    receiver_set_.Add(this, std::move(receiver));
  }

 private:
  mojo::ReceiverSet<mojom::blink::WebTransportConnector> receiver_set_;
  Vector<ConnectArgs> connect_args_;
};

class MockWebTransport : public network::mojom::blink::WebTransport {
 public:
  explicit MockWebTransport(
      mojo::PendingReceiver<network::mojom::blink::WebTransport>
          pending_receiver)
      : receiver_(this, std::move(pending_receiver)) {}

  MOCK_METHOD2(SendDatagram,
               void(base::span<const uint8_t> data,
                    base::OnceCallback<void(bool)> callback));

  MOCK_METHOD3(CreateStream,
               void(mojo::ScopedDataPipeConsumerHandle readable,
                    mojo::ScopedDataPipeProducerHandle writable,
                    base::OnceCallback<void(bool, uint32_t)> callback));

  MOCK_METHOD1(
      AcceptBidirectionalStream,
      void(base::OnceCallback<void(uint32_t,
                                   mojo::ScopedDataPipeConsumerHandle,
                                   mojo::ScopedDataPipeProducerHandle)>));

  MOCK_METHOD1(AcceptUnidirectionalStream,
               void(base::OnceCallback<
                    void(uint32_t, mojo::ScopedDataPipeConsumerHandle)>));

  MOCK_METHOD1(SetOutgoingDatagramExpirationDuration, void(base::TimeDelta));

  void SendFin(uint32_t stream_id) override {}
  void AbortStream(uint32_t stream_id, uint64_t code) override {}

 private:
  mojo::Receiver<network::mojom::blink::WebTransport> receiver_;
};

class WebTransportTest : public ::testing::Test {
 public:
  using AcceptUnidirectionalStreamCallback =
      base::OnceCallback<void(uint32_t, mojo::ScopedDataPipeConsumerHandle)>;
  using AcceptBidirectionalStreamCallback =
      base::OnceCallback<void(uint32_t,
                              mojo::ScopedDataPipeConsumerHandle,
                              mojo::ScopedDataPipeProducerHandle)>;

  void AddBinder(const V8TestingScope& scope) {
    interface_broker_ =
        &scope.GetExecutionContext()->GetBrowserInterfaceBroker();
    interface_broker_->SetBinderForTesting(
        mojom::blink::WebTransportConnector::Name_,
        base::BindRepeating(&WebTransportTest::BindConnector,
                            weak_ptr_factory_.GetWeakPtr()));
  }

  static WebTransportOptions* EmptyOptions() {
    return MakeGarbageCollected<WebTransportOptions>();
  }

  // Creates a WebTransport object with the given |url|.
  WebTransport* Create(const V8TestingScope& scope,
                       const String& url,
                       WebTransportOptions* options) {
    AddBinder(scope);
    return WebTransport::Create(scope.GetScriptState(), url, options,
                                ASSERT_NO_EXCEPTION);
  }

  // Connects a WebTransport object. Runs the event loop.
  void ConnectSuccessfully(WebTransport* web_transport) {
    DCHECK(!mock_web_transport_) << "Only one connection supported, sorry";

    test::RunPendingTasks();

    auto args = connector_.TakeConnectArgs();
    if (args.size() != 1u) {
      ADD_FAILURE() << "args.size() should be 1, but is " << args.size();
      return;
    }

    mojo::Remote<network::mojom::blink::WebTransportHandshakeClient>
        handshake_client(std::move(args[0].handshake_client));

    mojo::PendingRemote<network::mojom::blink::WebTransport>
        web_transport_to_pass;
    mojo::PendingRemote<network::mojom::blink::WebTransportClient>
        client_remote;

    mock_web_transport_ = std::make_unique<StrictMock<MockWebTransport>>(
        web_transport_to_pass.InitWithNewPipeAndPassReceiver());

    // These are called on every connection, so expect them in every test.
    EXPECT_CALL(*mock_web_transport_, AcceptUnidirectionalStream(_))
        .WillRepeatedly([this](AcceptUnidirectionalStreamCallback callback) {
          pending_unidirectional_accept_callbacks_.push_back(
              std::move(callback));
        });

    EXPECT_CALL(*mock_web_transport_, AcceptBidirectionalStream(_))
        .WillRepeatedly([this](AcceptBidirectionalStreamCallback callback) {
          pending_bidirectional_accept_callbacks_.push_back(
              std::move(callback));
        });

    handshake_client->OnConnectionEstablished(
        std::move(web_transport_to_pass),
        client_remote.InitWithNewPipeAndPassReceiver());
    client_remote_.Bind(std::move(client_remote));

    test::RunPendingTasks();
  }

  // Creates, connects and returns a WebTransport object with the given |url|.
  // Runs the event loop.
  WebTransport* CreateAndConnectSuccessfully(
      const V8TestingScope& scope,
      const String& url,
      WebTransportOptions* options = EmptyOptions()) {
    auto* web_transport = Create(scope, url, options);
    ConnectSuccessfully(web_transport);
    return web_transport;
  }

  SendStream* CreateSendStreamSuccessfully(const V8TestingScope& scope,
                                           WebTransport* web_transport) {
    EXPECT_CALL(*mock_web_transport_, CreateStream(_, _, _))
        .WillOnce([this](mojo::ScopedDataPipeConsumerHandle handle, Unused,
                         base::OnceCallback<void(bool, uint32_t)> callback) {
          send_stream_consumer_handle_ = std::move(handle);
          std::move(callback).Run(true, next_stream_id_++);
        });

    auto* script_state = scope.GetScriptState();
    ScriptPromise send_stream_promise =
        web_transport->createUnidirectionalStream(script_state,
                                                  ASSERT_NO_EXCEPTION);
    ScriptPromiseTester tester(script_state, send_stream_promise);

    tester.WaitUntilSettled();

    EXPECT_TRUE(tester.IsFulfilled());
    auto* send_stream = V8SendStream::ToImplWithTypeCheck(
        scope.GetIsolate(), tester.Value().V8Value());
    EXPECT_TRUE(send_stream);
    return send_stream;
  }

  mojo::ScopedDataPipeProducerHandle DoAcceptUnidirectionalStream() {
    mojo::ScopedDataPipeProducerHandle producer;
    mojo::ScopedDataPipeConsumerHandle consumer;

    // There's no good way to handle failure to create the pipe, so just
    // continue.
    CreateDataPipeForWebTransportTests(&producer, &consumer);

    std::move(pending_unidirectional_accept_callbacks_.front())
        .Run(next_stream_id_++, std::move(consumer));
    pending_unidirectional_accept_callbacks_.pop_front();

    return producer;
  }

  ReceiveStream* ReadReceiveStream(const V8TestingScope& scope,
                                   WebTransport* web_transport) {
    ReadableStream* streams = web_transport->incomingUnidirectionalStreams();

    v8::Local<v8::Value> v8value = ReadValueFromStream(scope, streams);

    ReceiveStream* receive_stream =
        V8ReceiveStream::ToImplWithTypeCheck(scope.GetIsolate(), v8value);
    EXPECT_TRUE(receive_stream);

    return receive_stream;
  }

  void BindConnector(mojo::ScopedMessagePipeHandle handle) {
    connector_.Bind(mojo::PendingReceiver<mojom::blink::WebTransportConnector>(
        std::move(handle)));
  }

  void TearDown() override {
    if (!interface_broker_)
      return;
    interface_broker_->SetBinderForTesting(
        mojom::blink::WebTransportConnector::Name_, {});
  }

  const BrowserInterfaceBrokerProxy* interface_broker_ = nullptr;
  WTF::Deque<AcceptUnidirectionalStreamCallback>
      pending_unidirectional_accept_callbacks_;
  WTF::Deque<AcceptBidirectionalStreamCallback>
      pending_bidirectional_accept_callbacks_;
  WebTransportConnector connector_;
  std::unique_ptr<MockWebTransport> mock_web_transport_;
  mojo::Remote<network::mojom::blink::WebTransportClient> client_remote_;
  uint32_t next_stream_id_ = 0;
  mojo::ScopedDataPipeConsumerHandle send_stream_consumer_handle_;

  base::WeakPtrFactory<WebTransportTest> weak_ptr_factory_{this};
};

TEST_F(WebTransportTest, FailWithNullURL) {
  V8TestingScope scope;
  auto& exception_state = scope.GetExceptionState();
  WebTransport::Create(scope.GetScriptState(), String(), EmptyOptions(),
                       exception_state);
  EXPECT_TRUE(exception_state.HadException());
  EXPECT_EQ(static_cast<int>(DOMExceptionCode::kSyntaxError),
            exception_state.Code());
}

TEST_F(WebTransportTest, FailWithEmptyURL) {
  V8TestingScope scope;
  auto& exception_state = scope.GetExceptionState();
  WebTransport::Create(scope.GetScriptState(), String(""), EmptyOptions(),
                       exception_state);
  EXPECT_TRUE(exception_state.HadException());
  EXPECT_EQ(static_cast<int>(DOMExceptionCode::kSyntaxError),
            exception_state.Code());
  EXPECT_EQ("The URL '' is invalid.", exception_state.Message());
}

TEST_F(WebTransportTest, FailWithNoScheme) {
  V8TestingScope scope;
  auto& exception_state = scope.GetExceptionState();
  WebTransport::Create(scope.GetScriptState(), String("no-scheme"),
                       EmptyOptions(), exception_state);
  EXPECT_TRUE(exception_state.HadException());
  EXPECT_EQ(static_cast<int>(DOMExceptionCode::kSyntaxError),
            exception_state.Code());
  EXPECT_EQ("The URL 'no-scheme' is invalid.", exception_state.Message());
}

TEST_F(WebTransportTest, FailWithHttpsURL) {
  V8TestingScope scope;
  auto& exception_state = scope.GetExceptionState();
  WebTransport::Create(scope.GetScriptState(), String("http://example.com/"),
                       EmptyOptions(), exception_state);
  EXPECT_TRUE(exception_state.HadException());
  EXPECT_EQ(static_cast<int>(DOMExceptionCode::kSyntaxError),
            exception_state.Code());
  EXPECT_EQ("The URL's scheme must be 'https'. 'http' is not allowed.",
            exception_state.Message());
}

TEST_F(WebTransportTest, FailWithNoHost) {
  V8TestingScope scope;
  auto& exception_state = scope.GetExceptionState();
  WebTransport::Create(scope.GetScriptState(), String("https:///"),
                       EmptyOptions(), exception_state);
  EXPECT_TRUE(exception_state.HadException());
  EXPECT_EQ(static_cast<int>(DOMExceptionCode::kSyntaxError),
            exception_state.Code());
  EXPECT_EQ("The URL 'https:///' is invalid.", exception_state.Message());
}

TEST_F(WebTransportTest, FailWithURLFragment) {
  V8TestingScope scope;
  auto& exception_state = scope.GetExceptionState();
  WebTransport::Create(scope.GetScriptState(),
                       String("https://example.com/#failing"), EmptyOptions(),
                       exception_state);
  EXPECT_TRUE(exception_state.HadException());
  EXPECT_EQ(static_cast<int>(DOMExceptionCode::kSyntaxError),
            exception_state.Code());
  EXPECT_EQ(
      "The URL contains a fragment identifier ('#failing'). Fragment "
      "identifiers are not allowed in WebTransport URLs.",
      exception_state.Message());
}

TEST_F(WebTransportTest, FailByCSP) {
  V8TestingScope scope;
  auto& exception_state = scope.GetExceptionState();
  scope.GetExecutionContext()
      ->GetContentSecurityPolicyForCurrentWorld()
      ->AddPolicies(ParseContentSecurityPolicies(
          "connect-src 'none'",
          network::mojom::ContentSecurityPolicyType::kEnforce,
          network::mojom::ContentSecurityPolicySource::kHTTP,
          *(scope.GetExecutionContext()->GetSecurityOrigin())));
  WebTransport::Create(scope.GetScriptState(), String("https://example.com/"),
                       EmptyOptions(), exception_state);
  EXPECT_TRUE(exception_state.HadException());
  EXPECT_EQ(static_cast<int>(DOMExceptionCode::kSecurityError),
            exception_state.Code());
  EXPECT_EQ("Failed to connect to 'https://example.com/'",
            exception_state.Message());
}

TEST_F(WebTransportTest, PassCSP) {
  V8TestingScope scope;
  // This doesn't work without the https:// prefix, even thought it should
  // according to
  // https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Content-Security-Policy/connect-src.
  auto& exception_state = scope.GetExceptionState();
  scope.GetExecutionContext()
      ->GetContentSecurityPolicyForCurrentWorld()
      ->AddPolicies(ParseContentSecurityPolicies(
          "connect-src https://example.com",
          network::mojom::ContentSecurityPolicyType::kEnforce,
          network::mojom::ContentSecurityPolicySource::kHTTP,
          *(scope.GetExecutionContext()->GetSecurityOrigin())));
  WebTransport::Create(scope.GetScriptState(), String("https://example.com/"),
                       EmptyOptions(), exception_state);
  EXPECT_FALSE(exception_state.HadException());
}

TEST_F(WebTransportTest, SendConnect) {
  V8TestingScope scope;
  AddBinder(scope);
  auto* web_transport = WebTransport::Create(
      scope.GetScriptState(), String("https://example.com/"), EmptyOptions(),
      ASSERT_NO_EXCEPTION);

  test::RunPendingTasks();

  auto args = connector_.TakeConnectArgs();
  ASSERT_EQ(1u, args.size());
  EXPECT_EQ(KURL("https://example.com/"), args[0].url);
  EXPECT_TRUE(args[0].fingerprints.IsEmpty());
  EXPECT_TRUE(web_transport->HasPendingActivity());
}

TEST_F(WebTransportTest, SuccessfulConnect) {
  V8TestingScope scope;
  auto* web_transport =
      CreateAndConnectSuccessfully(scope, "https://example.com");
  ScriptPromiseTester ready_tester(scope.GetScriptState(),
                                   web_transport->ready());

  EXPECT_TRUE(web_transport->HasPendingActivity());

  ready_tester.WaitUntilSettled();
  EXPECT_TRUE(ready_tester.IsFulfilled());
}

TEST_F(WebTransportTest, FailedConnect) {
  V8TestingScope scope;
  AddBinder(scope);
  auto* web_transport = WebTransport::Create(
      scope.GetScriptState(), String("https://example.com/"), EmptyOptions(),
      ASSERT_NO_EXCEPTION);
  ScriptPromiseTester ready_tester(scope.GetScriptState(),
                                   web_transport->ready());
  ScriptPromiseTester closed_tester(scope.GetScriptState(),
                                    web_transport->closed());

  test::RunPendingTasks();

  auto args = connector_.TakeConnectArgs();
  ASSERT_EQ(1u, args.size());

  mojo::Remote<network::mojom::blink::WebTransportHandshakeClient>
      handshake_client(std::move(args[0].handshake_client));

  handshake_client->OnHandshakeFailed(nullptr);

  test::RunPendingTasks();
  EXPECT_FALSE(web_transport->HasPendingActivity());
  EXPECT_TRUE(ready_tester.IsRejected());
  EXPECT_TRUE(closed_tester.IsRejected());
}

TEST_F(WebTransportTest, SendConnectWithFingerprint) {
  V8TestingScope scope;
  AddBinder(scope);
  auto* fingerprints = MakeGarbageCollected<RTCDtlsFingerprint>();
  fingerprints->setAlgorithm("sha-256");
  fingerprints->setValue(
      "ED:3D:D7:C3:67:10:94:68:D1:DC:D1:26:5C:B2:74:D7:1C:A2:63:3E:94:94:C0:84:"
      "39:D6:64:FA:08:B9:77:37");
  auto* options = MakeGarbageCollected<WebTransportOptions>();
  options->setServerCertificateFingerprints({fingerprints});
  WebTransport::Create(scope.GetScriptState(), String("https://example.com/"),
                       options, ASSERT_NO_EXCEPTION);

  test::RunPendingTasks();

  auto args = connector_.TakeConnectArgs();
  ASSERT_EQ(1u, args.size());
  ASSERT_EQ(1u, args[0].fingerprints.size());
  EXPECT_EQ(args[0].fingerprints[0]->algorithm, "sha-256");
  EXPECT_EQ(args[0].fingerprints[0]->fingerprint,
            "ED:3D:D7:C3:67:10:94:68:D1:DC:D1:26:5C:B2:74:D7:1C:A2:63:3E:94:94:"
            "C0:84:39:D6:64:FA:08:B9:77:37");
}

TEST_F(WebTransportTest, CloseDuringConnect) {
  V8TestingScope scope;
  AddBinder(scope);
  auto* web_transport = WebTransport::Create(
      scope.GetScriptState(), String("https://example.com/"), EmptyOptions(),
      ASSERT_NO_EXCEPTION);
  ScriptPromiseTester ready_tester(scope.GetScriptState(),
                                   web_transport->ready());
  ScriptPromiseTester closed_tester(scope.GetScriptState(),
                                    web_transport->closed());

  test::RunPendingTasks();

  auto args = connector_.TakeConnectArgs();
  ASSERT_EQ(1u, args.size());

  web_transport->close(nullptr);

  test::RunPendingTasks();

  EXPECT_FALSE(web_transport->HasPendingActivity());
  EXPECT_TRUE(ready_tester.IsRejected());
  EXPECT_TRUE(closed_tester.IsFulfilled());
}

TEST_F(WebTransportTest, CloseAfterConnection) {
  V8TestingScope scope;
  auto* web_transport =
      CreateAndConnectSuccessfully(scope, "https://example.com");
  ScriptPromiseTester ready_tester(scope.GetScriptState(),
                                   web_transport->ready());
  ScriptPromiseTester closed_tester(scope.GetScriptState(),
                                    web_transport->closed());

  WebTransportCloseInfo close_info;
  close_info.setErrorCode(42);
  close_info.setReason("because");
  web_transport->close(&close_info);

  test::RunPendingTasks();

  // TODO(ricea): Check that the close info is sent through correctly, once we
  // start sending it.

  EXPECT_FALSE(web_transport->HasPendingActivity());
  EXPECT_TRUE(ready_tester.IsFulfilled());
  EXPECT_TRUE(closed_tester.IsFulfilled());

  // Calling close again does nothing.
  web_transport->close(nullptr);
}

// A live connection will be kept alive even if there is no explicit reference.
// When the underlying connection is shut down, the connection will be swept.
TEST_F(WebTransportTest, GarbageCollection) {
  V8TestingScope scope;

  WeakPersistent<WebTransport> web_transport;

  {
    // The streams created when creating a WebTransport create some v8 handles.
    // To ensure these are collected, we need to create a handle scope. This is
    // not a problem for garbage collection in normal operation.
    v8::HandleScope handle_scope(scope.GetIsolate());
    web_transport = CreateAndConnectSuccessfully(scope, "https://example.com");
  }

  // Pretend the stack is empty. This will avoid accidentally treating any
  // copies of the |web_transport| pointer as references.
  ThreadState::Current()->CollectAllGarbageForTesting();

  EXPECT_TRUE(web_transport);

  web_transport->close(nullptr);

  test::RunPendingTasks();

  ThreadState::Current()->CollectAllGarbageForTesting();

  EXPECT_FALSE(web_transport);
}

TEST_F(WebTransportTest, GarbageCollectMojoConnectionError) {
  V8TestingScope scope;

  WeakPersistent<WebTransport> web_transport;

  {
    v8::HandleScope handle_scope(scope.GetIsolate());
    web_transport = CreateAndConnectSuccessfully(scope, "https://example.com");
  }

  ScriptPromiseTester closed_tester(scope.GetScriptState(),
                                    web_transport->closed());

  // Closing the server-side of the pipe causes a mojo connection error.
  client_remote_.reset();

  test::RunPendingTasks();

  ThreadState::Current()->CollectAllGarbageForTesting();

  EXPECT_FALSE(web_transport);
  EXPECT_TRUE(closed_tester.IsRejected());
}

TEST_F(WebTransportTest, SendDatagram) {
  V8TestingScope scope;
  auto* web_transport =
      CreateAndConnectSuccessfully(scope, "https://example.com");

  EXPECT_CALL(*mock_web_transport_, SendDatagram(ElementsAre('A'), _))
      .WillOnce(Invoke([](base::span<const uint8_t>,
                          MockWebTransport::SendDatagramCallback callback) {
        std::move(callback).Run(true);
      }));

  auto* writable = web_transport->datagrams()->writable();
  auto* script_state = scope.GetScriptState();
  auto* writer = writable->getWriter(script_state, ASSERT_NO_EXCEPTION);
  auto* chunk = DOMUint8Array::Create(1);
  *chunk->Data() = 'A';
  ScriptPromise result =
      writer->write(script_state, ScriptValue::From(script_state, chunk),
                    ASSERT_NO_EXCEPTION);
  ScriptPromiseTester tester(script_state, result);
  tester.WaitUntilSettled();
  EXPECT_TRUE(tester.IsFulfilled());
  EXPECT_TRUE(tester.Value().IsUndefined());
}

TEST_F(WebTransportTest, BackpressureForOutgoingDatagrams) {
  V8TestingScope scope;
  auto* const options = MakeGarbageCollected<WebTransportOptions>();
  options->setDatagramWritableHighWaterMark(3);
  auto* web_transport =
      CreateAndConnectSuccessfully(scope, "https://example.com", options);

  EXPECT_CALL(*mock_web_transport_, SendDatagram(_, _))
      .Times(4)
      .WillRepeatedly(
          Invoke([](base::span<const uint8_t>,
                    MockWebTransport::SendDatagramCallback callback) {
            std::move(callback).Run(true);
          }));

  auto* writable = web_transport->datagrams()->writable();
  auto* script_state = scope.GetScriptState();
  auto* writer = writable->getWriter(script_state, ASSERT_NO_EXCEPTION);

  ScriptPromise promise1;
  ScriptPromise promise2;
  ScriptPromise promise3;
  ScriptPromise promise4;

  {
    auto* chunk = DOMUint8Array::Create(1);
    *chunk->Data() = 'A';
    promise1 =
        writer->write(script_state, ScriptValue::From(script_state, chunk),
                      ASSERT_NO_EXCEPTION);
  }
  {
    auto* chunk = DOMUint8Array::Create(1);
    *chunk->Data() = 'B';
    promise2 =
        writer->write(script_state, ScriptValue::From(script_state, chunk),
                      ASSERT_NO_EXCEPTION);
  }
  {
    auto* chunk = DOMUint8Array::Create(1);
    *chunk->Data() = 'C';
    promise3 =
        writer->write(script_state, ScriptValue::From(script_state, chunk),
                      ASSERT_NO_EXCEPTION);
  }
  {
    auto* chunk = DOMUint8Array::Create(1);
    *chunk->Data() = 'D';
    promise4 =
        writer->write(script_state, ScriptValue::From(script_state, chunk),
                      ASSERT_NO_EXCEPTION);
  }

  // The first two promises are resolved immediately.
  v8::MicrotasksScope::PerformCheckpoint(scope.GetIsolate());
  EXPECT_EQ(promise1.V8Promise()->State(), v8::Promise::kFulfilled);
  EXPECT_EQ(promise2.V8Promise()->State(), v8::Promise::kFulfilled);
  EXPECT_EQ(promise3.V8Promise()->State(), v8::Promise::kPending);
  EXPECT_EQ(promise4.V8Promise()->State(), v8::Promise::kPending);

  // The rest are resolved by the callback.
  test::RunPendingTasks();
  v8::MicrotasksScope::PerformCheckpoint(scope.GetIsolate());
  EXPECT_EQ(promise3.V8Promise()->State(), v8::Promise::kFulfilled);
  EXPECT_EQ(promise4.V8Promise()->State(), v8::Promise::kFulfilled);
}

TEST_F(WebTransportTest, SendDatagramBeforeConnect) {
  V8TestingScope scope;
  auto* web_transport = Create(scope, "https://example.com", EmptyOptions());

  auto* writable = web_transport->datagrams()->writable();
  auto* script_state = scope.GetScriptState();
  auto* writer = writable->getWriter(script_state, ASSERT_NO_EXCEPTION);
  auto* chunk = DOMUint8Array::Create(1);
  *chunk->Data() = 'A';
  ScriptPromise result =
      writer->write(script_state, ScriptValue::From(script_state, chunk),
                    ASSERT_NO_EXCEPTION);

  ConnectSuccessfully(web_transport);

  // No datagram is sent.

  ScriptPromiseTester tester(script_state, result);
  tester.WaitUntilSettled();
  EXPECT_TRUE(tester.IsFulfilled());
  EXPECT_TRUE(tester.Value().IsUndefined());
}

TEST_F(WebTransportTest, SendDatagramAfterClose) {
  V8TestingScope scope;
  auto* web_transport =
      CreateAndConnectSuccessfully(scope, "https://example.com");

  web_transport->close(nullptr);
  test::RunPendingTasks();

  auto* writable = web_transport->datagrams()->writable();
  auto* script_state = scope.GetScriptState();
  auto* writer = writable->getWriter(script_state, ASSERT_NO_EXCEPTION);

  auto* chunk = DOMUint8Array::Create(1);
  *chunk->Data() = 'A';
  ScriptPromise result =
      writer->write(script_state, ScriptValue::From(script_state, chunk),
                    ASSERT_NO_EXCEPTION);

  // No datagram is sent.

  ScriptPromiseTester tester(script_state, result);
  tester.WaitUntilSettled();
  EXPECT_TRUE(tester.IsRejected());
}

Vector<uint8_t> GetValueAsVector(ScriptState* script_state,
                                 ScriptValue iterator_result) {
  bool done = false;
  v8::Local<v8::Value> value;
  if (!V8UnpackIteratorResult(script_state,
                              iterator_result.V8Value().As<v8::Object>(), &done)
           .ToLocal(&value)) {
    ADD_FAILURE() << "unable to unpack iterator_result";
    return {};
  }

  EXPECT_FALSE(done);
  auto* array =
      V8Uint8Array::ToImplWithTypeCheck(script_state->GetIsolate(), value);
  if (!array) {
    ADD_FAILURE() << "value was not a Uint8Array";
    return {};
  }

  Vector<uint8_t> result;
  result.Append(array->Data(), array->length());
  return result;
}

TEST_F(WebTransportTest, ReceiveDatagramBeforeRead) {
  V8TestingScope scope;
  auto* web_transport =
      CreateAndConnectSuccessfully(scope, "https://example.com");

  const std::array<uint8_t, 1> chunk = {'A'};
  client_remote_->OnDatagramReceived(chunk);

  test::RunPendingTasks();

  auto* readable = web_transport->datagrams()->readable();
  auto* script_state = scope.GetScriptState();
  auto* reader =
      readable->GetDefaultReaderForTesting(script_state, ASSERT_NO_EXCEPTION);
  ScriptPromise result = reader->read(script_state, ASSERT_NO_EXCEPTION);
  ScriptPromiseTester tester(script_state, result);
  tester.WaitUntilSettled();
  EXPECT_TRUE(tester.IsFulfilled());

  EXPECT_THAT(GetValueAsVector(script_state, tester.Value()), ElementsAre('A'));
}

TEST_F(WebTransportTest, ReceiveDatagramDuringRead) {
  V8TestingScope scope;
  auto* web_transport =
      CreateAndConnectSuccessfully(scope, "https://example.com");
  auto* readable = web_transport->datagrams()->readable();
  auto* script_state = scope.GetScriptState();
  auto* reader =
      readable->GetDefaultReaderForTesting(script_state, ASSERT_NO_EXCEPTION);
  ScriptPromise result = reader->read(script_state, ASSERT_NO_EXCEPTION);

  const std::array<uint8_t, 1> chunk = {'A'};
  client_remote_->OnDatagramReceived(chunk);

  ScriptPromiseTester tester(script_state, result);
  tester.WaitUntilSettled();
  EXPECT_TRUE(tester.IsFulfilled());

  EXPECT_THAT(GetValueAsVector(script_state, tester.Value()), ElementsAre('A'));
}

// This test documents the current behaviour. If you improve the behaviour,
// change the test!
TEST_F(WebTransportTest, DatagramsAreDropped) {
  V8TestingScope scope;
  auto* web_transport =
      CreateAndConnectSuccessfully(scope, "https://example.com");

  // Chunk 'A' gets placed in the readable queue.
  const std::array<uint8_t, 1> chunk1 = {'A'};
  client_remote_->OnDatagramReceived(chunk1);

  // Chunk 'B' gets dropped, because there is no space in the readable queue.
  const std::array<uint8_t, 1> chunk2 = {'B'};
  client_remote_->OnDatagramReceived(chunk2);

  // Make sure that the calls have run.
  test::RunPendingTasks();

  auto* readable = web_transport->datagrams()->readable();
  auto* script_state = scope.GetScriptState();
  auto* reader =
      readable->GetDefaultReaderForTesting(script_state, ASSERT_NO_EXCEPTION);
  ScriptPromise result1 = reader->read(script_state, ASSERT_NO_EXCEPTION);
  ScriptPromise result2 = reader->read(script_state, ASSERT_NO_EXCEPTION);

  ScriptPromiseTester tester1(script_state, result1);
  ScriptPromiseTester tester2(script_state, result2);
  tester1.WaitUntilSettled();
  EXPECT_TRUE(tester1.IsFulfilled());
  EXPECT_FALSE(tester2.IsFulfilled());

  EXPECT_THAT(GetValueAsVector(script_state, tester1.Value()),
              ElementsAre('A'));

  // Chunk 'C' fulfills the pending read.
  const std::array<uint8_t, 1> chunk3 = {'C'};
  client_remote_->OnDatagramReceived(chunk3);

  tester2.WaitUntilSettled();
  EXPECT_TRUE(tester2.IsFulfilled());

  EXPECT_THAT(GetValueAsVector(script_state, tester2.Value()),
              ElementsAre('C'));
}

bool ValidProducerHandle(const mojo::ScopedDataPipeProducerHandle& handle) {
  return handle.is_valid();
}

bool ValidConsumerHandle(const mojo::ScopedDataPipeConsumerHandle& handle) {
  return handle.is_valid();
}

TEST_F(WebTransportTest, CreateSendStream) {
  V8TestingScope scope;
  auto* web_transport =
      CreateAndConnectSuccessfully(scope, "https://example.com");

  EXPECT_CALL(*mock_web_transport_,
              CreateStream(Truly(ValidConsumerHandle),
                           Not(Truly(ValidProducerHandle)), _))
      .WillOnce([](Unused, Unused,
                   base::OnceCallback<void(bool, uint32_t)> callback) {
        std::move(callback).Run(true, 0);
      });

  auto* script_state = scope.GetScriptState();
  ScriptPromise send_stream_promise = web_transport->createUnidirectionalStream(
      script_state, ASSERT_NO_EXCEPTION);
  ScriptPromiseTester tester(script_state, send_stream_promise);

  tester.WaitUntilSettled();

  EXPECT_TRUE(tester.IsFulfilled());
  auto* send_stream = V8SendStream::ToImplWithTypeCheck(
      scope.GetIsolate(), tester.Value().V8Value());
  EXPECT_TRUE(send_stream);
}

TEST_F(WebTransportTest, CreateSendStreamBeforeConnect) {
  V8TestingScope scope;

  auto* script_state = scope.GetScriptState();
  auto* web_transport = WebTransport::Create(
      script_state, "https://example.com", EmptyOptions(), ASSERT_NO_EXCEPTION);
  auto& exception_state = scope.GetExceptionState();
  ScriptPromise send_stream_promise =
      web_transport->createUnidirectionalStream(script_state, exception_state);
  EXPECT_TRUE(send_stream_promise.IsEmpty());
  EXPECT_TRUE(exception_state.HadException());
  EXPECT_EQ(static_cast<int>(DOMExceptionCode::kNetworkError),
            exception_state.Code());
}

TEST_F(WebTransportTest, CreateSendStreamFailure) {
  V8TestingScope scope;
  auto* web_transport =
      CreateAndConnectSuccessfully(scope, "https://example.com");

  EXPECT_CALL(*mock_web_transport_, CreateStream(_, _, _))
      .WillOnce([](Unused, Unused,
                   base::OnceCallback<void(bool, uint32_t)> callback) {
        std::move(callback).Run(false, 0);
      });

  auto* script_state = scope.GetScriptState();
  ScriptPromise send_stream_promise = web_transport->createUnidirectionalStream(
      script_state, ASSERT_NO_EXCEPTION);
  ScriptPromiseTester tester(script_state, send_stream_promise);

  tester.WaitUntilSettled();

  EXPECT_TRUE(tester.IsRejected());
  DOMException* exception = V8DOMException::ToImplWithTypeCheck(
      scope.GetIsolate(), tester.Value().V8Value());
  EXPECT_EQ(exception->name(), "NetworkError");
  EXPECT_EQ(exception->message(), "Failed to create send stream.");
}

// Every active stream is kept alive by the WebTransport object.
TEST_F(WebTransportTest, SendStreamGarbageCollection) {
  V8TestingScope scope;

  WeakPersistent<WebTransport> web_transport;
  WeakPersistent<SendStream> send_stream;

  {
    // The streams created when creating a WebTransport or SendStream create
    // some v8 handles. To ensure these are collected, we need to create a
    // handle scope. This is not a problem for garbage collection in normal
    // operation.
    v8::HandleScope handle_scope(scope.GetIsolate());

    web_transport = CreateAndConnectSuccessfully(scope, "https://example.com");
    send_stream = CreateSendStreamSuccessfully(scope, web_transport);
  }

  ThreadState::Current()->CollectAllGarbageForTesting();

  EXPECT_TRUE(web_transport);
  EXPECT_TRUE(send_stream);

  web_transport->close(nullptr);

  test::RunPendingTasks();

  ThreadState::Current()->CollectAllGarbageForTesting();

  EXPECT_FALSE(web_transport);
  EXPECT_FALSE(send_stream);
}

// A live stream will be kept alive even if there is no explicit reference.
// When the underlying connection is shut down, the connection will be swept.
TEST_F(WebTransportTest, SendStreamGarbageCollectionLocalClose) {
  V8TestingScope scope;

  WeakPersistent<SendStream> send_stream;

  {
    // The writable stream created when creating a SendStream creates some
    // v8 handles. To ensure these are collected, we need to create a handle
    // scope. This is not a problem for garbage collection in normal operation.
    v8::HandleScope handle_scope(scope.GetIsolate());

    auto* web_transport =
        CreateAndConnectSuccessfully(scope, "https://example.com");
    send_stream = CreateSendStreamSuccessfully(scope, web_transport);
  }

  // Pretend the stack is empty. This will avoid accidentally treating any
  // copies of the |send_stream| pointer as references.
  ThreadState::Current()->CollectAllGarbageForTesting();

  ASSERT_TRUE(send_stream);

  auto* script_state = scope.GetScriptState();

  ScriptPromise close_promise;

  {
    // The close() method also creates v8 handles referencing the
    // SendStream via the base class.
    v8::HandleScope handle_scope(scope.GetIsolate());

    close_promise =
        send_stream->writable()->close(script_state, ASSERT_NO_EXCEPTION);
  }

  ScriptPromiseTester tester(script_state, close_promise);
  tester.WaitUntilSettled();
  EXPECT_TRUE(tester.IsFulfilled());

  ThreadState::Current()->CollectAllGarbageForTesting();

  EXPECT_FALSE(send_stream);
}

TEST_F(WebTransportTest, SendStreamGarbageCollectionRemoteClose) {
  V8TestingScope scope;

  WeakPersistent<SendStream> send_stream;

  {
    v8::HandleScope handle_scope(scope.GetIsolate());

    auto* web_transport =
        CreateAndConnectSuccessfully(scope, "https://example.com");
    send_stream = CreateSendStreamSuccessfully(scope, web_transport);
  }

  ThreadState::Current()->CollectAllGarbageForTesting();

  ASSERT_TRUE(send_stream);

  // Close the other end of the pipe.
  send_stream_consumer_handle_.reset();

  test::RunPendingTasks();

  ThreadState::Current()->CollectAllGarbageForTesting();

  EXPECT_FALSE(send_stream);
}

// A live stream will be kept alive even if there is no explicit reference.
// When the underlying connection is shut down, the connection will be swept.
TEST_F(WebTransportTest, ReceiveStreamGarbageCollectionCancel) {
  V8TestingScope scope;

  WeakPersistent<ReceiveStream> receive_stream;
  mojo::ScopedDataPipeProducerHandle producer;

  {
    // The readable stream created when creating a ReceiveStream creates some
    // v8 handles. To ensure these are collected, we need to create a handle
    // scope. This is not a problem for garbage collection in normal operation.
    v8::HandleScope handle_scope(scope.GetIsolate());

    auto* web_transport =
        CreateAndConnectSuccessfully(scope, "https://example.com");

    producer = DoAcceptUnidirectionalStream();
    receive_stream = ReadReceiveStream(scope, web_transport);
  }

  // Pretend the stack is empty. This will avoid accidentally treating any
  // copies of the |receive_stream| pointer as references.
  ThreadState::Current()->CollectAllGarbageForTesting();

  ASSERT_TRUE(receive_stream);

  auto* script_state = scope.GetScriptState();

  ScriptPromise cancel_promise;
  {
    // Cancelling also creates v8 handles, so we need a new handle scope as
    // above.
    v8::HandleScope handle_scope(scope.GetIsolate());
    cancel_promise =
        receive_stream->readable()->cancel(script_state, ASSERT_NO_EXCEPTION);
  }

  ScriptPromiseTester tester(script_state, cancel_promise);
  tester.WaitUntilSettled();
  EXPECT_TRUE(tester.IsFulfilled());

  ThreadState::Current()->CollectAllGarbageForTesting();

  EXPECT_FALSE(receive_stream);
}

TEST_F(WebTransportTest, ReceiveStreamGarbageCollectionRemoteClose) {
  V8TestingScope scope;

  WeakPersistent<ReceiveStream> receive_stream;
  mojo::ScopedDataPipeProducerHandle producer;

  {
    v8::HandleScope handle_scope(scope.GetIsolate());

    auto* web_transport =
        CreateAndConnectSuccessfully(scope, "https://example.com");
    producer = DoAcceptUnidirectionalStream();
    receive_stream = ReadReceiveStream(scope, web_transport);
  }

  ThreadState::Current()->CollectAllGarbageForTesting();

  ASSERT_TRUE(receive_stream);

  // Close the other end of the pipe.
  producer.reset();

  test::RunPendingTasks();

  ThreadState::Current()->CollectAllGarbageForTesting();

  ASSERT_TRUE(receive_stream);

  receive_stream->OnIncomingStreamClosed(false);

  test::RunPendingTasks();

  ThreadState::Current()->CollectAllGarbageForTesting();

  EXPECT_FALSE(receive_stream);
}

// This is the same test as ReceiveStreamGarbageCollectionRemoteClose, except
// that the order of the data pipe being reset and the OnIncomingStreamClosed
// message is reversed. It is important that the object is not collected until
// both events have happened.
TEST_F(WebTransportTest, ReceiveStreamGarbageCollectionRemoteCloseReverse) {
  V8TestingScope scope;

  WeakPersistent<ReceiveStream> receive_stream;
  mojo::ScopedDataPipeProducerHandle producer;

  {
    v8::HandleScope handle_scope(scope.GetIsolate());

    auto* web_transport =
        CreateAndConnectSuccessfully(scope, "https://example.com");

    producer = DoAcceptUnidirectionalStream();
    receive_stream = ReadReceiveStream(scope, web_transport);
  }

  ThreadState::Current()->CollectAllGarbageForTesting();

  ASSERT_TRUE(receive_stream);

  receive_stream->OnIncomingStreamClosed(false);

  test::RunPendingTasks();

  ThreadState::Current()->CollectAllGarbageForTesting();

  ASSERT_TRUE(receive_stream);

  producer.reset();

  test::RunPendingTasks();

  ThreadState::Current()->CollectAllGarbageForTesting();

  EXPECT_FALSE(receive_stream);
}

TEST_F(WebTransportTest, CreateSendStreamAbortedByClose) {
  V8TestingScope scope;

  auto* script_state = scope.GetScriptState();
  auto* web_transport =
      CreateAndConnectSuccessfully(scope, "https://example.com");

  base::OnceCallback<void(bool, uint32_t)> create_stream_callback;
  EXPECT_CALL(*mock_web_transport_, CreateStream(_, _, _))
      .WillOnce([&](Unused, Unused,
                    base::OnceCallback<void(bool, uint32_t)> callback) {
        create_stream_callback = std::move(callback);
      });

  ScriptPromise send_stream_promise = web_transport->createUnidirectionalStream(
      script_state, ASSERT_NO_EXCEPTION);
  ScriptPromiseTester tester(script_state, send_stream_promise);

  test::RunPendingTasks();

  web_transport->close(nullptr);
  std::move(create_stream_callback).Run(true, 0);

  tester.WaitUntilSettled();

  EXPECT_TRUE(tester.IsRejected());
}

// ReceiveStream functionality is thoroughly tested in incoming_stream_test.cc.
// This test just verifies that the creation is done correctly.
TEST_F(WebTransportTest, CreateReceiveStream) {
  V8TestingScope scope;

  auto* script_state = scope.GetScriptState();
  auto* web_transport =
      CreateAndConnectSuccessfully(scope, "https://example.com");

  mojo::ScopedDataPipeProducerHandle producer = DoAcceptUnidirectionalStream();

  ReceiveStream* receive_stream = ReadReceiveStream(scope, web_transport);

  const char data[] = "what";
  uint32_t num_bytes = 4u;

  EXPECT_EQ(
      producer->WriteData(data, &num_bytes, MOJO_WRITE_DATA_FLAG_ALL_OR_NONE),
      MOJO_RESULT_OK);
  EXPECT_EQ(num_bytes, 4u);

  producer.reset();
  web_transport->OnIncomingStreamClosed(/*stream_id=*/0, true);

  auto* reader = receive_stream->readable()->GetDefaultReaderForTesting(
      script_state, ASSERT_NO_EXCEPTION);
  ScriptPromise read_promise = reader->read(script_state, ASSERT_NO_EXCEPTION);
  ScriptPromiseTester read_tester(script_state, read_promise);
  read_tester.WaitUntilSettled();
  EXPECT_TRUE(read_tester.IsFulfilled());
  auto read_result = read_tester.Value().V8Value();
  ASSERT_TRUE(read_result->IsObject());
  v8::Local<v8::Value> value;
  bool done = false;
  ASSERT_TRUE(
      V8UnpackIteratorResult(script_state, read_result.As<v8::Object>(), &done)
          .ToLocal(&value));
  DOMUint8Array* u8array =
      V8Uint8Array::ToImplWithTypeCheck(scope.GetIsolate(), value);
  ASSERT_TRUE(u8array);
  EXPECT_THAT(base::make_span(static_cast<uint8_t*>(u8array->Data()),
                              u8array->byteLength()),
              ElementsAre('w', 'h', 'a', 't'));
}

TEST_F(WebTransportTest, CreateReceiveStreamThenClose) {
  V8TestingScope scope;

  auto* script_state = scope.GetScriptState();
  auto* web_transport =
      CreateAndConnectSuccessfully(scope, "https://example.com");

  mojo::ScopedDataPipeProducerHandle producer = DoAcceptUnidirectionalStream();

  ReceiveStream* receive_stream = ReadReceiveStream(scope, web_transport);

  auto* reader = receive_stream->readable()->GetDefaultReaderForTesting(
      script_state, ASSERT_NO_EXCEPTION);
  ScriptPromise read_promise = reader->read(script_state, ASSERT_NO_EXCEPTION);
  ScriptPromiseTester read_tester(script_state, read_promise);

  web_transport->close(nullptr);

  read_tester.WaitUntilSettled();
  EXPECT_TRUE(read_tester.IsRejected());
  DOMException* exception = V8DOMException::ToImplWithTypeCheck(
      scope.GetIsolate(), read_tester.Value().V8Value());
  ASSERT_TRUE(exception);
  EXPECT_EQ(exception->code(),
            static_cast<uint16_t>(DOMExceptionCode::kNetworkError));

  // TODO(ricea): Fix this message if possible.
  EXPECT_EQ(exception->message(),
            "The stream was aborted by the remote server");
}

TEST_F(WebTransportTest, CreateReceiveStreamThenRemoteClose) {
  V8TestingScope scope;

  auto* script_state = scope.GetScriptState();
  auto* web_transport =
      CreateAndConnectSuccessfully(scope, "https://example.com");

  mojo::ScopedDataPipeProducerHandle producer = DoAcceptUnidirectionalStream();

  ReceiveStream* receive_stream = ReadReceiveStream(scope, web_transport);

  auto* reader = receive_stream->readable()->GetDefaultReaderForTesting(
      script_state, ASSERT_NO_EXCEPTION);
  ScriptPromise read_promise = reader->read(script_state, ASSERT_NO_EXCEPTION);
  ScriptPromiseTester read_tester(script_state, read_promise);

  client_remote_.reset();

  read_tester.WaitUntilSettled();
  EXPECT_TRUE(read_tester.IsRejected());
  DOMException* exception = V8DOMException::ToImplWithTypeCheck(
      scope.GetIsolate(), read_tester.Value().V8Value());
  ASSERT_TRUE(exception);
  EXPECT_EQ(exception->code(),
            static_cast<uint16_t>(DOMExceptionCode::kNetworkError));

  // TODO(ricea): Fix this message if possible.
  EXPECT_EQ(exception->message(),
            "The stream was aborted by the remote server");
}

// BidirectionalStreams are thoroughly tested in bidirectional_stream_test.cc.
// Here we just test the WebTransport APIs.
TEST_F(WebTransportTest, CreateBidirectionalStream) {
  V8TestingScope scope;

  auto* web_transport =
      CreateAndConnectSuccessfully(scope, "https://example.com");

  EXPECT_CALL(*mock_web_transport_, CreateStream(Truly(ValidConsumerHandle),
                                                 Truly(ValidProducerHandle), _))
      .WillOnce([](Unused, Unused,
                   base::OnceCallback<void(bool, uint32_t)> callback) {
        std::move(callback).Run(true, 0);
      });

  auto* script_state = scope.GetScriptState();
  ScriptPromise bidirectional_stream_promise =
      web_transport->createBidirectionalStream(script_state,
                                               ASSERT_NO_EXCEPTION);
  ScriptPromiseTester tester(script_state, bidirectional_stream_promise);

  tester.WaitUntilSettled();

  EXPECT_TRUE(tester.IsFulfilled());
  auto* bidirectional_stream = V8BidirectionalStream::ToImplWithTypeCheck(
      scope.GetIsolate(), tester.Value().V8Value());
  EXPECT_TRUE(bidirectional_stream);
}

TEST_F(WebTransportTest, ReceiveBidirectionalStream) {
  V8TestingScope scope;

  auto* web_transport =
      CreateAndConnectSuccessfully(scope, "https://example.com");

  mojo::ScopedDataPipeProducerHandle outgoing_producer;
  mojo::ScopedDataPipeConsumerHandle outgoing_consumer;
  ASSERT_TRUE(CreateDataPipeForWebTransportTests(&outgoing_producer,
                                                 &outgoing_consumer));

  mojo::ScopedDataPipeProducerHandle incoming_producer;
  mojo::ScopedDataPipeConsumerHandle incoming_consumer;
  ASSERT_TRUE(CreateDataPipeForWebTransportTests(&incoming_producer,
                                                 &incoming_consumer));

  std::move(pending_bidirectional_accept_callbacks_.front())
      .Run(next_stream_id_++, std::move(incoming_consumer),
           std::move(outgoing_producer));

  ReadableStream* streams = web_transport->incomingBidirectionalStreams();

  v8::Local<v8::Value> v8value = ReadValueFromStream(scope, streams);

  BidirectionalStream* bidirectional_stream =
      V8BidirectionalStream::ToImplWithTypeCheck(scope.GetIsolate(), v8value);
  EXPECT_TRUE(bidirectional_stream);
}

TEST_F(WebTransportTest, SetDatagramWritableQueueExpirationDuration) {
  V8TestingScope scope;

  auto* web_transport =
      CreateAndConnectSuccessfully(scope, "https://example.com");

  constexpr double kDuration = 40;
  constexpr base::TimeDelta kDurationDelta =
      base::TimeDelta::FromMillisecondsD(kDuration);
  EXPECT_CALL(*mock_web_transport_,
              SetOutgoingDatagramExpirationDuration(kDurationDelta));

  web_transport->setDatagramWritableQueueExpirationDuration(kDuration);

  test::RunPendingTasks();
}

}  // namespace

}  // namespace blink
