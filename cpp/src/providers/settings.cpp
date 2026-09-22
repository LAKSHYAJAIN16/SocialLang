#include "providers/settings.h"

#include <windows.h>
#include <wincrypt.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace sl {

using json = nlohmann::json;
namespace fs = std::filesystem;

const ProviderInfo& providerInfo(ProviderKind kind) {
  static const ProviderInfo kInfo[] = {
      {"Mock (no LLM)", nullptr, nullptr, false},
      {"Anthropic", "ANTHROPIC_API_KEY", nullptr, true},
      {"OpenAI", "OPENAI_API_KEY", "https://api.openai.com/v1", true},
      {"Google Gemini", "GEMINI_API_KEY", nullptr, true},
      {"xAI", "XAI_API_KEY", "https://api.x.ai/v1", true},
      {"OpenRouter", "OPENROUTER_API_KEY", "https://openrouter.ai/api/v1", true},
      {"Local (OpenAI-compatible)", nullptr, "http://localhost:11434/v1", false},
  };
  return kInfo[static_cast<int>(kind)];
}

namespace {

fs::path settingsPath() {
  const char* appdata = std::getenv("APPDATA");
  fs::path dir = appdata ? fs::path(appdata) / "SocialSandbox" : fs::path(".");
  return dir / "settings.json";
}

// DPAPI: the ciphertext only decrypts for the same Windows user on the same
// machine, so a copied settings.json leaks nothing.
std::string protect(const std::string& plain) {
  if (plain.empty()) return "";
  DATA_BLOB in{static_cast<DWORD>(plain.size()), (BYTE*)plain.data()}, out{};
  if (!CryptProtectData(&in, L"SocialSandbox API key", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
    return "";
  DWORD len = 0;
  CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &len);
  std::string b64(len, '\0');
  CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64.data(), &len);
  LocalFree(out.pbData);
  b64.resize(len);
  return b64;
}

std::string unprotect(const std::string& b64) {
  if (b64.empty()) return "";
  DWORD len = 0;
  if (!CryptStringToBinaryA(b64.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &len, nullptr, nullptr)) return "";
  std::string bin(len, '\0');
  CryptStringToBinaryA(b64.c_str(), 0, CRYPT_STRING_BASE64, (BYTE*)bin.data(), &len, nullptr, nullptr);
  DATA_BLOB in{len, (BYTE*)bin.data()}, out{};
  if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return "";
  std::string plain(reinterpret_cast<char*>(out.pbData), out.cbData);
  SecureZeroMemory(out.pbData, out.cbData);
  LocalFree(out.pbData);
  return plain;
}

const char* kindKey(ProviderKind k) {
  static const char* kKeys[] = {"mock", "anthropic", "openai", "google", "xai", "openrouter", "local"};
  return kKeys[static_cast<int>(k)];
}

ProviderKind kindFromKey(const std::string& s) {
  for (int i = 0; i < static_cast<int>(ProviderKind::COUNT_); ++i)
    if (s == kindKey(static_cast<ProviderKind>(i))) return static_cast<ProviderKind>(i);
  return ProviderKind::Mock;
}

}  // namespace

std::string Settings::keyFor(ProviderKind kind) const {
  const std::string& saved = keys[static_cast<size_t>(kind)];
  if (!saved.empty()) return saved;
  const char* env = providerInfo(kind).envVar;
  const char* v = env ? std::getenv(env) : nullptr;
  return v ? v : "";
}

Settings Settings::defaults() {
  Settings s;
  // Only the mock is on out of the box -- a fresh install never spends money
  // or needs a network. The rest are starting points to enable.
  s.roster = {
      {"Mock", ProviderKind::Mock, "mock", "", true},
      {"Claude Sonnet 5", ProviderKind::Anthropic, "claude-sonnet-5", "", false},
      {"Claude Haiku 4.5", ProviderKind::Anthropic, "claude-haiku-4-5-20251001", "", false},
      {"GPT-5.4 Mini", ProviderKind::OpenAI, "gpt-5.4-mini", "", false},
      {"Gemini 3.6 Flash", ProviderKind::Google, "gemini-3.6-flash", "", false},
      {"Ollama: llama3.2", ProviderKind::Local, "llama3.2", "http://localhost:11434/v1", false},
      {"LM Studio", ProviderKind::Local, "local-model", "http://localhost:1234/v1", false},
  };
  return s;
}

Settings Settings::load() {
  std::ifstream in(settingsPath());
  if (!in) return defaults();
  try {
    json j = json::parse(in);
    Settings s;
    for (auto& m : j.value("roster", json::array())) {
      ModelEntry e;
      e.name = m.value("name", "");
      e.kind = kindFromKey(m.value("kind", "mock"));
      e.modelId = m.value("modelId", "");
      e.baseUrl = m.value("baseUrl", "");
      e.enabled = m.value("enabled", true);
      s.roster.push_back(std::move(e));
    }
    if (j.contains("keys"))
      for (int i = 0; i < static_cast<int>(ProviderKind::COUNT_); ++i)
        s.keys[i] = unprotect(j["keys"].value(kindKey(static_cast<ProviderKind>(i)), ""));
    s.maxConcurrency = j.value("maxConcurrency", 8);
    if (s.roster.empty()) s.roster = defaults().roster;
    return s;
  } catch (...) {
    return defaults();
  }
}

bool Settings::save() const {
  json j;
  j["roster"] = json::array();
  for (auto& e : roster)
    j["roster"].push_back(
        {{"name", e.name}, {"kind", kindKey(e.kind)}, {"modelId", e.modelId}, {"baseUrl", e.baseUrl}, {"enabled", e.enabled}});
  j["keys"] = json::object();
  for (int i = 0; i < static_cast<int>(ProviderKind::COUNT_); ++i)
    if (!keys[i].empty()) j["keys"][kindKey(static_cast<ProviderKind>(i))] = protect(keys[i]);
  j["maxConcurrency"] = maxConcurrency;
  std::error_code ec;
  fs::create_directories(settingsPath().parent_path(), ec);
  std::ofstream out(settingsPath());
  if (!out) return false;
  out << j.dump(2);
  return static_cast<bool>(out);
}

}  // namespace sl
