#include "update_config.h"

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <rex/logging.h>

#include "platform.h"
#include "update_restart_arguments.h"
#include "updater/update_platform.h"

namespace ssf2t {
namespace {

constexpr const char* kReleasesUrl =
    "https://api.github.com/repos/saulob/ssf2t-hd-remix-recompiled/releases/latest";
constexpr const char* kDownloadUrlPrefix = "https://github.com/saulob/ssf2t-hd-remix-recompiled/releases/download/";

std::optional<std::string> Environment(const char* name) {
#if defined(_WIN32)
  char* value = nullptr;
  size_t size = 0;
  if (_dupenv_s(&value, &size, name) != 0 || !value) {
    return std::nullopt;
  }
  std::string result(value);
  std::free(value);
  if (result.empty()) {
    return std::nullopt;
  }
  return result;
#else
  const char* value = std::getenv(name);
  if (!value || !*value) {
    return std::nullopt;
  }
  return std::string(value);
#endif
}

bool TestMode() {
  const std::optional<std::string> value = Environment("SSF2T_UPDATE_TEST");
  return value && *value == "1";
}

std::optional<std::string> TestVariable(const char* name) {
  if (!TestMode()) {
    return std::nullopt;
  }
  return Environment(name);
}

}  // namespace

trg::update::UpdateConfig MakeUpdateConfig(const std::filesystem::path& install_dir) {
  using trg::update::SemanticVersion;
  using trg::update::UpdatePackageKind;

  trg::update::UpdateConfig config;
  config.current_version = SSF2T_PORT_VERSION;
  config.releases_url = kReleasesUrl;
  config.download_url_prefix = kDownloadUrlPrefix;
  config.user_agent = std::string("SSF2THDRemix-Updater/") + SSF2T_PORT_VERSION;
  config.install_dir = install_dir;
  config.restart_arguments = UpdateRestartArguments(trg::update::CurrentProcessArguments());

#if defined(_WIN32)
  config.package_kind = UpdatePackageKind::kZip;
  config.asset_name = [](const SemanticVersion& version) {
    return "SSF2THDRemix-v" + version.CoreString() + "-windows-x64.zip";
  };
  config.checksum_asset_name = [](const SemanticVersion& version) {
    return "SSF2THDRemix-v" + version.CoreString() + "-windows-x64.sha256";
  };
  config.restart_executable = install_dir / "ssf2t_hd_remix.exe";
  config.required_staged_files = {"ssf2t_hd_remix.exe"};
  config.install_supported = std::filesystem::exists(install_dir / "trg-updater.exe");
  if (!config.install_supported) {
    config.install_unsupported_reason =
        "The update helper is missing from this installation. Reinstall this release to enable automatic updates.";
  }
#else
  config.package_kind = UpdatePackageKind::kAppImage;
  config.asset_name = [](const SemanticVersion& version) {
    return "SSF2THDRemix-v" + version.CoreString() + "-x86_64.AppImage";
  };
  config.checksum_asset_name = [](const SemanticVersion& version) {
    return "SSF2THDRemix-v" + version.CoreString() + "-x86_64.sha256";
  };
  std::filesystem::path appimage;
  if (trg::update::HasOriginalAppImage(appimage)) {
    config.appimage_path = appimage;
    config.restart_executable = appimage;
    config.install_supported = true;
  } else {
    config.install_unsupported_reason = "Automatic installation is available in the AppImage build.";
  }
#endif

  config.log = [](std::string_view text) { REXLOG_INFO("Updater: {}", text); };

  if (const std::optional<std::string> version = TestVariable("SSF2T_UPDATE_TEST_CURRENT_VERSION"); version) {
    config.current_version = *version;
    REXLOG_WARN("Updater test mode: reporting version {}", config.current_version);
  }
  if (const std::optional<std::string> json = TestVariable("SSF2T_UPDATE_TEST_RELEASE_JSON"); json) {
    config.release_json_override = PathFromUtf8(*json);
  }
  if (const std::optional<std::string> hash = TestVariable("SSF2T_UPDATE_TEST_ASSET_SHA256"); hash) {
    config.sha256_override = *hash;
  }
  return config;
}

}  // namespace ssf2t
