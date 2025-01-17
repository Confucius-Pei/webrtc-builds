// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/style/style_crossfade_image.h"

#include "third_party/blink/renderer/core/css/css_crossfade_value.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/platform/graphics/crossfade_generated_image.h"

namespace blink {

StyleCrossfadeImage::StyleCrossfadeImage(cssvalue::CSSCrossfadeValue& value,
                                         StyleImage* from_image,
                                         StyleImage* to_image)
    : original_value_(value), from_image_(from_image), to_image_(to_image) {
  is_crossfade_ = true;
}

StyleCrossfadeImage::~StyleCrossfadeImage() {
  DCHECK(clients_.IsEmpty());
}

bool StyleCrossfadeImage::IsEqual(const StyleImage& other) const {
  // Since this object is used as a listener, and contains a listener set, we
  // need to consider each instance unique.
  return this == &other;
}

CSSValue* StyleCrossfadeImage::CssValue() const {
  return original_value_;
}

CSSValue* StyleCrossfadeImage::ComputedCSSValue(
    const ComputedStyle& style,
    bool allow_visited_style) const {
  // If either of the images are null (meaning that they are 'none'), then use
  // the original value.
  CSSValue* from_value =
      from_image_ ? from_image_->ComputedCSSValue(style, allow_visited_style)
                  : &original_value_->From();
  CSSValue* to_value =
      to_image_ ? to_image_->ComputedCSSValue(style, allow_visited_style)
                : &original_value_->To();
  return MakeGarbageCollected<cssvalue::CSSCrossfadeValue>(
      from_value, to_value, &original_value_->Percentage());
}

bool StyleCrossfadeImage::CanRender() const {
  return (!from_image_ || from_image_->CanRender()) &&
         (!to_image_ || to_image_->CanRender());
}

bool StyleCrossfadeImage::IsLoaded() const {
  return (!from_image_ || from_image_->IsLoaded()) &&
         (!to_image_ || to_image_->IsLoaded());
}

bool StyleCrossfadeImage::ErrorOccurred() const {
  return (from_image_ && from_image_->ErrorOccurred()) ||
         (to_image_ && to_image_->ErrorOccurred());
}

bool StyleCrossfadeImage::IsAccessAllowed(String& failing_url) const {
  return (!from_image_ || from_image_->IsAccessAllowed(failing_url)) &&
         (!to_image_ || to_image_->IsAccessAllowed(failing_url));
}

FloatSize StyleCrossfadeImage::ImageSize(const Document& document,
                                         float multiplier,
                                         const FloatSize& default_object_size,
                                         RespectImageOrientationEnum) const {
  if (!from_image_ || !to_image_)
    return FloatSize();

  // TODO(fs): Consider |respect_orientation|?
  FloatSize from_image_size = from_image_->ImageSize(
      document, multiplier, default_object_size, kRespectImageOrientation);
  FloatSize to_image_size = to_image_->ImageSize(
      document, multiplier, default_object_size, kRespectImageOrientation);

  // Rounding issues can cause transitions between images of equal size to
  // return a different fixed size; avoid performing the interpolation if the
  // images are the same size.
  if (from_image_size == to_image_size)
    return from_image_size;

  float percentage = original_value_->Percentage().GetFloatValue();
  float inverse_percentage = 1 - percentage;
  return FloatSize(from_image_size.Width() * inverse_percentage +
                       to_image_size.Width() * percentage,
                   from_image_size.Height() * inverse_percentage +
                       to_image_size.Height() * percentage);
}

bool StyleCrossfadeImage::HasIntrinsicSize() const {
  return (from_image_ && from_image_->HasIntrinsicSize()) ||
         (to_image_ && to_image_->HasIntrinsicSize());
}

void StyleCrossfadeImage::AddClient(ImageResourceObserver* observer) {
  const bool clients_was_empty = clients_.IsEmpty();
  clients_.insert(observer);
  if (!clients_was_empty)
    return;
  if (from_image_)
    from_image_->AddClient(this);
  if (to_image_)
    to_image_->AddClient(this);
}

void StyleCrossfadeImage::RemoveClient(ImageResourceObserver* observer) {
  clients_.erase(observer);
  if (!clients_.IsEmpty())
    return;
  if (from_image_)
    from_image_->RemoveClient(this);
  if (to_image_)
    to_image_->RemoveClient(this);
}

scoped_refptr<Image> StyleCrossfadeImage::GetImage(
    const ImageResourceObserver& observer,
    const Document& document,
    const ComputedStyle& style,
    const FloatSize& target_size) const {
  if (target_size.IsEmpty())
    return nullptr;
  if (!from_image_ || !to_image_)
    return Image::NullImage();
  const FloatSize resolved_size = ImageSize(
      document, style.EffectiveZoom(), target_size, kRespectImageOrientation);
  return CrossfadeGeneratedImage::Create(
      from_image_->GetImage(*this, document, style, target_size),
      to_image_->GetImage(*this, document, style, target_size),
      original_value_->Percentage().GetFloatValue(), resolved_size);
}

WrappedImagePtr StyleCrossfadeImage::Data() const {
  return original_value_.Get();
}

bool StyleCrossfadeImage::KnownToBeOpaque(const Document& document,
                                          const ComputedStyle& style) const {
  return from_image_ && from_image_->KnownToBeOpaque(document, style) &&
         to_image_ && to_image_->KnownToBeOpaque(document, style);
}

void StyleCrossfadeImage::ImageChanged(ImageResourceContent*,
                                       CanDeferInvalidation defer) {
  for (auto& entry : clients_)
    entry.key->ImageChanged(Data(), defer);
}

bool StyleCrossfadeImage::WillRenderImage() {
  for (auto& entry : clients_) {
    if (entry.key->WillRenderImage())
      return true;
  }
  return false;
}

bool StyleCrossfadeImage::GetImageAnimationPolicy(
    mojom::blink::ImageAnimationPolicy& animation_policy) {
  for (auto& entry : clients_) {
    if (entry.key->GetImageAnimationPolicy(animation_policy))
      return true;
  }
  return false;
}

String StyleCrossfadeImage::DebugName() const {
  return "StyleCrossfadeImage";
}

void StyleCrossfadeImage::Trace(Visitor* visitor) const {
  visitor->Trace(original_value_);
  visitor->Trace(from_image_);
  visitor->Trace(to_image_);
  StyleImage::Trace(visitor);
}

}  // namespace blink
