#pragma once

#include <filesystem>

#include "updater/update_controller.h"

namespace ssf2t {

trg::update::UpdateConfig MakeUpdateConfig(const std::filesystem::path& install_dir);

}  // namespace ssf2t
