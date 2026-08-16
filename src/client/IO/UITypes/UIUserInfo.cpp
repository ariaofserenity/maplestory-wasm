#include "UIUserInfo.h"

#include "../UI.h"
#include "../Components/MapleButton.h"

#include "../../Character/Inventory/Inventory.h"
#include "../../Character/Job.h"
#include "../../Constants.h"
#include "../../Data/EquipData.h"
#include "../../Gameplay/Stage.h"
#include "../../Net/Packets/GameplayPackets.h"

#include "nlnx/nx.hpp"

#include <algorithm>
#include <utility>

namespace jrc
{
    namespace
    {
        // The window is laid out around the avatar on the left and a column
        // of values on the right, both at the offsets the original client
        // draws them at.
        constexpr Point<int16_t> AVATAR_POS = { 60, 127 };
        constexpr int16_t VALUE_X = 153;
        constexpr int16_t ROW_HEIGHT = 18;

        // Text is anchored by the top of its line box, not by the first row
        // of pixels: A11M draws capitals and digits six rows further down.
        // The values below are therefore where the glyphs should land minus
        // that lead-in, rather than the raw coordinates of the original
        // client, whose font carries a different amount of it.
        constexpr int16_t GLYPH_LEAD_IN = 6;
        // Lines up with the top of the 'LEVEL' engraving behind the column.
        constexpr int16_t FIRST_ROW_Y = 72 - GLYPH_LEAD_IN;
        // Centered in the name plate, biased one row up to leave room for
        // the descenders a name can have.
        constexpr Point<int16_t> NAME_POS = { 61, 139 - GLYPH_LEAD_IN };

        // The item list hangs off the right edge of the window, which is
        // where the original client puts it, and shows three rows at a time.
        constexpr Point<int16_t> EQUIP_PANEL = { 271, 0 };
        constexpr int16_t EQUIP_ROWS = 3;
        constexpr int16_t EQUIP_ROW_HEIGHT = 42;
        // Item icons carry their origin at the bottom left, so this is where
        // the icon rests inside its well rather than its top corner.
        constexpr Point<int16_t> EQUIP_ICON_POS = { 12, 60 };
        constexpr Point<int16_t> EQUIP_NAME_POS = { 51, 32 - GLYPH_LEAD_IN };
        constexpr Point<int16_t> EQUIP_LEVEL_POS = { 87, 49 - GLYPH_LEAD_IN };
        // Width of the name field, past which the original client cuts the
        // name short instead of letting it run into the scroll bar.
        constexpr int16_t EQUIP_NAME_WIDTH = 95;
        constexpr int16_t EQUIP_SLIDER_X = EQUIP_PANEL.x() + 150;
        constexpr Range<int16_t> EQUIP_SLIDER_Y = { 27, 136 };
        // The slot a cash weapon arrives under, which is outside the range
        // the other equipment uses.
        constexpr int8_t CASH_WEAPON_SLOT = -111;

        Text make_label(const std::string& content)
        {
            // Item names are data, so the '#' and '\' escapes the text
            // formatter would otherwise act on have to stay literal.
            return Text(Text::A11M, Text::LEFT, Text::BLACK, content, 0, false);
        }

        // Shorten a name that does not fit the field and mark the cut, the
        // way the game does when it lays out this list.
        std::string elide(const std::string& content, int16_t maxwidth)
        {
            Text measured = make_label(content);
            if (measured.width() <= maxwidth)
            {
                return content;
            }

            static const std::string ELLIPSIS = "..";
            int16_t limit = maxwidth - make_label(ELLIPSIS).width();

            size_t keep = 0;
            while (keep < content.size() && measured.advance(keep + 1) <= limit)
            {
                ++keep;
            }
            return content.substr(0, keep) + ELLIPSIS;
        }
    }

    UIUserInfo::UIUserInfo(const Info& userinfo, const CharLook& charlook)
        : UIDragElement<PosUSERINFO>(Point<int16_t>(271, 20)),
          info(userinfo), look(charlook)
    {
        nl::node src = nl::nx::ui["UIWindow2.img"]["UserInfo"]["character"];

        sprites.emplace_back(src["backgrnd"]);
        sprites.emplace_back(src["backgrnd2"]);
        sprites.emplace_back(src["name"]);

        gender = src["gender"][look.is_female() ? "1" : "0"];
        married = src["married"];

        buttons[BT_CLOSE] = std::make_unique<MapleButton>(
            nl::nx::ui["Basic.img"]["BtClose"], Point<int16_t>(255, 8)
        );

        // Every button below carries the origin that places it, so they are
        // created without a position of their own.
        buttons[BT_FAMILY]      = std::make_unique<MapleButton>(src["BtFamily"]);
        buttons[BT_PARTY]       = std::make_unique<MapleButton>(src["BtParty"]);
        buttons[BT_TRADE]       = std::make_unique<MapleButton>(src["BtTrad"]);
        buttons[BT_ITEM]        = std::make_unique<MapleButton>(src["BtItem"]);
        buttons[BT_WISH]        = std::make_unique<MapleButton>(src["BtWish"]);
        buttons[BT_FAME_UP]     = std::make_unique<MapleButton>(src["BtPopUp"]);
        buttons[BT_FAME_DOWN]   = std::make_unique<MapleButton>(src["BtPopDown"]);
        buttons[BT_PERSONALITY] = std::make_unique<MapleButton>(src["BtPersonality"]);
        buttons[BT_COLLECT]     = std::make_unique<MapleButton>(src["BtCollect"]);
        buttons[BT_RIDE]        = std::make_unique<MapleButton>(src["BtRide"]);
        buttons[BT_PET]         = std::make_unique<MapleButton>(src["BtPet"]);

        // The sub windows these open - guild, wish list, traits, medals,
        // mounts, pets and trading - are not part of the client yet, so they
        // stay greyed out the same way the game greys out actions that do
        // not apply to the target.
        buttons[BT_FAMILY]->set_state(Button::DISABLED);
        buttons[BT_TRADE]->set_state(Button::DISABLED);
        buttons[BT_WISH]->set_state(Button::DISABLED);
        buttons[BT_PERSONALITY]->set_state(Button::DISABLED);
        buttons[BT_COLLECT]->set_state(Button::DISABLED);
        buttons[BT_RIDE]->set_state(Button::DISABLED);
        buttons[BT_PET]->set_state(Button::DISABLED);

        // Inviting and faming only make sense for somebody else, and fame
        // additionally requires the level the server checks for.
        bool is_self = Stage::get().is_player(info.cid);
        uint16_t own_level = static_cast<uint16_t>(
            Stage::get().get_player().get_stats().get_stat(Maplestat::LEVEL)
        );
        bool can_fame = !is_self && own_level >= FAME_MIN_LEVEL;

        buttons[BT_PARTY]->set_state(is_self ? Button::DISABLED : Button::NORMAL);
        buttons[BT_FAME_UP]->set_state(can_fame ? Button::NORMAL : Button::DISABLED);
        buttons[BT_FAME_DOWN]->set_state(can_fame ? Button::NORMAL : Button::DISABLED);

        // The portrait is a fixed pose facing right, no matter what the
        // character happened to be doing on the map when it was clicked.
        look.set_direction(true);
        look.set_stance(Stance::STAND1);

        for (size_t i = 0; i < NUM_LABELS; ++i)
        {
            labels[i] = Text(Text::A11M, Text::LEFT, Text::BLACK);
        }

        // The name is centered on its own plate below the avatar, while the
        // remaining values share one column of evenly spaced rows.
        labels[NAME] = Text(Text::A11M, Text::CENTER, Text::BLACK);
        labeloffsets[NAME] = NAME_POS;

        for (size_t i = LEVEL; i < NUM_LABELS; ++i)
        {
            int16_t row = static_cast<int16_t>(i - LEVEL);
            labeloffsets[i] = Point<int16_t>(VALUE_X, FIRST_ROW_Y + ROW_HEIGHT * row);
        }

        labels[NAME].change_text(info.name);
        labels[LEVEL].change_text(std::to_string(info.level));
        labels[JOB].change_text(Job(info.job).get_name());
        labels[FAME].change_text(std::to_string(info.fame));
        labels[GUILD].change_text(info.guild);
        labels[ALLIANCE].change_text(info.alliance);

        nl::node itemsrc = nl::nx::ui["UIWindow2.img"]["UserInfo"]["item"];
        equipbg = itemsrc["backgrnd"];
        equipbg2 = itemsrc["backgrnd2"];

        load_equips();

        show_equips = false;
        equipoffset = 0;
        equipslider = {
            11, EQUIP_SLIDER_Y, EQUIP_SLIDER_X, EQUIP_ROWS,
            static_cast<int16_t>(equips.size()),
            [&](bool upwards) {
                int16_t shift = upwards ? -1 : 1;
                int16_t last = std::max<int16_t>(
                    static_cast<int16_t>(equips.size()) - EQUIP_ROWS, 0
                );
                equipoffset = std::min(std::max<int16_t>(equipoffset + shift, 0), last);
            }
        };
        // A list that fits on screen has nothing to scroll, and the greyed
        // out bar is what the game shows in that case.
        equipslider.setenabled(equips.size() > EQUIP_ROWS);

        dimension = Point<int16_t>(271, 190);
    }

    void UIUserInfo::draw(float alpha) const
    {
        UIElement::draw(alpha);

        look.draw(position + AVATAR_POS, alpha);

        gender.draw(position);
        if (info.married)
        {
            married.draw(position);
        }

        for (size_t i = 0; i < NUM_LABELS; ++i)
        {
            labels[i].draw(position + labeloffsets[i]);
        }

        if (show_equips)
        {
            draw_equips();
        }
    }

    void UIUserInfo::draw_equips() const
    {
        Point<int16_t> panel = position + EQUIP_PANEL;

        equipbg.draw(panel);
        equipbg2.draw(panel);

        for (int16_t row = 0; row < EQUIP_ROWS; ++row)
        {
            size_t index = static_cast<size_t>(equipoffset + row);
            if (index >= equips.size())
            {
                break;
            }

            const EquipEntry& entry = equips[index];
            Point<int16_t> origin = panel + Point<int16_t>(0, row * EQUIP_ROW_HEIGHT);

            entry.icon.draw(origin + EQUIP_ICON_POS);
            entry.name.draw(origin + EQUIP_NAME_POS);
            entry.reqlevel.draw(origin + EQUIP_LEVEL_POS);
        }

        equipslider.draw(position);
    }

    void UIUserInfo::load_equips()
    {
        // The server resends a remote character's look whenever their
        // equipment changes, so their look is current. The player's own look
        // is not rebuilt that way - equipping only hands the new item to the
        // renderer - so their inventory is what has kept up. Reading each
        // from whichever source tracks it keeps the list current for both.
        if (Stage::get().is_player(info.cid))
        {
            load_own_equips();
        }
        else
        {
            load_remote_equips();
        }
    }

    void UIUserInfo::load_own_equips()
    {
        const Inventory& inventory = Stage::get().get_player().get_inventory();

        for (Equipslot::Id slot : Equipslot::values)
        {
            if (slot == Equipslot::NONE ||
                slot == Equipslot::TAMEDMOB ||
                slot == Equipslot::SADDLE)
            {
                continue;
            }

            add_equip(inventory.get_item_id(InventoryType::EQUIPPED, slot));
        }
    }

    void UIUserInfo::load_remote_equips()
    {
        const std::map<int8_t, int32_t>& worn = look.get_equip_ids();
        const std::map<int8_t, int32_t>& hidden = look.get_hidden_equip_ids();

        // Walking the slots in order keeps the list stable, and listing what
        // a cash item covers before the cover itself is the order the game
        // builds this list in.
        for (auto& entry : worn)
        {
            int8_t slot = entry.first;

            // Mount gear belongs to the riding tab rather than this list.
            if (slot == Equipslot::TAMEDMOB || slot == Equipslot::SADDLE)
            {
                continue;
            }

            // A cash weapon is sent apart from the rest, so the weapon slot
            // is where the game folds it back into the list.
            if (slot == Equipslot::WEAPON)
            {
                auto sticker = hidden.find(CASH_WEAPON_SLOT);
                if (sticker != hidden.end())
                {
                    add_equip(sticker->second);
                }
            }

            auto covered = hidden.find(slot);
            if (covered != hidden.end())
            {
                add_equip(covered->second);
            }
            add_equip(entry.second);
        }
    }

    void UIUserInfo::add_equip(int32_t itemid)
    {
        if (itemid <= 0)
        {
            return;
        }

        const ItemData& itemdata = ItemData::get(itemid);
        if (!itemdata.is_valid())
        {
            return;
        }

        EquipEntry entry;
        entry.icon = itemdata.get_icon(false);
        entry.name = make_label(elide(itemdata.get_name(), EQUIP_NAME_WIDTH));
        entry.reqlevel = make_label(std::to_string(
            EquipData::get(itemid).get_reqstat(Maplestat::LEVEL)
        ));

        equips.push_back(std::move(entry));
    }

    void UIUserInfo::toggle_equips()
    {
        show_equips = !show_equips;

        // Widening the window makes the panel part of it, so that clicks on
        // it do not fall through to the map and dragging moves both.
        dimension = show_equips
            ? Point<int16_t>(EQUIP_PANEL.x() + 171, 190)
            : Point<int16_t>(271, 190);

        buttons[BT_ITEM]->set_state(show_equips ? Button::PRESSED : Button::NORMAL);
    }

    void UIUserInfo::update()
    {
        UIElement::update();

        look.update(Constants::TIMESTEP);
    }

    void UIUserInfo::send_key(int32_t, bool pressed, bool escape)
    {
        if (pressed && escape)
        {
            deactivate();
        }
    }

    void UIUserInfo::send_scroll(double yoffset)
    {
        if (show_equips && equipslider.isenabled())
        {
            equipslider.send_scroll(yoffset);
        }
    }

    UIElement::CursorResult UIUserInfo::send_cursor(bool clicked, Point<int16_t> cursorpos)
    {
        // A drag already in progress owns the cursor until it is released.
        if (dragged)
        {
            return UIDragElement::send_cursor(clicked, cursorpos);
        }

        if (show_equips && equipslider.isenabled())
        {
            Cursor::State state = equipslider.send_cursor(cursorpos - position, clicked);
            if (state != Cursor::IDLE)
            {
                return { state, true };
            }
        }

        return UIDragElement::send_cursor(clicked, cursorpos);
    }

    bool UIUserInfo::remove_cursor(bool clicked, Point<int16_t> cursorpos)
    {
        if (equipslider.remove_cursor(clicked))
        {
            return true;
        }

        return UIDragElement::remove_cursor(clicked, cursorpos);
    }

    void UIUserInfo::update_fame(const std::string& target, int16_t fame)
    {
        if (target != info.name)
        {
            return;
        }

        info.fame = fame;
        labels[FAME].change_text(std::to_string(fame));
    }

    int32_t UIUserInfo::get_cid() const
    {
        return info.cid;
    }

    Button::State UIUserInfo::button_pressed(uint16_t buttonid)
    {
        // The shared cursor handling hands presses to buttons regardless of
        // their state, so a greyed out button would both act and light up
        // unless it is turned away here.
        auto entry = buttons.find(buttonid);
        if (entry != buttons.end() && entry->second->get_state() == Button::DISABLED)
        {
            return Button::DISABLED;
        }

        switch (buttonid)
        {
        case BT_CLOSE:
            deactivate();
            return Button::NORMAL;
        case BT_PARTY:
            InviteToPartyPacket(info.name).dispatch();
            return Button::NORMAL;
        case BT_ITEM:
            toggle_equips();
            return buttons[BT_ITEM]->get_state();
        case BT_FAME_UP:
            send_fame(true);
            return Button::NORMAL;
        case BT_FAME_DOWN:
            send_fame(false);
            return Button::NORMAL;
        default:
            return Button::NORMAL;
        }
    }

    void UIUserInfo::send_fame(bool raise)
    {
        GiveFamePacket(info.cid, raise).dispatch();

        // The answer either reports the new fame or an error, and both are
        // easier to read once the buttons stop inviting another click.
        buttons[BT_FAME_UP]->set_state(Button::DISABLED);
        buttons[BT_FAME_DOWN]->set_state(Button::DISABLED);
    }
}
