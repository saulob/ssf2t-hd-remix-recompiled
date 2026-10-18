#include "trg/shader_pack.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <vector>

#include "trg/download.h"
#include "trg/launcher.h"
#include "utf8_path.h"

namespace trg {
namespace {

namespace fs = std::filesystem;
using detail::ToUtf8;
using detail::Utf8Path;

std::string ReadAll(const fs::path& path) {
  std::ifstream f(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(f)), {});
}

uint64_t Load64(const char* p) {
  uint64_t v;
  std::memcpy(&v, p, 8);
  return v;
}
uint32_t Load32(const char* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}

// Storage files as ReXGlue writes them (host byte order):
//   .xsh:  'XESH', version, then per shader {u64 hash, u32 dword count | type << 31, ucode}
//   .xpso: 'XEPS', 'DXRT'/'DXRO', version, then 72-byte {u64 hash, description} records
constexpr uint32_t kShaderMagic = 0x48534558, kPipelineMagic = 0x53504558;
constexpr size_t kPipelineRecord = 72;

struct Records {
  size_t header = 0;
  std::vector<std::pair<size_t, size_t>> spans;  // offset, size
};

bool Parse(const std::string& d, Records& out) {
  if (d.size() < 8) return false;
  const uint32_t magic = Load32(d.data());
  if (magic == kShaderMagic) {
    out.header = 8;
    for (size_t at = 8; at + 12 <= d.size();) {
      const size_t size = 12 + size_t(Load32(&d[at + 8]) & 0x7FFFFFFF) * 4;
      if (at + size > d.size()) break;  // cut short: keep what is whole
      out.spans.push_back({at, size});
      at += size;
    }
    return true;
  }
  if (magic == kPipelineMagic && d.size() >= 12) {
    out.header = 12;
    for (size_t at = 12; at + kPipelineRecord <= d.size(); at += kPipelineRecord) out.spans.push_back({at, kPipelineRecord});
    return true;
  }
  return false;
}

struct Manifest {
  int version = 0;
  std::vector<std::string> files;
};

Manifest ParseManifest(const std::string& text) {
  Manifest m;
  std::istringstream in(text);
  for (std::string line; std::getline(in, line);) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.rfind("version=", 0) == 0) {
      m.version = std::atoi(line.c_str() + 8);
    } else if (!line.empty() && line[0] != '#' && line.find_first_of("/\\:") == std::string::npos &&
               line.find("..") == std::string::npos) {
      m.files.push_back(line);  // plain file names only
    }
  }
  return m;
}

}  // namespace

int MergeShaderStorageFile(const std::string& utf8_from, const std::string& utf8_into) {
  const fs::path from = Utf8Path(utf8_from), into = Utf8Path(utf8_into);
  const std::string src = ReadAll(from);
  Records src_records;
  if (!Parse(src, src_records)) return -1;
  std::error_code ec;
  if (!fs::exists(into, ec) || fs::file_size(into, ec) <= src_records.header) {
    fs::create_directories(into.parent_path(), ec);
    fs::copy_file(from, into, fs::copy_options::overwrite_existing, ec);
    return ec ? -1 : int(src_records.spans.size());
  }
  std::string dst = ReadAll(into);
  Records dst_records;
  if (!Parse(dst, dst_records) || dst_records.header != src_records.header ||
      std::memcmp(dst.data(), src.data(), src_records.header) != 0)
    return -1;  // another runtime version: leave the player's cache alone
  size_t end = dst_records.header;  // drop a partly written last record
  std::unordered_set<uint64_t> have;
  for (auto [at, size] : dst_records.spans) {
    have.insert(Load64(&dst[at]));
    end = at + size;
  }
  dst.resize(end);
  int added = 0;
  for (auto [at, size] : src_records.spans) {
    if (!have.insert(Load64(&src[at])).second) continue;
    dst.append(src, at, size);
    ++added;
  }
  if (added) {
    std::ofstream out(into, std::ios::binary | std::ios::trunc);
    out.write(dst.data(), std::streamsize(dst.size()));
    if (!out) return -1;
  }
  return added;
}

int InstalledShaderPackVersion(const std::string& utf8_cache_dir) {
  return ParseManifest(ReadAll(Utf8Path(utf8_cache_dir) / "shaders" / "shader-pack.txt")).version;
}

std::string InstallShaderPack(Task& task, const std::string& base_url, const std::string& utf8_cache_dir,
                              std::string* message) {
  std::string manifest_text;
  if (std::string error = HttpGet(base_url + "shader-pack.txt", manifest_text, &task); !error.empty()) return error;
  const Manifest manifest = ParseManifest(manifest_text);
  if (!manifest.version || manifest.files.empty()) return "The published shader pack looks incomplete.";
  if (const int installed = InstalledShaderPackVersion(utf8_cache_dir); installed >= manifest.version) {
    if (message) *message = "You already have the newest shader pack (pack " + std::to_string(installed) + ").";
    return "";
  }
  const fs::path cache = Utf8Path(utf8_cache_dir);
  const fs::path download_dir = cache / "shaders" / "download";
  std::error_code ec;
  fs::create_directories(download_dir, ec);
  for (const std::string& name : manifest.files) {
    task.SetLabel("Downloading " + name);
    std::string data;
    if (std::string error = HttpGet(base_url + name, data, &task); !error.empty()) return error;
    const fs::path tmp = download_dir / Utf8Path(name);
    std::ofstream(tmp, std::ios::binary).write(data.data(), std::streamsize(data.size()));
    const auto into = cache / "shaders" / "shareable" / Utf8Path(name);
    const int added = MergeShaderStorageFile(ToUtf8(tmp), ToUtf8(into));
    fs::remove(tmp, ec);
    if (added < 0) return "This shader pack is for a different version of the port. Update the port first.";
  }
  fs::remove(download_dir, ec);
  std::ofstream(cache / "shaders" / "shader-pack.txt", std::ios::binary) << manifest_text;
  if (message)
    *message = "Shader pack " + std::to_string(manifest.version) +
               " installed. The game prepares its effects each time it starts.";
  return "";
}

void ShaderPackRow(Ui& ui, ShaderPackRowState& st, const std::string& base_url, const std::string& utf8_cache_dir,
                   const char* help) {
  std::string error;
  switch (st.task.Finish(&error)) {
    case Task::State::kDone: st.installed = -1; break;
    case Task::State::kFailed:
    case Task::State::kCancelled: st.message = error.empty() ? "Cancelled." : error; break;
    default: break;
  }
  if (st.installed < 0) st.installed = InstalledShaderPackVersion(utf8_cache_dir);
  ui.Row("Shader pack", help ? help
                             : "Effects already prepared by playing through the game. With it the game prepares them "
                               "all as it starts, so it never pauses for a new effect. Downloads from GitHub.");
  if (st.task.running()) {
    const float f = st.task.fraction();
    ImGui::ProgressBar(f >= 0 ? f : 0.0f, ImVec2(-FLT_MIN, 0), f >= 0 ? nullptr : "Connecting...");
    return;
  }
  const bool installed = st.installed > 0;
  if (installed ? ImGui::Button("Check for a newer pack", ImVec2(-FLT_MIN, 0))
                : ui.AccentButton("Download shader pack")) {
    st.message.clear();
    std::string* message = &st.message;
    st.task.Start("Downloading shader pack", [base_url, utf8_cache_dir, message](Task& t) {
      return InstallShaderPack(t, base_url, utf8_cache_dir, message);
    });
  }
  ImGui::PushStyleColor(ImGuiCol_Text, ui.theme().dim);
  if (!st.message.empty())
    ImGui::TextWrapped("%s", st.message.c_str());
  else if (installed)
    ImGui::Text("Pack %d installed.", st.installed);
  ImGui::PopStyleColor();
}

}  // namespace trg
