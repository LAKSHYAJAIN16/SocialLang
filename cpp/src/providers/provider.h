// LLM provider adapters -- the C++ counterpart of sociallang/providers/. One
// stateless single-turn call per agent turn: (system prompt, user prompt) in,
// text out, errors reported in-band rather than thrown so one failed call
// never kills a whole simulation.
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "engine/rng.h"

namespace sl {

struct CompletionRequest {
  std::string system;
  std::string user;
  double temperature = 0.9;
  int maxTokens = 500;
};

struct CompletionResult {
  std::string text;
  std::string error;  // empty on success
  int promptTokens = 0;
  int completionTokens = 0;
};

class Provider {
 public:
  virtual ~Provider() = default;
  virtual CompletionResult complete(const CompletionRequest& req) = 0;

  // Whether the user prompt needs the agent's memory context. Building it
  // means retrieving and rendering every visible event -- the single biggest
  // cost of a turn -- so the mock, which never reads it, opts out.
  virtual bool wantsContext() const { return true; }

  // Whether calls are slow network round trips worth issuing concurrently
  // (ask_all / ask_choice_all fan out across a thread pool when true).
  virtual bool isRemote() const { return true; }

  std::string label;  // shown in the Inspector and in error logs
};

// Answers "Respond with exactly one of: a, b, c" prompts with a random option
// and everything else from a small pool of noncommittal lines -- the
// reference's mock_provider.py, but seeded so a run is reproducible.
class MockProvider : public Provider {
 public:
  // readsContext = true makes the mock request full memory contexts like a
  // real provider would -- for benchmarking prompt construction offline.
  explicit MockProvider(uint32_t seed, bool readsContext = false)
      : readsContext_(readsContext), rng_(seed ^ 0x5bd1e995u) {
    label = "Mock";
  }
  CompletionResult complete(const CompletionRequest& req) override;
  bool wantsContext() const override { return readsContext_; }
  bool isRemote() const override { return false; }

 private:
  bool readsContext_;
  std::mutex mu_;
  SeededRandom rng_;
};

struct ModelEntry;
struct Settings;

// Builds the HTTP-backed provider for one roster entry (Anthropic, OpenAI,
// Google, xAI, OpenRouter, or a local OpenAI-compatible server).
std::shared_ptr<Provider> makeProvider(const ModelEntry& entry, const Settings& settings, uint32_t seed);

}  // namespace sl
