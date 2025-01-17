// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_REMOTE_FRAME_VIEW_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_REMOTE_FRAME_VIEW_H_

#include "cc/paint/paint_canvas.h"
#include "third_party/blink/public/mojom/frame/lifecycle.mojom-blink-forward.h"
#include "third_party/blink/public/mojom/frame/viewport_intersection_state.mojom-blink.h"
#include "third_party/blink/renderer/core/dom/document_lifecycle.h"
#include "third_party/blink/renderer/core/frame/frame_view.h"
#include "third_party/blink/renderer/core/layout/intrinsic_sizing_info.h"
#include "third_party/blink/renderer/platform/geometry/int_rect.h"
#include "third_party/blink/renderer/platform/heap/handle.h"

namespace cc {
class PaintCanvas;
}

namespace blink {
class CullRect;
class GraphicsContext;
class LocalFrameView;
class RemoteFrame;

class RemoteFrameView final : public GarbageCollected<RemoteFrameView>,
                              public FrameView {
 public:
  explicit RemoteFrameView(RemoteFrame*);
  ~RemoteFrameView() override;

  void AttachToLayout() override;
  void DetachFromLayout() override;

  LocalFrameView* ParentFrameView() const override;
  LayoutEmbeddedContent* GetLayoutEmbeddedContent() const override;
  RemoteFrame& GetFrame() const {
    DCHECK(remote_frame_);
    return *remote_frame_;
  }

  void Dispose() override;
  void SetFrameRect(const IntRect&) override;
  void PropagateFrameRects() override;
  void Paint(GraphicsContext&,
             const GlobalPaintFlags,
             const CullRect&,
             const IntSize& paint_offset = IntSize()) const override;
  void UpdateGeometry() override;
  void Hide() override;
  void Show() override;

  bool UpdateViewportIntersectionsForSubtree(unsigned parent_flags) override;
  void SetNeedsOcclusionTracking(bool);
  bool NeedsOcclusionTracking() const { return needs_occlusion_tracking_; }

  bool GetIntrinsicSizingInfo(IntrinsicSizingInfo&) const override;

  void SetIntrinsicSizeInfo(const IntrinsicSizingInfo& size_info);
  bool HasIntrinsicSizingInfo() const override;

  bool CanThrottleRendering() const override;
  void VisibilityForThrottlingChanged() override;
  void VisibilityChanged(blink::mojom::FrameVisibility visibility) override;

  // Compute the interest rect of this frame in its unscrolled space. This may
  // be used by the OOPIF's compositor to limit the amount of rastered tiles,
  // and reduce the number of paint-ops generated. UpdateCompositingRect must be
  // called before the parent frame commits a compositor frame.
  void UpdateCompositingRect();
  IntRect GetCompositingRect() const { return compositing_rect_; }

  void UpdateCompositingScaleFactor();
  float GetCompositingScaleFactor() const { return compositing_scale_factor_; }

  uint32_t Print(const IntRect&, cc::PaintCanvas*) const;
  uint32_t CapturePaintPreview(const IntRect&, cc::PaintCanvas*) const;

  void Trace(Visitor*) const override;

 protected:
  bool NeedsViewportOffset() const override { return true; }
  // This is used to service IntersectionObservers in an OOPIF child document.
  void SetViewportIntersection(const mojom::blink::ViewportIntersectionState&
                                   intersection_state) override;
  void ParentVisibleChanged() override;

 private:
  // This function returns the LocalFrameView associated with the parent frame's
  // local root, or nullptr if the parent frame is not a local frame. For
  // portals, this will return the local root associated with the portal's
  // owner.
  LocalFrameView* ParentLocalRootFrameView() const;

  // The properties and handling of the cycle between RemoteFrame
  // and its RemoteFrameView corresponds to that between LocalFrame
  // and LocalFrameView. Please see the LocalFrameView::frame_ comment for
  // details.
  Member<RemoteFrame> remote_frame_;
  mojom::blink::ViewportIntersectionState last_intersection_state_;
  IntRect compositing_rect_;
  float compositing_scale_factor_ = 1.0f;

  IntrinsicSizingInfo intrinsic_sizing_info_;
  bool has_intrinsic_sizing_info_ = false;
  bool needs_occlusion_tracking_ = false;
  bool needs_frame_rect_propagation_ = false;
};

template <>
struct DowncastTraits<RemoteFrameView> {
  static bool AllowFrom(const EmbeddedContentView& embedded_content_view) {
    return !embedded_content_view.IsLocalFrameView() &&
           !embedded_content_view.IsPluginView();
  }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_REMOTE_FRAME_VIEW_H_
