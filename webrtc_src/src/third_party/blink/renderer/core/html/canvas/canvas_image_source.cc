// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/html/canvas/canvas_image_source.h"

#include "gpu/command_buffer/client/shared_image_interface.h"
#include "third_party/blink/renderer/platform/graphics/canvas_resource_provider.h"
#include "third_party/blink/renderer/platform/graphics/skia/skia_utils.h"

namespace blink {
namespace {

std::unique_ptr<CanvasResourceProvider> CreateProvider(
    base::WeakPtr<WebGraphicsContext3DProviderWrapper> context_provider,
    const SkImageInfo& info,
    const scoped_refptr<StaticBitmapImage>& source_image,
    bool fallback_to_software) {
  IntSize size(info.width(), info.height());

  const SkFilterQuality filter_quality = kLow_SkFilterQuality;
  const CanvasResourceParams resource_params(info);

  if (context_provider) {
    uint32_t usage_flags =
        context_provider->ContextProvider()
            ->SharedImageInterface()
            ->UsageForMailbox(source_image->GetMailboxHolder().mailbox);
    auto resource_provider = CanvasResourceProvider::CreateSharedImageProvider(
        size, filter_quality, resource_params,
        CanvasResourceProvider::ShouldInitialize::kNo, context_provider,
        RasterMode::kGPU, source_image->IsOriginTopLeft(), usage_flags);
    if (resource_provider)
      return resource_provider;

    if (!fallback_to_software)
      return nullptr;
  }

  return CanvasResourceProvider::CreateBitmapProvider(
      size, filter_quality, resource_params,
      CanvasResourceProvider::ShouldInitialize::kNo);
}

}  // anonymous namespace

scoped_refptr<StaticBitmapImage> GetImageWithAlphaDisposition(
    scoped_refptr<StaticBitmapImage>&& image,
    const AlphaDisposition alpha_disposition) {
  if (!image)
    return nullptr;

  SkAlphaType alpha_type = (alpha_disposition == kPremultiplyAlpha)
                               ? kPremul_SkAlphaType
                               : kUnpremul_SkAlphaType;
  PaintImage paint_image = image->PaintImageForCurrentFrame();
  if (!paint_image)
    return nullptr;

  // Only if the content alphaType is not important or it will be recorded and
  // be handled in following step, kDontChangeAlpha could be provided to save
  // the conversion here.
  if (paint_image.GetAlphaType() == alpha_type ||
      alpha_disposition == kDontChangeAlpha)
    return std::move(image);

  SkImageInfo info =
      image->PaintImageForCurrentFrame().GetSkImageInfo().makeAlphaType(
          alpha_type);

  // To premul, draw the unpremul image on a surface to avoid GPU read back if
  // image is texture backed.
  if (alpha_type == kPremul_SkAlphaType) {
    auto resource_provider = CreateProvider(
        image->IsTextureBacked() ? image->ContextProviderWrapper() : nullptr,
        info, image, true /* fallback_to_software */);
    if (!resource_provider)
      return nullptr;

    cc::PaintFlags paint;
    paint.setBlendMode(SkBlendMode::kSrc);
    resource_provider->Canvas()->drawImage(image->PaintImageForCurrentFrame(),
                                           0, 0, SkSamplingOptions(), &paint);
    return resource_provider->Snapshot(image->CurrentFrameOrientation());
  }

  // To unpremul, read back the pixels.
  // TODO(crbug.com/1197369): we should try to keep the output resource(image)
  // in GPU when premultiply-alpha or unpremultiply-alpha transforms is
  // required.
  if (paint_image.GetSkImageInfo().isEmpty())
    return nullptr;

  sk_sp<SkData> dst_pixels = TryAllocateSkData(info.computeMinByteSize());
  if (!dst_pixels)
    return nullptr;

  uint8_t* writable_pixels = static_cast<uint8_t*>(dst_pixels->writable_data());
  size_t image_row_bytes = static_cast<size_t>(info.minRowBytes64());
  bool read_successful =
      paint_image.readPixels(info, writable_pixels, image_row_bytes, 0, 0);
  DCHECK(read_successful);
  return StaticBitmapImage::Create(std::move(dst_pixels), info,
                                   image->CurrentFrameOrientation());
}

}  // namespace blink
