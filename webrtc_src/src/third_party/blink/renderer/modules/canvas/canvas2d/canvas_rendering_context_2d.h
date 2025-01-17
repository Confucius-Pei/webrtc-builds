/*
 * Copyright (C) 2006, 2007, 2009, 2010, 2011, 2012 Apple Inc. All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_CANVAS_CANVAS2D_CANVAS_RENDERING_CONTEXT_2D_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_CANVAS_CANVAS2D_CANVAS_RENDERING_CONTEXT_2D_H_

#include <random>

#include "base/macros.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_canvas_formatted_text.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_canvas_rendering_context_2d_settings.h"
#include "third_party/blink/renderer/core/html/canvas/canvas_context_creation_attributes_core.h"
#include "third_party/blink/renderer/core/html/canvas/canvas_rendering_context.h"
#include "third_party/blink/renderer/core/html/canvas/canvas_rendering_context_factory.h"
#include "third_party/blink/renderer/core/html/canvas/image_data.h"
#include "third_party/blink/renderer/core/style/filter_operations.h"
#include "third_party/blink/renderer/core/svg/svg_resource_client.h"
#include "third_party/blink/renderer/modules/canvas/canvas2d/base_rendering_context_2d.h"
#include "third_party/blink/renderer/modules/canvas/canvas2d/canvas_formatted_text.h"
#include "third_party/blink/renderer/modules/canvas/canvas2d/canvas_rendering_context_2d_state.h"
#include "third_party/blink/renderer/modules/canvas/canvas2d/identifiability_study_helper.h"
#include "third_party/blink/renderer/modules/modules_export.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "third_party/blink/renderer/platform/graphics/graphics_types.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/privacy_budget/identifiability_digest_helpers.h"
#include "third_party/blink/renderer/platform/scheduler/public/thread.h"
#include "third_party/blink/renderer/platform/wtf/linked_hash_set.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace cc {
class Layer;
}

namespace blink {

class CanvasImageSource;
class Element;
class ExceptionState;
class Font;
class HitRegion;
class HitRegionOptions;
class HitRegionManager;
class HitTestCanvasResult;
class Path2D;
class TextMetrics;

class MODULES_EXPORT CanvasRenderingContext2D final
    : public CanvasRenderingContext,
      public BaseRenderingContext2D,
      public SVGResourceClient {
  DEFINE_WRAPPERTYPEINFO();

 public:
  class Factory : public CanvasRenderingContextFactory {
   public:
    Factory() = default;
    ~Factory() override = default;

    CanvasRenderingContext* Create(
        CanvasRenderingContextHost* host,
        const CanvasContextCreationAttributesCore& attrs) override;

    CanvasRenderingContext::ContextType GetContextType() const override {
      return CanvasRenderingContext::kContext2D;
    }

   private:
    DISALLOW_COPY_AND_ASSIGN(Factory);
  };

  CanvasRenderingContext2D(HTMLCanvasElement*,
                           const CanvasContextCreationAttributesCore&);
  ~CanvasRenderingContext2D() override;

  HTMLCanvasElement* canvas() const {
    DCHECK(!Host() || !Host()->IsOffscreenCanvas());
    return static_cast<HTMLCanvasElement*>(Host());
  }
  V8RenderingContext* AsV8RenderingContext() final;

  bool isContextLost() const override;

  bool ShouldAntialias() const override;
  void SetShouldAntialias(bool) override;

  void scrollPathIntoView();
  void scrollPathIntoView(Path2D*);

  void clearRect(double x, double y, double width, double height);
  void ClearRect(double x, double y, double width, double height) override {
    clearRect(x, y, width, height);
  }

  void Reset() override;

  String font() const;
  void setFont(const String&) override;

  String direction() const;
  void setDirection(const String&);

  void setTextLetterSpacing(const double letter_spacing);
  void setTextWordSpacing(const double word_spacing);
  void setTextRendering(const String&);

  void setFontKerning(const String&);
  void setFontStretch(const String&);
  void setFontVariantCaps(const String&);

  void fillText(const String& text, double x, double y);
  void fillText(const String& text, double x, double y, double max_width);
  void strokeText(const String& text, double x, double y);
  void strokeText(const String& text, double x, double y, double max_width);
  TextMetrics* measureText(const String& text);

  CanvasRenderingContext2DSettings* getContextAttributes() const;

  void fillFormattedText(CanvasFormattedText* formatted_text,
                         double x,
                         double y,
                         double wrap_width);

  void drawFocusIfNeeded(Element*);
  void drawFocusIfNeeded(Path2D*, Element*);

  void addHitRegion(const HitRegionOptions*, ExceptionState&);
  void removeHitRegion(const String& id);
  void clearHitRegions();
  HitRegion* HitRegionAtPoint(const FloatPoint&);
  unsigned HitRegionsCount() const override;

  void LoseContext(LostContextMode) override;
  void DidSetSurfaceSize() override;

  void RestoreCanvasMatrixClipStack(cc::PaintCanvas*) const override;

  // TaskObserver implementation
  void DidProcessTask(const base::PendingTask&) final;

  void StyleDidChange(const ComputedStyle* old_style,
                      const ComputedStyle& new_style) override;
  HitTestCanvasResult* GetControlAndIdIfHitRegionExists(
      const PhysicalOffset& location) override;
  String GetIdFromControl(const Element*) override;

  // SVGResourceClient implementation
  void ResourceContentChanged(SVGResource*) override;

  void UpdateFilterReferences(const FilterOperations&);
  void ClearFilterReferences();

  // BaseRenderingContext2D implementation
  void DidDraw2D(const SkIRect& dirty_rect) final;
  bool OriginClean() const final;
  void SetOriginTainted() final;
  bool WouldTaintOrigin(CanvasImageSource* source) final {
    return CanvasRenderingContext::WouldTaintOrigin(source);
  }
  void DisableAcceleration() override;

  int Width() const final;
  int Height() const final;

  bool CanCreateCanvas2dResourceProvider() const final;

  RespectImageOrientationEnum RespectImageOrientation() const final;

  bool ParseColorOrCurrentColor(Color&, const String& color_string) const final;

  cc::PaintCanvas* GetOrCreatePaintCanvas() final;
  cc::PaintCanvas* GetPaintCanvas() const final;

  scoped_refptr<StaticBitmapImage> GetImage() final;

  bool StateHasFilter() final;
  sk_sp<PaintFilter> StateGetFilter() final;
  void SnapshotStateForFilter() final;

  void ValidateStateStackWithCanvas(const cc::PaintCanvas*) const final;

  void FinalizeFrame() override;

  bool IsPaintable() const final {
    return canvas() && canvas()->GetCanvas2DLayerBridge();
  }

  void WillDrawImage(CanvasImageSource*) const final;

  void Trace(Visitor*) const override;

  ImageData* getImageDataInternal(int sx,
                                  int sy,
                                  int sw,
                                  int sh,
                                  ImageDataSettings*,
                                  ExceptionState&) final;

  CanvasColorParams ColorParamsForTest() const {
    return GetCanvas2DColorParams();
  }

  IdentifiableToken IdentifiableTextToken() const override {
    return identifiability_study_helper_.GetToken();
  }

  bool IdentifiabilityEncounteredSkippedOps() const override {
    return identifiability_study_helper_.encountered_skipped_ops();
  }

  bool IdentifiabilityEncounteredSensitiveOps() const override {
    return identifiability_study_helper_.encountered_sensitive_ops();
  }

 protected:
  // This reports CanvasColorParams to the CanvasRenderingContext interface.
  CanvasColorParams CanvasRenderingContextColorParams() const override {
    return color_params_;
  }
  // This reports CanvasColorParams to the BaseRenderingContext2D interface.
  CanvasColorParams GetCanvas2DColorParams() const override {
    return color_params_;
  }
  bool WritePixels(const SkImageInfo& orig_info,
                   const void* pixels,
                   size_t row_bytes,
                   int x,
                   int y) override;
  void WillOverwriteCanvas() override;

 private:
  friend class CanvasRenderingContext2DAutoRestoreSkCanvas;

  void DispatchContextLostEvent(TimerBase*);
  void DispatchContextRestoredEvent(TimerBase*);
  void TryRestoreContextEvent(TimerBase*);

  void PruneLocalFontCache(size_t target_size);

  void ScrollPathIntoViewInternal(const Path&);

  void DrawTextInternal(const String&,
                        double x,
                        double y,
                        CanvasRenderingContext2DState::PaintType,
                        double* max_width = nullptr);

  const Font& AccessFont();

  void DrawFocusIfNeededInternal(const Path&, Element*);
  bool FocusRingCallIsValid(const Path&, Element*);
  void DrawFocusRing(const Path&, Element*);
  void UpdateElementAccessibility(const Path&, Element*);

  CanvasRenderingContext::ContextType GetContextType() const override {
    return CanvasRenderingContext::kContext2D;
  }

  bool IsRenderingContext2D() const override { return true; }
  bool IsComposited() const override;
  bool IsAccelerated() const override;
  bool IsOriginTopLeft() const override;
  bool HasAlpha() const override { return CreationAttributes().alpha; }
  bool IsDesynchronized() const override {
    return CreationAttributes().desynchronized;
  }
  void SetIsInHiddenPage(bool) override;
  void SetIsBeingDisplayed(bool) override;
  void Stop() final;

  bool IsTransformInvertible() const override;
  TransformationMatrix GetTransform() const override;

  cc::Layer* CcLayer() const override;
  bool IsCanvas2DBufferValid() const override;

  Member<HitRegionManager> hit_region_manager_;
  LostContextMode context_lost_mode_;
  bool context_restorable_;
  unsigned try_restore_context_attempt_count_;
  HeapTaskRunnerTimer<CanvasRenderingContext2D>
      dispatch_context_lost_event_timer_;
  HeapTaskRunnerTimer<CanvasRenderingContext2D>
      dispatch_context_restored_event_timer_;
  HeapTaskRunnerTimer<CanvasRenderingContext2D>
      try_restore_context_event_timer_;

  FilterOperations filter_operations_;
  HashMap<String, FontDescription> fonts_resolved_using_current_style_;
  bool should_prune_local_font_cache_;
  LinkedHashSet<String> font_lru_list_;

  static constexpr float kRasterMetricProbability = 0.01;
  std::mt19937 random_generator_;
  std::bernoulli_distribution bernoulli_distribution_;
  CanvasColorParams color_params_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_CANVAS_CANVAS2D_CANVAS_RENDERING_CONTEXT_2D_H_
