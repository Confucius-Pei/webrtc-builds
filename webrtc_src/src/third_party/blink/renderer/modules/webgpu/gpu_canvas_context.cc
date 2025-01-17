// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/webgpu/gpu_canvas_context.h"

#include "components/viz/common/resources/resource_format_utils.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_gpu_swap_chain_descriptor.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_union_canvasrenderingcontext2d_gpucanvascontext_imagebitmaprenderingcontext_webgl2renderingcontext_webglrenderingcontext.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_union_gpucanvascontext_imagebitmaprenderingcontext_offscreencanvasrenderingcontext2d_webgl2renderingcontext_webglrenderingcontext.h"
#include "third_party/blink/renderer/core/imagebitmap/image_bitmap.h"
#include "third_party/blink/renderer/modules/webgpu/dawn_conversions.h"
#include "third_party/blink/renderer/modules/webgpu/gpu_adapter.h"
#include "third_party/blink/renderer/modules/webgpu/gpu_device.h"

namespace blink {

GPUCanvasContext::Factory::Factory() {}
GPUCanvasContext::Factory::~Factory() {}

CanvasRenderingContext* GPUCanvasContext::Factory::Create(
    CanvasRenderingContextHost* host,
    const CanvasContextCreationAttributesCore& attrs) {
  CanvasRenderingContext* rendering_context =
      MakeGarbageCollected<GPUCanvasContext>(host, attrs);
  DCHECK(host);
  rendering_context->RecordUKMCanvasRenderingAPI(
      CanvasRenderingContext::CanvasRenderingAPI::kWebgpu);
  return rendering_context;
}

CanvasRenderingContext::ContextType GPUCanvasContext::Factory::GetContextType()
    const {
  return CanvasRenderingContext::kContextGPUPresent;
}

GPUCanvasContext::GPUCanvasContext(
    CanvasRenderingContextHost* host,
    const CanvasContextCreationAttributesCore& attrs)
    : CanvasRenderingContext(host, attrs) {}

GPUCanvasContext::~GPUCanvasContext() {}

void GPUCanvasContext::Trace(Visitor* visitor) const {
  visitor->Trace(swapchain_);
  CanvasRenderingContext::Trace(visitor);
}

const IntSize& GPUCanvasContext::CanvasSize() const {
  return Host()->Size();
}

// CanvasRenderingContext implementation
CanvasRenderingContext::ContextType GPUCanvasContext::GetContextType() const {
  return CanvasRenderingContext::kContextGPUPresent;
}

V8RenderingContext* GPUCanvasContext::AsV8RenderingContext() {
  return MakeGarbageCollected<V8RenderingContext>(this);
}

V8OffscreenRenderingContext* GPUCanvasContext::AsV8OffscreenRenderingContext() {
  return MakeGarbageCollected<V8OffscreenRenderingContext>(this);
}

void GPUCanvasContext::Stop() {
  if (swapchain_) {
    swapchain_->Neuter();
    swapchain_ = nullptr;
  }
  stopped_ = true;
}

cc::Layer* GPUCanvasContext::CcLayer() const {
  if (swapchain_) {
    return swapchain_->CcLayer();
  }
  return nullptr;
}

scoped_refptr<StaticBitmapImage> GPUCanvasContext::GetImage() {
  if (!swapchain_)
    return nullptr;

  CanvasResourceParams resource_params;
  resource_params.SetSkColorType(viz::ResourceFormatToClosestSkColorType(
      /*gpu_compositing=*/true, swapchain_->Format()));

  auto resource_provider = CanvasResourceProvider::CreateWebGPUImageProvider(
      IntSize(swapchain_->Size()), resource_params,
      /*is_origin_top_left=*/true);
  if (!resource_provider)
    return nullptr;

  if (!swapchain_->CopyToResourceProvider(resource_provider.get()))
    return nullptr;

  return resource_provider->Snapshot();
}

bool GPUCanvasContext::PaintRenderingResultsToCanvas(
    SourceDrawingBuffer source_buffer) {
  DCHECK_EQ(source_buffer, kBackBuffer);
  if (!swapchain_)
    return false;

  if (Host()->ResourceProvider() &&
      Host()->ResourceProvider()->Size() != IntSize(swapchain_->Size())) {
    Host()->DiscardResourceProvider();
  }

  CanvasResourceProvider* resource_provider =
      Host()->GetOrCreateCanvasResourceProvider(RasterModeHint::kPreferGPU);

  return CopyRenderingResultsFromDrawingBuffer(resource_provider,
                                               source_buffer);
}

bool GPUCanvasContext::CopyRenderingResultsFromDrawingBuffer(
    CanvasResourceProvider* resource_provider,
    SourceDrawingBuffer source_buffer) {
  DCHECK_EQ(source_buffer, kBackBuffer);
  if (swapchain_)
    return swapchain_->CopyToResourceProvider(resource_provider);
  return false;
}

void GPUCanvasContext::SetFilterQuality(SkFilterQuality filter_quality) {
  if (filter_quality != filter_quality_) {
    filter_quality_ = filter_quality;
    if (swapchain_) {
      swapchain_->SetFilterQuality(filter_quality);
    }
  }
}

bool GPUCanvasContext::PushFrame() {
  DCHECK(Host());
  DCHECK(Host()->IsOffscreenCanvas());
  auto canvas_resource = swapchain_->ExportCanvasResource();
  if (!canvas_resource)
    return false;
  const int width = canvas_resource->Size().Width();
  const int height = canvas_resource->Size().Height();
  return Host()->PushFrame(std::move(canvas_resource),
                           SkIRect::MakeWH(width, height));
}

ImageBitmap* GPUCanvasContext::TransferToImageBitmap(
    ScriptState* script_state) {
  return MakeGarbageCollected<ImageBitmap>(
      swapchain_->TransferToStaticBitmapImage());
}

// gpu_presentation_context.idl
void GPUCanvasContext::configure(const GPUSwapChainDescriptor* descriptor,
                                 ExceptionState& exception_state) {
  ConfigureInternal(descriptor, exception_state);
}

void GPUCanvasContext::unconfigure() {
  if (stopped_) {
    return;
  }

  if (swapchain_) {
    // Tell any previous swapchain that it will no longer be used and can
    // destroy all its resources (and produce errors when used).
    swapchain_->Neuter();
    swapchain_ = nullptr;
  }
}

String GPUCanvasContext::getPreferredFormat(const GPUAdapter* adapter) {
  // TODO(crbug.com/1007166): Return actual preferred format for the swap chain.
  return "bgra8unorm";
}

GPUTexture* GPUCanvasContext::getCurrentTexture(
    ExceptionState& exception_state) {
  if (!swapchain_) {
    exception_state.ThrowDOMException(DOMExceptionCode::kOperationError,
                                      "context is not configured");
    return nullptr;
  }
  return swapchain_->getCurrentTexture();
}

// gpu_canvas_context.idl (Deprecated)
GPUSwapChain* GPUCanvasContext::configureSwapChain(
    const GPUSwapChainDescriptor* descriptor,
    ExceptionState& exception_state) {
  descriptor->device()->AddConsoleWarning(
      "configureSwapChain() is deprecated. Use configure() instead and call "
      "getCurrentTexture() directly on the context. Note that configure() must "
      "also be called if you want to change the size of the textures returned "
      "by getCurrentTexture()");
  ConfigureInternal(descriptor, exception_state, true);
  return swapchain_;
}

String GPUCanvasContext::getSwapChainPreferredFormat(
    ExecutionContext* execution_context,
    GPUAdapter* adapter) {
  adapter->AddConsoleWarning(
      execution_context,
      "getSwapChainPreferredFormat() is deprecated. Use getPreferredFormat() "
      "instead.");
  return getPreferredFormat(adapter);
}

void GPUCanvasContext::ConfigureInternal(
    const GPUSwapChainDescriptor* descriptor,
    ExceptionState& exception_state,
    bool deprecated_resize_behavior) {
  DCHECK(descriptor);

  if (stopped_) {
    // This is probably not possible, or at least would only happen during page
    // shutdown.
    exception_state.ThrowDOMException(DOMExceptionCode::kUnknownError,
                                      "canvas has been destroyed");
    return;
  }

  if (swapchain_) {
    // Tell any previous swapchain that it will no longer be used and can
    // destroy all its resources (and produce errors when used).
    swapchain_->Neuter();
    swapchain_ = nullptr;
  }

  WGPUTextureUsage usage = AsDawnEnum<WGPUTextureUsage>(descriptor->usage());
  WGPUTextureFormat format =
      AsDawnEnum<WGPUTextureFormat>(descriptor->format());
  switch (format) {
    case WGPUTextureFormat_BGRA8Unorm:
      break;
    case WGPUTextureFormat_RGBA16Float:
      exception_state.ThrowDOMException(
          DOMExceptionCode::kUnknownError,
          "rgba16float swap chain is not yet supported");
      return;
    default:
      exception_state.ThrowDOMException(DOMExceptionCode::kOperationError,
                                        "unsupported swap chain format");
      return;
  }

  // Set the default size.
  IntSize size;
  if (deprecated_resize_behavior) {
    // A negative size will indicate to the swap chain that it should follow the
    // deprecated behavior of resizing to match the canvas size each frame.
    size = IntSize(-1, -1);
  } else if (descriptor->hasSize()) {
    WGPUExtent3D dawn_extent = AsDawnType(descriptor->size());
    size = IntSize(dawn_extent.width, dawn_extent.height);

    if (dawn_extent.depthOrArrayLayers != 1) {
      exception_state.ThrowDOMException(
          DOMExceptionCode::kOperationError,
          "swap chain size must have depthOrArrayLayers set to 1");
      return;
    }
  } else {
    size = CanvasSize();
  }

  swapchain_ = MakeGarbageCollected<GPUSwapChain>(
      this, descriptor->device(), usage, format, filter_quality_, size);
  swapchain_->CcLayer()->SetContentsOpaque(!CreationAttributes().alpha);
  if (descriptor->hasLabel())
    swapchain_->setLabel(descriptor->label());

  // If we don't notify the host that something has changed it may never check
  // for the new cc::Layer.
  Host()->SetNeedsCompositingUpdate();
}

}  // namespace blink
