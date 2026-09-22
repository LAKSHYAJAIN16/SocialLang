// Provider adapters: the mock, plus Anthropic, Google Gemini, and an
// OpenAI-compatible adapter that covers OpenAI, xAI, OpenRouter, and local
// servers (Ollama, LM Studio, llama.cpp's server, vLLM) alike. Request and
// response shapes match sociallang/providers/*.py.
#include "providers/provider.h"

#include <chrono>
#include <nlohmann/json.hpp>
#include <thread>

#include "providers/http_win.h"
#include "providers/settings.h"

namespace sl {

using json = nlohmann::json;

// ---- Mock

CompletionResult MockProvider::complete(const CompletionRequest& req) {
  static const char* kPool[] = {
      "I'm not sure yet, let's see how this plays out.",
      "Something about this doesn't add up to me.",
      "I'll go along with the group for now.",
      "Let's hear more before anyone decides anything.",
  };
  std::lock_guard<std::mutex> lock(mu_);
  static const std::string kMarker = "Respond with exactly one of:";
  size_t at = req.user.find(kMarker);
  if (at != std::string::npos) {
    size_t start = req.user.find_first_not_of(" \t", at + kMarker.size());
    size_t end = req.user.find('\n', start);
    std::string list = start == std::string::npos ? "" : req.user.substr(start, end - start);
    std::vector<std::string> options;
    size_t pos = 0;
    while (pos <= list.size()) {
      size_t comma = list.find(',', pos);
      if (comma == std::string::npos) comma = list.size();
      std::string opt = list.substr(pos, comma - pos);
      size_t a = opt.find_first_not_of(" \t"), b = opt.find_last_not_of(" \t");
      if (a != std::string::npos) options.push_back(opt.substr(a, b - a + 1));
      pos = comma + 1;
    }
    if (!options.empty()) return {options[rng_.index(options.size())]};
  }
  return {kPool[rng_.index(4)]};
}

// ---- HTTP adapters

namespace {

bool retryable(int status) { return status == 429 || status == 500 || status == 502 || status == 503 || status == 504 || status == 529; }

// Up to 3 attempts with exponential backoff on 429/5xx and transport errors
// -- sociallang/providers/retry.py's with_backoff.
HttpResponse postWithRetry(const std::string& url, const HttpHeaders& headers, const std::string& body) {
  HttpResponse resp;
  for (int attempt = 0; attempt < 3; ++attempt) {
    resp = httpRequest("POST", url, headers, body);
    bool transient = !resp.error.empty() || retryable(resp.status);
    if (!transient) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000 << attempt));
  }
  return resp;
}

CompletionResult httpFailure(const HttpResponse& r) {
  CompletionResult out;
  if (!r.error.empty()) out.error = "request_failed: " + r.error;
  else out.error = "http_" + std::to_string(r.status) + ": " + r.body.substr(0, 300);
  return out;
}

class AnthropicProvider : public Provider {
 public:
  AnthropicProvider(std::string model, std::string key) : model_(std::move(model)), key_(std::move(key)) {}
  CompletionResult complete(const CompletionRequest& req) override {
    if (key_.empty()) return {"", "missing_api_key: add an Anthropic key in Model Settings"};
    json body = {{"model", model_},
                 {"system", req.system},
                 {"messages", json::array({{{"role", "user"}, {"content", req.user}}})},
                 {"temperature", req.temperature},
                 {"max_tokens", req.maxTokens}};
    auto r = postWithRetry("https://api.anthropic.com/v1/messages",
                           {{"x-api-key", key_}, {"anthropic-version", "2023-06-01"}, {"content-type", "application/json"}},
                           body.dump());
    if (r.status != 200) return httpFailure(r);
    try {
      auto data = json::parse(r.body);
      CompletionResult out;
      for (auto& block : data.value("content", json::array()))
        if (block.value("type", "") == "text") out.text += block.value("text", "");
      out.promptTokens = data["usage"].value("input_tokens", 0);
      out.completionTokens = data["usage"].value("output_tokens", 0);
      return out;
    } catch (const std::exception& e) {
      return {"", std::string("invalid_json_response: ") + e.what()};
    }
  }

 private:
  std::string model_, key_;
};

class OpenAICompatProvider : public Provider {
 public:
  OpenAICompatProvider(std::string model, std::string key, std::string baseUrl, bool keyOptional)
      : model_(std::move(model)), key_(std::move(key)), base_(std::move(baseUrl)), keyOptional_(keyOptional) {
    while (!base_.empty() && base_.back() == '/') base_.pop_back();
  }
  CompletionResult complete(const CompletionRequest& req) override {
    if (key_.empty() && !keyOptional_) return {"", "missing_api_key: add a key for this provider in Model Settings"};
    json body = {{"model", model_},
                 {"messages", json::array({{{"role", "system"}, {"content", req.system}},
                                           {{"role", "user"}, {"content", req.user}}})},
                 {"temperature", req.temperature},
                 {"max_tokens", req.maxTokens}};
    HttpHeaders headers = {{"Content-Type", "application/json"}};
    if (!key_.empty()) headers.emplace_back("Authorization", "Bearer " + key_);
    auto r = postWithRetry(base_ + "/chat/completions", headers, body.dump());
    if (r.status != 200) return httpFailure(r);
    try {
      auto data = json::parse(r.body);
      CompletionResult out;
      auto& content = data["choices"][0]["message"]["content"];
      out.text = content.is_string() ? content.get<std::string>() : "";
      if (data.contains("usage") && data["usage"].is_object()) {
        out.promptTokens = data["usage"].value("prompt_tokens", 0);
        out.completionTokens = data["usage"].value("completion_tokens", 0);
      }
      // Reasoning models can spend the whole budget invisibly and return "".
      if (out.text.empty()) out.error = "empty_completion";
      return out;
    } catch (const std::exception& e) {
      return {"", std::string("unexpected_response_shape: ") + e.what()};
    }
  }

 private:
  std::string model_, key_, base_;
  bool keyOptional_;
};

class GoogleProvider : public Provider {
 public:
  GoogleProvider(std::string model, std::string key) : model_(std::move(model)), key_(std::move(key)) {}
  CompletionResult complete(const CompletionRequest& req) override {
    if (key_.empty()) return {"", "missing_api_key: add a Gemini key in Model Settings"};
    json body = {{"system_instruction", {{"parts", json::array({{{"text", req.system}}})}}},
                 {"contents", json::array({{{"role", "user"}, {"parts", json::array({{{"text", req.user}}})}}})},
                 {"generationConfig", {{"temperature", req.temperature}, {"maxOutputTokens", req.maxTokens}}}};
    // Key in a header rather than the query string, so it never lands in a
    // proxy or server access log.
    auto r = postWithRetry("https://generativelanguage.googleapis.com/v1beta/models/" + model_ + ":generateContent",
                           {{"Content-Type", "application/json"}, {"x-goog-api-key", key_}}, body.dump());
    if (r.status != 200) return httpFailure(r);
    try {
      auto data = json::parse(r.body);
      CompletionResult out;
      for (auto& part : data["candidates"][0]["content"].value("parts", json::array()))
        out.text += part.value("text", "");
      if (data.contains("usageMetadata")) {
        out.promptTokens = data["usageMetadata"].value("promptTokenCount", 0);
        out.completionTokens = data["usageMetadata"].value("candidatesTokenCount", 0);
      }
      return out;
    } catch (const std::exception& e) {
      return {"", std::string("unexpected_response_shape: ") + e.what()};
    }
  }

 private:
  std::string model_, key_;
};

}  // namespace

std::shared_ptr<Provider> makeProvider(const ModelEntry& entry, const Settings& settings, uint32_t seed) {
  std::shared_ptr<Provider> p;
  std::string key = settings.keyFor(entry.kind);
  const ProviderInfo& info = providerInfo(entry.kind);
  switch (entry.kind) {
    case ProviderKind::Mock: p = std::make_shared<MockProvider>(seed); break;
    case ProviderKind::Anthropic: p = std::make_shared<AnthropicProvider>(entry.modelId, key); break;
    case ProviderKind::Google: p = std::make_shared<GoogleProvider>(entry.modelId, key); break;
    case ProviderKind::OpenAI:
    case ProviderKind::XAI:
    case ProviderKind::OpenRouter:
      p = std::make_shared<OpenAICompatProvider>(entry.modelId, key, info.defaultBase, false);
      break;
    case ProviderKind::Local:
      p = std::make_shared<OpenAICompatProvider>(entry.modelId, key,
                                                 entry.baseUrl.empty() ? info.defaultBase : entry.baseUrl, true);
      break;
    default: p = std::make_shared<MockProvider>(seed); break;
  }
  p->label = entry.name.empty() ? entry.modelId : entry.name;
  return p;
}

}  // namespace sl
