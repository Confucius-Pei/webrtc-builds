// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_STYLE_STYLE_CROSSFADE_IMAGE_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_STYLE_STYLE_CROSSFADE_IMAGE_H_

#include "third_party/blink/renderer/core/loader/resource/image_resource_observer.h"
#include "third_party/blink/renderer/core/style/style_image.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"

namespace blink {

namespace cssvalue {
class CSSCrossfadeValue;
}

// This class represents a <cross-fade()> <image> function.
class StyleCrossfadeImage final : public StyleImage,
                                  public ImageResourceObserver {
 public:
  StyleCrossfadeImage(cssvalue::CSSCrossfadeValue&,
                      StyleImage* from_image,
                      StyleImage* to_image);
  ~StyleCrossfadeImage() override;

  CSSValue* CssValue() const override;
  CSSValue* ComputedCSSValue(const ComputedStyle&,
                             bool allow_visited_style) const override;

  bool CanRender() const override;
  bool IsLoaded() const override;
  bool ErrorOccurred() const override;
  bool IsAccessAllowed(String&) const override;

  FloatSize ImageSize(const Document&,
                      float multiplier,
                      const FloatSize& default_object_size,
                      RespectImageOrientationEnum) const override;

  bool HasIntrinsicSize() const override;

  void AddClient(ImageResourceObserver*) override;
  void RemoveClient(ImageResourceObserver*) override;

  scoped_refptr<Image> GetImage(const ImageResourceObserver&,
                                const Document&,
                                const ComputedStyle&,
                                const FloatSize& target_size) const override;

  WrappedImagePtr Data() const override;
  bool KnownToBeOpaque(const Document&, const ComputedStyle&) const override;

  void Trace(Visitor*) const override;

 private:
  bool IsEqual(const StyleImage&) const override;

  // ImageResourceObserver:
  void ImageChanged(ImageResourceContent*, CanDeferInvalidation) override;
  bool WillRenderImage() override;
  bool GetImageAnimationPolicy(mojom::blink::ImageAnimationPolicy&) override;
  String DebugName() const override;

  HashCountedSet<ImageResourceObserver*> clients_;
  Member<cssvalue::CSSCrossfadeValue> original_value_;
  Member<StyleImage> from_image_;
  Member<StyleImage> to_image_;
};

template <>
struct DowncastTraits<StyleCrossfadeImage> {
  static bool AllowFrom(const StyleImage& style_image) {
    return style_image.IsCrossfadeImage();
  }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_STYLE_STYLE_CROSSFADE_IMAGE_H_
