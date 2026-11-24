#pragma once
#include "platform/types.h"

#include "compiled/warning_off.h"
#include <string>
#include <vector>
#include "compiled/warning_on.h"

namespace Rml
{
class ElementDocument;
}

namespace crittercap
{
class Renderer2D;

// A sprite placed on the 240x160 screen (top-left corner and size, in game pixels).
struct UiSprite
{
    std::string sprite;
    Float x = 0.0f;
    Float y = 0.0f;
    Float w = 0.0f;
    Float h = 0.0f;
};

// A menu entry or label at a fixed spot.
struct UiOption
{
    std::string label;
    Float x = 0.0f;
    Float y = 0.0f;
    Bool selected = false;
    Bool disabled = false;
};

// One critter row in the team screens.
struct UiCritterRow
{
    std::string name;
    std::string info;
    std::string level;
    std::string hpText;
    std::string hpColor = "#59d966";
    Float hpWidth = 0.0f;
    Float y = 0.0f;
    Bool selected = false;
    Bool fainted = false;
    Bool lead = false;
    Bool out = false;
    UiSprite sprite;
};

struct UiDexEntry
{
    UiSprite sprite;
    std::string number;
    Float x = 0.0f;
    Float y = 0.0f;
    Bool seen = false;
    Bool caught = false;
    Bool selected = false;
};

// A critter's box in battle.
struct UiBattler
{
    Bool visible = false;
    std::string name;
    std::string level;
    std::string hpText;
    std::string hpColor = "#59d966";
    Float hpWidth = 0.0f;
    Float xpWidth = 0.0f;
    Float nameWidth = 0.0f;
    Bool caught = false;
};

// Everything the UI document (resources/ui/game.rml) shows. The game fills it in every frame;
// the document reads it through an RmlUi data model.
struct UiState
{
    std::string screen = "title"; // title, world, battle or ending
    Int32 arrowFrame = 0;

    // Title
    Bool blink = true;
    std::vector<UiSprite> titleSprites;

    // HUD
    Bool hud = false;
    Bool hasParty = false;
    Int32 caps = 0;
    std::string gems;
    Bool dungeon = false;
    Int32 keys = 0;
    Bool bigKey = false;
    std::string prompt;

    // Dialog and yes/no questions
    Bool dialog = false;
    Bool dialogTop = false;
    std::string speaker;
    std::string text;
    Bool more = false;
    Bool choice = false;
    std::string question;
    Int32 choiceCursor = 0;

    // Starter pick
    Bool starter = false;
    Int32 starterCursor = 0;
    std::vector<UiSprite> starterSprites;
    std::vector<UiOption> starterNames;
    std::string starterType;
    std::string starterDex;

    // Team and Critterdex
    Bool menu = false;
    Int32 menuTab = 0;
    std::string playTime;
    std::vector<UiCritterRow> party;
    std::string gemCount;
    std::string pocket;
    std::vector<UiDexEntry> dex;
    std::string caughtText;
    std::string dexTitle;
    std::string dexText;
    Bool dexSeen = false;
    Bool dexCaught = false;

    // Battle
    UiBattler enemy;
    UiBattler mine;
    std::string battleMode; // menu, moves, text or ""
    std::vector<UiOption> battleOptions;
    std::string moveType;
    std::string movePower;
    std::string moveEffect;
    Bool moveStrong = false;
    std::string battleText;
    Bool battleMore = false;
    Bool battleTeam = false;
    Bool battleTeamBack = false;
    std::string battleTeamTitle;

    // Ending
    Int32 endingLines = 0;
    std::string stats;
};

class GameUi final
{
  public:
    // Loads the font, the sprite sheet and the document. Needs the engine's UI manager.
    Bool Initialize(const Renderer2D *renderer);
    void Shutdown();

    UiState &State()
    {
        return state;
    }
    // Sends the state to the document; call once per frame after filling it in.
    void Commit();

    // A sprite drawn at (x, y) like Renderer2D::Draw does: the point is the sprite's pivot.
    [[nodiscard]] UiSprite Sprite(const std::string &name, Float x, Float y, Float scale = 1.0f) const;

  private:
    UiState state;
    const Renderer2D *renderer = nullptr;
    Rml::ElementDocument *document = nullptr;
    Bool ready = false;
};

// Text helpers shared by the scenes.
std::string HpColor(Float ratio);
// The bitmap font keeps a heart under \x01; the UI font has it at U+2665.
std::string ToUiText(const std::string &text);
} // namespace crittercap
