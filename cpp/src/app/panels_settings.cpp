// Model Settings: which models play, API keys, and local LLM servers. Edits
// go to a draft and only take effect on Save (and the next load or reset,
// since a running sim keeps the roster it started with).
#include <thread>

#include "app/editor.h"
#include "providers/provider.h"

namespace app {

namespace {

using sl::ProviderKind;

const char* kKindNames[] = {"Mock (no LLM)", "Anthropic", "OpenAI", "Google Gemini", "xAI", "OpenRouter",
                            "Local (Ollama / LM Studio)"};

std::future<std::string> startTest(const sl::ModelEntry& entry, const sl::Settings& settings) {
  return std::async(std::launch::async, [entry, settings] {
    auto provider = sl::makeProvider(entry, settings, 1);
    sl::CompletionRequest req;
    req.system = "You are a connectivity check.";
    req.user = "Reply with exactly: OK";
    req.maxTokens = 16;
    req.temperature = 0;
    auto r = provider->complete(req);
    if (!r.error.empty()) return "Failed: " + r.error;
    std::string text = r.text.substr(0, 60);
    return "Connected: \"" + text + "\"";
  });
}

}  // namespace

void drawModelSettings(EditorState& ed) {
  if (!ed.showSettings) return;
  ImGui::SetNextWindowSize(ImVec2(1040, 660), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
  if (!ImGui::Begin("Model Settings", &ed.showSettings, ImGuiWindowFlags_NoDocking)) {
    ImGui::End();
    return;
  }
  sl::Settings& s = ed.settingsDraft;
  if (ed.modelTests.size() != s.roster.size()) ed.modelTests.resize(s.roster.size());

  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled(
      "Every enabled model below is dealt to agents at random when a game loads. With none enabled, the "
      "mock answers every question randomly -- free and offline, but not real behavior.");
  ImGui::PopTextWrapPos();

  sectionHeader(ed, "Models");
  ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp |
                       ImGuiTableFlags_PadOuterX;
  int removeAt = -1;
  if (ImGui::BeginTable("##roster", 6, tf)) {
    ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 28);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("Provider", ImGuiTableColumnFlags_WidthStretch, 1.3f);
    ImGui::TableSetupColumn("Model ID", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn("Server URL", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 118);
    ImGui::TableHeadersRow();
    for (size_t i = 0; i < s.roster.size(); ++i) {
      sl::ModelEntry& m = s.roster[i];
      ImGui::PushID(static_cast<int>(i));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Checkbox("##on", &m.enabled);
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputText("##name", &m.name);
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      int kind = static_cast<int>(m.kind);
      if (ImGui::Combo("##kind", &kind, kKindNames, IM_ARRAYSIZE(kKindNames))) {
        m.kind = static_cast<ProviderKind>(kind);
        if (m.kind == ProviderKind::Local && m.baseUrl.empty()) m.baseUrl = "http://localhost:11434/v1";
      }
      ImGui::TableNextColumn();
      if (m.kind == ProviderKind::Mock) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("-");
      } else {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##model", "e.g. claude-sonnet-5", &m.modelId);
      }
      ImGui::TableNextColumn();
      if (m.kind == ProviderKind::Local) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##url", "http://localhost:11434/v1", &m.baseUrl);
      } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled(m.kind == ProviderKind::Mock ? "-" : "provider default");
      }
      ImGui::TableNextColumn();
      ModelTest& t = ed.modelTests[i];
      if (t.running && t.result.valid() &&
          t.result.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        t.text = t.result.get();
        t.running = false;
      }
      ImGui::BeginDisabled(t.running || m.kind == ProviderKind::Mock);
      if (ImGui::SmallButton(t.running ? "Testing..." : "Test")) {
        t.result = startTest(m, s);
        t.running = true;
        t.text.clear();
      }
      ImGui::EndDisabled();
      ImGui::SetItemTooltip("Send one tiny request to check the key / server works");
      ImGui::SameLine();
      if (ImGui::SmallButton("Remove")) removeAt = static_cast<int>(i);
      if (!t.text.empty()) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        bool ok = t.text.rfind("Connected", 0) == 0;
        ImGui::PushStyleColor(ImGuiCol_Text, ok ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                                                : ImGui::ColorConvertU32ToFloat4(ed.pal.error));
        ImGui::TextWrapped("%s", t.text.c_str());
        ImGui::PopStyleColor();
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (removeAt >= 0) {
    s.roster.erase(s.roster.begin() + removeAt);
    ed.modelTests.erase(ed.modelTests.begin() + removeAt);
  }
  if (ImGui::Button("+ Add Model")) s.roster.push_back({"New model", ProviderKind::Anthropic, "", "", false});
  ImGui::SameLine();
  if (ImGui::Button("+ Add Local LLM"))
    s.roster.push_back({"Ollama: llama3.2", ProviderKind::Local, "llama3.2", "http://localhost:11434/v1", true});

  sectionHeader(ed, "API Keys");
  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled(
      "Keys are encrypted with Windows DPAPI (only your Windows account on this PC can decrypt them) and "
      "sent only to the provider they belong to. A blank key falls back to the matching environment variable.");
  ImGui::PopTextWrapPos();
  if (ImGui::BeginTable("##keys", 3, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Provider", ImGuiTableColumnFlags_WidthFixed, 130);
    ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 190);
    for (int k = 1; k < static_cast<int>(ProviderKind::COUNT_); ++k) {
      auto kind = static_cast<ProviderKind>(k);
      const sl::ProviderInfo& info = sl::providerInfo(kind);
      ImGui::PushID(k);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(info.name);
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputTextWithHint("##key", kind == ProviderKind::Local ? "optional -- most local servers need none" : "paste key",
                               &s.keys[k], ImGuiInputTextFlags_Password);
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      if (!s.keys[k].empty()) ImGui::TextDisabled("set (encrypted on save)");
      else if (info.envVar && s.hasKey(kind)) ImGui::TextDisabled("from %s", info.envVar);
      else if (info.needsKey) ImGui::TextDisabled("not set");
      else ImGui::TextDisabled("not needed");
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  sectionHeader(ed, "Local LLMs");
  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled("Any OpenAI-compatible server works. No key, no cost, nothing leaves your machine.");
  ImGui::BulletText("Ollama: install from ollama.com, run  ollama pull llama3.2 , server URL http://localhost:11434/v1");
  ImGui::BulletText("LM Studio: load a model, start the local server, URL http://localhost:1234/v1, model ID as shown");
  ImGui::PopTextWrapPos();

  sectionHeader(ed, "Performance");
  ImGui::SetNextItemWidth(220);
  ImGui::SliderInt("Concurrent requests", &s.maxConcurrency, 1, 64);
  ImGui::SetItemTooltip("How many model calls ask_all / ask_choice_all keep in flight at once");

  ImGui::Spacing();
  ImGui::Separator();
  bool anyPaid = false;
  for (auto& m : s.roster)
    anyPaid = anyPaid || (m.enabled && m.kind != ProviderKind::Mock && m.kind != ProviderKind::Local);
  if (anyPaid) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ed.pal.warning));
    ImGui::TextWrapped("Paid APIs are enabled: every agent turn is a billed request. Large games (city.sl, "
                       "outbreak.sl) can make thousands. Calls and tokens are counted in the status bar.");
    ImGui::PopStyleColor();
  }
  if (ImGui::Button("Save", ImVec2(120, 0))) {
    ed.settings = s;
    if (ed.settings.save()) notify(ed, "Model settings saved -- reloading the game with the new roster");
    else notify(ed, "Could not write settings.json");
    loadAsset(ed, ed.activeAsset);
    ed.showSettings = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(120, 0))) {
    ed.settingsDraft = ed.settings;
    ed.showSettings = false;
  }
  ImGui::End();
}

}  // namespace app
