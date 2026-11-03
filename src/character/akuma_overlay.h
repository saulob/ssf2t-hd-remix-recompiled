#pragma once

#include <filesystem>

namespace rex::ui {
class ImGuiDrawer;
class ImmediateDrawer;
}  // namespace rex::ui

namespace ssf2t::character {

// Draws the Akuma face on Character Select while Enable Akuma is on and Akuma is not the selected fighter.
void CreateAkumaOverlay(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer,
                        const std::filesystem::path& game_dir);

}  // namespace ssf2t::character
