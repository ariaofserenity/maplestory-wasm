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
#include "UIItemInventory.h"

#include "../UI.h"
#include "../Components/MapleButton.h"
#include "../Components/TwoSpriteButton.h"
#include "UIKeyConfig.h"
#include "UINotice.h"

#include "../../Audio/Audio.h"
#include "../../Data/ItemData.h"
#include "../../Net/Packets/InventoryPackets.h"
#include "../../Util/Misc.h"

#include "nlnx/nx.hpp"

#include <algorithm>

namespace jrc
{
    UIItemInventory::UIItemInventory(const Inventory& invent)
        : UIDragElement<PosINV>(Point<int16_t>(172, 20)), inventory(invent) {

        nl::node src = nl::nx::ui["UIWindow2.img"]["Item"];

        sprites.emplace_back(src["backgrnd"]);
        sprites.emplace_back(src["backgrnd2"]);
        sprites.emplace_back(src["backgrnd3"]);

        newitemslot = src["New"]["inventory"];
        newitemtab = src["New"]["Tab0"];
        newitemcurrenttab = src["New"]["Tab1"];
        projectile = src["activeIcon"];

        nl::node taben = src["Tab"]["enabled"];
        nl::node tabdis = src["Tab"]["disabled"];

        buttons[BT_TAB_EQUIP]= std::make_unique<TwoSpriteButton>(tabdis["0"], taben["0"], Point<int16_t>(-1, -4));
        buttons[BT_TAB_USE] = std::make_unique<TwoSpriteButton>(tabdis["1"], taben["1"], Point<int16_t>(-1, -4));
        buttons[BT_TAB_ETC] = std::make_unique<TwoSpriteButton>(tabdis["2"], taben["2"], Point<int16_t>(0, -4));
        buttons[BT_TAB_SETUP] = std::make_unique<TwoSpriteButton>(tabdis["3"], taben["3"], Point<int16_t>(-1, -4));
        buttons[BT_TAB_CASH]= std::make_unique<TwoSpriteButton>(tabdis["4"], taben["4"], Point<int16_t>(-1, -4));

        buttons[BT_DROPMESO]= std::make_unique<MapleButton>(src["BtCoin"]);
        buttons[BT_POINTS]= std::make_unique<MapleButton>(src["BtPoint0"]);
        buttons[BT_GATHER]= std::make_unique<MapleButton>(src["BtGather"]);
        buttons[BT_SORT]= std::make_unique<MapleButton>(src["BtSort"]);
        buttons[BT_EXPAND]= std::make_unique<MapleButton>(src["BtFull"]);
        buttons[BT_ITEMPOT]= std::make_unique<MapleButton>(src["BtPot3"]);
        buttons[BT_UPGRADE]= std::make_unique<MapleButton>(src["BtUpgrade3"]);
        buttons[BT_MAGNIFY]= std::make_unique<MapleButton>(src["BtAppraise3"]);
        buttons[BT_BITCASE]= std::make_unique<MapleButton>(src["BtBits3"]);

        tab = InventoryType::EQUIP;
        slotrange.first = 1;
        slotrange.second = 24;
        newtab = InventoryType::NONE;
        newslot = 0;

        buttons[BT_SORT]->set_active(false);
        buttons[button_by_tab(tab)]->set_state(Button::PRESSED);

        mesolabel = { Text::A11M, Text::RIGHT, Text::LIGHTGREY };

        slider = {
            11, { 50, 248 }, 152, 6, 1 + inventory.get_slotmax(tab) / 4,
            [&](bool upwards) {
            int16_t shift = upwards ? -4 : 4;
            bool above = slotrange.first + shift > 0;
            bool below = slotrange.second + shift < inventory.get_slotmax(tab) + 1 + 4;
            if (above && below)
            {
                slotrange.first += shift;
                slotrange.second += shift;
            }
        } };

        dimension = { 172, 335 };
        active = true;

        load_icons();
    }

    void UIItemInventory::draw(float alpha) const
    {
        UIElement::draw(alpha);

        slider.draw(position);

        for (auto& icon : icons)
        {
            int16_t slot = icon.first;
            if (icon.second && slot >= slotrange.first && slot <= slotrange.second)
            {
                Point<int16_t> slotpos = get_slotpos(slot);
                icon.second->draw(position + slotpos);
            }
        }

        int16_t bulletslot = inventory.get_bulletslot();
        if (tab == InventoryType::USE && is_visible(bulletslot))
        {
            Point<int16_t> bulletslotpos = position + get_slotpos(bulletslot);
            projectile.draw({ bulletslotpos });
        }
        else if (newtab == tab && is_visible(newslot))
        {
            Point<int16_t> newslotpos = position + get_slotpos(newslot);
            newslotpos.shift_y(1);
            newitemslot.draw({ newslotpos }, alpha);
        }

        if (newtab != InventoryType::NONE)
        {
            // The tab the item landed in is marked either way; the open tab
            // just gets the quieter of the two animations.
            Point<int16_t> newtabpos = position + get_tabpos(newtab);
            const Animation& tabeffect = (newtab == tab) ? newitemcurrenttab : newitemtab;
            tabeffect.draw({ newtabpos }, alpha);
        }

        Point<int16_t> mesopos = position + Point<int16_t>(124, 264);
        mesolabel.draw(mesopos);
    }

    void UIItemInventory::update()
    {
        UIElement::update();

        newitemtab.update(6);
        newitemcurrenttab.update(6);
        newitemslot.update(6);

        int64_t meso = inventory.get_meso();
        std::string mesostr = std::to_string(meso);
        string_format::split_number(mesostr);
        mesolabel.change_text(mesostr);
    }

    void UIItemInventory::update_slot(int16_t slot)
    {
        if (int32_t item_id = inventory.get_item_id(tab, slot))
        {
            int16_t count;
            if (tab == InventoryType::EQUIP)
            {
                count = -1;
            }
            else
            {
                count = inventory.get_item_count(tab, slot);
            }

            const Texture& texture = ItemData::get(item_id).get_icon(false);
            Equipslot::Id eqslot = inventory.find_equipslot(item_id);
            icons[slot] = std::make_unique<Icon>(
                std::make_unique<ItemIcon>(tab, eqslot, slot, item_id, count, is_splittable(slot)),
                texture,
                count
                );
        }
        else if (icons.count(slot))
        {
            icons.erase(slot);
        }
    }

    bool UIItemInventory::is_splittable(int16_t slot) const
    {
        // Only the three plain bundle tabs hold stacks that can be broken up.
        if (tab != InventoryType::USE && tab != InventoryType::SETUP && tab != InventoryType::ETC)
            return false;

        // Throwing stars and bullets are recharged rather than stacked, so
        // they leave the inventory as one item however many rounds are left.
        int32_t item_id = inventory.get_item_id(tab, slot);
        int32_t prefix = item_id / 10000;
        if (prefix == 207 || prefix == 233)
            return false;

        return inventory.is_permanent(tab, slot);
    }

    void UIItemInventory::load_icons()
    {
        icons.clear();

        uint8_t numslots = inventory.get_slotmax(tab);
        for (uint8_t i = 1; i < numslots; ++i)
        {
            update_slot(i);
        }
    }

    Button::State UIItemInventory::button_pressed(uint16_t buttonid)
    {
        InventoryType::Id oldtab = tab;
        switch (buttonid)
        {
        case BT_TAB_EQUIP:
            tab = InventoryType::EQUIP;
            break;
        case BT_TAB_USE:
            tab = InventoryType::USE;
            break;
        case BT_TAB_SETUP:
            tab = InventoryType::SETUP;
            break;
        case BT_TAB_ETC:
            tab = InventoryType::ETC;
            break;
        case BT_TAB_CASH:
            tab = InventoryType::CASH;
            break;
        case BT_GATHER:
            GatherItemsPacket(tab).dispatch();
            break;
        case BT_SORT:
            SortItemsPacket(tab).dispatch();
            break;
        case BT_DROPMESO:
            drop_mesos();
            // Only the tabs stay latched; this one pops back out.
            return Button::NORMAL;
        }

        if (tab != oldtab)
        {
            slotrange.first = 1;
            slotrange.second = 24;

            slider.setrows(6, 1 + inventory.get_slotmax(tab) / 4);

            buttons[button_by_tab(oldtab)]->set_state(Button::NORMAL);

            load_icons();
            enable_gather();
        }
        return Button::PRESSED;
    }

    void UIItemInventory::doubleclick(Point<int16_t> cursorpos)
    {
        int16_t slot = slot_by_position(cursorpos - position);
        if (icons.count(slot) && is_visible(slot))
        {
            if (int32_t item_id = inventory.get_item_id(tab, slot))
            {
                switch (tab)
                {
                case InventoryType::EQUIP:
                    Sound(Sound::DRAGEND).play();
                    EquipItemPacket(
                        slot,
                        inventory.find_equipslot(item_id)
                    ).dispatch();
                    break;
                case InventoryType::USE:
                    if (
                        item_id / 10000 != 204 &&
                        item_id / 10000 != 206 &&
                        item_id / 10000 != 207
                    ) {
                        UseItemPacket(slot, item_id).dispatch();
                    }
                    break;
                default:
                    break;
                }
            }
        }
    }

    void UIItemInventory::send_icon(const Icon& icon, Point<int16_t> cursorpos)
    {
        int16_t slot = slot_by_position(cursorpos - position);
        if (slot > 0)
        {
            int32_t item_id = inventory.get_item_id(tab, slot);
            Equipslot::Id eqslot;
            bool equip;
            if (item_id && tab == InventoryType::EQUIP)
            {
                eqslot = inventory.find_equipslot(item_id);
                equip = true;
            }
            else
            {
                eqslot = Equipslot::NONE;
                equip = false;
            }
            icon.drop_on_items(tab, eqslot, slot, equip);
        }
    }

    UIElement::CursorResult UIItemInventory::send_cursor(bool pressed, Point<int16_t> cursorpos)
    {
        if (dragged)
        {
            clear_tooltip();
            return UIDragElement::send_cursor(pressed, cursorpos);
        }

        Point<int16_t> cursor_relative = cursorpos - position;
        if (slider.isenabled())
        {
            Cursor::State sstate = slider.send_cursor(cursor_relative, pressed);
            if (sstate != Cursor::IDLE)
            {
                clear_tooltip();
                return { sstate, true };
            }
        }

        int16_t slot = slot_by_position(cursor_relative);
        Icon* icon = get_icon(slot);
        if (icon && is_visible(slot))
        {
            if (pressed)
            {
                Point<int16_t> slotpos = get_slotpos(slot);
                icon->start_drag(cursor_relative - slotpos);
                UI::get().drag_icon(icon);
                Sound(Sound::DRAGSTART).play();

                clear_tooltip();
                return { Cursor::GRABBING, true };
            }
            else
            {
                show_item(slot);
                return { Cursor::CANGRAB, true };
            }
        }

        clear_tooltip();
        return UIDragElement::send_cursor(pressed, cursorpos);
    }

    void UIItemInventory::modify(InventoryType::Id type, int16_t slot, int8_t mode, int16_t arg)
    {
        if (slot <= 0)
            return;

        if (type == tab)
        {
            switch (mode)
            {
            case Inventory::ADD:
                update_slot(slot);
                newtab = type;
                newslot = slot;
                break;
            case Inventory::CHANGECOUNT:
            case Inventory::ADDCOUNT:
                if (auto icon = get_icon(slot))
                    icon->set_count(arg);
                break;
            case Inventory::SWAP:
                if (arg != slot)
                {
                    update_slot(slot);
                    update_slot(arg);
                }
                break;
            case Inventory::REMOVE:
                update_slot(slot);
                break;
            }
        }

        switch (mode)
        {
        case Inventory::ADD:
        case Inventory::ADDCOUNT:
            newtab = type;
            newslot = slot;
            break;
        case Inventory::SWAP:
            // The item did not leave the inventory, so the mark follows it
            // into whichever of the two slots it ended up in.
            if (newtab == type)
            {
                if (newslot == slot)
                    newslot = arg;
                else if (newslot == arg)
                    newslot = slot;
            }
            break;
        case Inventory::CHANGECOUNT:
        case Inventory::REMOVE:
            if (newslot == slot && newtab == type)
            {
                newslot = 0;
                newtab = InventoryType::NONE;
            }
            break;
        }
    }

    void UIItemInventory::refresh(InventoryType::Id type, int16_t slot)
    {
        if (slot > 0 && type == tab)
        {
            update_slot(slot);
        }
    }

    void UIItemInventory::drop_mesos()
    {
        int64_t owned = inventory.get_meso();
        if (owned < DropMesosPacket::MIN)
            return;

        int32_t most = static_cast<int32_t>(
            std::min<int64_t>(owned, DropMesosPacket::MAX)
            );

        auto onenter = [](int32_t amount) {
            DropMesosPacket(amount).dispatch();
        };

        UI::get().emplace<UIEnterNumber>(
            "How many mesos would you like to drop?",
            onenter,
            DropMesosPacket::MIN,
            most,
            DropMesosPacket::MIN
            );
    }

    void UIItemInventory::enable_sort()
    {
        buttons[BT_GATHER]->set_active(false);
        buttons[BT_SORT]->set_active(true);
        buttons[BT_SORT]->set_state(Button::NORMAL);
    }

    void UIItemInventory::enable_gather()
    {
        buttons[BT_SORT]->set_active(false);
        buttons[BT_GATHER]->set_active(true);
        buttons[BT_GATHER]->set_state(Button::NORMAL);
    }

    void UIItemInventory::toggle_active()
    {
        clear_tooltip();
        UIElement::toggle_active();
    }

    bool UIItemInventory::remove_cursor(bool clicked, Point<int16_t> cursorpos)
    {
        if (UIDragElement::remove_cursor(clicked, cursorpos))
            return true;

        return slider.remove_cursor(clicked);
    }

    void UIItemInventory::send_key(int32_t, bool pressed, bool escape)
    {
        if (pressed && escape)
        {
            toggle_active();
        }
    }

    void UIItemInventory::show_item(int16_t slot)
    {
        if (tab == InventoryType::EQUIP)
        {
            UI::get().show_equip(Tooltip::ITEMINVENTORY, slot);
        }
        else
        {
            int32_t item_id = inventory.get_item_id(tab, slot);
            UI::get().show_item(Tooltip::ITEMINVENTORY, item_id);
        }
    }

    void UIItemInventory::clear_tooltip()
    {
        UI::get().clear_tooltip(Tooltip::ITEMINVENTORY);
    }

    bool UIItemInventory::is_visible(int16_t slot) const
    {
        return !is_not_visible(slot);
    }

    bool UIItemInventory::is_not_visible(int16_t slot) const
    {
        return slot < slotrange.first || slot > slotrange.second;
    }

    int16_t UIItemInventory::slot_by_position(Point<int16_t> cursorpos) const
    {
        int16_t xoff = cursorpos.x() - 11;
        int16_t yoff = cursorpos.y() - 51;
        if (xoff < 1 || xoff > 143 || yoff < 1)
            return 0;

        int16_t slot = slotrange.first + (xoff / 36) + 4 * (yoff / 35);
        return is_visible(slot) ? slot : 0;
    }

    Point<int16_t> UIItemInventory::get_slotpos(int16_t slot) const
    {
        int16_t absslot = slot - slotrange.first;
        return Point<int16_t>(
            11 + (absslot % 4) * 36,
            51 + (absslot / 4) * 35
            );
    }

    Point<int16_t> UIItemInventory::get_tabpos(InventoryType::Id tb) const
    {
        switch (tb)
        {
        case InventoryType::EQUIP:
            return Point<int16_t>(10, 28);
        case InventoryType::USE:
            return Point<int16_t>(42, 28);
        // Etc sits third and Set-up fourth, matching the order the tab
        // graphics are laid out in and the order the original client uses.
        case InventoryType::ETC:
            return Point<int16_t>(74, 28);
        case InventoryType::SETUP:
            return Point<int16_t>(105, 28);
        case InventoryType::CASH:
            return Point<int16_t>(138, 28);
        default:
            return Point<int16_t>();
        }
    }

    uint16_t UIItemInventory::button_by_tab(InventoryType::Id tb) const
    {
        switch (tb)
        {
        case InventoryType::EQUIP:
            return BT_TAB_EQUIP;
        case InventoryType::USE:
            return BT_TAB_USE;
        case InventoryType::SETUP:
            return BT_TAB_SETUP;
        case InventoryType::ETC:
            return BT_TAB_ETC;
        default:
            return BT_TAB_CASH;
        }
    }

    Icon* UIItemInventory::get_icon(int16_t slot)
    {
        auto iter = icons.find(slot);
        if (iter != icons.end())
        {
            return iter->second.get();
        }
        else
        {
            return nullptr;
        }
    }


    UIItemInventory::ItemIcon::ItemIcon(InventoryType::Id st, Equipslot::Id eqs, int16_t s,
        int32_t id, int16_t c, bool sp) {

        sourcetab = st;
        eqsource = eqs;
        source = s;
        item_id = id;
        count = c;
        splittable = sp;
    }

    void UIItemInventory::ItemIcon::set_count(int16_t c)
    {
        count = c;
    }

    void UIItemInventory::ItemIcon::drop_on_stage() const
    {
        if (splittable && count > 1)
        {
            InventoryType::Id tab = sourcetab;
            int16_t slot = source;
            auto onenter = [tab, slot](int32_t quantity) {
                MoveItemPacket(tab, slot, 0, static_cast<int16_t>(quantity)).dispatch();
            };

            UI::get().emplace<UIEnterNumber>(
                "How many will you drop?", onenter, 1, count, count
                );
            return;
        }

        MoveItemPacket(sourcetab, source, 0, 1).dispatch();
    }

    void UIItemInventory::ItemIcon::drop_on_equips(Equipslot::Id eqslot) const
    {
        switch (sourcetab)
        {
        case InventoryType::EQUIP:
            if (eqsource == eqslot)
            {
                Sound(Sound::DRAGEND).play();
                EquipItemPacket(source, eqslot).dispatch();
            }
            break;
        case InventoryType::USE:
            Sound(Sound::DRAGEND).play();
            ScrollEquipPacket(source, eqslot).dispatch();
            break;
        default:
            break;
        }
    }

    void UIItemInventory::ItemIcon::drop_on_items(InventoryType::Id tab, Equipslot::Id, int16_t slot, bool) const
    {
        if (tab != sourcetab || slot == source)
            return;

        Sound(Sound::DRAGEND).play();
        MoveItemPacket(tab, source, slot, 1).dispatch();
    }

    Keyboard::Mapping UIItemInventory::ItemIcon::get_binding() const
    {
        // Only the two consumable tabs hold things a key can reach; equips and
        // etc items have no use that a key press could stand for.
        if (sourcetab != InventoryType::USE && sourcetab != InventoryType::SETUP)
            return {};

        return { KeyType::ITEM, item_id };
    }

    void UIItemInventory::ItemIcon::drop_on_bindings(Point<int16_t> cursorposition, bool remove) const
    {
        if (sourcetab != InventoryType::USE && sourcetab != InventoryType::SETUP)
            return;

        if (auto keyconfig = UI::get().get_element<UIKeyConfig>())
        {
            Keyboard::Mapping mapping{ KeyType::ITEM, item_id };
            if (remove)
                keyconfig->unstage_mapping(mapping);
            else
                keyconfig->stage_mapping(cursorposition, mapping);
        }
    }
}
