/*
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012 Apple Inc.
 * All rights reserved.
 * Copyright (C) 2008, 2010 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 * Copyright (C) 2008 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2008 Dirk Schulze <krit@webkit.org>
 * Copyright (C) 2010 Torch Mobile (Beijing) Co. Ltd. All rights reserved.
 * Copyright (C) 2012, 2013 Intel Corporation. All rights reserved.
 * Copyright (C) 2012, 2013 Adobe Systems Incorporated. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "third_party/blink/renderer/modules/canvas/canvas2d/canvas_path.h"

#include "base/numerics/safe_conversions.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_union_dompoint_unrestricteddouble.h"
#include "third_party/blink/renderer/core/geometry/dom_point.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/geometry/float_rect.h"
#include "third_party/blink/renderer/platform/transforms/affine_transform.h"
#include "third_party/blink/renderer/platform/wtf/math_extras.h"

namespace blink {

// TODO(crbug.com/940846): Consider using double-type without casting and
// DoublePoint & DoubleRect instead of FloatPoint & FloatRect.

void CanvasPath::closePath() {
  if (path_.IsEmpty())
    return;
  path_.CloseSubpath();
}

void CanvasPath::moveTo(double double_x, double double_y) {
  float x = base::saturated_cast<float>(double_x);
  float y = base::saturated_cast<float>(double_y);
  if (!std::isfinite(x) || !std::isfinite(y))
    return;
  if (!IsTransformInvertible()) {
    path_.MoveTo(GetTransform().MapPoint(FloatPoint(x, y)));
    return;
  }
  path_.MoveTo(FloatPoint(x, y));
}

void CanvasPath::lineTo(double double_x, double double_y) {
  float x = base::saturated_cast<float>(double_x);
  float y = base::saturated_cast<float>(double_y);
  if (!std::isfinite(x) || !std::isfinite(y))
    return;
  FloatPoint p1 = FloatPoint(x, y);

  if (!IsTransformInvertible()) {
    p1 = GetTransform().MapPoint(p1);
  }

  if (!path_.HasCurrentPoint())
    path_.MoveTo(p1);

  path_.AddLineTo(p1);
}

void CanvasPath::quadraticCurveTo(double double_cpx,
                                  double double_cpy,
                                  double double_x,
                                  double double_y) {
  float cpx = base::saturated_cast<float>(double_cpx);
  float cpy = base::saturated_cast<float>(double_cpy);
  float x = base::saturated_cast<float>(double_x);
  float y = base::saturated_cast<float>(double_y);

  if (!std::isfinite(cpx) || !std::isfinite(cpy) || !std::isfinite(x) ||
      !std::isfinite(y))
    return;
  FloatPoint p1 = FloatPoint(x, y);
  FloatPoint cp = FloatPoint(cpx, cpy);

  if (!IsTransformInvertible()) {
    p1 = GetTransform().MapPoint(p1);
    cp = GetTransform().MapPoint(cp);
  }

  if (!path_.HasCurrentPoint())
    path_.MoveTo(FloatPoint(cpx, cpy));

  path_.AddQuadCurveTo(cp, p1);
}

void CanvasPath::bezierCurveTo(double double_cp1x,
                               double double_cp1y,
                               double double_cp2x,
                               double double_cp2y,
                               double double_x,
                               double double_y) {
  float cp1x = base::saturated_cast<float>(double_cp1x);
  float cp1y = base::saturated_cast<float>(double_cp1y);
  float cp2x = base::saturated_cast<float>(double_cp2x);
  float cp2y = base::saturated_cast<float>(double_cp2y);
  float x = base::saturated_cast<float>(double_x);
  float y = base::saturated_cast<float>(double_y);
  if (!std::isfinite(cp1x) || !std::isfinite(cp1y) || !std::isfinite(cp2x) ||
      !std::isfinite(cp2y) || !std::isfinite(x) || !std::isfinite(y))
    return;

  FloatPoint p1 = FloatPoint(x, y);
  FloatPoint cp1 = FloatPoint(cp1x, cp1y);
  FloatPoint cp2 = FloatPoint(cp2x, cp2y);

  if (!IsTransformInvertible()) {
    p1 = GetTransform().MapPoint(p1);
    cp1 = GetTransform().MapPoint(cp1);
    cp2 = GetTransform().MapPoint(cp2);
  }
  if (!path_.HasCurrentPoint())
    path_.MoveTo(FloatPoint(cp1x, cp1y));

  path_.AddBezierCurveTo(cp1, cp2, p1);
}

void CanvasPath::arcTo(double double_x1,
                       double double_y1,
                       double double_x2,
                       double double_y2,
                       double double_r,
                       ExceptionState& exception_state) {
  float x1 = base::saturated_cast<float>(double_x1);
  float y1 = base::saturated_cast<float>(double_y1);
  float x2 = base::saturated_cast<float>(double_x2);
  float y2 = base::saturated_cast<float>(double_y2);
  float r = base::saturated_cast<float>(double_r);
  if (!std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(x2) ||
      !std::isfinite(y2) || !std::isfinite(r))
    return;

  if (r < 0) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kIndexSizeError,
        "The radius provided (" + String::Number(r) + ") is negative.");
    return;
  }

  FloatPoint p1 = FloatPoint(x1, y1);
  FloatPoint p2 = FloatPoint(x2, y2);

  if (!IsTransformInvertible()) {
    p1 = GetTransform().MapPoint(p1);
    p2 = GetTransform().MapPoint(p2);
  }

  if (!path_.HasCurrentPoint())
    path_.MoveTo(p1);
  else if (p1 == path_.CurrentPoint() || p1 == p2 || !r)
    lineTo(x1, y1);
  else
    path_.AddArcTo(p1, p2, r);
}

namespace {

float AdjustEndAngle(float start_angle, float end_angle, bool anticlockwise) {
  float new_end_angle = end_angle;
  /* http://www.whatwg.org/specs/web-apps/current-work/multipage/the-canvas-element.html#dom-context-2d-arc
   * If the anticlockwise argument is false and endAngle-startAngle is equal
   * to or greater than 2pi, or,
   * if the anticlockwise argument is true and startAngle-endAngle is equal to
   * or greater than 2pi,
   * then the arc is the whole circumference of this ellipse, and the point at
   * startAngle along this circle's circumference, measured in radians clockwise
   * from the ellipse's semi-major axis, acts as both the start point and the
   * end point.
   */
  if (!anticlockwise && end_angle - start_angle >= kTwoPiFloat) {
    new_end_angle = start_angle + kTwoPiFloat;
  } else if (anticlockwise && start_angle - end_angle >= kTwoPiFloat) {
    new_end_angle = start_angle - kTwoPiFloat;

    /*
     * Otherwise, the arc is the path along the circumference of this ellipse
     * from the start point to the end point, going anti-clockwise if the
     * anticlockwise argument is true, and clockwise otherwise.
     * Since the points are on the ellipse, as opposed to being simply angles
     * from zero, the arc can never cover an angle greater than 2pi radians.
     */
    /* NOTE: When startAngle = 0, endAngle = 2Pi and anticlockwise = true, the
     * spec does not indicate clearly.
     * We draw the entire circle, because some web sites use arc(x, y, r, 0,
     * 2*Math.PI, true) to draw circle.
     * We preserve backward-compatibility.
     */
  } else if (!anticlockwise && start_angle > end_angle) {
    new_end_angle = start_angle +
                    (kTwoPiFloat - fmodf(start_angle - end_angle, kTwoPiFloat));
  } else if (anticlockwise && start_angle < end_angle) {
    new_end_angle = start_angle -
                    (kTwoPiFloat - fmodf(end_angle - start_angle, kTwoPiFloat));
  }

  DCHECK(EllipseIsRenderable(start_angle, new_end_angle));
  DCHECK((anticlockwise && (start_angle >= new_end_angle)) ||
         (!anticlockwise && (new_end_angle >= start_angle)));
  return new_end_angle;
}

inline void LineToFloatPoint(CanvasPath* path, const FloatPoint& p) {
  path->lineTo(p.X(), p.Y());
}

inline FloatPoint GetPointOnEllipse(float radius_x,
                                    float radius_y,
                                    float theta) {
  return FloatPoint(radius_x * cosf(theta), radius_y * sinf(theta));
}

void CanonicalizeAngle(float* start_angle, float* end_angle) {
  // Make 0 <= startAngle < 2*PI
  float new_start_angle = fmodf(*start_angle, kTwoPiFloat);

  if (new_start_angle < 0) {
    new_start_angle += kTwoPiFloat;
    // Check for possible catastrophic cancellation in cases where
    // newStartAngle was a tiny negative number (c.f. crbug.com/503422)
    if (new_start_angle >= kTwoPiFloat)
      new_start_angle -= kTwoPiFloat;
  }

  float delta = new_start_angle - *start_angle;
  *start_angle = new_start_angle;
  *end_angle = *end_angle + delta;

  DCHECK_GE(new_start_angle, 0);
  DCHECK_LT(new_start_angle, kTwoPiFloat);
}

/*
 * degenerateEllipse() handles a degenerated ellipse using several lines.
 *
 * Let's see a following example: line to ellipse to line.
 *        _--^\
 *       (     )
 * -----(      )
 *            )
 *           /--------
 *
 * If radiusX becomes zero, the ellipse of the example is degenerated.
 *         _
 *        // P
 *       //
 * -----//
 *      /
 *     /--------
 *
 * To draw the above example, need to get P that is a local maximum point.
 * Angles for P are 0.5Pi and 1.5Pi in the ellipse coordinates.
 *
 * If radiusY becomes zero, the result is as follows.
 * -----__
 *        --_
 *          ----------
 *            ``P
 * Angles for P are 0 and Pi in the ellipse coordinates.
 *
 * To handle both cases, degenerateEllipse() lines to start angle, local maximum
 * points(every 0.5Pi), and end angle.
 * NOTE: Before ellipse() calls this function, adjustEndAngle() is called, so
 * endAngle - startAngle must be equal to or less than 2Pi.
 */
void DegenerateEllipse(CanvasPath* path,
                       float x,
                       float y,
                       float radius_x,
                       float radius_y,
                       float rotation,
                       float start_angle,
                       float end_angle,
                       bool anticlockwise) {
  DCHECK(EllipseIsRenderable(start_angle, end_angle));
  DCHECK_GE(start_angle, 0);
  DCHECK_LT(start_angle, kTwoPiFloat);
  DCHECK((anticlockwise && (start_angle - end_angle) >= 0) ||
         (!anticlockwise && (end_angle - start_angle) >= 0));

  FloatPoint center(x, y);
  AffineTransform rotation_matrix;
  rotation_matrix.RotateRadians(rotation);
  // First, if the object's path has any subpaths, then the method must add a
  // straight line from the last point in the subpath to the start point of the
  // arc.
  LineToFloatPoint(path, center + rotation_matrix.MapPoint(GetPointOnEllipse(
                                      radius_x, radius_y, start_angle)));
  if ((!radius_x && !radius_y) || start_angle == end_angle)
    return;

  if (!anticlockwise) {
    // start_angle - fmodf(start_angle, kPiOverTwoFloat) + kPiOverTwoFloat is
    // the one of (0, 0.5Pi, Pi, 1.5Pi, 2Pi) that is the closest to start_angle
    // on the clockwise direction.
    for (float angle = start_angle - fmodf(start_angle, kPiOverTwoFloat) +
                       kPiOverTwoFloat;
         angle < end_angle; angle += kPiOverTwoFloat) {
      LineToFloatPoint(
          path, center + rotation_matrix.MapPoint(
                             GetPointOnEllipse(radius_x, radius_y, angle)));
    }
  } else {
    for (float angle = start_angle - fmodf(start_angle, kPiOverTwoFloat);
         angle > end_angle; angle -= kPiOverTwoFloat) {
      LineToFloatPoint(
          path, center + rotation_matrix.MapPoint(
                             GetPointOnEllipse(radius_x, radius_y, angle)));
    }
  }

  LineToFloatPoint(path, center + rotation_matrix.MapPoint(GetPointOnEllipse(
                                      radius_x, radius_y, end_angle)));
}

}  // namespace

void CanvasPath::arc(double double_x,
                     double double_y,
                     double double_radius,
                     double double_start_angle,
                     double double_end_angle,
                     bool anticlockwise,
                     ExceptionState& exception_state) {
  float x = base::saturated_cast<float>(double_x);
  float y = base::saturated_cast<float>(double_y);
  float radius = base::saturated_cast<float>(double_radius);
  float start_angle = base::saturated_cast<float>(double_start_angle);
  float end_angle = base::saturated_cast<float>(double_end_angle);
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius) ||
      !std::isfinite(start_angle) || !std::isfinite(end_angle))
    return;

  if (radius < 0) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kIndexSizeError,
        "The radius provided (" + String::Number(radius) + ") is negative.");
    return;
  }

  if (!IsTransformInvertible())
    return;

  if (!radius || start_angle == end_angle) {
    // The arc is empty but we still need to draw the connecting line.
    lineTo(x + radius * cosf(start_angle), y + radius * sinf(start_angle));
    return;
  }

  CanonicalizeAngle(&start_angle, &end_angle);
  path_.AddArc(FloatPoint(x, y), radius, start_angle,
               AdjustEndAngle(start_angle, end_angle, anticlockwise));
}

void CanvasPath::ellipse(double double_x,
                         double double_y,
                         double double_radius_x,
                         double double_radius_y,
                         double double_rotation,
                         double double_start_angle,
                         double double_end_angle,
                         bool anticlockwise,
                         ExceptionState& exception_state) {
  float x = base::saturated_cast<float>(double_x);
  float y = base::saturated_cast<float>(double_y);
  float radius_x = base::saturated_cast<float>(double_radius_x);
  float radius_y = base::saturated_cast<float>(double_radius_y);
  float rotation = base::saturated_cast<float>(double_rotation);
  float start_angle = base::saturated_cast<float>(double_start_angle);
  float end_angle = base::saturated_cast<float>(double_end_angle);
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius_x) ||
      !std::isfinite(radius_y) || !std::isfinite(rotation) ||
      !std::isfinite(start_angle) || !std::isfinite(end_angle))
    return;

  if (radius_x < 0) {
    exception_state.ThrowDOMException(DOMExceptionCode::kIndexSizeError,
                                      "The major-axis radius provided (" +
                                          String::Number(radius_x) +
                                          ") is negative.");
    return;
  }
  if (radius_y < 0) {
    exception_state.ThrowDOMException(DOMExceptionCode::kIndexSizeError,
                                      "The minor-axis radius provided (" +
                                          String::Number(radius_y) +
                                          ") is negative.");
    return;
  }

  if (!IsTransformInvertible())
    return;

  CanonicalizeAngle(&start_angle, &end_angle);
  float adjusted_end_angle =
      AdjustEndAngle(start_angle, end_angle, anticlockwise);
  if (!radius_x || !radius_y || start_angle == adjusted_end_angle) {
    // The ellipse is empty but we still need to draw the connecting line to
    // start point.
    DegenerateEllipse(this, x, y, radius_x, radius_y, rotation, start_angle,
                      adjusted_end_angle, anticlockwise);
    return;
  }

  path_.AddEllipse(FloatPoint(x, y), radius_x, radius_y, rotation, start_angle,
                   adjusted_end_angle);
}

void CanvasPath::rect(double double_x,
                      double double_y,
                      double double_width,
                      double double_height) {
  float x = base::saturated_cast<float>(double_x);
  float y = base::saturated_cast<float>(double_y);
  float width = base::saturated_cast<float>(double_width);
  float height = base::saturated_cast<float>(double_height);
  if (!IsTransformInvertible())
    return;

  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
      !std::isfinite(height))
    return;

  path_.AddRect(FloatRect(x, y, width, height));
}

void CanvasPath::roundRect(
    double double_x,
    double double_y,
    double double_width,
    double double_height,
    const HeapVector<Member<V8UnionDOMPointOrUnrestrictedDouble>>& radii,
    ExceptionState& exception_state) {
  const int num_radii = radii.size();
  if (num_radii < 1 || num_radii > 4) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kIndexSizeError,
        String::Number(num_radii) +
            " radii provided. Between one and four radii are necessary.");
  }

  float x = base::saturated_cast<float>(double_x);
  float y = base::saturated_cast<float>(double_y);
  float width = base::saturated_cast<float>(double_width);
  float height = base::saturated_cast<float>(double_height);
  if (!IsTransformInvertible())
    return;

  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
      !std::isfinite(height))
    return;

  FloatSize r[num_radii];
  for (int i = 0; i < num_radii; ++i) {
    switch (radii[i]->GetContentType()) {
      case V8UnionDOMPointOrUnrestrictedDouble::ContentType::kDOMPoint: {
        DOMPoint* p = radii[i]->GetAsDOMPoint();
        float r_x = base::saturated_cast<float>(p->x());
        float r_y = base::saturated_cast<float>(p->y());
        if (UNLIKELY(!std::isfinite(r_x)) || UNLIKELY(!std::isfinite(r_y)))
          return;
        if (UNLIKELY(r_x < 0.0f)) {
          exception_state.ThrowDOMException(
              DOMExceptionCode::kIndexSizeError,
              "X-radius value " + String::Number(r_x) + " is negative.");
        }
        if (UNLIKELY(r_y < 0.0f)) {
          exception_state.ThrowDOMException(
              DOMExceptionCode::kIndexSizeError,
              "Y-radius value " + String::Number(r_y) + " is negative.");
        }
        r[i] = FloatSize(base::saturated_cast<float>(p->x()),
                         base::saturated_cast<float>(p->y()));
        break;
      }
      case V8UnionDOMPointOrUnrestrictedDouble::ContentType::
          kUnrestrictedDouble: {
        float a =
            base::saturated_cast<float>(radii[i]->GetAsUnrestrictedDouble());
        if (UNLIKELY(!std::isfinite(a)))
          return;
        if (UNLIKELY(a < 0.0f)) {
          exception_state.ThrowDOMException(
              DOMExceptionCode::kIndexSizeError,
              "Radius value " + String::Number(a) + " is negative.");
        }
        r[i] = FloatSize(a, a);
        break;
      }
    }
  }

  if (UNLIKELY(width == 0) || UNLIKELY(height == 0)) {
    // AddRoundRect does not handle flat rects, correctly.  But since there are
    // no rounded corners on a flat rect, we can just use AddRect.
    path_.AddRect(FloatRect(x, y, width, height));
    return;
  }

  FloatSize corner_radii[4];  // row-wise ordering
  switch (num_radii) {
    case 1:
      corner_radii[0] = corner_radii[1] = corner_radii[2] = corner_radii[3] =
          r[0];
      break;
    case 2:
      corner_radii[0] = corner_radii[3] = r[0];
      corner_radii[1] = corner_radii[2] = r[1];
      break;
    case 3:
      corner_radii[0] = r[0];
      corner_radii[1] = corner_radii[2] = r[1];
      corner_radii[3] = r[2];
      break;
    case 4:
      corner_radii[0] = r[0];
      corner_radii[1] = r[1];
      corner_radii[2] = r[3];
      corner_radii[3] = r[2];
  }

  bool clockwise = true;
  if (UNLIKELY(width < 0)) {
    // Horizontal flip
    clockwise = false;
    x += width;
    width = -width;
    FloatSize tmp = corner_radii[1];
    corner_radii[1] = corner_radii[0];
    corner_radii[0] = tmp;
    tmp = corner_radii[3];
    corner_radii[3] = corner_radii[2];
    corner_radii[2] = tmp;
  }

  if (UNLIKELY(height < 0)) {
    // Vertical flip
    clockwise = !clockwise;
    y += height;
    height = -height;
    FloatSize tmp = corner_radii[2];
    corner_radii[2] = corner_radii[0];
    corner_radii[0] = tmp;
    tmp = corner_radii[3];
    corner_radii[3] = corner_radii[1];
    corner_radii[1] = tmp;
  }

  FloatRect rect(x, y, width, height);
  path_.AddPathForRoundedRect(rect, corner_radii[0], corner_radii[1],
                              corner_radii[2], corner_radii[3], clockwise);
  path_.MoveTo(FloatPoint(x, y));
}

}  // namespace blink
