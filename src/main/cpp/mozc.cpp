/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 dreamingdog0529
 */
/*
 * fcitx5 input method engine backed by an in-process Mozc session.
 * Talks to libmozc_engine.so over its JSON C API: each key event becomes
 * a mozc commands::Command in JSON, the reply drives preedit/candidates.
 */
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <dlfcn.h>

#include "mozc_engine_capi.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;
using namespace fcitx;

FCITX_DEFINE_LOG_CATEGORY(mozc_logcategory, "fcitx-mozc");
#define FCITX_MOZC_DEBUG() FCITX_LOGC(mozc_logcategory, Debug)
#define FCITX_MOZC_WARN() FCITX_LOGC(mozc_logcategory, Warn)

namespace {

uint64_t jsonId(const json &j) {
    if (j.is_string()) {
        try {
            return std::stoull(j.get<std::string>());
        } catch (...) {
            return 0;
        }
    }
    if (j.is_number_unsigned()) return j.get<uint64_t>();
    if (j.is_number_integer()) return static_cast<uint64_t>(j.get<int64_t>());
    return 0;
}

// Map an fcitx key to a mozc KeyEvent JSON object. Returns false when the
// key should not be forwarded at all (modifiers, releases handled by the
// caller, function keys we do not map, ...).
bool toMozcKey(const Key &key, json &out) {
    auto sym = key.sym();
    auto states = key.states();
    if (states.testAny(KeyStates{KeyState::Ctrl, KeyState::Alt, KeyState::Super})) {
        return false;
    }
    auto special = [&out](int code) {
        out["special_key"] = code;
        return true;
    };
    switch (sym) {
    case FcitxKey_space: return special(4);       // SPACE
    case FcitxKey_Return:
    case FcitxKey_KP_Enter: return special(5);    // ENTER
    case FcitxKey_Left: return special(6);
    case FcitxKey_Right: return special(7);
    case FcitxKey_Up: return special(8);
    case FcitxKey_Down: return special(9);
    case FcitxKey_Escape: return special(10);
    case FcitxKey_Delete: return special(11);     // DEL
    case FcitxKey_BackSpace: return special(12);
    case FcitxKey_Home: return special(16);
    case FcitxKey_End: return special(17);
    case FcitxKey_Tab: return special(18);
    case FcitxKey_Page_Up: return special(31);
    case FcitxKey_Page_Down: return special(32);
    default: break;
    }
    if (sym >= 0x20 && sym < 0x7f) {
        // NOTE: send key_code only. With key_string set, mozc takes the
        // text-input path and romaji syllables do not combine (verified
        // against a host build: "kyouha" stayed split with key_string,
        // and converted to a full phrase with key_code only).
        out["key_code"] = static_cast<int>(sym);
        return true;
    }
    return false;
}

} // namespace

// The mozc engine lives in a separate .so (libmozc_engine.so) shipped in
// the same plugin lib directory. It is loaded by absolute path (derived
// from this addon's own location) instead of a DT_NEEDED entry, because
// the host process may not have the plugin lib directory in its linker
// search path.
using MozcEngineCreateFn = void *(*)(const char *, const char *);
using MozcEngineDestroyFn = void (*)(void *);
using MozcEngineEvalJsonFn = char *(*)(void *, const char *);
using MozcEngineFreeFn = void (*)(unsigned char *);

MozcEngineCreateFn g_createFn = nullptr;
MozcEngineDestroyFn g_destroyFn = nullptr;
MozcEngineEvalJsonFn g_evalJsonFn = nullptr;
MozcEngineFreeFn g_freeFn = nullptr;
bool g_engineLoadTried = false;
bool g_engineLoaded = false;

void loadEngineAnchor() {}

bool loadEngine() {
    if (g_engineLoadTried) {
        return g_engineLoaded;
    }
    g_engineLoadTried = true;
    Dl_info info{};
    if (dladdr(reinterpret_cast<void *>(&loadEngineAnchor), &info) == 0 ||
        info.dli_fname == nullptr) {
        FCITX_MOZC_WARN() << "dladdr failed for addon library";
        return false;
    }
    std::string dir =
        std::filesystem::path(info.dli_fname).parent_path().string();
    std::string path = dir + "/libmozc_engine.so";
    void *lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr) {
        FCITX_MOZC_WARN() << "dlopen failed: " << path << ": " << dlerror();
        return false;
    }
    g_createFn = reinterpret_cast<MozcEngineCreateFn>(
        dlsym(lib, "mozc_engine_create"));
    g_destroyFn = reinterpret_cast<MozcEngineDestroyFn>(
        dlsym(lib, "mozc_engine_destroy"));
    g_evalJsonFn = reinterpret_cast<MozcEngineEvalJsonFn>(
        dlsym(lib, "mozc_engine_eval_json"));
    g_freeFn =
        reinterpret_cast<MozcEngineFreeFn>(dlsym(lib, "mozc_engine_free"));
    if (!g_createFn || !g_destroyFn || !g_evalJsonFn || !g_freeFn) {
        FCITX_MOZC_WARN() << "engine symbols missing in " << path;
        return false;
    }
    g_engineLoaded = true;
    return true;
}

class MozcEngine;


class MozcState : public InputContextProperty {
public:
    explicit MozcState(MozcEngine *engine) : engine_(engine) {}

    bool processKey(InputContext *ic, KeyEvent &event);
    void selectCandidate(InputContext *ic, int candidateId);
    void clearSession(InputContext *ic);
    void resetPanel(InputContext *ic);

private:
    bool ensureSession();
    json eval(const json &command);
    void applyOutput(InputContext *ic, const json &output);

    MozcEngine *engine_;
    uint64_t sessionId_ = 0;
    bool hasSession_ = false;
    std::vector<int> candidateIds_;
};

class MozcCandidateWord : public CandidateWord {
public:
    MozcCandidateWord(MozcState *state, int id, Text text)
        : CandidateWord(std::move(text)), state_(state), id_(id) {}
    void select(InputContext *ic) const override {
        state_->selectCandidate(ic, id_);
    }

private:
    MozcState *state_;
    int id_;
};

class MozcEngine : public InputMethodEngineV3 {
public:
    explicit MozcEngine(Instance *instance)
        : instance_(instance), factory_([this](InputContext &ic) {
              return new MozcState(this);
          }) {
        instance_->inputContextManager().registerProperty("mozcState",
                                                          &factory_);
    }

    void keyEvent(const InputMethodEntry & /*entry*/,
                  KeyEvent &keyEvent) override {
        if (keyEvent.isRelease()) {
            return;
        }
        auto *state = keyEvent.inputContext()->propertyFor(&factory_);
        if (state->processKey(keyEvent.inputContext(), keyEvent)) {
            keyEvent.filterAndAccept();
        }
    }

    void reset(const InputMethodEntry & /*entry*/,
               InputContextEvent &event) override {
        auto *state = event.inputContext()->propertyFor(&factory_);
        state->clearSession(event.inputContext());
    }

    void deactivate(const InputMethodEntry & /*entry*/,
                    InputContextEvent &event) override {
        auto *state = event.inputContext()->propertyFor(&factory_);
        state->clearSession(event.inputContext());
    }

    // Lazily creates the shared mozc session handler.
    void *handler() {
        if (!loadEngine()) {
            handlerFailed_ = true;
        }
        if (handler_ == nullptr && !handlerFailed_) {
            // On this Android port, PkgData does not cover the app's
            // merged plugin data; Type::Data finds usr/share/... instead.
            // Verify existence because locate() may return a path whose
            // suffix was mangled (".data" dropped).
            std::string dataPath;
            {
                std::string cands[2] = {
                    StandardPath::global().locate(StandardPath::Type::Data,
                                                  "mozc/mozc.data"),
                    StandardPath::global().locate(
                        StandardPath::Type::PkgData, "mozc/mozc.data")};
                for (const auto &c : cands) {
                    if (c.empty()) {
                        continue;
                    }
                    if (std::filesystem::exists(c)) {
                        dataPath = c;
                        break;
                    }
                    if (std::filesystem::exists(c + ".data")) {
                        dataPath = c + ".data";
                        break;
                    }
                }
                FCITX_MOZC_WARN() << "mozc data resolved: " << dataPath;
            }
            std::string profileDir =
                StandardPath::global().userDirectory(StandardPath::Type::Data);
            if (!profileDir.empty()) {
                profileDir += "/mozc";
                std::error_code ec;
                std::filesystem::create_directories(profileDir, ec);
            }
            handler_ = g_createFn(
                profileDir.empty() ? nullptr : profileDir.c_str(),
                dataPath.empty() ? nullptr : dataPath.c_str());
            if (handler_ == nullptr) {
                handlerFailed_ = true;
                FCITX_MOZC_WARN() << "mozc_engine_create failed";
            } else {
                FCITX_MOZC_DEBUG()
                    << "mozc handler created, data=" << dataPath;
            }
        }
        return handler_;
    }

private:
    Instance *instance_;
    FactoryFor<MozcState> factory_;
    void *handler_ = nullptr;
    bool handlerFailed_ = false;
};

// ---------------------------------------------------------------- MozcState

json MozcState::eval(const json &command) {
    void *h = engine_->handler();
    if (h == nullptr) {
        return json();
    }
    std::string in = command.dump();
    char *out = g_evalJsonFn(h, in.c_str());
    if (out != nullptr) {
        std::string outStr(out);
        auto cursorPos = outStr.find("\"cursor\":");
        std::string cursorVal = "?";
        if (cursorPos != std::string::npos) {
            cursorVal = outStr.substr(cursorPos + 9, 6);
        }
        FCITX_MOZC_WARN()
            << "mozc eval: len=" << outStr.size()
            << " candwin=" << (outStr.find("candidate_window") !=
                               std::string::npos)
            << " result=" << (outStr.find("\"result\"") !=
                              std::string::npos)
            << " cursor=" << cursorVal;
    }
    if (out == nullptr) {
        return json();
    }
    json result = json::parse(out, nullptr, false);
    g_freeFn(reinterpret_cast<unsigned char *>(out));
    if (result.is_discarded()) {
        return json();
    }
    return result;
}

bool MozcState::ensureSession() {
    if (hasSession_) {
        return true;
    }
    json cmd;
    cmd["input"]["type"] = "CREATE_SESSION";
    json reply = eval(cmd);
    if (reply.is_null() || !reply.contains("output")) {
        return false;
    }
    sessionId_ = jsonId(reply["output"].value("id", json(0)));
    if (sessionId_ == 0) {
        return false;
    }
    hasSession_ = true;
    // Mobile sessions start with the IME off on some builds; turn it on.
    json on;
    on["input"]["type"] = "SEND_COMMAND";
    on["input"]["id"] = sessionId_;
    on["input"]["command"]["type"] = "TURN_ON_IME";
    eval(on);
    return true;
}

void MozcState::clearSession(InputContext *ic) {
    if (hasSession_) {
        json cmd;
        cmd["input"]["type"] = "DELETE_SESSION";
        cmd["input"]["id"] = sessionId_;
        eval(cmd);
        hasSession_ = false;
        sessionId_ = 0;
    }
    resetPanel(ic);
}

void MozcState::resetPanel(InputContext *ic) {
    candidateIds_.clear();
    auto &panel = ic->inputPanel();
    panel.reset();
    ic->updatePreedit();
    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void MozcState::applyOutput(InputContext *ic, const json &output) {
    // Commit text first.
    if (output.contains("result")) {
        const auto &result = output["result"];
        std::string value = result.value("value", std::string());
        if (!value.empty()) {
            ic->commitString(value);
        }
    }

    auto &panel = ic->inputPanel();
    panel.reset();
    candidateIds_.clear();

    // Preedit.
    Text preedit;
    bool hasPreedit = false;
    if (output.contains("preedit")) {
        const auto &pre = output["preedit"];
        const json *segments = nullptr;
        if (pre.contains("segment")) {
            segments = &pre["segment"];
        } else if (pre.contains("Segment")) {
            segments = &pre["Segment"];
        }
        std::string allText;
        if (segments != nullptr && segments->is_array()) {
            for (const auto &seg : *segments) {
                std::string value = seg.value("value", std::string());
                allText += value;
                std::string annotation = seg.value("annotation", "NONE");
                TextFormatFlags flags;
                if (annotation == "HIGHLIGHT") {
                    flags |= TextFormatFlag::HighLight;
                } else if (annotation == "UNDERLINE") {
                    flags |= TextFormatFlag::Underline;
                }
                preedit.append(value, flags);
                hasPreedit = true;
            }
        }
        if (pre.contains("cursor")) {
            // mozc's preedit cursor counts characters, but fcitx Text
            // expects a UTF-8 byte offset (the Android frontend converts
            // it back with FormattedText.fromByteCursor). Convert here.
            uint32_t charCursor = pre.value("cursor", 0u);
            size_t bytePos = 0;
            uint32_t counted = 0;
            while (bytePos < allText.size() && counted < charCursor) {
                unsigned char c =
                    static_cast<unsigned char>(allText[bytePos]);
                size_t len = 1;
                if (c >= 0xF0) {
                    len = 4;
                } else if (c >= 0xE0) {
                    len = 3;
                } else if (c >= 0xC0) {
                    len = 2;
                }
                bytePos += len;
                counted++;
            }
            preedit.setCursor(static_cast<int>(bytePos));
        }
    }
    if (hasPreedit) {
        if (ic->capabilityFlags().test(CapabilityFlag::Preedit)) {
            panel.setClientPreedit(preedit);
        } else {
            panel.setPreedit(preedit);
        }
    }

    // Candidates.
    if (output.contains("candidate_window")) {
        const auto &cw = output["candidate_window"];
        const json *candidates = nullptr;
        if (cw.contains("candidate")) {
            candidates = &cw["candidate"];
        } else if (cw.contains("Candidate")) {
            candidates = &cw["Candidate"];
        }
        if (candidates != nullptr && candidates->is_array() &&
            !candidates->empty()) {
            auto list = std::make_unique<CommonCandidateList>();
            list->setPageSize(cw.value("page_size", 9u));
            for (const auto &cand : *candidates) {
                std::string value = cand.value("value", std::string());
                int id = cand.value("id", 0);
                candidateIds_.push_back(id);
                list->append<MozcCandidateWord>(this, id, Text(value));
            }
            if (cw.contains("focused_index")) {
                list->setGlobalCursorIndex(
                    static_cast<int>(cw.value("focused_index", 0u)));
            }
            panel.setCandidateList(std::move(list));
        }
    }

    ic->updatePreedit();
    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
}

bool MozcState::processKey(InputContext *ic, KeyEvent &event) {
    json keyJson;
    if (!toMozcKey(event.key(), keyJson)) {
        return false;
    }
    if (!ensureSession()) {
        return false;
    }
    json cmd;
    cmd["input"]["type"] = "SEND_KEY";
    cmd["input"]["id"] = sessionId_;
    cmd["input"]["key"] = keyJson;
    json reply = eval(cmd);
    if (reply.is_null() || !reply.contains("output")) {
        return false;
    }
    const auto &output = reply["output"];
    bool consumed = output.value("consumed", false);
    bool hasResult = output.contains("result") &&
                     !output["result"].value("value", std::string()).empty();
    bool hasPreedit = output.contains("preedit");
    if (!consumed && !hasResult && !hasPreedit) {
        return false;
    }
    applyOutput(ic, output);
    return true;
}

void MozcState::selectCandidate(InputContext *ic, int candidateId) {
    if (!hasSession_) {
        return;
    }
    json cmd;
    cmd["input"]["type"] = "SEND_COMMAND";
    cmd["input"]["id"] = sessionId_;
    cmd["input"]["command"]["type"] = "SUBMIT_CANDIDATE";
    cmd["input"]["command"]["id"] = candidateId;
    json reply = eval(cmd);
    if (!reply.is_null() && reply.contains("output")) {
        applyOutput(ic, reply["output"]);
    }
}

class MozcEngineFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        return new MozcEngine(manager->instance());
    }
};

FCITX_ADDON_FACTORY(MozcEngineFactory);
