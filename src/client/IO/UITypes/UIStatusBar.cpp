//////////////////////////////////////////////////////////////////////////////
// This file is part of the Journey MMORPG client                           //
// Copyright © 2015-2016 Daniel Allendorf                                   //
//                                                                          //
// This program is free software: you can redistribute it and/or modify     //
// it under the terms of the GNU Affero General Public License as           //
// published by the Free Software Foundation, either version 3 of the       //
// License, or (at your option) any later version.                          //
//                                                                          //
// This program is distributed in the hope that it will be useful,          //
// but WITHOUT ANY WARRANTY; without even the implied warranty of           //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            //
// GNU Affero General Public License for more details.                      //
//                                                                          //
// You should have received a copy of the GNU Affero General Public License //
// along with this program.  If not, see <http://www.gnu.org/licenses/>.    //
//////////////////////////////////////////////////////////////////////////////
#include "UIStatusBar.h"

#include "UIKeyConfig.h"
#include "UIUserInfo.h"

#include "../KeyBinding.h"
#include "../UI.h"
#include "../Components/MapleButton.h"

#include "../../Character/ExpTable.h"
#include "../../Constants.h"
#include "../../Gameplay/Stage.h"

#include "nlnx/nx.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>


namespace jrc
{
    constexpr Point<int16_t> UIStatusbar::POSITION;
    constexpr Point<int16_t> UIStatusbar::DIMENSION;
    constexpr int32_t UIStatusbar::DEFAULT_QUICKSLOT_KEYS[UIStatusbar::QUICKSLOT_COUNT];

    UIStatusbar::UIStatusbar(const CharStats& st, const Inventory& inv, const Skillbook& sb)
        : UIElement(POSITION, DIMENSION), stats(st), inventory(inv), skillbook(sb), chatbar(POSITION)
    {
        std::copy(
            std::begin(DEFAULT_QUICKSLOT_KEYS),
            std::end(DEFAULT_QUICKSLOT_KEYS),
            quickslot_keys.begin()
        );
        quickslot_counts.fill(-1);

        nl::node mainbar = nl::nx::ui["StatusBar2.img"]["mainBar"];

        sprites.emplace_back(mainbar["backgrnd"]);
        sprites.emplace_back(mainbar["gaugeBackgrd"]);
        sprites.emplace_back(mainbar["notice"]);
        sprites.emplace_back(mainbar["lvBacktrnd"]);
        sprites.emplace_back(mainbar["lvCover"]);

        expbar = {
            mainbar.resolve("gauge/exp/0"),
            mainbar.resolve("gauge/exp/1"),
            mainbar.resolve("gauge/exp/2"),
            308, 0.0f
        };
        hpbar = {
            mainbar.resolve("gauge/hp/0"),
            mainbar.resolve("gauge/hp/1"),
            mainbar.resolve("gauge/hp/2"),
            137, 0.0f
        };
        mpbar = {
            mainbar.resolve("gauge/mp/0"),
            mainbar.resolve("gauge/mp/1"),
            mainbar.resolve("gauge/mp/2"),
            137, 0.0f
        };

        statset   = { mainbar["gauge"]["number"], Charset::RIGHT };
        levelset  = { mainbar["lvNumber"],        Charset::LEFT  };

        joblabel  = { Text::A11M, Text::LEFT, Text::YELLOW };
        namelabel = { Text::A13M, Text::LEFT, Text::WHITE  };

        buttons[BT_WHISPER]   = std::make_unique<MapleButton>(mainbar["BtChat"]);
        buttons[BT_CALLGM]    = std::make_unique<MapleButton>(mainbar["BtClaim"]);

        // Each piece of button artwork carries an origin that MapleButton
        // subtracts when it draws and when it hit tests. Folding that back in
        // here lets the constants in the header read as plain offsets from the
        // bar instead of as corrections to whatever the artwork happens to
        // specify. BtMTS is deliberately absent: the bar shows it in place of
        // BtCashShop rather than alongside it, and drawing both leaves the two
        // overlapping each other and BtMenu.
        auto place_button = [this](uint16_t id, nl::node src, Point<int16_t> offset) {
            Point<int16_t> origin = src["normal"]["0"]["origin"];
            buttons[id] = std::make_unique<MapleButton>(src, offset + origin);
        };

        // Column artwork shared by both popups, taken from System.
        nl::node bubble = mainbar["System"]["backgrnd"];
        bubble_head = bubble["0"];
        bubble_body = bubble["1"];
        bubble_foot = bubble["2"];
        open_bubble = BUBBLE_NONE;
        escape_held = 0;

        // Entries stack down from the top of whichever bubble owns them, so the
        // same placement serves both.
        auto place_entry = [this](uint16_t id, nl::node entry_src, Point<int16_t> bubble_pos, int16_t row) {
            Point<int16_t> at = bubble_pos + Point<int16_t>(
                BUBBLE_ENTRY_INSET,
                static_cast<int16_t>(BUBBLE_ENTRY_TOP + row * BUBBLE_ENTRY_HEIGHT)
            );

            buttons[id] = std::make_unique<MapleButton>(entry_src, at);
            buttons[id]->set_active(false);
        };

        nl::node menu = mainbar["Menu"];
        place_entry(BT_MENU_ITEM,  menu["BtItem"],  MENU_POS, 0);
        place_entry(BT_MENU_EQUIP, menu["BtEquip"], MENU_POS, 1);
        place_entry(BT_MENU_STAT,  menu["BtStat"],  MENU_POS, 2);
        place_entry(BT_MENU_SKILL, menu["BtSkill"], MENU_POS, 3);
        place_entry(BT_MENU_QUEST, menu["BtQuest"], MENU_POS, 4);
        place_entry(BT_MENU_MSN,   menu["BtMSN"],   MENU_POS, 5);

        nl::node system = mainbar["System"];
        place_entry(BT_OPTION_CHANNEL,      system["BtChannel"],      OPTIONS_POS, 0);
        place_entry(BT_OPTION_KEYSETTING,   system["BtKeySetting"],   OPTIONS_POS, 1);
        place_entry(BT_OPTION_GAMEOPTION,   system["BtGameOption"],   OPTIONS_POS, 2);
        place_entry(BT_OPTION_SYSTEMOPTION, system["BtSystemOption"], OPTIONS_POS, 3);
        place_entry(BT_OPTION_QUIT,         system["BtGameQuit"],     OPTIONS_POS, 4);

        place_button(BT_CASHSHOP, mainbar["BtCashShop"], BT_CASHSHOP_POS);
        place_button(BT_MENU,     mainbar["BtMenu"],     BT_MENU_POS);
        place_button(BT_CHANNEL,  mainbar["BtChannel"],  BT_CHANNEL_POS);
        place_button(BT_OPTIONS,  mainbar["BtSystem"],   BT_OPTIONS_POS);

        buttons[BT_CHARACTER] = std::make_unique<MapleButton>(mainbar["BtCharacter"]);
        buttons[BT_STATS]     = std::make_unique<MapleButton>(mainbar["BtStat"]);
        buttons[BT_QUEST]     = std::make_unique<MapleButton>(mainbar["BtQuest"]);
        buttons[BT_INVENTORY] = std::make_unique<MapleButton>(mainbar["BtInven"]);
        buttons[BT_EQUIPS]    = std::make_unique<MapleButton>(mainbar["BtEquip"]);
        buttons[BT_SKILL]     = std::make_unique<MapleButton>(mainbar["BtSkill"]);

        place_button(BT_KEYSETTING, mainbar["BtKeysetting"], BT_KEYSETTING_POS);

        // Shared by every cell, so they are read once here rather than looked
        // up per frame while a cooldown runs.
        nl::node cooltime_src = nl::nx::ui["UIWindow.img"]["Skill"]["CoolTime"];
        for (size_t frame = 0; frame < COOLTIME_FRAMES; ++frame)
        {
            cooltime[frame] = cooltime_src[std::to_string(frame)];
        }

        rebuild_quickslot_labels();
        rebuild_quickslot();
        update_layout_position();
    }

    void UIStatusbar::draw(float alpha) const
    {
        // Drawn in three passes rather than through UIElement::draw, because the
        // bubble has to sit above the bar's own buttons, which it overlaps, while
        // staying below the entries it contains.
        draw_sprites(alpha);

        for (const auto& iter : buttons)
        {
            if (iter.first < BT_MENU_ITEM && iter.second)
            {
                iter.second->draw(position);
            }
        }

        if (open_bubble == BUBBLE_MENU)
        {
            draw_bubble(position + MENU_POS, MENU_HEIGHT);
        }
        else if (open_bubble == BUBBLE_OPTIONS)
        {
            draw_bubble(position + OPTIONS_POS, OPTIONS_HEIGHT);
        }

        for (const auto& iter : buttons)
        {
            if (iter.first >= BT_MENU_ITEM && iter.second)
            {
                iter.second->draw(position);
            }
        }

        expbar.draw(position + Point<int16_t>(-261, -15));
        hpbar.draw(position + Point<int16_t>(-261, -31));
        mpbar.draw(position + Point<int16_t>(-90, -31));

        int16_t level = stats.get_stat(Maplestat::LEVEL);
        int16_t hp    = stats.get_stat(Maplestat::HP);
        int16_t mp    = stats.get_stat(Maplestat::MP);
        int32_t maxhp = stats.get_total(Equipstat::HP);
        int32_t maxmp = stats.get_total(Equipstat::MP);
        int64_t exp   = stats.get_exp();

        std::string expstring = std::to_string(100 * getexppercent());
        statset.draw(
            std::to_string(exp) + "[" + expstring.substr(0, expstring.find('.') + 3) + "%]",
            position + Point<int16_t>(47, -13)
        );
        statset.draw(
            "[" + std::to_string(hp) + "/" + std::to_string(maxhp) + "]",
            position + Point<int16_t>(-124, -29)
        );
        statset.draw(
            "[" + std::to_string(mp) + "/" + std::to_string(maxmp) + "]",
            position + Point<int16_t>(47, -29)
        );
        levelset.draw(
            std::to_string(level),
            position + Point<int16_t>(-480, -24)
        );

        joblabel.draw(position + Point<int16_t>(-435, -21));
        namelabel.draw(position + Point<int16_t>(-435, -36));

        for (size_t i = 0; i < QUICKSLOT_COUNT; ++i)
        {
            Point<int16_t> cell = position + quickslot_position(i);

            if (quickslot_icons[i])
            {
                quickslot_icons[i]->draw(cell);

                if (const Texture* shade = quickslot_cooltime(i))
                {
                    shade->draw(cell);
                }

                continue;
            }

            // Only an empty cell says which key it is. The cell is exactly one
            // icon wide and the bar leaves no room underneath, so a name on a
            // filled one would have to sit across the artwork it is standing in
            // for, and the icon is the more useful of the two to be able to read.
            if (quickslot_labels[i].is_valid())
            {
                Point<int16_t> label = quickslot_labels[i].get_dimensions();

                quickslot_labels[i].draw(cell + Point<int16_t>(
                    static_cast<int16_t>((QUICKSLOT_ICON_SIZE - label.x()) / 2),
                    static_cast<int16_t>(QUICKSLOT_ICON_SIZE - label.y() - QUICKSLOT_LABEL_INSET)
                ));
            }
        }

        chatbar.draw(alpha);
    }

    void UIStatusbar::update()
    {
        update_layout_position();
        UIElement::update();

        rebuild_quickslot();
        chatbar.update();

        expbar.update(getexppercent());
        hpbar.update(gethppercent());
        mpbar.update(getmppercent());

        namelabel.change_text(stats.get_name());
        joblabel.change_text(stats.get_jobname());

        for (auto iter : message_cooldowns)
        {
            iter.second -= Constants::TIMESTEP;
        }

        // Holding escape dismisses an open popup. The key is polled rather than
        // handled as an event because escape is delivered to whichever window is
        // frontmost, so a press or a release can go elsewhere entirely and leave
        // a counter driven by those events stuck part way.
        if (open_bubble != BUBBLE_NONE && UI::get().is_key_pressed(GLFW_KEY_ESCAPE))
        {
            escape_held += Constants::TIMESTEP;

            if (escape_held >= BUBBLE_ESCAPE_HOLD)
            {
                set_bubble(BUBBLE_NONE);
            }
        }
        else
        {
            escape_held = 0;
        }
    }

    void UIStatusbar::draw_bubble(Point<int16_t> at, int16_t height) const
    {
        int16_t body_height = height - BUBBLE_HEAD_HEIGHT - BUBBLE_FOOT_HEIGHT;

        bubble_head.draw(at);
        // The body is a single row of pixels, stretched over whatever room the
        // entries of this particular bubble need.
        bubble_body.draw({
            at + Point<int16_t>(0, BUBBLE_HEAD_HEIGHT),
            Point<int16_t>(BUBBLE_WIDTH, body_height)
        });
        bubble_foot.draw(at + Point<int16_t>(0, BUBBLE_HEAD_HEIGHT + body_height));
    }

    Rectangle<int16_t> UIStatusbar::bubble_bounds() const
    {
        Point<int16_t> at = position;
        int16_t height = 0;

        switch (open_bubble)
        {
        case BUBBLE_MENU:
            at += MENU_POS;
            height = MENU_HEIGHT;
            break;
        case BUBBLE_OPTIONS:
            at += OPTIONS_POS;
            height = OPTIONS_HEIGHT;
            break;
        default:
            return {};
        }

        return { at, at + Point<int16_t>(BUBBLE_WIDTH, height) };
    }

    void UIStatusbar::open_own_userinfo()
    {
        int32_t cid = Stage::get().get_player().get_oid();
        Optional<UIUserInfo> userinfo = UI::get().get_element<UIUserInfo>();

        // The window is built from the server's answer rather than from what
        // the client already knows, so all this can do is ask for the dump and
        // leave the opening to the handler. Only a window already showing the
        // player is closed again: one showing somebody else is a different
        // target, which the fresh dump replaces.
        if (userinfo && userinfo->is_active() && userinfo->get_cid() == cid)
        {
            UI::get().remove(UIUserInfo::TYPE);
            return;
        }

        Stage::get().request_charinfo(cid);
    }

    void UIStatusbar::set_bubble(Bubble which)
    {
        if (open_bubble == which)
        {
            return;
        }

        open_bubble = which;

        auto show = [this](uint16_t first, uint16_t last, bool on) {
            for (uint16_t id = first; id <= last; ++id)
            {
                buttons[id]->set_active(on);
                buttons[id]->set_state(Button::NORMAL);
            }
        };

        show(BT_MENU_ITEM, BT_MENU_MSN, which == BUBBLE_MENU);
        show(BT_OPTION_CHANNEL, BT_OPTION_QUIT, which == BUBBLE_OPTIONS);

        if (which != BUBBLE_NONE)
        {
            Sound(Sound::DLGNOTICE).play();
        }
    }

    void UIStatusbar::set_quickslot_keys(const std::array<int32_t, QUICKSLOT_COUNT>& keys)
    {
        quickslot_keys = keys;
        rebuild_quickslot_labels();
        rebuild_quickslot();
    }

    const std::array<int32_t, UIStatusbar::QUICKSLOT_COUNT>& UIStatusbar::get_quickslot_keys() const
    {
        return quickslot_keys;
    }

    void UIStatusbar::rebuild_quickslot_labels()
    {
        // Named per key code rather than per cell, so the names follow the keys
        // when the server hands back an order other than the stock one. The set
        // belongs to the dialog that reorders the quickslot, which is why it is
        // sized for these cells rather than for the key config's larger caps.
        nl::node names = nl::nx::ui["UIWindow2.img"]["KeyConfig"]["quickslotConfig"]["key"];

        for (size_t i = 0; i < QUICKSLOT_COUNT; ++i)
        {
            nl::node name = names[std::to_string(quickslot_keys[i])];
            quickslot_labels[i] = name ? Texture(name) : Texture();
        }
    }

    void UIStatusbar::rebuild_quickslot()
    {
        const Keyboard& keyboard = UI::get().get_keyboard();

        for (size_t i = 0; i < QUICKSLOT_COUNT; ++i)
        {
            // A cell being dragged from has handed its icon out as a bare
            // pointer, which the drag holds until it is dropped. Replacing the
            // icon underneath it, which a stack shrinking mid-drag would
            // otherwise do, leaves that pointer dangling.
            if (quickslot_icons[i] && quickslot_icons[i]->is_dragged())
            {
                continue;
            }

            Keyboard::Mapping mapping = keyboard.get_maple_mapping(quickslot_keys[i]);

            // Stacks are only shown for items, and the number is the whole
            // inventory rather than one slot, because the key uses whichever
            // stack comes to hand rather than a slot the player picked.
            int16_t count = -1;
            if (mapping.type == KeyType::ITEM || mapping.type == KeyType::CASH)
            {
                count = static_cast<int16_t>(inventory.get_total_item_count(mapping.action));
            }

            if (quickslot_bindings[i] == mapping && quickslot_counts[i] == count)
            {
                continue;
            }

            quickslot_bindings[i] = mapping;
            quickslot_counts[i] = count;

            Texture texture = KeyBinding::icon_for(mapping);
            if (!texture.is_valid())
            {
                quickslot_icons[i].reset();
                continue;
            }

            quickslot_icons[i] = std::make_unique<Icon>(
                std::make_unique<QuickslotIcon>(mapping), texture, count
            );
        }
    }

    const Texture* UIStatusbar::quickslot_cooltime(size_t index) const
    {
        const Keyboard::Mapping& mapping = quickslot_bindings[index];
        if (mapping.type != KeyType::SKILL)
        {
            return nullptr;
        }

        Player::Cooldown cd = Stage::get().get_player().get_cooldown(mapping.action);
        if (cd.total <= 0)
        {
            return nullptr;
        }

        // Stepped on whole seconds of remaining time rather than on the raw
        // milliseconds, so a long cooldown moves in visible steps instead of
        // creeping, and a short one still reaches the end of the run.
        int32_t total = cd.total / 1000;
        int32_t elapsed = total - cd.remaining / 1000;
        if (total <= 0)
        {
            return nullptr;
        }

        auto frame = static_cast<size_t>(
            std::clamp<int32_t>(elapsed * COOLTIME_LAST / total, 0, COOLTIME_LAST)
        );

        return &cooltime[frame];
    }

    Point<int16_t> UIStatusbar::quickslot_position(size_t index) const
    {
        auto column = static_cast<int16_t>(index % QUICKSLOT_COLUMNS);
        auto row = static_cast<int16_t>(index / QUICKSLOT_COLUMNS);

        return QUICKSLOT_POS + Point<int16_t>(
            static_cast<int16_t>(column * QUICKSLOT_STEP),
            static_cast<int16_t>(row * QUICKSLOT_STEP)
        );
    }

    Rectangle<int16_t> UIStatusbar::quickslot_bounds() const
    {
        constexpr auto rows = static_cast<int16_t>(QUICKSLOT_COUNT / QUICKSLOT_COLUMNS);

        Point<int16_t> lt = position + QUICKSLOT_POS;
        Point<int16_t> size = {
            static_cast<int16_t>((QUICKSLOT_COLUMNS - 1) * QUICKSLOT_STEP + QUICKSLOT_ICON_SIZE),
            static_cast<int16_t>((rows - 1) * QUICKSLOT_STEP + QUICKSLOT_ICON_SIZE)
        };

        return { lt, lt + size };
    }

    size_t UIStatusbar::quickslot_by_position(Point<int16_t> cursorpos) const
    {
        for (size_t i = 0; i < QUICKSLOT_COUNT; ++i)
        {
            Point<int16_t> lt = position + quickslot_position(i);
            Rectangle<int16_t> cell(
                lt,
                lt + Point<int16_t>(QUICKSLOT_ICON_SIZE, QUICKSLOT_ICON_SIZE)
            );

            if (cell.contains(cursorpos))
            {
                return i;
            }
        }

        return QUICKSLOT_COUNT;
    }

    void UIStatusbar::show_quickslot_tooltip(size_t index) const
    {
        const Keyboard::Mapping& mapping = quickslot_bindings[index];

        switch (mapping.type)
        {
        case KeyType::SKILL:
            UI::get().show_skill(
                Tooltip::STATUSBAR,
                mapping.action,
                skillbook.get_level(mapping.action),
                skillbook.get_masterlevel(mapping.action),
                skillbook.get_expiration(mapping.action)
            );
            break;
        case KeyType::ITEM:
        case KeyType::CASH:
            UI::get().show_item(Tooltip::STATUSBAR, mapping.action);
            break;
        default:
            UI::get().clear_tooltip(Tooltip::STATUSBAR);
            break;
        }
    }

    void UIStatusbar::QuickslotIcon::drop_on_stage() const
    {
        KeyBinding::unbind(mapping);
    }

    void UIStatusbar::QuickslotIcon::drop_on_bindings(Point<int16_t> cursorposition, bool remove) const
    {
        // Dropped back into the key config window, which stages its changes
        // instead of sending them, so the binding has to travel through it
        // rather than being applied here.
        if (auto keyconfig = UI::get().get_element<UIKeyConfig>())
        {
            if (remove)
            {
                keyconfig->unstage_mapping(mapping);
            }
            else
            {
                keyconfig->stage_mapping(cursorposition, mapping);
            }
        }
    }

    void UIStatusbar::update_layout_position()
    {
        position = {
            POSITION.x(),
            static_cast<int16_t>(Constants::viewheight() - Constants::VIEWYOFFSET)
        };
        chatbar.set_position(position);
    }

    Button::State UIStatusbar::button_pressed(uint16_t id)
    {
        switch (id)
        {
        case BT_CHARACTER:
            open_own_userinfo();
            return Button::NORMAL;
        case BT_STATS:
            UI::get().send_menu(KeyAction::CHARSTATS);
            return Button::NORMAL;
        case BT_INVENTORY:
            UI::get().send_menu(KeyAction::INVENTORY);
            return Button::NORMAL;
        case BT_EQUIPS:
            UI::get().send_menu(KeyAction::EQUIPS);
            return Button::NORMAL;
        case BT_SKILL:
            UI::get().send_menu(KeyAction::SKILLBOOK);
            return Button::NORMAL;
        case BT_QUEST:
            UI::get().send_menu(KeyAction::QUESTLOG);
            return Button::NORMAL;
        case BT_KEYSETTING:
            UI::get().send_menu(KeyAction::KEYCONFIG);
            return Button::NORMAL;
        case BT_MENU:
            set_bubble(open_bubble == BUBBLE_MENU ? BUBBLE_NONE : BUBBLE_MENU);
            return Button::NORMAL;
        case BT_OPTIONS:
            set_bubble(open_bubble == BUBBLE_OPTIONS ? BUBBLE_NONE : BUBBLE_OPTIONS);
            return Button::NORMAL;
        case BT_MENU_ITEM:
            UI::get().send_menu(KeyAction::INVENTORY);
            set_bubble(BUBBLE_NONE);
            return Button::NORMAL;
        case BT_MENU_EQUIP:
            UI::get().send_menu(KeyAction::EQUIPS);
            set_bubble(BUBBLE_NONE);
            return Button::NORMAL;
        case BT_MENU_STAT:
            UI::get().send_menu(KeyAction::CHARSTATS);
            set_bubble(BUBBLE_NONE);
            return Button::NORMAL;
        case BT_MENU_SKILL:
            UI::get().send_menu(KeyAction::SKILLBOOK);
            set_bubble(BUBBLE_NONE);
            return Button::NORMAL;
        case BT_OPTION_KEYSETTING:
            UI::get().send_menu(KeyAction::KEYCONFIG);
            set_bubble(BUBBLE_NONE);
            return Button::NORMAL;
        case BT_MENU_QUEST:
            UI::get().send_menu(KeyAction::QUESTLOG);
            set_bubble(BUBBLE_NONE);
            return Button::NORMAL;
        case BT_MENU_MSN:
        case BT_OPTION_CHANNEL:
        case BT_OPTION_GAMEOPTION:
        case BT_OPTION_SYSTEMOPTION:
        case BT_OPTION_QUIT:
            // Nothing behind these two yet, but they still close the bubble so
            // it does not sit open over an unchanged screen.
            set_bubble(BUBBLE_NONE);
            return Button::NORMAL;
        default:
            // Nothing is wired to these yet, but they still have to settle back
            // to a resting state. Returning PRESSED would leave them stuck in
            // that artwork, since the shared click handling only ever clears a
            // hover, never a press.
            return Button::NORMAL;
        }
    }

    bool UIStatusbar::is_in_range(Point<int16_t> cursorpos) const
    {
        Rectangle<int16_t> bounds(
            position - Point<int16_t>(512, 84),
            position - Point<int16_t>(512, 84) + dimension
        );

        // A bubble rises well above the bar's own strip, so without this its
        // entries would sit outside the area that receives the cursor at all.
        if (open_bubble != BUBBLE_NONE && bubble_bounds().contains(cursorpos))
        {
            return true;
        }

        // The bottom row of quickslot cells ends a couple of pixels below the
        // area above, which would otherwise leave a strip of them unreachable.
        if (quickslot_bounds().contains(cursorpos))
        {
            return true;
        }

        return bounds.contains(cursorpos) || chatbar.is_in_range(cursorpos);
    }

    bool UIStatusbar::remove_cursor(bool clicked, Point<int16_t> cursorpos)
    {
        UI::get().clear_tooltip(Tooltip::STATUSBAR);

        if (chatbar.remove_cursor(clicked, cursorpos))
        {
            return true;
        }

        return UIElement::remove_cursor(clicked, cursorpos);
    }

    UIElement::CursorResult UIStatusbar::send_cursor(bool pressed, Point<int16_t> cursorpos)
    {
        if (size_t slot = quickslot_by_position(cursorpos); slot < QUICKSLOT_COUNT)
        {
            show_quickslot_tooltip(slot);

            if (quickslot_icons[slot])
            {
                if (pressed)
                {
                    // A press starts a drag rather than firing the binding,
                    // matching every other icon in the ui. The action still runs
                    // if the button comes back up without the cursor having left
                    // the cell, which is handled where the drag is dropped.
                    quickslot_icons[slot]->start_drag(cursorpos - position - quickslot_position(slot));
                    UI::get().drag_icon(quickslot_icons[slot].get());
                    return { Cursor::GRABBING, true };
                }

                return { Cursor::CANGRAB, true };
            }

            return { Cursor::IDLE, true };
        }

        UI::get().clear_tooltip(Tooltip::STATUSBAR);

        if (chatbar.is_in_range(cursorpos))
        {
            if (CursorResult child_result = chatbar.send_cursor(pressed, cursorpos))
            {
                return child_result;
            }

            return UIElement::send_cursor(pressed, cursorpos);
        }

        chatbar.send_cursor(pressed, cursorpos);
        return UIElement::send_cursor(pressed, cursorpos);
    }

    void UIStatusbar::send_icon(const Icon& icon, Point<int16_t> cursorpos)
    {
        size_t slot = quickslot_by_position(cursorpos);
        if (slot >= QUICKSLOT_COUNT)
        {
            return;
        }

        Keyboard::Mapping mapping = icon.get_binding();
        if (mapping.type == KeyType::NONE)
        {
            return;
        }

        // Lifted off a cell and put straight back down on it. Nothing moved, so
        // the player meant to click it rather than to drag it anywhere.
        if (mapping == quickslot_bindings[slot])
        {
            UI::get().send_action(mapping.type, mapping.action);
            return;
        }

        // Deliberately not rebuilt here. The icon that was dropped may well be
        // one of these, and the drag still holds a pointer to it for a moment
        // after this returns; the next tick picks the change up instead.
        KeyBinding::bind(quickslot_keys[slot], mapping);
    }

    void UIStatusbar::send_chatline(const std::string& line, UIChatbar::LineType type)
    {
        chatbar.send_line(line, type);
    }

    void UIStatusbar::focus_chatfield()
    {
        chatbar.focus_chatfield();
    }

    void UIStatusbar::set_chat_target(UIChatbar::ChatTarget target)
    {
        chatbar.set_chat_target(target);
    }

    void UIStatusbar::cycle_chat_target()
    {
        chatbar.cycle_chat_target();
    }

    void UIStatusbar::set_pending_party_invite(int32_t party_id, const std::string& inviter)
    {
        chatbar.set_pending_party_invite(party_id, inviter);
    }

    void UIStatusbar::clear_pending_party_invite()
    {
        chatbar.clear_pending_party_invite();
    }

    void UIStatusbar::set_party_state(int32_t party_id, int32_t leader_id, const std::vector<UIChatbar::PartyMember>& members)
    {
        chatbar.set_party_state(party_id, leader_id, members);
    }

    void UIStatusbar::clear_party_state()
    {
        chatbar.clear_party_state();
    }

    void UIStatusbar::set_party_leader(int32_t leader_id)
    {
        chatbar.set_party_leader(leader_id);
    }

    void UIStatusbar::update_party_member_hp(int32_t cid, int32_t hp, int32_t max_hp)
    {
        chatbar.update_party_member_hp(cid, hp, max_hp);
    }

    int32_t UIStatusbar::get_party_id() const
    {
        return chatbar.get_party_id();
    }

    int32_t UIStatusbar::get_party_leader_id() const
    {
        return chatbar.get_party_leader_id();
    }

    int32_t UIStatusbar::get_pending_party_invite_id() const
    {
        return chatbar.get_pending_party_invite_id();
    }

    const std::string& UIStatusbar::get_pending_party_inviter() const
    {
        return chatbar.get_pending_party_inviter();
    }

    const std::vector<UIChatbar::PartyMember>& UIStatusbar::get_party_members() const
    {
        return chatbar.get_party_members();
    }

    void UIStatusbar::display_message(Messages::Type line, UIChatbar::LineType type)
    {
        if (message_cooldowns[line] > 0)
        {
            return;
        }

        std::string message{ Messages::messages[line] };
        chatbar.send_line(message, type);

        message_cooldowns[line] = MESSAGE_COOLDOWN;
    }

    float UIStatusbar::getexppercent() const
    {
        int16_t level = stats.get_stat(Maplestat::LEVEL);
        if (level >= ExpTable::LEVELCAP)
        {
            return 0.0f;
        }

        int64_t exp = stats.get_exp();
        return static_cast<float>(
            static_cast<double>(exp) / ExpTable::values[level]
        );
    }

    float UIStatusbar::gethppercent() const
    {
        int16_t hp = stats.get_stat(Maplestat::HP);
        int32_t maxhp = stats.get_total(Equipstat::HP);

        return static_cast<float>(hp) / maxhp;
    }

    float UIStatusbar::getmppercent() const
    {
        int16_t mp = stats.get_stat(Maplestat::MP);
        int32_t maxmp = stats.get_total(Equipstat::MP);

        return static_cast<float>(mp) / maxmp;
    }
}
