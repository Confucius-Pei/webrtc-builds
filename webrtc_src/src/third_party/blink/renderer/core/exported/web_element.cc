/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "third_party/blink/public/web/web_element.h"

#include "third_party/blink/renderer/bindings/core/v8/v8_element.h"
#include "third_party/blink/renderer/core/css/css_computed_style_declaration.h"
#include "third_party/blink/renderer/core/css/css_property_names.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/editing/editing_utilities.h"
#include "third_party/blink/renderer/core/html/custom/custom_element.h"
#include "third_party/blink/renderer/core/html/forms/text_control_element.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/graphics/image.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "ui/gfx/geometry/size.h"

namespace blink {

WebElement WebElement::FromV8Value(v8::Local<v8::Value> value) {
  Element* element =
      V8Element::ToImplWithTypeCheck(v8::Isolate::GetCurrent(), value);
  return WebElement(element);
}

bool WebElement::IsFormControlElement() const {
  return ConstUnwrap<Element>()->IsFormControlElement();
}

// TODO(dglazkov): Remove. Consumers of this code should use
// Node:hasEditableStyle.  http://crbug.com/612560
bool WebElement::IsEditable() const {
  const Element* element = ConstUnwrap<Element>();

  element->GetDocument().UpdateStyleAndLayoutTree();
  if (HasEditableStyle(*element))
    return true;

  if (auto* text_control = ToTextControlOrNull(element)) {
    if (!text_control->IsDisabledOrReadOnly())
      return true;
  }

  return EqualIgnoringASCIICase(
      element->FastGetAttribute(html_names::kRoleAttr), "textbox");
}

WebString WebElement::TagName() const {
  return ConstUnwrap<Element>()->tagName();
}

WebString WebElement::GetIdAttribute() const {
  return ConstUnwrap<Element>()->GetIdAttribute();
}

bool WebElement::HasHTMLTagName(const WebString& tag_name) const {
  // How to create                     class              nodeName localName
  // createElement('input')            HTMLInputElement   INPUT    input
  // createElement('INPUT')            HTMLInputElement   INPUT    input
  // createElementNS(xhtmlNS, 'input') HTMLInputElement   INPUT    input
  // createElementNS(xhtmlNS, 'INPUT') HTMLUnknownElement INPUT    INPUT
  const Element* element = ConstUnwrap<Element>();
  return html_names::xhtmlNamespaceURI == element->namespaceURI() &&
         element->localName() == String(tag_name).LowerASCII();
}

bool WebElement::HasAttribute(const WebString& attr_name) const {
  return ConstUnwrap<Element>()->hasAttribute(attr_name);
}

WebString WebElement::GetAttribute(const WebString& attr_name) const {
  return ConstUnwrap<Element>()->getAttribute(attr_name);
}

void WebElement::SetAttribute(const WebString& attr_name,
                              const WebString& attr_value) {
  Unwrap<Element>()->setAttribute(attr_name, attr_value,
                                  IGNORE_EXCEPTION_FOR_TESTING);
}

unsigned WebElement::AttributeCount() const {
  if (!ConstUnwrap<Element>()->hasAttributes())
    return 0;
  return ConstUnwrap<Element>()->Attributes().size();
}

WebString WebElement::AttributeLocalName(unsigned index) const {
  if (index >= AttributeCount())
    return WebString();
  return ConstUnwrap<Element>()->Attributes().at(index).LocalName();
}

WebString WebElement::AttributeValue(unsigned index) const {
  if (index >= AttributeCount())
    return WebString();
  return ConstUnwrap<Element>()->Attributes().at(index).Value();
}

WebString WebElement::TextContent() const {
  return ConstUnwrap<Element>()->textContent();
}

WebString WebElement::InnerHTML() const {
  return ConstUnwrap<Element>()->innerHTML();
}

bool WebElement::IsAutonomousCustomElement() const {
  auto* element = ConstUnwrap<Element>();
  if (element->GetCustomElementState() == CustomElementState::kCustom)
    return CustomElement::IsValidName(element->localName());
  return false;
}

WebNode WebElement::ShadowRoot() const {
  auto* root = ConstUnwrap<Element>()->GetShadowRoot();
  if (!root || root->IsUserAgent())
    return WebNode();
  return WebNode(root);
}

WebElement WebElement::OwnerShadowHost() const {
  if (auto* host = ConstUnwrap<Element>()->OwnerShadowHost()) {
    return WebElement(host);
  }
  return WebElement();
}

WebNode WebElement::OpenOrClosedShadowRoot() {
  if (IsNull())
    return WebNode();

  auto* root = ConstUnwrap<Element>()->AuthorShadowRoot();
  return WebNode(root);
}

gfx::Rect WebElement::BoundsInViewport() const {
  return ConstUnwrap<Element>()->BoundsInViewport();
}

SkBitmap WebElement::ImageContents() {
  Image* image = GetImage();
  if (!image)
    return {};
  return image->AsSkBitmapForCurrentFrame(kRespectImageOrientation);
}

std::vector<uint8_t> WebElement::CopyOfImageData() {
  Image* image = GetImage();
  if (!image || !image->Data())
    return std::vector<uint8_t>();
  return image->Data()->CopyAs<std::vector<uint8_t>>();
}

std::string WebElement::ImageExtension() {
  Image* image = GetImage();
  if (!image)
    return std::string();
  return image->FilenameExtension().Utf8();
}

gfx::Size WebElement::GetImageSize() {
  Image* image = GetImage();
  if (!image)
    return gfx::Size();
  return gfx::Size(image->width(), image->height());
}

WebString WebElement::GetComputedValue(const WebString& property_name) {
  if (IsNull())
    return WebString();

  Element* element = Unwrap<Element>();
  CSSPropertyID property_id = CssPropertyID(
      element->GetDocument().GetExecutionContext(), property_name);
  if (property_id == CSSPropertyID::kInvalid)
    return WebString();

  element->GetDocument().UpdateStyleAndLayoutTree();
  auto* computed_style =
      MakeGarbageCollected<CSSComputedStyleDeclaration>(element);
  return computed_style->GetPropertyCSSValue(property_id)->CssText();
}

WebElement::WebElement(Element* elem) : WebNode(elem) {}

DEFINE_WEB_NODE_TYPE_CASTS(WebElement, IsElementNode())

WebElement& WebElement::operator=(Element* elem) {
  private_ = elem;
  return *this;
}

WebElement::operator Element*() const {
  return blink::To<Element>(private_.Get());
}

Image* WebElement::GetImage() {
  if (IsNull())
    return nullptr;
  return Unwrap<Element>()->ImageContents();
}

}  // namespace blink
