// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_MEDIA_WEB_CONTENT_DECRYPTION_MODULE_ACCESS_IMPL_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_MEDIA_WEB_CONTENT_DECRYPTION_MODULE_ACCESS_IMPL_H_

#include <memory>

#include "base/memory/weak_ptr.h"
#include "media/base/cdm_config.h"
#include "third_party/blink/public/platform/web_content_decryption_module_access.h"
#include "third_party/blink/public/platform/web_content_decryption_module_result.h"
#include "third_party/blink/public/platform/web_media_key_system_configuration.h"
#include "third_party/blink/public/platform/web_security_origin.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/renderer/platform/platform_export.h"

namespace media {

class WebEncryptedMediaClientImpl;

class PLATFORM_EXPORT WebContentDecryptionModuleAccessImpl
    : public blink::WebContentDecryptionModuleAccess {
 public:
  // Allow typecasting from blink type as this is the only implementation.
  static WebContentDecryptionModuleAccessImpl* From(
      blink::WebContentDecryptionModuleAccess* cdm_access);

  static std::unique_ptr<WebContentDecryptionModuleAccessImpl> Create(
      const blink::WebString& key_system,
      const blink::WebSecurityOrigin& security_origin,
      const blink::WebMediaKeySystemConfiguration& configuration,
      const CdmConfig& cdm_config,
      const base::WeakPtr<WebEncryptedMediaClientImpl>& client);
  WebContentDecryptionModuleAccessImpl(
      const blink::WebString& key_system,
      const blink::WebSecurityOrigin& security_origin,
      const blink::WebMediaKeySystemConfiguration& configuration,
      const CdmConfig& cdm_config,
      const base::WeakPtr<WebEncryptedMediaClientImpl>& client);
  WebContentDecryptionModuleAccessImpl(
      const WebContentDecryptionModuleAccessImpl&) = delete;
  WebContentDecryptionModuleAccessImpl& operator=(
      const WebContentDecryptionModuleAccessImpl&) = delete;
  ~WebContentDecryptionModuleAccessImpl() override;

  // blink::WebContentDecryptionModuleAccess interface.
  blink::WebString GetKeySystem() override;
  blink::WebMediaKeySystemConfiguration GetConfiguration() override;
  void CreateContentDecryptionModule(
      blink::WebContentDecryptionModuleResult result,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner) override;
  bool UseHardwareSecureCodecs() const override;

 private:
  const blink::WebString key_system_;
  const blink::WebSecurityOrigin security_origin_;
  const blink::WebMediaKeySystemConfiguration configuration_;
  const CdmConfig cdm_config_;

  // Keep a WeakPtr as client is owned by render_frame_impl.
  base::WeakPtr<WebEncryptedMediaClientImpl> client_;
};

}  // namespace media

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_MEDIA_WEB_CONTENT_DECRYPTION_MODULE_ACCESS_IMPL_H_
