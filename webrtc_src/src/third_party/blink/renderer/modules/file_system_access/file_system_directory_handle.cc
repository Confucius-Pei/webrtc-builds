// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/file_system_access/file_system_directory_handle.h"

#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/mojom/web_sandbox_flags.mojom-blink.h"
#include "third_party/blink/public/common/browser_interface_broker_proxy.h"
#include "third_party/blink/public/mojom/file_system_access/file_system_access_error.mojom-blink.h"
#include "third_party/blink/public/mojom/file_system_access/file_system_access_manager.mojom-blink.h"
#include "third_party/blink/public/platform/task_type.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_resolver.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_file_system_directory_handle.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_file_system_get_directory_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_file_system_get_file_options.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_file_system_remove_options.h"
#include "third_party/blink/renderer/core/dom/dom_exception.h"
#include "third_party/blink/renderer/core/execution_context/security_context.h"
#include "third_party/blink/renderer/core/fileapi/file_error.h"
#include "third_party/blink/renderer/modules/file_system_access/file_system_access_error.h"
#include "third_party/blink/renderer/modules/file_system_access/file_system_directory_iterator.h"
#include "third_party/blink/renderer/modules/file_system_access/file_system_file_handle.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/weborigin/security_origin.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"

namespace blink {

using mojom::blink::FileSystemAccessErrorPtr;

FileSystemDirectoryHandle::FileSystemDirectoryHandle(
    ExecutionContext* context,
    const String& name,
    mojo::PendingRemote<mojom::blink::FileSystemAccessDirectoryHandle> mojo_ptr)
    : FileSystemHandle(context, name), mojo_ptr_(context) {
  mojo_ptr_.Bind(std::move(mojo_ptr),
                 context->GetTaskRunner(TaskType::kMiscPlatformAPI));
  DCHECK(mojo_ptr_.is_bound());
}

FileSystemDirectoryIterator* FileSystemDirectoryHandle::entries() {
  return MakeGarbageCollected<FileSystemDirectoryIterator>(
      this, FileSystemDirectoryIterator::Mode::kKeyValue,
      GetExecutionContext());
}

FileSystemDirectoryIterator* FileSystemDirectoryHandle::keys() {
  return MakeGarbageCollected<FileSystemDirectoryIterator>(
      this, FileSystemDirectoryIterator::Mode::kKey, GetExecutionContext());
}

FileSystemDirectoryIterator* FileSystemDirectoryHandle::values() {
  return MakeGarbageCollected<FileSystemDirectoryIterator>(
      this, FileSystemDirectoryIterator::Mode::kValue, GetExecutionContext());
}

ScriptPromise FileSystemDirectoryHandle::getFileHandle(
    ScriptState* script_state,
    const String& name,
    const FileSystemGetFileOptions* options) {
  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver>(script_state);
  ScriptPromise result = resolver->Promise();

  mojo_ptr_->GetFile(
      name, options->create(),
      WTF::Bind(
          [](ScriptPromiseResolver* resolver, const String& name,
             FileSystemAccessErrorPtr result,
             mojo::PendingRemote<mojom::blink::FileSystemAccessFileHandle>
                 handle) {
            ExecutionContext* context = resolver->GetExecutionContext();
            if (!context)
              return;
            if (result->status != mojom::blink::FileSystemAccessStatus::kOk) {
              file_system_access_error::Reject(resolver, *result);
              return;
            }
            resolver->Resolve(MakeGarbageCollected<FileSystemFileHandle>(
                context, name, std::move(handle)));
          },
          WrapPersistent(resolver), name));

  return result;
}

ScriptPromise FileSystemDirectoryHandle::getDirectoryHandle(
    ScriptState* script_state,
    const String& name,
    const FileSystemGetDirectoryOptions* options) {
  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver>(script_state);
  ScriptPromise result = resolver->Promise();

  if (!mojo_ptr_.is_bound()) {
    resolver->Reject(MakeGarbageCollected<DOMException>(
        DOMExceptionCode::kInvalidStateError));
    return result;
  }

  mojo_ptr_->GetDirectory(
      name, options->create(),
      WTF::Bind(
          [](ScriptPromiseResolver* resolver, const String& name,
             FileSystemAccessErrorPtr result,
             mojo::PendingRemote<mojom::blink::FileSystemAccessDirectoryHandle>
                 handle) {
            ExecutionContext* context = resolver->GetExecutionContext();
            if (!context)
              return;
            if (result->status != mojom::blink::FileSystemAccessStatus::kOk) {
              file_system_access_error::Reject(resolver, *result);
              return;
            }
            resolver->Resolve(MakeGarbageCollected<FileSystemDirectoryHandle>(
                context, name, std::move(handle)));
          },
          WrapPersistent(resolver), name));

  return result;
}

namespace {

void ReturnDataFunction(const v8::FunctionCallbackInfo<v8::Value>& info) {
  V8SetReturnValue(info, info.Data());
}

}  // namespace

ScriptValue FileSystemDirectoryHandle::getEntries(ScriptState* script_state) {
  auto* iterator = MakeGarbageCollected<FileSystemDirectoryIterator>(
      this, FileSystemDirectoryIterator::Mode::kValue,
      ExecutionContext::From(script_state));
  auto* isolate = script_state->GetIsolate();
  auto context = script_state->GetContext();
  v8::Local<v8::Object> result = v8::Object::New(isolate);
  if (!result
           ->Set(context, v8::Symbol::GetAsyncIterator(isolate),
                 v8::Function::New(context, &ReturnDataFunction,
                                   ToV8(iterator, script_state))
                     .ToLocalChecked())
           .ToChecked()) {
    return ScriptValue();
  }
  return ScriptValue(script_state->GetIsolate(), result);
}

ScriptPromise FileSystemDirectoryHandle::removeEntry(
    ScriptState* script_state,
    const String& name,
    const FileSystemRemoveOptions* options) {
  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver>(script_state);
  ScriptPromise result = resolver->Promise();

  if (!mojo_ptr_.is_bound()) {
    resolver->Reject(MakeGarbageCollected<DOMException>(
        DOMExceptionCode::kInvalidStateError));
    return result;
  }

  mojo_ptr_->RemoveEntry(
      name, options->recursive(),
      WTF::Bind(
          [](ScriptPromiseResolver* resolver, FileSystemAccessErrorPtr result) {
            file_system_access_error::ResolveOrReject(resolver, *result);
          },
          WrapPersistent(resolver)));

  return result;
}

ScriptPromise FileSystemDirectoryHandle::resolve(
    ScriptState* script_state,
    FileSystemHandle* possible_child) {
  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver>(script_state);
  ScriptPromise result = resolver->Promise();

  if (!mojo_ptr_.is_bound()) {
    resolver->Reject(MakeGarbageCollected<DOMException>(
        DOMExceptionCode::kInvalidStateError));
    return result;
  }

  mojo_ptr_->Resolve(
      possible_child->Transfer(),
      WTF::Bind(
          [](ScriptPromiseResolver* resolver, FileSystemAccessErrorPtr result,
             const absl::optional<Vector<String>>& path) {
            if (result->status != mojom::blink::FileSystemAccessStatus::kOk) {
              file_system_access_error::Reject(resolver, *result);
              return;
            }
            if (!path.has_value()) {
              resolver->Resolve(static_cast<ScriptWrappable*>(nullptr));
              return;
            }
            resolver->Resolve(*path);
          },
          WrapPersistent(resolver)));

  return result;
}

mojo::PendingRemote<mojom::blink::FileSystemAccessTransferToken>
FileSystemDirectoryHandle::Transfer() {
  mojo::PendingRemote<mojom::blink::FileSystemAccessTransferToken> result;
  if (mojo_ptr_.is_bound())
    mojo_ptr_->Transfer(result.InitWithNewPipeAndPassReceiver());
  return result;
}

void FileSystemDirectoryHandle::Trace(Visitor* visitor) const {
  visitor->Trace(mojo_ptr_);
  FileSystemHandle::Trace(visitor);
}

void FileSystemDirectoryHandle::QueryPermissionImpl(
    bool writable,
    base::OnceCallback<void(mojom::blink::PermissionStatus)> callback) {
  if (!mojo_ptr_.is_bound()) {
    std::move(callback).Run(mojom::blink::PermissionStatus::DENIED);
    return;
  }
  mojo_ptr_->GetPermissionStatus(writable, std::move(callback));
}

void FileSystemDirectoryHandle::RequestPermissionImpl(
    bool writable,
    base::OnceCallback<void(mojom::blink::FileSystemAccessErrorPtr,
                            mojom::blink::PermissionStatus)> callback) {
  if (!mojo_ptr_.is_bound()) {
    std::move(callback).Run(
        mojom::blink::FileSystemAccessError::New(
            mojom::blink::FileSystemAccessStatus::kInvalidState,
            base::File::Error::FILE_ERROR_FAILED, "Context Destroyed"),
        mojom::blink::PermissionStatus::DENIED);
    return;
  }

  mojo_ptr_->RequestPermission(writable, std::move(callback));
}

void FileSystemDirectoryHandle::IsSameEntryImpl(
    mojo::PendingRemote<mojom::blink::FileSystemAccessTransferToken> other,
    base::OnceCallback<void(mojom::blink::FileSystemAccessErrorPtr, bool)>
        callback) {
  if (!mojo_ptr_.is_bound()) {
    std::move(callback).Run(
        mojom::blink::FileSystemAccessError::New(
            mojom::blink::FileSystemAccessStatus::kInvalidState,
            base::File::Error::FILE_ERROR_FAILED, "Context Destroyed"),
        false);
    return;
  }

  mojo_ptr_->Resolve(
      std::move(other),
      WTF::Bind(
          [](base::OnceCallback<void(mojom::blink::FileSystemAccessErrorPtr,
                                     bool)> callback,
             FileSystemAccessErrorPtr result,
             const absl::optional<Vector<String>>& path) {
            std::move(callback).Run(std::move(result),
                                    path.has_value() && path->IsEmpty());
          },
          std::move(callback)));
}

}  // namespace blink
