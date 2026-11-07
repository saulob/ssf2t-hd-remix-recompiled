#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace ssf2t::character {

// The ending's nine USF2 lines, end_162..end_170 (chr 18: win_msg rows 248..256), on legacy lines of the guest's string table
// that no screen, script or code reads.
inline constexpr int kEvilRyuEndingLines = 9;
inline constexpr std::array<std::string_view, kEvilRyuEndingLines> kEvilRyuEndingIds = {
    "Dhalsim_YogaFire", "Dhalsim_YogaFlame", "Dhalsim_UpwardYogaFlame", "Dhalsim_Drill",  "Dhalsim_HeadDrill",
    "Dhalsim_Teleport", "Dhalsim_Super",     "THawk_HawkUppercut",      "THawk_HawkDive"};
inline constexpr std::array<uint32_t, kEvilRyuEndingLines> kEvilRyuEndingTableLines = {50, 51, 52, 53, 54, 55, 56, 58, 59};

namespace evil_ryu_ending_detail {

struct Texts {
  std::string_view wins_word;
  std::array<std::string_view, kEvilRyuEndingLines> lines;
};

// USF2 win_msg_<language>, one line each. The last row is this port's own pt-BR translation, not Capcom text.
inline constexpr std::array<Texts, 6> kTexts = {{
    {"WINS",
     {"\"Messatsu!\"",
      "\"I-Impossible!\"",
      "\"To think that my power would yield to the likes of you...\"",
      "\"This is nothing... Defeating weaklings isn't enough for me now.\"",
      "\"Your feeble power does nothing for me... Nothing to sate my unquenchable thirst!\"",
      "\"My blood...it yearns for power!\"",
      "\"Wooooohhh!!!\"",
      "\"I feel it! The power that will take me to new heights... The power to destroy anything!\"",
      "\"Now...where is the demon that is worth a fight to the death?\""}},
    {"GAGNE",
     {"Messatsu !",
      "I-Impossible !",
      "Même mon pouvoir est insignifiant contre le tien...",
      "Ce n'est pas assez. Vaincre les faibles comme toi ne me satisfait plus.",
      "Ton faible pouvoir n'a aucun effet sur moi... Rien ne peut étancher ma soif de destruction !",
      "Mon sang... Il a besoin de plus de pouvoir !",
      "Wooooohhh !!!",
      "Je le sens ! Le pouvoir qui m'élèvera vers de nouveaux sommets ! Le pouvoir de tout détruire !",
      "Et maintenant... Où ce trouve ce démon que je dois combattre à mort ?"}},
    {"SIEGT",
     {"Messatsu!",
      "U-Unmöglich!",
      "Dass meine Macht von jemandem wie dir gebrochen werden könnte...",
      "Das ist nichts. Schwächlinge zu besiegen, ist mir nicht genug.",
      "Deine kläglichen Kräfte lassen mich kalt... Nichts, was meinen unlöschbaren Durst stillen könnte!",
      "Mein Blut... Es sehnt sich nach Macht!",
      "Wooooohhh!!!",
      "Ich fühle sie! Die Macht, die mich neue Höhen erreichen lassen wird... Die Macht, alles zu zerstören!",
      "Nun... Wo ist der Dämon, der einen Kampf um Leben und Tod wert ist?"}},
    {"VINCE",
     {"Messatsu!",
      "I-Impossibile!",
      "Non riesco ancora a credere che tu sia stato in grado di sconfiggermi...",
      "Non importa. Soggiogare un debole come te non mi fa alcun effetto.",
      "Il tuo ridicolo potere non è abbastanza. Non è in grado di saziare la mia sete!",
      "Il mio sangue... brama qualcosa di più devastante!",
      "Wooooohhh!!!",
      "Riesco a sentirla! L'energia che mi farà dominare su tutto e tutti... Il potere della distruzione!",
      "Devo trovare un demone degno di uno scontro mortale..."}},
    {"GANA",
     {"¡Messatsu!",
      "¡Im... imposible!",
      "No me puedo creer que alguien como tú haya superado mi poder...",
      "Esto no es nada. Ya no me basta con derrotar a mequetrefes como tú.",
      "Tu patético poder no me hace sentir nada. ¡Nada puede saciar mi sed!",
      "¡Mi sangre ansía más poder!",
      "¡¡¡Wooooohhh!!!",
      "¡Puedo sentirlo! El poder que me hará alcanzar nuevos límites... ¡El poder necesario para destruir cualquier cosa!",
      "Y ahora... ¿dónde hay un demonio con el que valga la pena luchar a muerte?"}},
    {"VENCE",
     {"\"Messatsu!\"",
      "\"I-Impossível!\"",
      "\"Pensar que o meu poder cederia a alguém como você...\"",
      "\"Isto não é nada... Derrotar fracos não me basta mais.\"",
      "\"Seu poder fraco não significa nada para mim... Nada que sacie a minha sede insaciável!\"",
      "\"O meu sangue... Ele anseia por poder!\"",
      "\"Wooooohhh!!!\"",
      "\"Eu sinto! O poder que me levará a novos patamares... O poder de destruir qualquer coisa!\"",
      "\"Agora... onde está o demônio digno de uma luta até a morte?\""}},
}};

constexpr bool EndsWith(std::string_view text, std::string_view tail) {
  return text.size() >= tail.size() && text.substr(text.size() - tail.size()) == tail;
}

}  // namespace evil_ryu_ending_detail

// The lines in the language of a string table, picked by the wording of its WINS_RYU line.
constexpr const std::array<std::string_view, kEvilRyuEndingLines>& EvilRyuEnding(std::string_view ryu_wins) {
  for (const auto& texts : evil_ryu_ending_detail::kTexts) {
    if (evil_ryu_ending_detail::EndsWith(ryu_wins, texts.wins_word)) return texts.lines;
  }
  return evil_ryu_ending_detail::kTexts[0].lines;
}

}  // namespace ssf2t::character
