#pragma once

#include <filesystem>

namespace rex::ui {
class ImGuiDrawer;
class ImmediateDrawer;
}  // namespace rex::ui

namespace ssf2t::character {

// Draws Evil Ryu and Violent Ken as an extra roster column on Character Select while Enable Evil Ryu and Violent Ken
// is on.
void CreateUltraPortraitOverlay(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer,
                                const std::filesystem::path& game_dir);

}  // namespace ssf2t::character
