// SPDX-License-Identifier: MIT
// Copyright (c) 2026 dreamingdog0529
// C API wrapper around mozc::SessionHandler, for use by the fcitx5 addon.
// Mirrors the JNI wrapper (android/jni/mozcjni.cc) without the JNI parts:
// the boundary is a serialized mozc::commands::Command protobuf.
#ifdef __ANDROID__

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "base/system_util.h"
#include "data_manager/data_manager.h"
#include "engine/engine.h"
#include "engine/engine_interface.h"
#include "google/protobuf/util/json_util.h"
#include "protocol/commands.pb.h"
#include "session/session_handler.h"

namespace {

std::unique_ptr<mozc::EngineInterface> CreateMobileEngine(
    const std::string& data_file_path) {
  absl::StatusOr<std::unique_ptr<const mozc::DataManager>> data_manager =
      mozc::DataManager::CreateFromFile(data_file_path);
  if (!data_manager.ok()) {
    LOG(ERROR) << "Fallback to minimal engine: " << data_manager.status();
    return mozc::Engine::CreateEngine();
  }
  const std::string data_version =
      std::string((*data_manager)->GetDataVersion());
  auto engine = mozc::Engine::CreateEngine(*std::move(data_manager));
  if (!engine.ok()) {
    LOG(ERROR) << "Failed to create engine, data version " << data_version
               << ": " << engine.status();
    return mozc::Engine::CreateEngine();
  }
  return *std::move(engine);
}

}  // namespace

extern "C" {

// Creates a session handler. profile_dir is used for learning data;
// data_path is the mozc.data system dictionary file. Returns nullptr on
// total failure.
void* mozc_engine_create(const char* profile_dir, const char* data_path) {
  if (profile_dir != nullptr && profile_dir[0] != '\0') {
    mozc::SystemUtil::SetUserProfileDirectory(profile_dir);
  }
  std::unique_ptr<mozc::EngineInterface> engine;
  if (data_path == nullptr || data_path[0] == '\0') {
    engine = mozc::Engine::CreateEngine();
  } else {
    engine = CreateMobileEngine(data_path);
  }
  if (!engine) {
    return nullptr;
  }
  return new mozc::SessionHandler(std::move(engine));
}

void mozc_engine_destroy(void* handle) {
  delete static_cast<mozc::SessionHandler*>(handle);
}

// Evaluates one serialized Command. On success returns 0 and sets
// *out/*out_len to a malloc'd serialized Command (free with
// mozc_engine_free).
int mozc_engine_eval(void* handle, const unsigned char* in, int in_len,
                     unsigned char** out, int* out_len) {
  auto* handler = static_cast<mozc::SessionHandler*>(handle);
  if (handler == nullptr || in == nullptr || out == nullptr ||
      out_len == nullptr) {
    return -1;
  }
  mozc::commands::Command command;
  if (!command.ParseFromArray(in, in_len)) {
    return -2;
  }
  handler->EvalCommand(&command);
  const size_t size = command.ByteSizeLong();
  auto* buf =
      static_cast<unsigned char*>(std::malloc(size == 0 ? 1 : size));
  if (buf == nullptr) {
    return -3;
  }
  command.SerializeToArray(buf, static_cast<int>(size));
  *out = buf;
  *out_len = static_cast<int>(size);
  return 0;
}


// JSON boundary: evaluates one Command given as JSON and returns the
// resulting Command as a malloc'd JSON string (free with
// mozc_engine_free). Lets the fcitx addon avoid linking protobuf itself.
char* mozc_engine_eval_json(void* handle, const char* input_json) {
  auto* handler = static_cast<mozc::SessionHandler*>(handle);
  if (handler == nullptr || input_json == nullptr) {
    return nullptr;
  }
  mozc::commands::Command command;
  auto parse_status = google::protobuf::util::JsonStringToMessage(
      input_json, &command, google::protobuf::util::JsonParseOptions());
  if (!parse_status.ok()) {
    return nullptr;
  }
  handler->EvalCommand(&command);
  std::string out_json;
  google::protobuf::util::JsonPrintOptions print_options;
  print_options.preserve_proto_field_names = true;
  auto print_status = google::protobuf::util::MessageToJsonString(
      command, &out_json, print_options);
  if (!print_status.ok()) {
    return nullptr;
  }
  auto* buf = static_cast<char*>(std::malloc(out_json.size() + 1));
  if (buf == nullptr) {
    return nullptr;
  }
  std::memcpy(buf, out_json.c_str(), out_json.size() + 1);
  return buf;
}

void mozc_engine_free(unsigned char* p) { std::free(p); }
}

#endif  // __ANDROID__
