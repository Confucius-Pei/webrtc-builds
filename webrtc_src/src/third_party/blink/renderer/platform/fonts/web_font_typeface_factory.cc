// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/fonts/web_font_typeface_factory.h"

#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "third_party/blink/renderer/platform/fonts/font_cache.h"
#include "third_party/blink/renderer/platform/fonts/opentype/font_format_check.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/skia/include/core/SkStream.h"
#include "third_party/skia/include/core/SkTypeface.h"

#if defined(OS_WIN)
#include "third_party/blink/public/common/dwrite_rasterizer_support/dwrite_rasterizer_support.h"
#include "third_party/blink/renderer/platform/fonts/win/dwrite_font_format_support.h"
#endif

#if defined(OS_WIN) || defined(OS_MAC)
#include "third_party/skia/include/ports/SkFontMgr_empty.h"
#endif

#if defined(OS_MAC)
#include "third_party/blink/renderer/platform/fonts/mac/core_text_font_format_support.h"
#endif

namespace blink {

bool WebFontTypefaceFactory::CreateTypeface(sk_sp<SkData> sk_data,
                                            sk_sp<SkTypeface>& typeface) {
  CHECK(!typeface);

  FontFormatCheck format_check(sk_data);

  std::unique_ptr<SkStreamAsset> stream(new SkMemoryStream(sk_data));

  if (!format_check.IsVariableFont() && !format_check.IsColorFont()) {
    typeface = DefaultFontManager()->makeFromStream(std::move(stream));
    if (typeface) {
      ReportInstantiationResult(
          InstantiationResult::kSuccessConventionalWebFont);
      return true;
    }
    // Not UMA reporting general decoding errors as these are already recorded
    // as kPackageFormatUnknown in FontResource.cpp.
    return false;
  }

  // We don't expect variable CBDT/CBLC or Sbix variable fonts for now.
  if (format_check.IsCbdtCblcColorFont()) {
    typeface = FreeTypeFontManager()->makeFromStream(std::move(stream));
    if (typeface) {
      ReportInstantiationResult(InstantiationResult::kSuccessCbdtCblcColorFont);
    }
    return typeface.get();
  }

  if (format_check.IsColrCpalColorFontV1()) {
    if (RuntimeEnabledFeatures::COLRV1FontsEnabled()) {
      typeface = FreeTypeFontManager()->makeFromStream(std::move(stream));
      if (typeface) {
        ReportInstantiationResult(InstantiationResult::kSuccessColrV1Font);
      }
      return typeface.get();
    } else {
      // Always reject COLRv1 fonts when the feature is off.
      return false;
    }
  }

  if (format_check.IsSbixColorFont()) {
    typeface = FontManagerForSbix()->makeFromStream(std::move(stream));
    if (typeface) {
      ReportInstantiationResult(InstantiationResult::kSuccessSbixFont);
    }
    return typeface.get();
  }

  // CFF2 must always go through the FreeTypeFontManager, even on Mac OS, as it
  // is not natively supported.
  if (format_check.IsCff2OutlineFont()) {
    typeface = FreeTypeFontManager()->makeFromStream(std::move(stream));
    if (typeface)
      ReportInstantiationResult(InstantiationResult::kSuccessCff2Font);
    return typeface.get();
  }

  // Variable COLR/CPAL fonts must go through the Variations
  // FontManager, which is FreeType on Windows.
  if (format_check.IsVariableFont()) {
    typeface = FontManagerForVariations()->makeFromStream(std::move(stream));
    if (typeface) {
      ReportInstantiationResult(InstantiationResult::kSuccessVariableWebFont);
    } else {
      ReportInstantiationResult(
          InstantiationResult::kErrorInstantiatingVariableFont);
    }
    return typeface.get();
  }

  if (format_check.IsColrCpalColorFontV0()) {
    typeface = FontManagerForColrCpal()->makeFromStream(std::move(stream));
    if (typeface) {
      ReportInstantiationResult(InstantiationResult::kSuccessColrCpalFont);
    }
    return typeface.get();
  }

  return false;
}

sk_sp<SkFontMgr> WebFontTypefaceFactory::FontManagerForVariations() {
#if defined(OS_WIN)
  if (DWriteVersionSupportsVariations())
    return DefaultFontManager();
  return FreeTypeFontManager();
#else
#if defined(OS_MAC)
  if (!CoreTextVersionSupportsVariations())
    return FreeTypeFontManager();
#endif
  return DefaultFontManager();
#endif
}

sk_sp<SkFontMgr> WebFontTypefaceFactory::FontManagerForSbix() {
#if defined(OS_MAC)
  return DefaultFontManager();
#endif
  return FreeTypeFontManager();
}

sk_sp<SkFontMgr> WebFontTypefaceFactory::DefaultFontManager() {
#if defined(OS_WIN)
  return FontCache::GetFontCache()->FontManager();
#else
  return sk_sp<SkFontMgr>(SkFontMgr::RefDefault());
#endif
}

sk_sp<SkFontMgr> WebFontTypefaceFactory::FreeTypeFontManager() {
#if defined(OS_WIN) || defined(OS_MAC)
  return sk_sp<SkFontMgr>(SkFontMgr_New_Custom_Empty());
#else
  return DefaultFontManager();
#endif
}

sk_sp<SkFontMgr> WebFontTypefaceFactory::FontManagerForColrCpal() {
#if defined(OS_WIN)
  if (!blink::DWriteRasterizerSupport::IsDWriteFactory2Available())
    return FreeTypeFontManager();
#endif
#if defined(OS_MAC)
  if (!CoreTextVersionSupportsColrCpal())
    return FreeTypeFontManager();
#endif
  return DefaultFontManager();
}

void WebFontTypefaceFactory::ReportInstantiationResult(
    InstantiationResult result) {
  UMA_HISTOGRAM_ENUMERATION("Blink.Fonts.VariableFontsRatio", result);
}

}  // namespace blink
