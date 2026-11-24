#include "ui/game_ui.h"

#include "render/renderer2d.h"
#include "ui/ui_manager.h"
#include "utils/logger.h"
#include "window/window_manager.h"

#include "compiled/warning_off.h"
#include <RmlUi/Core.h>
#include <cmath>
#include "compiled/warning_on.h"

namespace crittercap
{
namespace
{
const std::string UI_DIR = std::string(RESOURCE_UI_DIR) + "/";

Rml::DataModelHandle model;

// Sprite names in the sprite sheet use dashes (RCSS identifiers), the atlas uses underscores.
std::string SheetName(std::string name)
{
    for (char &c : name)
    {
        if (c == '_')
        {
            c = '-';
        }
    }
    return name;
}
} // namespace

std::string ToUiText(const std::string &text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text)
    {
        if (c == '\x01')
        {
            out += "\xE2\x99\xA5"; // U+2665
        }
        else
        {
            out += c;
        }
    }
    return out;
}

std::string HpColor(const Float ratio)
{
    if (ratio > 0.5f)
    {
        return "#59d966";
    }
    if (ratio > 0.2f)
    {
        return "#facc40";
    }
    return "#f24d40";
}

Bool GameUi::Initialize(const Renderer2D *renderer_2d)
{
    renderer = renderer_2d;
    auto *manager = ui::Manager::GetInstance();
    if (!manager || !manager->GetContext())
    {
        LOGERROR("Critter Cap needs the engine UI");
        return false;
    }
    if (!manager->LoadFontFace(UI_DIR + "pixel.ttf", true))
    {
        return false;
    }
    manager->SetPixelatedImages(true);

    Rml::DataModelConstructor c = manager->GetContext()->CreateDataModel("game");
    if (!c)
    {
        return false;
    }
    if (auto s = c.RegisterStruct<UiSprite>())
    {
        s.RegisterMember("sprite", &UiSprite::sprite);
        s.RegisterMember("x", &UiSprite::x);
        s.RegisterMember("y", &UiSprite::y);
        s.RegisterMember("w", &UiSprite::w);
        s.RegisterMember("h", &UiSprite::h);
    }
    if (auto s = c.RegisterStruct<UiOption>())
    {
        s.RegisterMember("label", &UiOption::label);
        s.RegisterMember("x", &UiOption::x);
        s.RegisterMember("y", &UiOption::y);
        s.RegisterMember("selected", &UiOption::selected);
        s.RegisterMember("disabled", &UiOption::disabled);
    }
    if (auto s = c.RegisterStruct<UiCritterRow>())
    {
        s.RegisterMember("name", &UiCritterRow::name);
        s.RegisterMember("info", &UiCritterRow::info);
        s.RegisterMember("level", &UiCritterRow::level);
        s.RegisterMember("hpText", &UiCritterRow::hpText);
        s.RegisterMember("hpColor", &UiCritterRow::hpColor);
        s.RegisterMember("hpWidth", &UiCritterRow::hpWidth);
        s.RegisterMember("y", &UiCritterRow::y);
        s.RegisterMember("selected", &UiCritterRow::selected);
        s.RegisterMember("fainted", &UiCritterRow::fainted);
        s.RegisterMember("lead", &UiCritterRow::lead);
        s.RegisterMember("out", &UiCritterRow::out);
        s.RegisterMember("sprite", &UiCritterRow::sprite);
    }
    if (auto s = c.RegisterStruct<UiDexEntry>())
    {
        s.RegisterMember("sprite", &UiDexEntry::sprite);
        s.RegisterMember("number", &UiDexEntry::number);
        s.RegisterMember("x", &UiDexEntry::x);
        s.RegisterMember("y", &UiDexEntry::y);
        s.RegisterMember("seen", &UiDexEntry::seen);
        s.RegisterMember("caught", &UiDexEntry::caught);
        s.RegisterMember("selected", &UiDexEntry::selected);
    }
    if (auto s = c.RegisterStruct<UiBattler>())
    {
        s.RegisterMember("visible", &UiBattler::visible);
        s.RegisterMember("name", &UiBattler::name);
        s.RegisterMember("level", &UiBattler::level);
        s.RegisterMember("hpText", &UiBattler::hpText);
        s.RegisterMember("hpColor", &UiBattler::hpColor);
        s.RegisterMember("hpWidth", &UiBattler::hpWidth);
        s.RegisterMember("xpWidth", &UiBattler::xpWidth);
        s.RegisterMember("nameWidth", &UiBattler::nameWidth);
        s.RegisterMember("caught", &UiBattler::caught);
    }
    c.RegisterArray<std::vector<UiSprite>>();
    c.RegisterArray<std::vector<UiOption>>();
    c.RegisterArray<std::vector<UiCritterRow>>();
    c.RegisterArray<std::vector<UiDexEntry>>();

    UiState &s = state;
    c.Bind("screen", &s.screen);
    c.Bind("arrowFrame", &s.arrowFrame);
    c.Bind("blink", &s.blink);
    c.Bind("titleSprites", &s.titleSprites);
    c.Bind("hud", &s.hud);
    c.Bind("hasParty", &s.hasParty);
    c.Bind("caps", &s.caps);
    c.Bind("gems", &s.gems);
    c.Bind("dungeon", &s.dungeon);
    c.Bind("keys", &s.keys);
    c.Bind("bigKey", &s.bigKey);
    c.Bind("prompt", &s.prompt);
    c.Bind("dialog", &s.dialog);
    c.Bind("dialogTop", &s.dialogTop);
    c.Bind("speaker", &s.speaker);
    c.Bind("text", &s.text);
    c.Bind("more", &s.more);
    c.Bind("choice", &s.choice);
    c.Bind("question", &s.question);
    c.Bind("choiceCursor", &s.choiceCursor);
    c.Bind("starter", &s.starter);
    c.Bind("starterCursor", &s.starterCursor);
    c.Bind("starterSprites", &s.starterSprites);
    c.Bind("starterNames", &s.starterNames);
    c.Bind("starterType", &s.starterType);
    c.Bind("starterDex", &s.starterDex);
    c.Bind("menu", &s.menu);
    c.Bind("menuTab", &s.menuTab);
    c.Bind("playTime", &s.playTime);
    c.Bind("party", &s.party);
    c.Bind("gemCount", &s.gemCount);
    c.Bind("pocket", &s.pocket);
    c.Bind("dex", &s.dex);
    c.Bind("caughtText", &s.caughtText);
    c.Bind("dexTitle", &s.dexTitle);
    c.Bind("dexText", &s.dexText);
    c.Bind("dexSeen", &s.dexSeen);
    c.Bind("dexCaught", &s.dexCaught);
    c.Bind("enemy", &s.enemy);
    c.Bind("mine", &s.mine);
    c.Bind("battleMode", &s.battleMode);
    c.Bind("battleOptions", &s.battleOptions);
    c.Bind("moveType", &s.moveType);
    c.Bind("movePower", &s.movePower);
    c.Bind("moveEffect", &s.moveEffect);
    c.Bind("moveStrong", &s.moveStrong);
    c.Bind("battleText", &s.battleText);
    c.Bind("battleMore", &s.battleMore);
    c.Bind("battleTeam", &s.battleTeam);
    c.Bind("battleTeamBack", &s.battleTeamBack);
    c.Bind("battleTeamTitle", &s.battleTeamTitle);
    c.Bind("endingLines", &s.endingLines);
    c.Bind("stats", &s.stats);
    model = c.GetModelHandle();

    document = manager->LoadDocument(UI_DIR + "game.rml");
    if (!document)
    {
        return false;
    }
    document->Show();
    ready = true;
    Commit();
    return true;
}

void GameUi::Shutdown()
{
    if (document)
    {
        document->Close();
        document = nullptr;
    }
    model = {};
    ready = false;
}

void GameUi::Commit()
{
    if (!ready)
    {
        return;
    }
    // The UI is laid out on the 240x160 game screen, which the window shows scaled up.
    const IVec2 size = window::Manager::GetInstance()->GetWindowSize(0);
    if (size.y > 0)
    {
        ui::Manager::GetInstance()->SetDensityIndependentPixelRatio(
            static_cast<Float>(size.y) / 160.0f
        );
    }
    model.DirtyAllVariables();
}

UiSprite GameUi::Sprite(const std::string &name, const Float x, const Float y, const Float scale) const
{
    UiSprite out;
    out.sprite = SheetName(name);
    const SpriteId id = renderer ? renderer->Find(name) : NO_SPRITE;
    if (id == NO_SPRITE)
    {
        out.x = x;
        out.y = y;
        return out;
    }
    const struct Sprite &info = renderer->Get(id);
    out.w = info.width * scale;
    out.h = info.height * scale;
    // Snapped like Renderer2D::Draw.
    out.x = std::floor(x) - std::floor(info.pivotX * scale);
    out.y = std::floor(y) - std::floor(info.pivotY * scale);
    return out;
}
} // namespace crittercap
