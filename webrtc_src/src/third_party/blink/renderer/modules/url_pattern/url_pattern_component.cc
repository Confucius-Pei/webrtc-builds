// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/url_pattern/url_pattern_component.h"

#include "base/strings/string_util.h"
#include "third_party/blink/renderer/modules/url_pattern/url_pattern_canon.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/wtf/text/string_utf8_adaptor.h"
#include "url/url_util.h"

namespace blink {
namespace url_pattern {

namespace {

// Utility method to convert a type to a string.
StringView TypeToString(Component::Type type) {
  switch (type) {
    case Component::Type::kProtocol:
      return "protocol";
    case Component::Type::kUsername:
      return "username";
    case Component::Type::kPassword:
      return "password";
    case Component::Type::kHostname:
      return "hostname";
    case Component::Type::kPort:
      return "port";
    case Component::Type::kPathname:
      return "pathname";
    case Component::Type::kSearch:
      return "search";
    case Component::Type::kHash:
      return "hash";
  }
  NOTREACHED();
}

// Utility method to get the correct encoding callback for a given type.
liburlpattern::EncodeCallback GetEncodeCallback(Component::Type type,
                                                Component* protocol_component) {
  switch (type) {
    case Component::Type::kProtocol:
      return ProtocolEncodeCallback;
    case Component::Type::kUsername:
      return UsernameEncodeCallback;
    case Component::Type::kPassword:
      return PasswordEncodeCallback;
    case Component::Type::kHostname:
      return HostnameEncodeCallback;
    case Component::Type::kPort:
      return PortEncodeCallback;
    case Component::Type::kPathname:
      // Different types of URLs use different canonicalization for pathname.
      // A "standard" URL flattens `.`/`..` and performs full percent encoding.
      // A "path" URL does not flatten and uses a more lax percent encoding.
      // The spec calls "path" URLs as "cannot-be-a-base-URL" URLs:
      //
      //  https://url.spec.whatwg.org/#cannot-be-a-base-url-path-state
      //
      // We prefer "standard" URL here by checking to see if the protocol
      // pattern matches any of the known standard protocol strings.  So
      // an exact pattern of `http` will match, but so will `http{s}?` and
      // `*`.
      //
      // If the protocol pattern does not match any of the known standard URL
      // protocols then we fall back to the "path" URL behavior.  This will
      // normally be triggered by `data`, `javascript`, `about`, etc.  It
      // will also be triggered for custom protocol strings.  We favor "path"
      // behavior here because its better to under canonicalize since the
      // developer can always manually canonicalize the pathname for a custom
      // protocol.
      //
      // ShouldTreatAsStandardURL can by a bit expensive, so only do it if we
      // actually have a pathname pattern to compile.
      CHECK(protocol_component);
      if (protocol_component->ShouldTreatAsStandardURL())
        return StandardURLPathnameEncodeCallback;
      else
        return PathURLPathnameEncodeCallback;
    case Component::Type::kSearch:
      return SearchEncodeCallback;
    case Component::Type::kHash:
      return HashEncodeCallback;
  }
  NOTREACHED();
}

// Utility method to get the correct liburlpattern parse options for a given
// type.
const liburlpattern::Options& GetOptions(Component::Type type) {
  // The liburlpattern::Options to use for most component patterns.  We
  // default to strict mode and case sensitivity.  In addition, most
  // components have no concept of a delimiter or prefix character.
  DEFINE_THREAD_SAFE_STATIC_LOCAL(liburlpattern::Options, default_options,
                                  ({.delimiter_list = "",
                                    .prefix_list = "",
                                    .sensitive = true,
                                    .strict = true}));

  // The liburlpattern::Options to use for hostname patterns.  This uses a
  // "." delimiter controlling how far a named group like ":bar" will match
  // by default.  Note, hostnames are case insensitive but we require case
  // sensitivity here.  This assumes that the hostname values have already
  // been normalized to lower case as in URL().
  DEFINE_THREAD_SAFE_STATIC_LOCAL(liburlpattern::Options, hostname_options,
                                  ({.delimiter_list = ".",
                                    .prefix_list = "",
                                    .sensitive = true,
                                    .strict = true}));

  // The liburlpattern::Options to use for pathname patterns.  This uses a
  // "/" delimiter controlling how far a named group like ":bar" will match
  // by default.  It also configures "/" to be treated as an automatic
  // prefix before groups.
  DEFINE_THREAD_SAFE_STATIC_LOCAL(liburlpattern::Options, pathname_options,
                                  ({.delimiter_list = "/",
                                    .prefix_list = "/",
                                    .sensitive = true,
                                    .strict = true}));

  switch (type) {
    case Component::Type::kHostname:
      return hostname_options;
    case Component::Type::kPathname:
      return pathname_options;
    case Component::Type::kProtocol:
    case Component::Type::kUsername:
    case Component::Type::kPassword:
    case Component::Type::kPort:
    case Component::Type::kSearch:
    case Component::Type::kHash:
      return default_options;
  }
  NOTREACHED();
}

}  // anonymous namespace

// static
Component* Component::Compile(const String& pattern,
                              Type type,
                              Component* protocol_component,
                              ExceptionState& exception_state) {
  // If the pattern is null then return a special Component object that matches
  // any input as if the pattern was `*`.
  if (pattern.IsNull()) {
    return MakeGarbageCollected<Component>(type, base::PassKey<Component>());
  }

  const liburlpattern::Options& options = GetOptions(type);

  // Parse the pattern.
  StringUTF8Adaptor utf8(pattern);
  auto parse_result = liburlpattern::Parse(
      absl::string_view(utf8.data(), utf8.size()),
      GetEncodeCallback(type, protocol_component), options);
  if (!parse_result.ok()) {
    exception_state.ThrowTypeError("Invalid " + TypeToString(type) +
                                   " pattern '" + pattern + "'.");
    return nullptr;
  }

  // Extract a regular expression string from the parsed pattern.
  std::vector<std::string> name_list;
  std::string regexp_string =
      parse_result.value().GenerateRegexString(&name_list);

  // Compile the regular expression to verify it is valid.
  auto case_sensitive = options.sensitive ? WTF::kTextCaseSensitive
                                          : WTF::kTextCaseASCIIInsensitive;
  DCHECK(base::IsStringASCII(regexp_string));
  ScriptRegexp* regexp = MakeGarbageCollected<ScriptRegexp>(
      String(regexp_string.data(), regexp_string.size()), case_sensitive,
      kMultilineDisabled, ScriptRegexp::UTF16);
  if (!regexp->IsValid()) {
    // The regular expression failed to compile.  This means that some
    // custom regexp group within the pattern is illegal.  Attempt to
    // compile each regexp group individually in order to identify the
    // culprit.
    for (auto& part : parse_result.value().PartList()) {
      if (part.type != liburlpattern::PartType::kRegex)
        continue;
      DCHECK(base::IsStringASCII(part.value));
      String group_value(part.value.data(), part.value.size());
      regexp = MakeGarbageCollected<ScriptRegexp>(
          group_value, case_sensitive, kMultilineDisabled, ScriptRegexp::UTF16);
      if (regexp->IsValid())
        continue;
      exception_state.ThrowTypeError("Invalid " + TypeToString(type) +
                                     " pattern '" + pattern +
                                     "'. Custom regular expression group '" +
                                     group_value + "' is invalid.");
      return nullptr;
    }
    // We couldn't find a bad regexp group, but we still have an overall
    // error.  This shouldn't happen, but we handle it anyway.
    exception_state.ThrowTypeError("Invalid " + TypeToString(type) +
                                   " pattern '" + pattern +
                                   "'. An unexpected error has occurred.");
    return nullptr;
  }

  Vector<String> wtf_name_list;
  wtf_name_list.ReserveInitialCapacity(
      static_cast<wtf_size_t>(name_list.size()));
  for (const auto& name : name_list) {
    wtf_name_list.push_back(String::FromUTF8(name.data(), name.size()));
  }

  return MakeGarbageCollected<Component>(
      type, std::move(parse_result.value()), std::move(regexp),
      std::move(wtf_name_list), base::PassKey<Component>());
}

Component::Component(Type type,
                     liburlpattern::Pattern pattern,
                     ScriptRegexp* regexp,
                     Vector<String> name_list,
                     base::PassKey<Component> key)
    : type_(type),
      pattern_(std::move(pattern)),
      regexp_(regexp),
      name_list_(std::move(name_list)) {}

Component::Component(Type type, base::PassKey<Component> key)
    : type_(type), name_list_({"0"}) {}

bool Component::Match(StringView input, Vector<String>* group_list) const {
  if (regexp_) {
    return regexp_->Match(input, /*start_from=*/0, /*match_length=*/nullptr,
                          group_list) == 0;
  } else {
    if (group_list)
      group_list->push_back(input.ToString());
    return true;
  }
}

String Component::GeneratePatternString() const {
  if (pattern_.has_value())
    return String::FromUTF8(pattern_->GeneratePatternString());
  else
    return "*";
}

Vector<std::pair<String, String>> Component::MakeGroupList(
    const Vector<String>& group_values) const {
  DCHECK_EQ(name_list_.size(), group_values.size());
  Vector<std::pair<String, String>> result;
  result.ReserveInitialCapacity(group_values.size());
  for (wtf_size_t i = 0; i < group_values.size(); ++i) {
    result.emplace_back(name_list_[i], group_values[i]);
  }
  return result;
}

bool Component::ShouldTreatAsStandardURL() const {
  DCHECK(type_ == Type::kProtocol);
  if (!pattern_.has_value())
    return true;
  const auto protocol_matches = [&](const std::string& scheme) {
    DCHECK(base::IsStringASCII(scheme));
    return Match(
        StringView(scheme.data(), static_cast<unsigned>(scheme.size())),
        /*group_list=*/nullptr);
  };
  return base::ranges::any_of(url::GetStandardSchemes(), protocol_matches);
}

void Component::Trace(Visitor* visitor) const {
  visitor->Trace(regexp_);
}

}  // namespace url_pattern
}  // namespace blink
