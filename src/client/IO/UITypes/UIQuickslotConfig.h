//////////////////////////////////////////////////////////////////////////////
// This file is part of the Journey MMORPG client                           //
//////////////////////////////////////////////////////////////////////////////
#pragma once

#include "UIStatusBar.h"

#include "../Keyboard.h"
#include "../UIElement.h"

#include "../../Graphics/Texture.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace jrc
{
    // Picks which eight keys the quickslot on the status bar watches, and in
    // which order. It only moves keys around: what each key does stays with the
    // keymap, so a cell here shows whatever its key is bound to.
    class UIQuickslotConfig : public UIElement
    {
    public:
        static constexpr Type TYPE = QUICKSLOTCONFIG;
        // Takes the keyboard while it is open, because it has to be told which
        // key the player wants, including keys the keymap has nothing on.
        static constexpr bool FOCUSED = true;
        static constexpr bool TOGGLED = false;

        UIQuickslotConfig();

        void draw(float alpha) const override;

        CursorResult send_cursor(bool clicked, Point<int16_t> cursorpos) override;
        void send_key(int32_t keycode, bool pressed, bool escape) override;
        bool send_raw_key(int32_t keycode, bool pressed) override;

    protected:
        Button::State button_pressed(uint16_t buttonid) override;

    private:
        enum Buttons : uint16_t
        {
            BT_OK,
            BT_CANCEL
        };

        void commit();
        void assign_key(int32_t maple_keycode);
        Point<int16_t> slot_position(size_t index) const;
        size_t slot_by_position(Point<int16_t> cursorpos) const;

        // Placement inside the dialog artwork, which prints the empty cells but
        // leaves everything drawn over them to the client. The cells are 32
        // square with a divider between them, hence the step of one more.
        static constexpr Point<int16_t> SLOT_POS = { 50, 97 };
        static constexpr int16_t SLOT_STEP = 33;
        static constexpr int16_t SLOT_COLUMNS = 4;
        static constexpr int16_t SLOT_SIZE = 32;
        static constexpr int16_t LABEL_INSET = 3;

        static constexpr Point<int16_t> BT_OK_POS     = { 127, 180 };
        static constexpr Point<int16_t> BT_CANCEL_POS = { 171, 180 };

        // Keys being picked, kept apart from the bar's own until the player
        // confirms, so cancelling leaves nothing behind.
        std::array<int32_t, UIStatusbar::QUICKSLOT_COUNT> staged_keys;
        std::array<Texture, UIStatusbar::QUICKSLOT_COUNT> staged_labels;
        size_t focused_slot;

        // Every key that may be picked has a name here; one that does not is
        // one the quickslot cannot show, which is how the reference client
        // decides what to refuse.
        nl::node keynames;
        Texture focus;
    };
}
