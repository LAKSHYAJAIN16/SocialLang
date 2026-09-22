// Model roster and API keys. Lives in %APPDATA%\SocialSandbox\settings.json;
// API keys are encrypted at rest with Windows DPAPI (tied to the signed-in
// Windows user), never written in plaintext. A key left blank falls back to
// the same environment variables the Python CLI reads (ANTHROPIC_API_KEY, ...).
#pragma once

#include <array>
#include <string>
#include <vector>

namespace sl {

enum class ProviderKind : int { Mock, Anthropic, OpenAI, Google, XAI, OpenRouter, Local, COUNT_ };

struct ProviderInfo {
  const char* name;         // shown in the UI
  const char* envVar;       // API key fallback, or nullptr
  const char* defaultBase;  // OpenAI-compatible base URL, or nullptr
  bool needsKey;
};

const ProviderInfo& providerInfo(ProviderKind kind);

struct ModelEntry {
  std::string name;     // display name, e.g. "Claude Sonnet 5"
  ProviderKind kind = ProviderKind::Mock;
  std::string modelId;  // e.g. "claude-sonnet-5", "llama3.2"
  std::string baseUrl;  // Local only: e.g. http://localhost:11434/v1 (Ollama)
  bool enabled = true;
};

struct Settings {
  std::vector<ModelEntry> roster;
  std::array<std::string, static_cast<size_t>(ProviderKind::COUNT_)> keys;
  int maxConcurrency = 8;

  // The key actually used for `kind`: the saved one, else its env var.
  std::string keyFor(ProviderKind kind) const;
  bool hasKey(ProviderKind kind) const { return !keyFor(kind).empty(); }

  static Settings defaults();
  static Settings load();
  bool save() const;
};

}  // namespace sl
