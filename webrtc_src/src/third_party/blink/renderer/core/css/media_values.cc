// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/media_values.h"

#include "third_party/blink/public/common/widget/screen_info.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/platform/web_theme_engine.h"
#include "third_party/blink/renderer/core/css/css_resolution_units.h"
#include "third_party/blink/renderer/core/css/media_feature_overrides.h"
#include "third_party/blink/renderer/core/css/media_values.h"
#include "third_party/blink/renderer/core/css/media_values_cached.h"
#include "third_party/blink/renderer/core/css/media_values_dynamic.h"
#include "third_party/blink/renderer/core/css/style_engine.h"
#include "third_party/blink/renderer/core/css_value_keywords.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/platform/graphics/color_space_gamut.h"
#include "third_party/blink/renderer/platform/network/network_state_notifier.h"
#include "third_party/blink/renderer/platform/widget/frame_widget.h"

namespace blink {

namespace {
static ForcedColors CSSValueIDToForcedColors(CSSValueID id) {
  switch (id) {
    case CSSValueID::kActive:
      return ForcedColors::kActive;
    case CSSValueID::kNone:
      return ForcedColors::kNone;
    default:
      NOTREACHED();
      return ForcedColors::kNone;
  }
}
}  // namespace

mojom::blink::PreferredColorScheme CSSValueIDToPreferredColorScheme(
    CSSValueID id) {
  switch (id) {
    case CSSValueID::kLight:
      return mojom::blink::PreferredColorScheme::kLight;
    case CSSValueID::kDark:
      return mojom::blink::PreferredColorScheme::kDark;
    default:
      NOTREACHED();
      return mojom::blink::PreferredColorScheme::kLight;
  }
}

MediaValues* MediaValues::CreateDynamicIfFrameExists(LocalFrame* frame) {
  if (frame)
    return MediaValuesDynamic::Create(frame);
  return MakeGarbageCollected<MediaValuesCached>();
}

double MediaValues::CalculateViewportWidth(LocalFrame* frame) {
  DCHECK(frame);
  DCHECK(frame->View());
  DCHECK(frame->GetDocument());
  return frame->View()->ViewportSizeForMediaQueries().Width();
}

double MediaValues::CalculateViewportHeight(LocalFrame* frame) {
  DCHECK(frame);
  DCHECK(frame->View());
  DCHECK(frame->GetDocument());
  return frame->View()->ViewportSizeForMediaQueries().Height();
}

int MediaValues::CalculateDeviceWidth(LocalFrame* frame) {
  DCHECK(frame && frame->View() && frame->GetSettings() && frame->GetPage());
  const ScreenInfo& screen_info =
      frame->GetPage()->GetChromeClient().GetScreenInfo(*frame);
  int device_width = screen_info.rect.width();
  if (frame->GetSettings()->GetReportScreenSizeInPhysicalPixelsQuirk()) {
    device_width = static_cast<int>(
        lroundf(device_width * screen_info.device_scale_factor));
  }
  return device_width;
}

int MediaValues::CalculateDeviceHeight(LocalFrame* frame) {
  DCHECK(frame && frame->View() && frame->GetSettings() && frame->GetPage());
  const ScreenInfo& screen_info =
      frame->GetPage()->GetChromeClient().GetScreenInfo(*frame);
  int device_height = screen_info.rect.height();
  if (frame->GetSettings()->GetReportScreenSizeInPhysicalPixelsQuirk()) {
    device_height = static_cast<int>(
        lroundf(device_height * screen_info.device_scale_factor));
  }
  return device_height;
}

bool MediaValues::CalculateStrictMode(LocalFrame* frame) {
  DCHECK(frame);
  DCHECK(frame->GetDocument());
  return !frame->GetDocument()->InQuirksMode();
}

float MediaValues::CalculateDevicePixelRatio(LocalFrame* frame) {
  return frame->DevicePixelRatio();
}

int MediaValues::CalculateColorBitsPerComponent(LocalFrame* frame) {
  DCHECK(frame);
  DCHECK(frame->GetPage());
  const ScreenInfo& screen_info =
      frame->GetPage()->GetChromeClient().GetScreenInfo(*frame);
  if (screen_info.is_monochrome)
    return 0;
  return screen_info.depth_per_component;
}

int MediaValues::CalculateMonochromeBitsPerComponent(LocalFrame* frame) {
  DCHECK(frame);
  DCHECK(frame->GetPage());
  const ScreenInfo& screen_info =
      frame->GetPage()->GetChromeClient().GetScreenInfo(*frame);
  if (!screen_info.is_monochrome)
    return 0;
  return screen_info.depth_per_component;
}

int MediaValues::CalculateDefaultFontSize(LocalFrame* frame) {
  return frame->GetPage()->GetSettings().GetDefaultFontSize();
}

const String MediaValues::CalculateMediaType(LocalFrame* frame) {
  DCHECK(frame);
  if (!frame->View())
    return g_empty_atom;
  return frame->View()->MediaType();
}

mojom::blink::DisplayMode MediaValues::CalculateDisplayMode(LocalFrame* frame) {
  DCHECK(frame);

  blink::mojom::DisplayMode mode =
      frame->GetPage()->GetSettings().GetDisplayModeOverride();
  if (mode != mojom::blink::DisplayMode::kUndefined)
    return mode;

  FrameWidget* widget = frame->GetWidgetForLocalRoot();
  if (!widget)  // Is null in non-ordinary Pages.
    return mojom::blink::DisplayMode::kBrowser;

  return widget->DisplayMode();
}

bool MediaValues::CalculateThreeDEnabled(LocalFrame* frame) {
  return frame->GetPage()->GetSettings().GetAcceleratedCompositingEnabled();
}

bool MediaValues::CalculateInImmersiveMode(LocalFrame* frame) {
  DCHECK(frame);
  DCHECK(frame->GetSettings());
  return frame->GetSettings()->GetImmersiveModeEnabled();
}

mojom::blink::PointerType MediaValues::CalculatePrimaryPointerType(
    LocalFrame* frame) {
  DCHECK(frame);
  DCHECK(frame->GetSettings());
  return frame->GetSettings()->GetPrimaryPointerType();
}

int MediaValues::CalculateAvailablePointerTypes(LocalFrame* frame) {
  DCHECK(frame);
  DCHECK(frame->GetSettings());
  return frame->GetSettings()->GetAvailablePointerTypes();
}

mojom::blink::HoverType MediaValues::CalculatePrimaryHoverType(
    LocalFrame* frame) {
  DCHECK(frame);
  DCHECK(frame->GetSettings());
  return frame->GetSettings()->GetPrimaryHoverType();
}

int MediaValues::CalculateAvailableHoverTypes(LocalFrame* frame) {
  DCHECK(frame);
  DCHECK(frame->GetSettings());
  return frame->GetSettings()->GetAvailableHoverTypes();
}

ColorSpaceGamut MediaValues::CalculateColorGamut(LocalFrame* frame) {
  DCHECK(frame);
  DCHECK(frame->GetPage());
  if (const auto* overrides = frame->GetPage()->GetMediaFeatureOverrides()) {
    MediaQueryExpValue value = overrides->GetOverride("color-gamut");
    if (value.IsValid()) {
      if (value.id == CSSValueID::kSRGB)
        return ColorSpaceGamut::SRGB;
      if (value.id == CSSValueID::kP3)
        return ColorSpaceGamut::P3;
      // Rec. 2020 is also known as ITU-R-Empfehlung BT.2020.
      if (value.id == CSSValueID::kRec2020)
        return ColorSpaceGamut::BT2020;
      NOTREACHED();
    }
  }
  return color_space_utilities::GetColorSpaceGamut(
      frame->GetPage()->GetChromeClient().GetScreenInfo(*frame));
}

mojom::blink::PreferredColorScheme MediaValues::CalculatePreferredColorScheme(
    LocalFrame* frame) {
  DCHECK(frame);
  DCHECK(frame->GetSettings());
  DCHECK(frame->GetDocument());
  DCHECK(frame->GetPage());
  if (const auto* overrides = frame->GetPage()->GetMediaFeatureOverrides()) {
    MediaQueryExpValue value = overrides->GetOverride("prefers-color-scheme");
    if (value.IsValid())
      return CSSValueIDToPreferredColorScheme(value.id);
  }
  return frame->GetDocument()->GetStyleEngine().GetPreferredColorScheme();
}

mojom::blink::PreferredContrast MediaValues::CalculatePreferredContrast(
    LocalFrame* frame) {
  DCHECK(frame);
  DCHECK(frame->GetSettings());
  return frame->GetSettings()->GetPreferredContrast();
}

bool MediaValues::CalculatePrefersReducedMotion(LocalFrame* frame) {
  DCHECK(frame);
  DCHECK(frame->GetSettings());
  if (const auto* overrides = frame->GetPage()->GetMediaFeatureOverrides()) {
    MediaQueryExpValue value = overrides->GetOverride("prefers-reduced-motion");
    if (value.IsValid())
      return value.id == CSSValueID::kReduce;
  }
  return frame->GetSettings()->GetPrefersReducedMotion();
}

bool MediaValues::CalculatePrefersReducedData(LocalFrame* frame) {
  DCHECK(frame);
  DCHECK(frame->GetSettings());
  if (const auto* overrides = frame->GetPage()->GetMediaFeatureOverrides()) {
    MediaQueryExpValue value = overrides->GetOverride("prefers-reduced-data");
    if (value.IsValid())
      return value.id == CSSValueID::kReduce;
  }
  return (GetNetworkStateNotifier().SaveDataEnabled() &&
          !frame->GetSettings()->GetDataSaverHoldbackWebApi());
}

ForcedColors MediaValues::CalculateForcedColors(LocalFrame* frame) {
  DCHECK(frame);
  DCHECK(frame->GetSettings());
  if (const auto* overrides = frame->GetPage()->GetMediaFeatureOverrides()) {
    MediaQueryExpValue value = overrides->GetOverride("forced-colors");
    if (value.IsValid())
      return CSSValueIDToForcedColors(value.id);
  }
  if (Platform::Current() && Platform::Current()->ThemeEngine())
    return Platform::Current()->ThemeEngine()->GetForcedColors();
  else
    return ForcedColors::kNone;
}

NavigationControls MediaValues::CalculateNavigationControls(LocalFrame* frame) {
  DCHECK(frame);
  DCHECK(frame->GetSettings());
  return frame->GetSettings()->GetNavigationControls();
}

ScreenSpanning MediaValues::CalculateScreenSpanning(LocalFrame* frame) {
  if (!frame->GetWidgetForLocalRoot())
    return ScreenSpanning::kNone;

  WebVector<gfx::Rect> window_segments =
      frame->GetWidgetForLocalRoot()->WindowSegments();

  if (window_segments.size() == 2) {
    // If there are two segments and the y value of the segments is the same,
    // we have side-by-side segments which are represented as a single vertical
    // fold.
    if (window_segments[0].y() == window_segments[1].y())
      return ScreenSpanning::kSingleFoldVertical;

    // If the x value of the segments is the same, we have stacked segments
    // which are represented as a single horizontal fold.
    if (window_segments[0].x() == window_segments[1].x())
      return ScreenSpanning::kSingleFoldHorizontal;
  }

  return ScreenSpanning::kNone;
}

DevicePosture MediaValues::CalculateDevicePosture(LocalFrame* frame) {
  // TODO(darktears): Retrieve information from the host.
  return DevicePosture::kNoFold;
}

bool MediaValues::ComputeLengthImpl(double value,
                                    CSSPrimitiveValue::UnitType type,
                                    unsigned default_font_size,
                                    double viewport_width,
                                    double viewport_height,
                                    double& result) {
  // The logic in this function is duplicated from
  // CSSToLengthConversionData::ZoomedComputedPixels() because
  // MediaValues::ComputeLength() needs nearly identical logic, but we haven't
  // found a way to make CSSToLengthConversionData::ZoomedComputedPixels() more
  // generic (to solve both cases) without hurting performance.
  // FIXME - Unite the logic here with CSSToLengthConversionData in a performant
  // way.
  switch (type) {
    case CSSPrimitiveValue::UnitType::kEms:
    case CSSPrimitiveValue::UnitType::kRems:
      result = value * default_font_size;
      return true;
    case CSSPrimitiveValue::UnitType::kPixels:
    case CSSPrimitiveValue::UnitType::kUserUnits:
      result = value;
      return true;
    case CSSPrimitiveValue::UnitType::kExs:
    // FIXME: We have a bug right now where the zoom will be applied twice to EX
    // units.
    case CSSPrimitiveValue::UnitType::kChs:
      // FIXME: We don't seem to be able to cache fontMetrics related values.
      // Trying to access them is triggering some sort of microtask. Serving the
      // spec's default instead.
      result = (value * default_font_size) / 2.0;
      return true;
    case CSSPrimitiveValue::UnitType::kViewportWidth:
      result = (value * viewport_width) / 100.0;
      return true;
    case CSSPrimitiveValue::UnitType::kViewportHeight:
      result = (value * viewport_height) / 100.0;
      return true;
    case CSSPrimitiveValue::UnitType::kViewportMin:
      result = (value * std::min(viewport_width, viewport_height)) / 100.0;
      return true;
    case CSSPrimitiveValue::UnitType::kViewportMax:
      result = (value * std::max(viewport_width, viewport_height)) / 100.0;
      return true;
    case CSSPrimitiveValue::UnitType::kCentimeters:
      result = value * kCssPixelsPerCentimeter;
      return true;
    case CSSPrimitiveValue::UnitType::kMillimeters:
      result = value * kCssPixelsPerMillimeter;
      return true;
    case CSSPrimitiveValue::UnitType::kQuarterMillimeters:
      result = value * kCssPixelsPerQuarterMillimeter;
      return true;
    case CSSPrimitiveValue::UnitType::kInches:
      result = value * kCssPixelsPerInch;
      return true;
    case CSSPrimitiveValue::UnitType::kPoints:
      result = value * kCssPixelsPerPoint;
      return true;
    case CSSPrimitiveValue::UnitType::kPicas:
      result = value * kCssPixelsPerPica;
      return true;
    default:
      return false;
  }
}

}  // namespace blink
