/*
 * Copyright (C) 2003, 2006, 2007, 2008, 2009 Apple Inc. All rights reserved.
 * Copyright (C) 2008-2009 Torch Mobile, Inc.
 * Copyright (C) 2013 Google Inc. All rights reserved.
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

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_GRAPHICS_CONTEXT_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_GRAPHICS_CONTEXT_H_

#include <memory>

#include "base/dcheck_is_on.h"
#include "third_party/blink/public/mojom/frame/color_scheme.mojom-blink-forward.h"
#include "third_party/blink/renderer/platform/fonts/font.h"
#include "third_party/blink/renderer/platform/graphics/dark_mode_filter.h"
#include "third_party/blink/renderer/platform/graphics/dark_mode_settings.h"
#include "third_party/blink/renderer/platform/graphics/dash_array.h"
#include "third_party/blink/renderer/platform/graphics/dom_node_id.h"
#include "third_party/blink/renderer/platform/graphics/draw_looper_builder.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context_state.h"
#include "third_party/blink/renderer/platform/graphics/image_orientation.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_filter.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_record.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_recorder.h"
#include "third_party/blink/renderer/platform/graphics/skia/skia_utils.h"
#include "third_party/blink/renderer/platform/platform_export.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/blink/renderer/platform/wtf/forward.h"
#include "third_party/skia/include/core/SkClipOp.h"
#include "third_party/skia/include/core/SkRefCnt.h"

class SkPath;
class SkRRect;
struct SkRect;

namespace paint_preview {
class PaintPreviewTracker;
}  // namespace paint_preview

namespace blink {

class FloatRect;
class FloatRoundedRect;
class KURL;
class PaintController;
class Path;
struct TextRunPaintInfo;

class PLATFORM_EXPORT GraphicsContext {
  USING_FAST_MALLOC(GraphicsContext);

 public:
  explicit GraphicsContext(PaintController&);
  GraphicsContext(const GraphicsContext&) = delete;
  GraphicsContext& operator=(const GraphicsContext&) = delete;
  ~GraphicsContext();

  // Copy configs such as printing, dark mode, device scale factor etc. from
  // another GraphicsContext.
  void CopyConfigFrom(GraphicsContext&);

  void SetPrintingMetafile(printing::MetafileSkia* metafile) {
    printing_metafile_ = metafile;
  }

  void SetPaintPreviewTracker(paint_preview::PaintPreviewTracker* tracker) {
    paint_preview_tracker_ = tracker;
  }

  cc::PaintCanvas* Canvas() { return canvas_; }
  const cc::PaintCanvas* Canvas() const { return canvas_; }

  PaintController& GetPaintController() { return paint_controller_; }
  const PaintController& GetPaintController() const {
    return paint_controller_;
  }

  bool IsDarkModeEnabled() const { return is_dark_mode_enabled_; }
  void SetDarkModeEnabled(bool enabled) { is_dark_mode_enabled_ = enabled; }

  DarkModeFilter* GetDarkModeFilter();

  void UpdateDarkModeSettingsForTest(const DarkModeSettings&);

  // ---------- State management methods -----------------
  void Save();
  void Restore();

#if DCHECK_IS_ON()
  unsigned SaveCount() const;
#endif

  float StrokeThickness() const {
    return ImmutableState()->GetStrokeData().Thickness();
  }
  void SetStrokeThickness(float thickness) {
    MutableState()->SetStrokeThickness(thickness);
  }

  StrokeStyle GetStrokeStyle() const {
    return ImmutableState()->GetStrokeData().Style();
  }
  void SetStrokeStyle(StrokeStyle style) {
    MutableState()->SetStrokeStyle(style);
  }

  Color StrokeColor() const { return ImmutableState()->StrokeColor(); }
  void SetStrokeColor(const Color& color) {
    MutableState()->SetStrokeColor(color);
  }

  void SetLineCap(LineCap cap) { MutableState()->SetLineCap(cap); }
  void SetLineDash(const DashArray& dashes, float dash_offset) {
    MutableState()->SetLineDash(dashes, dash_offset);
  }
  void SetLineJoin(LineJoin join) { MutableState()->SetLineJoin(join); }
  void SetMiterLimit(float limit) { MutableState()->SetMiterLimit(limit); }

  Color FillColor() const { return ImmutableState()->FillColor(); }
  void SetFillColor(const Color& color) { MutableState()->SetFillColor(color); }

  void SetShouldAntialias(bool antialias) {
    MutableState()->SetShouldAntialias(antialias);
  }
  bool ShouldAntialias() const { return ImmutableState()->ShouldAntialias(); }

  void SetTextDrawingMode(TextDrawingModeFlags mode) {
    MutableState()->SetTextDrawingMode(mode);
  }
  TextDrawingModeFlags TextDrawingMode() const {
    return ImmutableState()->TextDrawingMode();
  }

  void SetImageInterpolationQuality(InterpolationQuality quality) {
    MutableState()->SetInterpolationQuality(quality);
  }
  InterpolationQuality ImageInterpolationQuality() const {
    return ImmutableState()->GetInterpolationQuality();
  }

  SkSamplingOptions ImageSamplingOptions() const {
    return PaintFlags::FilterQualityToSkSamplingOptions(
        static_cast<SkFilterQuality>(ImageInterpolationQuality()));
  }

  // Specify the device scale factor which may change the way document markers
  // and fonts are rendered.
  void SetDeviceScaleFactor(float factor) { device_scale_factor_ = factor; }
  float DeviceScaleFactor() const { return device_scale_factor_; }

  // Set to true if context is for printing. Bitmaps won't be resampled when
  // printing to keep the best possible quality. When printing text will be
  // provided along with glyphs.
  void SetPrinting(bool printing) { printing_ = printing; }

  SkColorFilter* GetColorFilter() const;
  void SetColorFilter(ColorFilter);
  // ---------- End state management methods -----------------

  // DrawRect() fills and always strokes using a 1-pixel stroke inset from
  // the rect borders (of the pre-set stroke color).
  void DrawRect(const IntRect&);

  // DrawLine() only operates on horizontal or vertical lines and uses the
  // current stroke settings.
  void DrawLine(const IntPoint&,
                const IntPoint&,
                const DarkModeFilter::ElementRole role =
                    DarkModeFilter::ElementRole::kBackground,
                bool is_text_line = false);

  void FillPath(const Path&);

  // The length parameter is only used when the path has a dashed or dotted
  // stroke style, with the default dash/dot path effect. If a non-zero length
  // is provided the number of dashes/dots on a dashed/dotted
  // line will be adjusted to start and end that length with a dash/dot.
  // The dash_thickness parameter is only used when drawing dashed borders,
  // where the stroke thickness has been set for corner miters but we want the
  // dash length set from the border width.
  void StrokePath(const Path&,
                  const int length = 0,
                  const int dash_thickness = 0);

  void FillEllipse(const FloatRect&);
  void StrokeEllipse(const FloatRect&);

  void FillRect(const IntRect&);
  void FillRect(const IntRect&,
                const Color&,
                SkBlendMode = SkBlendMode::kSrcOver);
  void FillRect(const IntRect& rect,
                const Color& color,
                DarkModeFilter::ElementRole role);
  void FillRect(const FloatRect&);
  void FillRect(
      const FloatRect&,
      const Color&,
      SkBlendMode = SkBlendMode::kSrcOver,
      DarkModeFilter::ElementRole = DarkModeFilter::ElementRole::kBackground);
  void FillRoundedRect(const FloatRoundedRect&, const Color&);
  void FillDRRect(const FloatRoundedRect&,
                  const FloatRoundedRect&,
                  const Color&);
  void FillRectWithRoundedHole(const FloatRect&,
                               const FloatRoundedRect& rounded_hole_rect,
                               const Color&);

  void StrokeRect(const FloatRect&, float line_width);

  void DrawRecord(sk_sp<const PaintRecord>);
  void CompositeRecord(sk_sp<PaintRecord>,
                       const FloatRect& dest,
                       const FloatRect& src,
                       SkBlendMode);

  void DrawImage(Image*,
                 Image::ImageDecodingMode,
                 const FloatRect& dest_rect,
                 const FloatRect* src_rect = nullptr,
                 bool has_filter_property = false,
                 SkBlendMode = SkBlendMode::kSrcOver,
                 RespectImageOrientationEnum = kRespectImageOrientation);
  void DrawImageRRect(Image*,
                      Image::ImageDecodingMode,
                      const FloatRoundedRect& dest,
                      const FloatRect& src_rect,
                      bool has_filter_property = false,
                      SkBlendMode = SkBlendMode::kSrcOver,
                      RespectImageOrientationEnum = kRespectImageOrientation);
  void DrawImageTiled(Image* image,
                      const FloatRect& dest_rect,
                      const FloatRect& src_rect,
                      const FloatSize& scale_src_to_dest,
                      const FloatPoint& phase,
                      const FloatSize& repeat_spacing,
                      bool has_filter_property = false,
                      SkBlendMode = SkBlendMode::kSrcOver,
                      RespectImageOrientationEnum = kRespectImageOrientation);

  // These methods write to the canvas.
  // Also drawLine(const IntPoint& point1, const IntPoint& point2) and
  // fillRoundedRect().
  void DrawOval(const SkRect&,
                const PaintFlags&,
                const DarkModeFilter::ElementRole role =
                    DarkModeFilter::ElementRole::kBackground);
  void DrawPath(const SkPath&,
                const PaintFlags&,
                const DarkModeFilter::ElementRole role =
                    DarkModeFilter::ElementRole::kBackground);
  void DrawRect(const SkRect&,
                const PaintFlags&,
                const DarkModeFilter::ElementRole role =
                    DarkModeFilter::ElementRole::kBackground);
  void DrawRRect(const SkRRect&, const PaintFlags&);

  void Clip(const IntRect& rect) { ClipRect(rect); }
  void Clip(const FloatRect& rect) { ClipRect(rect); }
  void ClipRoundedRect(const FloatRoundedRect&,
                       SkClipOp = SkClipOp::kIntersect,
                       AntiAliasingMode = kAntiAliased);
  void ClipOut(const IntRect& rect) {
    ClipRect(rect, kNotAntiAliased, SkClipOp::kDifference);
  }
  void ClipOut(const FloatRect& rect) {
    ClipRect(rect, kNotAntiAliased, SkClipOp::kDifference);
  }
  void ClipOut(const Path&);
  void ClipOutRoundedRect(const FloatRoundedRect&);
  void ClipPath(const SkPath&,
                AntiAliasingMode = kNotAntiAliased,
                SkClipOp = SkClipOp::kIntersect);
  void ClipRect(const SkRect&,
                AntiAliasingMode = kNotAntiAliased,
                SkClipOp = SkClipOp::kIntersect);

  void DrawText(const Font&,
                const TextRunPaintInfo&,
                const FloatPoint&,
                DOMNodeId);
  void DrawText(const Font&,
                const NGTextFragmentPaintInfo&,
                const FloatPoint&,
                DOMNodeId);

  // TODO(layout-dev): This method is only used by SVGInlineTextBoxPainter, see
  // if we can change that to use the four parameter version above.
  void DrawText(const Font&,
                const TextRunPaintInfo&,
                const FloatPoint&,
                const PaintFlags&,
                DOMNodeId);

  // TODO(layout-dev): This method is only used by NGTextPainter, see if the
  // four parameter overload can be removed or if it can wrap this method.
  void DrawText(const Font&,
                const NGTextFragmentPaintInfo&,
                const FloatPoint&,
                const PaintFlags&,
                DOMNodeId);

  void DrawEmphasisMarks(const Font&,
                         const TextRunPaintInfo&,
                         const AtomicString& mark,
                         const FloatPoint&);
  void DrawEmphasisMarks(const Font&,
                         const NGTextFragmentPaintInfo&,
                         const AtomicString& mark,
                         const FloatPoint&);

  void DrawBidiText(
      const Font&,
      const TextRunPaintInfo&,
      const FloatPoint&,
      Font::CustomFontNotReadyAction = Font::kDoNotPaintIfFontNotReady);
  void DrawHighlightForText(const Font&,
                            const TextRun&,
                            const FloatPoint&,
                            int h,
                            const Color& background_color,
                            int from = 0,
                            int to = -1);

  void DrawLineForText(const FloatPoint&, float width);

  // beginLayer()/endLayer() behave like save()/restore() for CTM and clip
  // states. Apply SkBlendMode when the layer is composited on the backdrop
  // (i.e. endLayer()).
  void BeginLayer(float opacity = 1.0f,
                  SkBlendMode = SkBlendMode::kSrcOver,
                  const FloatRect* = nullptr,
                  ColorFilter = kColorFilterNone,
                  sk_sp<PaintFilter> = nullptr);
  void EndLayer();

  // Instead of being dispatched to the active canvas, draw commands following
  // beginRecording() are stored in a display list that can be replayed at a
  // later time. Pass in the bounding rectangle for the content in the list.
  void BeginRecording(const FloatRect&);

  // Returns a record with any recorded draw commands since the prerequisite
  // call to beginRecording().  The record is guaranteed to be non-null (but
  // not necessarily non-empty), even when the context is disabled.
  sk_sp<PaintRecord> EndRecording();

  void SetDrawLooper(sk_sp<SkDrawLooper>);

  void DrawFocusRing(const Vector<IntRect>&,
                     float width,
                     int offset,
                     float border_radius,
                     float min_border_width,
                     const Color&,
                     mojom::blink::ColorScheme color_scheme);
  void DrawFocusRing(const Path&, float width, int offset, const Color&);

  const PaintFlags& FillFlags() const { return ImmutableState()->FillFlags(); }
  // If the length of the path to be stroked is known, pass it in for correct
  // dash or dot placement. Border painting uses a stroke thickness determined
  // by the corner miters. Set the dash_thickness to a non-zero number for
  // cases where dashes should be based on a different thickness.
  const PaintFlags& StrokeFlags(const int length = 0,
                                const int dash_thickness = 0) const {
    return ImmutableState()->StrokeFlags(length, dash_thickness);
  }

  // ---------- Transformation methods -----------------
  void ConcatCTM(const AffineTransform&);

  void Scale(float x, float y);
  void Rotate(float angle_in_radians);
  void Translate(float x, float y);
  // ---------- End transformation methods -----------------

  SkFilterQuality ComputeFilterQuality(Image*,
                                       const FloatRect& dest,
                                       const FloatRect& src) const;

  SkSamplingOptions ComputeSamplingOptions(Image* image,
                                           const FloatRect& dest,
                                           const FloatRect& src) const {
    return PaintFlags::FilterQualityToSkSamplingOptions(
        ComputeFilterQuality(image, dest, src));
  }

  // Sets target URL of a clickable area.
  void SetURLForRect(const KURL&, const IntRect&);

  // Sets the destination of a clickable area of a URL fragment (in a URL
  // pointing to the same web page). When the area is clicked, the page should
  // be scrolled to the location set by setURLDestinationLocation() for the
  // destination whose name is |name|.
  void SetURLFragmentForRect(const String& name, const IntRect&);

  // Sets location of a URL destination (a.k.a. anchor) in the page.
  void SetURLDestinationLocation(const String& name, const IntPoint&);

  static void AdjustLineToPixelBoundaries(FloatPoint& p1,
                                          FloatPoint& p2,
                                          float stroke_width);

  static Path GetPathForTextLine(const FloatPoint&,
                                 float width,
                                 float stroke_thickness,
                                 StrokeStyle);
  static bool ShouldUseStrokeForTextLine(StrokeStyle);

  static int FocusRingOutsetExtent(int offset, int width);

  void SetInDrawingRecorder(bool);
  bool InDrawingRecorder() const { return in_drawing_recorder_; }

  // Set the DOM Node Id on the canvas. This is used to associate
  // the drawing commands with the structure tree for the page when
  // creating a tagged PDF. Callers are responsible for restoring it.
  void SetDOMNodeId(DOMNodeId);
  DOMNodeId GetDOMNodeId() const;
  bool NeedsDOMNodeId() const { return printing_; }

  static sk_sp<SkColorFilter> WebCoreColorFilterToSkiaColorFilter(ColorFilter);

 private:
  friend class ScopedDarkModeElementRoleOverride;

  const GraphicsContextState* ImmutableState() const { return paint_state_; }

  GraphicsContextState* MutableState() {
    RealizePaintSave();
    return paint_state_;
  }

  template <typename TextPaintInfo>
  void DrawTextInternal(const Font&,
                        const TextPaintInfo&,
                        const FloatPoint&,
                        DOMNodeId);

  template <typename TextPaintInfo>
  void DrawEmphasisMarksInternal(const Font&,
                                 const TextPaintInfo&,
                                 const AtomicString& mark,
                                 const FloatPoint&);

  template <typename DrawTextFunc>
  void DrawTextPasses(const DrawTextFunc&);

  void SaveLayer(const SkRect* bounds, const PaintFlags*);
  void RestoreLayer();

  // Helpers for drawing a focus ring (drawFocusRing)
  void DrawFocusRingPath(const SkPath&,
                         const Color&,
                         float width,
                         float border_radius);
  void DrawFocusRingRect(const SkRect&,
                         const Color&,
                         float width,
                         float border_radius);

  void DrawFocusRingInternal(const Vector<IntRect>&,
                             float width,
                             int offset,
                             float border_radius,
                             const Color&);

  // SkCanvas wrappers.
  void ClipRRect(const SkRRect&,
                 AntiAliasingMode = kNotAntiAliased,
                 SkClipOp = SkClipOp::kIntersect);
  void Concat(const SkMatrix&);

  // Apply deferred paint state saves
  void RealizePaintSave() {
    if (paint_state_->SaveCount()) {
      paint_state_->DecrementSaveCount();
      ++paint_state_index_;
      if (paint_state_stack_.size() == paint_state_index_) {
        paint_state_stack_.push_back(
            GraphicsContextState::CreateAndCopy(*paint_state_));
        paint_state_ = paint_state_stack_[paint_state_index_].get();
      } else {
        GraphicsContextState* prior_paint_state = paint_state_;
        paint_state_ = paint_state_stack_[paint_state_index_].get();
        paint_state_->Copy(*prior_paint_state);
      }
    }
  }

  class DarkModeFlags;

  bool ShouldDrawDarkModeTextContrastOutline(
      const PaintFlags& original_flags,
      const DarkModeFlags& dark_flags) const;

  // This is owned by paint_recorder_. Never delete this object.
  // Drawing operations are allowed only after the first BeginRecording() which
  // initializes this to not null.
  cc::PaintCanvas* canvas_ = nullptr;

  PaintController& paint_controller_;

  // Paint states stack. The state controls the appearance of drawn content, so
  // this stack enables local drawing state changes with save()/restore() calls.
  // We do not delete from this stack to avoid memory churn.
  Vector<std::unique_ptr<GraphicsContextState>> paint_state_stack_;

  // Current index on the stack. May not be the last thing on the stack.
  wtf_size_t paint_state_index_ = 0;

  // Raw pointer to the current state.
  GraphicsContextState* paint_state_ = nullptr;

  PaintRecorder paint_recorder_;

  printing::MetafileSkia* printing_metafile_ = nullptr;
  paint_preview::PaintPreviewTracker* paint_preview_tracker_ = nullptr;

#if DCHECK_IS_ON()
  int layer_count_ = 0;
  bool disable_destruction_checks_ = false;
#endif

  float device_scale_factor_ = 1.0f;

  std::unique_ptr<DarkModeFilter> dark_mode_filter_;

  bool printing_ = false;
  bool in_drawing_recorder_ = false;
  bool is_dark_mode_enabled_ = false;

  // The current node ID, which is used for marked content in a tagged PDF.
  DOMNodeId dom_node_id_ = kInvalidDOMNodeId;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_GRAPHICS_CONTEXT_H_
