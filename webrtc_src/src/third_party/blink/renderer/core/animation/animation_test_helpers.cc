// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/animation/animation_test_helpers.h"

#include "third_party/blink/renderer/bindings/core/v8/v8_binding_for_core.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_union_csskeywordvalue_cssnumericvalue_scrolltimelineelementbasedoffset_string.h"
#include "third_party/blink/renderer/core/animation/css_interpolation_environment.h"
#include "third_party/blink/renderer/core/animation/css_interpolation_types_map.h"
#include "third_party/blink/renderer/core/animation/invalidatable_interpolation.h"
#include "third_party/blink/renderer/core/css/css_test_helpers.h"
#include "third_party/blink/renderer/core/css/cssom/css_keyword_value.h"
#include "third_party/blink/renderer/core/css/cssom/css_numeric_value.h"
#include "third_party/blink/renderer/core/css/resolver/style_cascade.h"
#include "third_party/blink/renderer/core/css/resolver/style_resolver.h"
#include "third_party/blink/renderer/core/css/resolver/style_resolver_state.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/style/computed_style.h"

namespace blink {
namespace animation_test_helpers {

void SetV8ObjectPropertyAsString(v8::Isolate* isolate,
                                 v8::Local<v8::Object> object,
                                 const StringView& name,
                                 const StringView& value) {
  v8::MicrotasksScope microtasks_scope(
      isolate, v8::MicrotasksScope::kDoNotRunMicrotasks);
  object
      ->Set(isolate->GetCurrentContext(), V8String(isolate, name),
            V8String(isolate, value))
      .ToChecked();
}

void SetV8ObjectPropertyAsNumber(v8::Isolate* isolate,
                                 v8::Local<v8::Object> object,
                                 const StringView& name,
                                 double value) {
  v8::MicrotasksScope microtasks_scope(
      isolate, v8::MicrotasksScope::kDoNotRunMicrotasks);
  object
      ->Set(isolate->GetCurrentContext(), V8String(isolate, name),
            v8::Number::New(isolate, value))
      .ToChecked();
}

KeyframeEffect* CreateSimpleKeyframeEffectForTest(Element* target,
                                                  CSSPropertyID property,
                                                  String value_start,
                                                  String value_end) {
  Timing timing;
  timing.iteration_duration = AnimationTimeDelta::FromSecondsD(1000);

  StringKeyframe* start_keyframe = MakeGarbageCollected<StringKeyframe>();
  start_keyframe->SetOffset(0.0);
  start_keyframe->SetCSSPropertyValue(
      property, value_start, SecureContextMode::kSecureContext, nullptr);

  StringKeyframe* end_keyframe = MakeGarbageCollected<StringKeyframe>();
  end_keyframe->SetOffset(1.0);
  end_keyframe->SetCSSPropertyValue(property, value_end,
                                    SecureContextMode::kSecureContext, nullptr);

  StringKeyframeVector keyframes;
  keyframes.push_back(start_keyframe);
  keyframes.push_back(end_keyframe);

  auto* model = MakeGarbageCollected<StringKeyframeEffectModel>(keyframes);
  return MakeGarbageCollected<KeyframeEffect>(target, model, timing);
}

void EnsureInterpolatedValueCached(ActiveInterpolations* interpolations,
                                   Document& document,
                                   Element* element) {
  // TODO(smcgruer): We should be able to use a saner API approach like
  // document.GetStyleResolver().ResolveStyle(element). However that would
  // require our callers to properly register every animation they pass in
  // here, which the current tests do not do.
  auto style = document.GetStyleResolver().CreateComputedStyle();
  StyleResolverState state(document, *element, StyleRequest(style.get()));
  state.SetStyle(style);

  ActiveInterpolationsMap map;
  map.Set(PropertyHandle("--unused"), interpolations);

  StyleCascade cascade(state);
  cascade.AddInterpolations(&map, CascadeOrigin::kAnimation);
  cascade.Apply();
}

V8ScrollTimelineOffset* OffsetFromString(Document& document,
                                         const String& string) {
  const CSSValue* value = css_test_helpers::ParseValue(
      document, "<length-percentage> | auto", string);

  if (const auto* primitive = DynamicTo<CSSPrimitiveValue>(value)) {
    return MakeGarbageCollected<V8ScrollTimelineOffset>(
        CSSNumericValue::FromCSSValue(*primitive));
  } else if (DynamicTo<CSSIdentifierValue>(value)) {
    return MakeGarbageCollected<V8ScrollTimelineOffset>(
        CSSKeywordValue::Create("auto"));
  }
  return MakeGarbageCollected<V8ScrollTimelineOffset>(string);
}

}  // namespace animation_test_helpers
}  // namespace blink
