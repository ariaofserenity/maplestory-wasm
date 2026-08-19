//////////////////////////////////////////////////////////////////////////////
// This file is part of the Journey MMORPG client                           //
//////////////////////////////////////////////////////////////////////////////
#include "UIQuickslotConfig.h"

#include "../KeyBinding.h"
#include "../KeyConfig.h"
#include "../UI.h"
#include "../Components/MapleButton.h"

#include "../../Constants.h"
#include "../../Net/Packets/PlayerPackets.h"

#include "nlnx/nx.hpp"

#include <algorithm>
#include <string>

namespace jrc
{
    constexpr Point<int16_t> UIQuickslotConfig::SLOT_POS;
    constexpr Point<int16_t> UIQuickslotConfig::BT_OK_POS;
    constexpr Point<int16_t> UIQuickslotConfig::BT_CANCEL_POS;

    UIQuickslotConfig::UIQuickslotConfig()
        // Nothing is picked until the player points at a cell, matching the
        // dialog's own instructions: click a slot, then press a key.
        : staged_keys{}, focused_slot(UIStatusbar::QUICKSLOT_COUNT)
    {
        // The compact panel rather than the wide one: it carries the same eight
        // cells in the same places, and the bar it belongs to is drawn at the
        // smaller of the two ui sizes.
        nl::node src = nl::nx::ui["UIWindow2.img"]["KeyConfig"]["quickslotConfig"];
        keynames = src["key"];

        sprites.emplace_back(src["backgrnd"]);

        Texture background = src["backgrnd"];
        dimension = background.get_dimensions();
        position = {
            static_cast<int16_t>((Constants::viewwidth() - dimension.x()) / 2),
            static_cast<int16_t>((Constants::viewheight() - dimension.y()) / 2)
        };

        // Both carry an origin placing them where the artwork expects, so they
        // need no offset of their own. The constants in the header record the
        // same two spots so a reader does not have to open the artwork to see
        // where the row sits.
        buttons[BT_OK]     = std::make_unique<MapleButton>(src["BtOK"]);
        buttons[BT_CANCEL] = std::make_unique<MapleButton>(src["BtCancel"]);

        // The only state of this button that shows anything is the focused one,
        // so it doubles as the marker for which cell a key will land in.
        focus = src["BtQuickSetting"]["keyFocused"]["0"];

        if (auto statusbar = UI::get().get_element<UIStatusbar>())
        {
            staged_keys = statusbar->get_quickslot_keys();
        }

        for (size_t i = 0; i < staged_keys.size(); ++i)
        {
            staged_labels[i] = keynames[std::to_string(staged_keys[i])];
        }
    }

    void UIQuickslotConfig::draw(float alpha) const
    {
        UIElement::draw(alpha);

        const Keyboard& keyboard = UI::get().get_keyboard();

        for (size_t i = 0; i < staged_keys.size(); ++i)
        {
            Point<int16_t> cell = position + slot_position(i);

            // Shows what the key does, not what the cell does, because this
            // dialog only decides which keys are on the bar. The artwork is the
            // same the bar itself draws, so a cell reads the same in both.
            Texture icon = KeyBinding::icon_for(keyboard.get_maple_mapping(staged_keys[i]));
            if (icon.is_valid())
            {
                icon.shift({ 0, 32 });
                icon.draw(cell);
            }
            else if (staged_labels[i].is_valid())
            {
                Point<int16_t> label = staged_labels[i].get_dimensions();

                staged_labels[i].draw(cell + Point<int16_t>(
                    static_cast<int16_t>((SLOT_SIZE - label.x()) / 2),
                    static_cast<int16_t>(SLOT_SIZE - label.y() - LABEL_INSET)
                ));
            }

            if (i == focused_slot)
            {
                focus.draw(cell);
            }
        }
    }

    UIElement::CursorResult UIQuickslotConfig::send_cursor(bool clicked, Point<int16_t> cursorpos)
    {
        if (clicked)
        {
            if (size_t slot = slot_by_position(cursorpos); slot < staged_keys.size())
            {
                focused_slot = slot;
                return { Cursor::CLICKING, true };
            }
        }

        return UIElement::send_cursor(clicked, cursorpos);
    }

    void UIQuickslotConfig::send_key(int32_t, bool pressed, bool escape)
    {
        if (pressed && escape)
        {
            deactivate();
        }
    }

    bool UIQuickslotConfig::send_raw_key(int32_t keycode, bool pressed)
    {
        // Held while the dialog is up, so nothing the player presses while
        // choosing a key also runs whatever that key normally does.
        if (pressed)
        {
            assign_key(Keyboard::maple_keycode(keycode));
        }

        return true;
    }

    void UIQuickslotConfig::assign_key(int32_t maple_keycode)
    {
        if (focused_slot >= staged_keys.size())
        {
            return;
        }

        // The two shift keys are one key as far as the quickslot is concerned,
        // and the right one has no name of its own, so it stands in for the left.
        if (maple_keycode == KeyConfig::RIGHT_SHIFT)
        {
            maple_keycode = KeyConfig::LEFT_SHIFT;
        }

        auto complain = [](const std::string& why) {
            if (auto statusbar = UI::get().get_element<UIStatusbar>())
            {
                statusbar->send_chatline(why, UIChatbar::RED);
            }
        };

        // A key with no name is one the quickslot has no way to label, which is
        // exactly the test the reference client makes before accepting one.
        if (maple_keycode == 0 || !keynames[std::to_string(maple_keycode)])
        {
            complain("The key you just pressed cannot be used in the Quick Slot.");
            return;
        }

        if (std::find(staged_keys.begin(), staged_keys.end(), maple_keycode) != staged_keys.end())
        {
            complain("This key is already being used.");
            return;
        }

        staged_keys[focused_slot] = maple_keycode;
        staged_labels[focused_slot] = keynames[std::to_string(maple_keycode)];
    }

    Button::State UIQuickslotConfig::button_pressed(uint16_t buttonid)
    {
        switch (buttonid)
        {
        case BT_OK:
            commit();
            deactivate();
            break;
        case BT_CANCEL:
        default:
            deactivate();
            break;
        }

        return Button::NORMAL;
    }

    void UIQuickslotConfig::commit()
    {
        ChangeQuickslotPacket(staged_keys).dispatch();

        // Applied locally as well as sent, because the server stores the order
        // without echoing it back: it only sends one on entering a field.
        if (auto statusbar = UI::get().get_element<UIStatusbar>())
        {
            statusbar->set_quickslot_keys(staged_keys);
        }
    }

    Point<int16_t> UIQuickslotConfig::slot_position(size_t index) const
    {
        auto column = static_cast<int16_t>(index % SLOT_COLUMNS);
        auto row = static_cast<int16_t>(index / SLOT_COLUMNS);

        return SLOT_POS + Point<int16_t>(
            static_cast<int16_t>(column * SLOT_STEP),
            static_cast<int16_t>(row * SLOT_STEP)
        );
    }

    size_t UIQuickslotConfig::slot_by_position(Point<int16_t> cursorpos) const
    {
        for (size_t i = 0; i < staged_keys.size(); ++i)
        {
            Point<int16_t> lt = position + slot_position(i);
            Rectangle<int16_t> cell(lt, lt + Point<int16_t>(SLOT_SIZE, SLOT_SIZE));

            if (cell.contains(cursorpos))
            {
                return i;
            }
        }

        return staged_keys.size();
    }
}
