/*
    Copyright (C) 1998 Lars Knoll (knoll@mpi-hd.mpg.de)
    Copyright (C) 2001 Dirk Mueller (mueller@kde.org)
    Copyright (C) 2002 Waldo Bastian (bastian@kde.org)
    Copyright (C) 2006 Samuel Weinig (sam.weinig@gmail.com)
    Copyright (C) 2004, 2005, 2006, 2007, 2008 Apple Inc. All rights reserved.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.

    This class provides all functionality needed for loading images, style
    sheets and html pages from the web. It has a memory cache for these objects.
*/

#include "third_party/blink/renderer/core/loader/resource/script_resource.h"

#include <utility>

#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/mojom/fetch/fetch_api_request.mojom-blink.h"
#include "third_party/blink/public/mojom/loader/code_cache.mojom-blink.h"
#include "third_party/blink/public/mojom/loader/request_context_frame_type.mojom-blink.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/loader/subresource_integrity_helper.h"
#include "third_party/blink/renderer/platform/instrumentation/tracing/web_memory_allocator_dump.h"
#include "third_party/blink/renderer/platform/instrumentation/tracing/web_process_memory_dump.h"
#include "third_party/blink/renderer/platform/loader/fetch/cached_metadata.h"
#include "third_party/blink/renderer/platform/loader/fetch/data_pipe_bytes_consumer.h"
#include "third_party/blink/renderer/platform/loader/fetch/fetch_parameters.h"
#include "third_party/blink/renderer/platform/loader/fetch/integrity_metadata.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_client_walker.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_fetcher.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_loader.h"
#include "third_party/blink/renderer/platform/loader/fetch/response_body_loader.h"
#include "third_party/blink/renderer/platform/loader/fetch/script_cached_metadata_handler.h"
#include "third_party/blink/renderer/platform/loader/fetch/text_resource_decoder_options.h"
#include "third_party/blink/renderer/platform/loader/subresource_integrity.h"
#include "third_party/blink/renderer/platform/network/mime/mime_type_registry.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#include "third_party/blink/renderer/platform/wtf/shared_buffer.h"

namespace blink {

namespace {

// Returns true if the given request context is a script-like destination
// defined in the Fetch spec:
// https://fetch.spec.whatwg.org/#request-destination-script-like
bool IsRequestContextSupported(
    mojom::blink::RequestContextType request_context) {
  // TODO(nhiroki): Support "audioworklet" and "paintworklet" destinations.
  switch (request_context) {
    case mojom::blink::RequestContextType::SCRIPT:
    case mojom::blink::RequestContextType::WORKER:
    case mojom::blink::RequestContextType::SERVICE_WORKER:
    case mojom::blink::RequestContextType::SHARED_WORKER:
      return true;
    default:
      break;
  }
  NOTREACHED() << "Incompatible request context type: " << request_context;
  return false;
}

}  // namespace

ScriptResource* ScriptResource::Fetch(FetchParameters& params,
                                      ResourceFetcher* fetcher,
                                      ResourceClient* client,
                                      StreamingAllowed streaming_allowed) {
  DCHECK(IsRequestContextSupported(
      params.GetResourceRequest().GetRequestContext()));
  auto* resource = To<ScriptResource>(fetcher->RequestResource(
      params, ScriptResourceFactory(streaming_allowed, params.GetScriptType()),
      client));
  return resource;
}

ScriptResource* ScriptResource::CreateForTest(
    const KURL& url,
    const WTF::TextEncoding& encoding,
    mojom::blink::ScriptType script_type) {
  ResourceRequest request(url);
  request.SetCredentialsMode(network::mojom::CredentialsMode::kOmit);
  ResourceLoaderOptions options(nullptr /* world */);
  TextResourceDecoderOptions decoder_options(
      TextResourceDecoderOptions::kPlainTextContent, encoding);
  return MakeGarbageCollected<ScriptResource>(request, options, decoder_options,
                                              kNoStreaming, script_type);
}

ScriptResource::ScriptResource(
    const ResourceRequest& resource_request,
    const ResourceLoaderOptions& options,
    const TextResourceDecoderOptions& decoder_options,
    StreamingAllowed streaming_allowed,
    mojom::blink::ScriptType initial_request_script_type)
    : TextResource(resource_request,
                   ResourceType::kScript,
                   options,
                   decoder_options),
      initial_request_script_type_(initial_request_script_type) {
  static bool script_streaming_enabled =
      base::FeatureList::IsEnabled(features::kScriptStreaming);

  if (!script_streaming_enabled) {
    DisableStreaming(
        ScriptStreamer::NotStreamingReason::kDisabledByFeatureList);
  } else if (streaming_allowed == kNoStreaming) {
    DisableStreaming(ScriptStreamer::NotStreamingReason::kStreamingDisabled);
  } else if (!Url().ProtocolIsInHTTPFamily()) {
    DisableStreaming(ScriptStreamer::NotStreamingReason::kNotHTTP);
  }
}

ScriptResource::~ScriptResource() = default;

void ScriptResource::Trace(Visitor* visitor) const {
  visitor->Trace(streamer_);
  visitor->Trace(cached_metadata_handler_);
  TextResource::Trace(visitor);
}

void ScriptResource::OnMemoryDump(WebMemoryDumpLevelOfDetail level_of_detail,
                                  WebProcessMemoryDump* memory_dump) const {
  Resource::OnMemoryDump(level_of_detail, memory_dump);
  {
    const String name = GetMemoryDumpName() + "/decoded_script";
    source_text_.OnMemoryDump(memory_dump, name);
  }
  if (cached_metadata_handler_) {
    const String name = GetMemoryDumpName() + "/code_cache";
    cached_metadata_handler_->OnMemoryDump(memory_dump, name);
  }
}

const ParkableString& ScriptResource::SourceText() {
  CHECK(IsLoaded());

  if (source_text_.IsNull() && Data()) {
    String source_text = DecodedText();
    ClearData();
    SetDecodedSize(source_text.CharactersSizeInBytes());
    source_text_ = ParkableString(source_text.ReleaseImpl());
  }

  return source_text_;
}

String ScriptResource::TextForInspector() const {
  // If the resource buffer exists, we can safely return the decoded text.
  if (ResourceBuffer())
    return DecodedText();

  // If there is no resource buffer, then we have three cases.
  // TODO(crbug.com/865098): Simplify the below code and remove the CHECKs once
  // the assumptions are confirmed.

  if (IsLoaded()) {
    if (!source_text_.IsNull()) {
      // 1. We have finished loading, and have already decoded the buffer into
      //    the source text and cleared the resource buffer to save space.
      return source_text_.ToString();
    }

    // 2. We have finished loading with no data received, so no streaming ever
    //    happened or streaming was suppressed.
    DCHECK(!streamer_ ||
           streamer_->StreamingSuppressedReason() ==
               ScriptStreamer::NotStreamingReason::kScriptTooSmall);
    return "";
  }

  // 3. We haven't started loading, and actually haven't received any data yet
  //    at all to initialise the resource buffer, so the resource is empty.
  return "";
}

SingleCachedMetadataHandler* ScriptResource::CacheHandler() {
  return cached_metadata_handler_;
}

void ScriptResource::SetSerializedCachedMetadata(mojo_base::BigBuffer data) {
  // Resource ignores the cached metadata.
  Resource::SetSerializedCachedMetadata(mojo_base::BigBuffer());
  if (cached_metadata_handler_) {
    cached_metadata_handler_->SetSerializedCachedMetadata(std::move(data));
  }
}

void ScriptResource::DestroyDecodedDataIfPossible() {
  if (cached_metadata_handler_) {
    cached_metadata_handler_->ClearCachedMetadata(
        CachedMetadataHandler::kClearLocally);
  }
}

void ScriptResource::DestroyDecodedDataForFailedRevalidation() {
  source_text_ = ParkableString();
  // Make sure there's no streaming.
  DCHECK(!streamer_);
  DCHECK_EQ(streaming_state_, StreamingState::kStreamingDisabled);
  SetDecodedSize(0);
  cached_metadata_handler_ = nullptr;
}

void ScriptResource::SetRevalidatingRequest(
    const ResourceRequestHead& request) {
  CHECK(IsLoaded());
  if (streamer_) {
    CHECK(streamer_->IsFinished());
    streamer_ = nullptr;
  }
  // Revalidation requests don't actually load the current Resource, so disable
  // streaming.
  DisableStreaming(ScriptStreamer::NotStreamingReason::kRevalidate);

  TextResource::SetRevalidatingRequest(request);
}

bool ScriptResource::CanUseCacheValidator() const {
  // Do not revalidate until ClassicPendingScript is removed, i.e. the script
  // content is retrieved in ScriptLoader::ExecuteScriptBlock().
  // crbug.com/692856
  if (HasClientsOrObservers())
    return false;

  // Do not revalidate until streaming is complete.
  if (!IsLoaded())
    return false;

  return Resource::CanUseCacheValidator();
}

size_t ScriptResource::CodeCacheSize() const {
  return cached_metadata_handler_ ? cached_metadata_handler_->GetCodeCacheSize()
                                  : 0;
}

void ScriptResource::ResponseReceived(const ResourceResponse& response) {
  const bool is_successful_revalidation =
      IsSuccessfulRevalidationResponse(response);
  Resource::ResponseReceived(response);

  if (is_successful_revalidation) {
    return;
  }

  cached_metadata_handler_ = nullptr;
  // Currently we support the metadata caching only for HTTP family.
  if (GetResourceRequest().Url().ProtocolIsInHTTPFamily() &&
      response.CurrentRequestUrl().ProtocolIsInHTTPFamily()) {
    cached_metadata_handler_ =
        MakeGarbageCollected<ScriptCachedMetadataHandler>(
            Encoding(), CachedMetadataSender::Create(
                            response, mojom::blink::CodeCacheType::kJavascript,
                            GetResourceRequest().RequestorOrigin()));
  }
}  // namespace blink

void ScriptResource::ResponseBodyReceived(
    ResponseBodyLoaderDrainableInterface& body_loader,
    scoped_refptr<base::SingleThreadTaskRunner> loader_task_runner) {
  if (streaming_state_ == StreamingState::kStreamingDisabled)
    return;

  CHECK_EQ(streaming_state_, StreamingState::kWaitingForDataPipe);

  // Checked in the constructor.
  CHECK(Url().ProtocolIsInHTTPFamily());
  CHECK(base::FeatureList::IsEnabled(features::kScriptStreaming));

  ResponseBodyLoaderClient* response_body_loader_client;
  mojo::ScopedDataPipeConsumerHandle data_pipe =
      body_loader.DrainAsDataPipe(&response_body_loader_client);
  if (!data_pipe) {
    DisableStreaming(ScriptStreamer::NotStreamingReason::kNoDataPipe);
    return;
  }

  CheckStreamingState();
  CHECK(!ErrorOccurred());

  streamer_ = MakeGarbageCollected<ScriptStreamer>(this, std::move(data_pipe),
                                                   response_body_loader_client,
                                                   loader_task_runner);
  CHECK_EQ(no_streamer_reason_, ScriptStreamer::NotStreamingReason::kInvalid);
  AdvanceStreamingState(StreamingState::kStreaming);
}

void ScriptResource::NotifyFinished() {
  DCHECK(IsLoaded());
  switch (streaming_state_) {
    case StreamingState::kWaitingForDataPipe:
      // We never received a response body, otherwise the state would be
      // one of kStreaming or kNoStreaming. So, either there was an error, or
      // there was no response body loader (thus no data pipe) at all. Either
      // way, we want to disable streaming.
      if (ErrorOccurred()) {
        DisableStreaming(ScriptStreamer::NotStreamingReason::kErrorOccurred);
      } else {
        DisableStreaming(ScriptStreamer::NotStreamingReason::kNoDataPipe);
      }
      break;

    case StreamingState::kStreaming:
      DCHECK(streamer_);
      if (!streamer_->IsFinished()) {
        // This notification didn't come from the streaming finishing, so it
        // must be an external error (e.g. cancelling the resource).
        CHECK(ErrorOccurred());
        streamer_->Cancel();
        streamer_.Release();
        DisableStreaming(ScriptStreamer::NotStreamingReason::kErrorOccurred);
      }
      break;

    case StreamingState::kStreamingDisabled:
      // If streaming is already disabled, we can just continue as before.
      break;
  }
  CheckStreamingState();
  TextResource::NotifyFinished();
}

ScriptStreamer* ScriptResource::TakeStreamer() {
  CHECK(IsLoaded());
  if (!streamer_)
    return nullptr;

  ScriptStreamer* streamer = streamer_;
  // A second use of the streamer is not possible, so we null it out and disable
  // streaming for subsequent uses.
  streamer_ = nullptr;
  DisableStreaming(
      ScriptStreamer::NotStreamingReason::kSecondScriptResourceUse);
  return streamer;
}

void ScriptResource::DisableStreaming(
    ScriptStreamer::NotStreamingReason no_streamer_reason) {
  CHECK_NE(no_streamer_reason, ScriptStreamer::NotStreamingReason::kInvalid);
  if (no_streamer_reason_ != ScriptStreamer::NotStreamingReason::kInvalid) {
    // Streaming is already disabled, no need to disable it again.
    return;
  }
  no_streamer_reason_ = no_streamer_reason;
  AdvanceStreamingState(StreamingState::kStreamingDisabled);
}

void ScriptResource::AdvanceStreamingState(StreamingState new_state) {
  switch (streaming_state_) {
    case StreamingState::kWaitingForDataPipe:
      CHECK(new_state == StreamingState::kStreaming ||
            new_state == StreamingState::kStreamingDisabled);
      break;
    case StreamingState::kStreaming:
      CHECK_EQ(new_state, StreamingState::kStreamingDisabled);
      break;
    case StreamingState::kStreamingDisabled:
      CHECK(false);
      break;
  }

  streaming_state_ = new_state;
  CheckStreamingState();
}

void ScriptResource::CheckStreamingState() const {
  // TODO(leszeks): Eventually convert these CHECKs into DCHECKs once the logic
  // is a bit more baked in.
  switch (streaming_state_) {
    case StreamingState::kWaitingForDataPipe:
      CHECK(!streamer_);
      CHECK_EQ(no_streamer_reason_,
               ScriptStreamer::NotStreamingReason::kInvalid);
      break;
    case StreamingState::kStreaming:
      CHECK(streamer_);
      CHECK(streamer_->CanStartStreaming() || streamer_->IsStreamingStarted() ||
            streamer_->IsStreamingSuppressed());
      CHECK(IsLoading() || streamer_->IsFinished());
      break;
    case StreamingState::kStreamingDisabled:
      CHECK(!streamer_);
      CHECK_NE(no_streamer_reason_,
               ScriptStreamer::NotStreamingReason::kInvalid);
      break;
  }
}

}  // namespace blink
