#include "character/character_moves.h"

#include "character/evil_ryu_ending_text.h"

#include <atomic>
#include <cstdio>
#include <vector>

namespace ssf2t::character {
namespace {

constexpr size_t npos = std::string::npos;

std::atomic<bool> g_screen_patched{false};
std::atomic<bool> g_scripts_patched{false};
std::atomic<bool> g_text_patched{false};

constexpr uint32_t kBlockHeaderLine = 1;
constexpr std::string_view kBlockHeader = "// Ryu's Moves";

constexpr std::string_view kOpen = "<UIElementGroup>";
constexpr std::string_view kClose = "</UIElementGroup>";

// End of the <UIElementGroup> that opens at `begin`, one past its closing tag.
size_t GroupEnd(const std::string& xml, size_t begin) {
  int depth = 0;
  for (size_t at = begin; at < xml.size();) {
    const size_t open = xml.find(kOpen, at);
    const size_t close = xml.find(kClose, at);
    if (close == npos) return npos;
    if (open != npos && open < close) {
      ++depth;
      at = open + kOpen.size();
    } else {
      --depth;
      at = close + kClose.size();
      if (depth == 0) return at;
    }
  }
  return npos;
}

// Start of the group named `name` and the whitespace that indents its first line.
bool FindGroup(const std::string& xml, std::string_view name, size_t* begin, std::string* indent) {
  const std::string tag = "<name>" + std::string(name) + "</name>";
  const size_t named = xml.find(tag);
  if (named == npos || xml.find(tag, named + 1) != npos) return false;
  const size_t open = xml.rfind(kOpen, named);
  if (open == npos || xml.find_first_not_of(" \t\r\n", open + kOpen.size()) != named) return false;
  size_t line = open;
  while (line > 0 && (xml[line - 1] == '\t' || xml[line - 1] == ' ')) --line;
  *begin = open;
  *indent = xml.substr(line, open - line);
  return true;
}

struct Xml {
  std::string text;
  std::string newline;
  std::string indent;
  int depth = 0;

  void Open(std::string_view tag) {
    Line("<" + std::string(tag) + ">");
    ++depth;
  }
  void Close(std::string_view tag) {
    --depth;
    Line("</" + std::string(tag) + ">");
  }
  void Field(std::string_view tag, std::string_view value) {
    Line("<" + std::string(tag) + ">" + std::string(value) + "</" + std::string(tag) + ">");
  }
  void Line(const std::string& content) {
    if (!text.empty()) text += newline;
    text += indent;
    for (int i = 0; i < depth; ++i) text += '\t';
    text += content;
  }
};

std::string Number(float value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%g", double(value));
  return buffer;
}

std::string Triple(float x, float y, float z) { return Number(x) + " " + Number(y) + " " + Number(z); }

struct TextCell {
  std::string_view name;
  std::string_view font;
  std::string_view text_id;
  float x, y, width, height;
  float shade;
  int wrap;
  bool centered;
};

void WriteText(Xml& xml, const TextCell& cell) {
  xml.Open("UIText");
  xml.Field("name", cell.name);
  xml.Field("descName", "default_text");
  xml.Field("pos", Triple(cell.x, cell.y, 11));
  xml.Field("size", Triple(cell.width, cell.height, 1));
  xml.Field("color", Number(cell.shade) + " " + Number(cell.shade) + " " + Number(cell.shade) + " 1");
  xml.Field("fontType", "Rf");
  xml.Field("font", cell.font);
  xml.Field("textID", cell.text_id);
  if (cell.wrap) xml.Field("wrapLength", std::to_string(cell.wrap));
  if (cell.centered) xml.Field("hAlign", "Center");
  xml.Close("UIText");
}

constexpr std::string_view kNameFont = "HelveticaBoldItalic60";
constexpr std::string_view kCommandFont = "charactermoves";
constexpr float kLabelShade = 0.784314f;
constexpr float kCommandX[2] = {838, 1470};

struct Row {
  float name_y;
  float name_size;
  float command_y;
  float label_x;
  int command_wrap;
};

constexpr std::array<Row, 7> kRows = {{
    {270, 45, 273, 220, 600},
    {330, 45, 330, 220, 600},
    {383, 45, 383, 220, 600},
    {425, 41, 440, 220, 600},
    {493, 45, 494, 220, 1200},
    {548, 45, 549, 220, 1200},
    {601, 45, 604, 390, 1200},
}};
struct PageLayout {
  std::string_view prefix;
  const std::array<EvilRyuMove, 7>& moves;
  std::string_view title_id;
  int index;
  int air_note_row;
};

constexpr PageLayout kEvilRyuPage = {"evilryu", kEvilRyuMoves, kEvilRyuTitleId, kEvilRyuMoveListIndex, 3};
constexpr PageLayout kViolentKenPage = {"violentken", kViolentKenMoves, kViolentKenTitleId, kViolentKenMoveListIndex, -1};
constexpr std::array<const PageLayout*, 2> kPages = {&kEvilRyuPage, &kViolentKenPage};

// The airborne name is the page's longest and has no note under it.
constexpr int kLongNameRow = 3;
constexpr float kLongNameY = 438;
constexpr float kLongNameWidth = 33;

constexpr float kAirNoteX = 240;
constexpr float kAirNoteY = 454;
constexpr float kSuperBadgeY = 589;
constexpr std::array<float, 4> kSeparatorY = {260, 370, 480, 590};

std::string RowName(const char* prefix, int row) { return std::string(prefix) + std::to_string(row + 1); }

std::string Prefixed(std::string_view head, const PageLayout& page, std::string_view tail = {}) {
  return std::string(head) + std::string(page.prefix) + std::string(tail);
}

void WriteColumn(Xml& xml, const PageLayout& page, const char* suffix, bool remixed) {
  xml.Open("UIElementGroup");
  xml.Field("name", Prefixed("moves_", page, "_") + suffix);
  for (size_t row = 0; row < kRows.size(); ++row) {
    const EvilRyuMove& move = page.moves[row];
    const std::string name = Prefixed("move_", page, "_") + std::to_string(row + 1) + suffix;
    WriteText(xml, {name, kCommandFont, remixed ? move.remixed_id : move.classic_id, kCommandX[remixed ? 1 : 0],
                    kRows[row].command_y, 45, 45, 1.0f, kRows[row].command_wrap, true});
  }
  xml.Close("UIElementGroup");
}

std::string BuildPageGroup(const PageLayout& page, const std::string& indent, const std::string& newline) {
  Xml xml;
  xml.indent = indent;
  xml.newline = newline;
  xml.Open("UIElementGroup");
  xml.Field("name", Prefixed("moves_", page));
  xml.Field("pos", "-30 0 0");
  xml.Field("color", "0.749020 0.749020 0.749020 0");
  for (size_t row = 0; row < kRows.size(); ++row) {
    const std::string name = RowName(Prefixed("move_", page).c_str(), int(row));
    const bool long_name = page.air_note_row < 0 && int(row) == kLongNameRow;
    const float width = long_name ? kLongNameWidth : row == 3 ? 37.0f : 40.0f;
    WriteText(xml, {name, kNameFont, page.moves[row].name_id, kRows[row].label_x,
                    long_name ? kLongNameY : kRows[row].name_y, width, kRows[row].name_size, kLabelShade, 600, false});
    if (int(row) == page.air_note_row) {
      WriteText(xml, {Prefixed("move_", page, "4b"), kNameFont, "CMD_HURRICANEAIR", kAirNoteX, kAirNoteY, 32, 38,
                      kLabelShade, 0, false});
    }
  }
  WriteColumn(xml, page, "C", false);
  WriteColumn(xml, page, "R", true);
  xml.Open("UIGraphic");
  xml.Field("name", "moves_super_" + std::to_string(page.index));
  xml.Field("descName", "HUD_supertext_art");
  xml.Field("pos", Triple(220, kSuperBadgeY, 8));
  xml.Field("size", "150 75 1");
  xml.Field("fileName", "user_interface\\In_game\\GameScreen\\HUD_Super_Text_Hot1");
  xml.Close("UIGraphic");
  xml.Open("UIText");
  xml.Field("name", Prefixed("name_", page));
  xml.Field("descName", "default_text");
  xml.Field("pos", "227 168 15");
  xml.Field("size", "50 55 1");
  xml.Field("color", "0.815686 0.815686 0.815686 1");
  xml.Field("fontType", "Rf");
  xml.Field("font", kNameFont);
  xml.Field("textID", page.title_id);
  xml.Field("wrapLength", "600");
  xml.Close("UIText");
  xml.Open("UIElementGroup");
  xml.Field("name", Prefixed("rowLines_", page));
  xml.Field("pos", "30 10 0");
  int line = 1;
  for (const float y : kSeparatorY) {
    xml.Open("UIGraphic");
    xml.Field("name", "line0" + std::to_string(line++) + "_" + std::string(page.prefix));
    xml.Field("descName", "null_graphic");
    xml.Field("visible", "true");
    xml.Field("pos", Triple(175, y, y == 260 ? 12.0f : 14.0f));
    xml.Field("size", "1550 55 1");
    xml.Field("color", "1 1 1 0.200000");
    xml.Close("UIGraphic");
  }
  xml.Close("UIElementGroup");
  xml.Close("UIElementGroup");
  return xml.text;
}

}  // namespace

bool PatchCharacterMovesScreen(std::string& xml) {
  if (xml.find("<name>moves_evilryu</name>") != npos) return false;
  size_t begin = 0;
  std::string indent;
  if (!FindGroup(xml, "moves_akuma", &begin, &indent)) return false;
  const size_t end = GroupEnd(xml, begin);
  if (end == npos) return false;
  const std::string newline = xml.find("\r\n") != npos ? "\r\n" : "\n";
  std::string pages;
  for (const PageLayout* page : kPages) pages += newline + BuildPageGroup(*page, indent, newline);
  xml.insert(end, pages);
  g_screen_patched.store(true, std::memory_order_relaxed);
  return true;
}

bool PatchCharacterMovesScripts(std::string& xml) {
  constexpr std::string_view kHide = "UIElement_SetColor_FloatsA(moves_akuma,0.0f);";
  constexpr std::string_view kShow = "UIElement_SetColor_FloatsA(moves_akuma,1.0f);";
  if (xml.find("moves_evilryu") != npos || xml.find("name=\"hide_moves_except\"") == npos) return false;
  const size_t hide = xml.find(kHide);
  const size_t show = xml.find(kShow);
  if (hide == npos || show == npos || xml.find(kHide, hide + 1) != npos || xml.find(kShow, show + 1) != npos ||
      show < hide) {
    return false;
  }
  const size_t brace = xml.find('}', show);
  if (brace == npos) return false;
  const std::string newline = xml.find("\r\n") != npos ? "\r\n" : "\n";
  std::string branches;
  std::string hides;
  for (const PageLayout* page : kPages) {
    const std::string group = Prefixed("moves_", *page);
    branches += newline + "    else if( GetResultRegister() eq " + std::to_string(page->index) + " )" + newline +
                "    {" + newline + "      UIElement_SetColor_FloatsA(" + group + ",1.0f);" + newline + "    }";
    hides += newline + "    UIElement_SetColor_FloatsA(" + group + ",0.0f);";
  }
  xml.insert(brace + 1, branches);
  xml.insert(hide + kHide.size(), hides);
  g_scripts_patched.store(true, std::memory_order_relaxed);
  return true;
}

namespace {

std::string WinsTextWithPrefix(std::string_view wins, std::string_view name, std::string_view prefix) {
  const size_t at = wins.find(name);
  std::string text(wins);
  text.insert(at == std::string_view::npos ? 0 : at, prefix);
  return text;
}

struct QuoteTexts {
  std::string_view wins_word;
  std::array<std::string_view, kViolentKenQuotes> quotes;
  std::array<std::string_view, kViolentKenEndingLines> ending;
};

// USF2 win_msg_<language> on one line each: quotes are messages 131..134, the ending is end_171..end_180. The last row is
// this port's own pt-BR translation, not Capcom text.
constexpr std::array<QuoteTexts, 6> kQuoteTexts = {{
    {"WINS",
     {"I'll become stronger than anyone, no matter what the cost!",
      "I'm not looking for a good fight. I just want to win!",
      "I feel like a man reborn. And now... Now I can never lose!",
      "This is what I was missing all along. The desire to destroy... And the desire to kill!"},
     {"\"U-Urgh!\"",
      "\"Hmph... Why does this fool cling to his worthless friendship?\"",
      "\"I could not have predicted that his will would resist the force of my Psycho Power...\"",
      "\"Rrraaaghh!\"",
      "\"What...? Where am I?\"",
      "\"Oh yeah! Urgh... I was under Bison's control.\"",
      "\"Heh, what a joke. What kind of warrior lets that happen to him?\"",
      "\"Looks like we'll never be able to walk the same road, Ryu.\"",
      "\"But I promise you, friend--I'll find my own path.\"",
      "\"And Ryu... You'd better be waiting for me at the end of it!\""}},
    {"GAGNE",
     {"Je deviendras plus fort que quiconque, quel qu'en soit le prix !",
      "Je me fiche de faire un bon combat. Seule compte la victoire !",
      "C'est comme si je m'étais réincarné. Maintenant... Je ne peux plus perdre !",
      "C'est le sentiment qui me manquait depuis toujours... Le désir de tout détruire... Et la volonté de tuer !"},
     {"A-Argh !",
      "Hmph... Pourquoi cet imbécile s'accroche-t-il si désespérément à une amitié sans valeur ?",
      "Je ne pensais pas que sa volonté puisse résister à mon Psycho Power...",
      "Rrraaaghh !",
      "Que... ? Où suis-je ?",
      "Je me souviens ! Argh... J'étais sous le contrôle de Bison.",
      "Ridicule. Un guerrier comme moi ne peut se laisser dominer aussi facilement.",
      "On dirait que nos chemins se séparent, Ryu.",
      "Mais je trouverai ma propre voie. Je te le promets, mon ami.",
      "Ryu... J'espère que tu seras prêt lorsque nos routes se croiserons de nouveau !"}},
    {"SIEGT",
     {"Ich werde stärker als alle anderen werden. Koste es, was es wolle!",
      "Ich bin nicht auf einen guten Kampf aus, ich möchte einfach nur gewinnen!",
      "Ich fühle mich wie wiedergeboren. Und jetzt... Jetzt kann ich nicht verlieren!",
      "Es ist das Verlangen zu zerstören, das ich vermisst habe... Und das Verlangen zu töten!"},
     {"*stöhn*",
      "*grummel* Warum klammert sich dieser Narr an seine wertlose Freundschaft?",
      "Ich habe nicht vorhergesehen, dass sein Wille sich meiner Psycho Power widersetzen würde...",
      "Rrraaaghh!",
      "Was? Wo bin ich?!",
      "Oh ja! *stöhn* Ich war unter Bisons Kontrolle.",
      "Hm, was für ein Witz. Was für ein Kämpfer lässt das mit sich machen?",
      "Scheint so, als würden wir nie Seite an Seite gehen, Ryu.",
      "Aber ich verspreche dir, Freund – ich werde meinen eigenen Weg finden.",
      "Und Ryu... Du wartest besser an dessen Ende auf mich!"}},
    {"VINCE",
     {"Diventerò il più forte di tutti, qualunque sia il prezzo da pagare!",
      "Non mi interessa uno scontro leale. L'unica cosa che voglio è vincere!",
      "È come se fossi rinato. Adesso... non posso più perdere!",
      "Ecco cosa mi mancava... Il desiderio di distruggere... e di uccidere!"},
     {"U-Urgh!",
      "Perché si ostina con quell'inutile amicizia?",
      "Non avrei mai potuto pensare che la sua mente sarebbe stata in grado di resistere al mio Psycho Power...",
      "Rrraaaghh!",
      "Cosa...? Dove sono?",
      "Ah... già. Ero sotto il controllo di Bison.",
      "Ridicolo... Un vero combattente come me non può farsi dominare così.",
      "A quanto pare le nostre strade si dividono qui, Ryu.",
      "Ma ti prometto, amico mio, che un giorno troverò la mia.",
      "E Ryu... Spero di ritrovarti in fondo a essa."}},
    {"GANA",
     {"¡Seré más fuerte que nadie, cueste lo que cueste!",
      "No estoy buscando un buen combate, ¡solo quiero ganar!",
      "Me siento como si hubiera renacido. Y ahora... ¡ahora soy invencible!",
      "Esto es lo que echaba tanto de menos. ¡El deseo de destruir... y el deseo de matar!"},
     {"¡Ur... urgh!",
      "Pfff... ¿Por qué se agarra este idiota a una amistad tan inútil?",
      "Jamás habría imaginado que su voluntad podría resistir la fuerza de mi Psycho Power...",
      "¡Rrraaaghh!",
      "¿Qué...? ¿Dónde estoy?",
      "¡Ah, sí! Urgh... estaba bajo el control de Bison.",
      "Ja, qué absurdo. ¿Qué tipo de guerrero dejaría que le pasara algo así?",
      "Parece que seguiremos recorriendo el mismo camino, Ryu.",
      "Pero te lo prometo, amigo mío: yo crearé mi propio camino.",
      "Y a ti, Ryu... ¡Más te vale estar allí cuando llegue al final!"}},
    {"VENCE",
     {"Vou ficar mais forte que todos, custe o que custar!",
      "Não estou procurando uma boa luta. Eu só quero vencer!",
      "Eu me sinto renascido. E agora... Agora nunca mais vou perder!",
      "Era isso que me faltava o tempo todo. O desejo de destruir... E o desejo de matar!"},
     {"\"U-Urgh!\"",
      "\"Hmph... Por que esse tolo se agarra a essa amizade sem valor?\"",
      "\"Eu não previa que a vontade dele resistiria à força do meu Psycho Power...\"",
      "\"Rrraaaghh!\"",
      "\"O quê...? Onde estou?\"",
      "\"Ah, é! Urgh... Eu estava sob o controle de Bison.\"",
      "\"Heh, que piada. Que tipo de guerreiro deixa isso acontecer?\"",
      "\"Parece que nunca vamos trilhar o mesmo caminho, Ryu.\"",
      "\"Mas eu prometo, amigo... Vou encontrar o meu próprio caminho.\"",
      "\"E Ryu... É melhor você estar me esperando no fim dele!\""}},
}};

struct EvilQuoteTexts {
  std::string_view wins_word;
  std::array<std::string_view, kEvilRyuQuotes> quotes;
};

// USF2 win_msg_<language> messages 126..130 on one line each; the last row is this port's own pt-BR translation.
constexpr std::array<EvilQuoteTexts, 6> kEvilQuoteTexts = {{
    {"WINS",
     {"I'll put an end to the beating of your feeble heart!",
      "A match as worthless as this is a far cry from the true blood match I seek...",
      "This... THIS is my power! No one can stop me!",
      "My power will destroy all!",
      "You're weak...too weak to exist! You must pay for your weakness with death!"}},
    {"GAGNE",
     {"Je vais mettre fin aux battements de ton cœur insignifiant !",
      "Ce match ridicule n'est pas digne de ma quête pour le véritable combat.",
      "C'est... C'est mon pouvoir ! Personne ne peut m'arrêter !",
      "Mon pouvoir vous détruira tous !",
      "Tu es faible... Trop faible pour exister ! Tu dois expier ta faiblesse par la mort !"}},
    {"SIEGT",
     {"Ich werde dem Schlag deines schwachen Herzens ein Ende bereiten!",
      "Diese wertlose Auseinandersetzung ist weit von dem wahren blutigem Kampf entfernt, nach dem ich mich sehne.",
      "Das... DAS ist meine Macht! Niemand kann mich aufhalten!",
      "Meine Macht wird alles zuerstören!",
      "Du bist schwach... Zu schwach, um zu existieren! Du musst für deine Schwäche mit dem Leben bezahlen."}},
    {"VINCE",
     {"Metterò fine al battito del tuo debole cuore!",
      "È stato un combattimento insignificante, ben lontano dallo scontro sanguinoso che cerco...",
      "Questo è il mio vero potere! Nessuno può fermarmi!",
      "Il mio potere vi annienterà!",
      "Sei debole... Troppo debole per esistere! Pagherai per questa tua fragilità con la morte!"}},
    {"GANA",
     {"¡Voy a hacer que tu débil corazón deje de latir!",
      "Un combate tan insignificante como este está muy lejos de ser el combate antológico que busco...",
      "¡Este... ESTE es mi poder! ¡Nadie puede detenerme!",
      "¡Mi poder lo destruirá todo!",
      "Eres débil... ¡demasiado débil para existir! ¡Debes pagar tu debilidad con la muerte!"}},
    {"VENCE",
     {"Vou pôr fim aos batimentos do seu coração fraco!",
      "Uma luta sem valor como esta está muito longe da luta de sangue verdadeira que eu busco...",
      "Isto... ISTO é o meu poder! Ninguém pode me deter!",
      "Meu poder vai destruir tudo!",
      "Você é fraco... fraco demais para existir! Você vai pagar a sua fraqueza com a morte!"}},
}};

std::string TableLine(const std::string& table, uint32_t wanted) {
  size_t start = 0;
  for (uint32_t line = 0; line < wanted && start != npos; ++line) {
    const size_t next = table.find('\n', start);
    start = next == npos ? npos : next + 1;
  }
  if (start == npos || start >= table.size()) return {};
  const size_t end = table.find_first_of("\r\n", start);
  return table.substr(start, end == npos ? npos : end - start);
}

struct LinePatch {
  uint32_t line;
  std::string text;
};

// Lines built from the language's own table; a table too short to hold their sources does not get them.
std::vector<LinePatch> LanguageLines(const std::string& table) {
  std::vector<LinePatch> lines;
  const std::string ryu_wins = TableLine(table, kRyuWinsLine);
  const std::string ken_wins = TableLine(table, kKenWinsLine);
  const std::string in_air = TableLine(table, kInAirLine);
  if (!ryu_wins.empty()) {
    lines.push_back({kEvilRyuWinsLine, EvilRyuWinsText(ryu_wins)});
    const auto& evil_quotes = EvilRyuQuotes(ryu_wins);
    for (size_t i = 0; i < evil_quotes.size(); ++i) lines.push_back({kEvilRyuQuoteLines[i], std::string(evil_quotes[i])});
    const auto& evil_ending = EvilRyuEnding(ryu_wins);
    for (size_t i = 0; i < evil_ending.size(); ++i) {
      lines.push_back({kEvilRyuEndingTableLines[i], std::string(evil_ending[i])});
    }
  }
  if (!ken_wins.empty()) {
    lines.push_back({kViolentKenWinsLine, ViolentKenWinsText(ken_wins)});
    const auto& quotes = ViolentKenQuotes(ken_wins);
    for (size_t i = 0; i < quotes.size(); ++i) lines.push_back({kViolentKenQuoteLines[i], std::string(quotes[i])});
    const auto& ending = ViolentKenEnding(ken_wins);
    for (size_t i = 0; i < ending.size(); ++i) lines.push_back({kViolentKenEndingTableLines[i], std::string(ending[i])});
  }
  if (!in_air.empty()) lines.push_back({kAirTatsumakiCommandLine, "(" + in_air + ") \xC2\x81+\xC2\x97"});
  return lines;
}

const MoveListText* PageLine(uint32_t number) {
  for (const MoveListText& entry : kEvilRyuMoveListText) {
    if (entry.line == number) return &entry;
  }
  for (const MoveListText& entry : kViolentKenMoveListText) {
    if (entry.line == number) return &entry;
  }
  return nullptr;
}

}  // namespace

std::string EvilRyuWinsText(std::string_view ryu_wins) { return WinsTextWithPrefix(ryu_wins, "RYU", "EVIL "); }

std::string ViolentKenWinsText(std::string_view ken_wins) { return WinsTextWithPrefix(ken_wins, "KEN", "VIOLENT "); }

const std::array<std::string_view, kViolentKenQuotes>& ViolentKenQuotes(std::string_view ken_wins) {
  for (const QuoteTexts& texts : kQuoteTexts) {
    if (ken_wins.ends_with(texts.wins_word)) return texts.quotes;
  }
  return kQuoteTexts[0].quotes;
}

const std::array<std::string_view, kEvilRyuQuotes>& EvilRyuQuotes(std::string_view ryu_wins) {
  for (const EvilQuoteTexts& texts : kEvilQuoteTexts) {
    if (ryu_wins.ends_with(texts.wins_word)) return texts.quotes;
  }
  return kEvilQuoteTexts[0].quotes;
}

const std::array<std::string_view, kViolentKenEndingLines>& ViolentKenEnding(std::string_view ken_wins) {
  for (const QuoteTexts& texts : kQuoteTexts) {
    if (ken_wins.ends_with(texts.wins_word)) return texts.ending;
  }
  return kQuoteTexts[0].ending;
}

uint32_t PatchMoveListText(std::string& table) {
  const std::vector<LinePatch> language = LanguageLines(table);

  std::string patched;
  patched.reserve(table.size() + 128);
  uint32_t changed = 0;
  uint32_t number = 0;
  bool anchored = false;
  size_t start = 0;
  while (start < table.size()) {
    size_t end = table.find('\n', start);
    const bool has_newline = end != npos;
    if (!has_newline) end = table.size();
    std::string_view line = std::string_view(table).substr(start, end - start);
    const bool carriage = !line.empty() && line.back() == '\r';
    if (carriage) line.remove_suffix(1);
    if (number == kBlockHeaderLine) anchored = line == kBlockHeader;
    const MoveListText* match = PageLine(number);
    const LinePatch* derived = nullptr;
    for (const LinePatch& entry : language) {
      if (entry.line == number) derived = &entry;
    }
    if (match && anchored) {
      patched.append(match->text);
      ++changed;
    } else if (derived && anchored) {
      patched.append(derived->text);
    } else {
      patched.append(line);
    }
    if (carriage) patched.push_back('\r');
    if (has_newline) patched.push_back('\n');
    start = end + 1;
    ++number;
  }
  if (changed != kEvilRyuMoveListText.size() + kViolentKenMoveListText.size()) return 0;
  table = std::move(patched);
  g_text_patched.store(true, std::memory_order_relaxed);
  return changed;
}

bool CharacterMovesPageReady() {
  return g_screen_patched.load(std::memory_order_relaxed) && g_scripts_patched.load(std::memory_order_relaxed) &&
         g_text_patched.load(std::memory_order_relaxed);
}

}  // namespace ssf2t::character
