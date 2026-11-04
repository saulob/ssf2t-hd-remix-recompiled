#pragma once

#include <cstdint>
#include <filesystem>

#include "character/akuma_navigation.h"

namespace rex::ui {
class ImGuiDrawer;
class ImmediateDrawer;
}  // namespace rex::ui

namespace ssf2t::character {

// True while the guest Character Select screen is running and either Akuma or Evil Ryu and Violent Ken is enabled.
bool CharacterSelectActive();

// True while that Character Select belongs to an Online Match.
bool CharacterSelectOnline();

// Changes each time Character Select starts running after being idle.
uint32_t CharacterSelectSession();

// The grid the guest is drawing: the cursor position table it reads.
SelectLayout CharacterSelectLayout();

// Draws the Akuma face on Character Select while Enable Akuma is on and Akuma is not the selected fighter.
void CreateAkumaOverlay(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer,
                        const std::filesystem::path& game_dir);

}  // namespace ssf2t::character
