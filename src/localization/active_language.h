#pragma once

#include <string_view>

namespace ssf2t::localization {

// The language the program's own text (launcher, overlays, game table) is shown in. Set from the saved setting at start
// and by the launcher as the choice changes.
void SetActiveLanguage(std::string_view key);

bool PortugueseBrazilSelected();

}  // namespace ssf2t::localization
