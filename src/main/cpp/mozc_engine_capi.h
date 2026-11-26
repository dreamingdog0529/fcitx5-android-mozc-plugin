// SPDX-License-Identifier: MIT
// Copyright (c) 2026 dreamingdog0529
// C API of libmozc_engine.so (built from mozc with bazel).
#pragma once

extern "C" {
void* mozc_engine_create(const char* profile_dir, const char* data_path);
void mozc_engine_destroy(void* handle);
int mozc_engine_eval(void* handle, const unsigned char* in, int in_len,
                     unsigned char** out, int* out_len);
char* mozc_engine_eval_json(void* handle, const char* input_json);
void mozc_engine_free(unsigned char* p);
}
