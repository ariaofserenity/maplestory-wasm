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
#pragma once
#include "UIChatBar.h"

#include "../UIElement.h"
#include "../Messages.h"

#include "../Components/Charset.h"
#include "../Components/Gauge.h"
#include "../Components/Textfield.h"

#include "../../Character/CharStats.h"
#include "../../Character/Inventory/Inventory.h"
#include "../../Character/Job.h"
#include "../../Graphics/Animation.h"
#include "../../Graphics/Text.h"

#include <vector>

namespace jrc
{
    class UIStatusbar : public UIElement
    {
    public:
        static constexpr Type TYPE = STATUSBAR;
        static constexpr bool FOCUSED = false;
        static constexpr bool TOGGLED = true;

        UIStatusbar(const CharStats& stats);

        void draw(float alpha) const override;
        void update() override;

        bool is_in_range(Point<int16_t> cursorpos) const override;
        bool remove_cursor(bool clicked, Point<int16_t> cursorpos) override;
        CursorResult send_cursor(bool pressed, Point<int16_t> cursorpos) override;

        void send_chatline(const std::string& line, UIChatbar::LineType type);
        void focus_chatfield();
        void display_message(Messages::Type line, UIChatbar::LineType type);
        void set_chat_target(UIChatbar::ChatTarget target);
        void cycle_chat_target();
        void set_pending_party_invite(int32_t party_id, const std::string& inviter);
        void clear_pending_party_invite();
        void set_party_state(int32_t party_id, int32_t leader_id, const std::vector<UIChatbar::PartyMember>& members);
        void clear_party_state();
        void set_party_leader(int32_t leader_id);
        void update_party_member_hp(int32_t cid, int32_t hp, int32_t max_hp);
        int32_t get_party_id() const;
        int32_t get_party_leader_id() const;
        int32_t get_pending_party_invite_id() const;
        const std::string& get_pending_party_inviter() const;
        const std::vector<UIChatbar::PartyMember>& get_party_members() const;

    protected:
        Button::State button_pressed(uint16_t buttonid) override;

    private:
        // Which popup is currently raised, if any. Only one can be open at a
        // time, so opening one closes the other.
        enum Bubble
        {
            BUBBLE_NONE,
            BUBBLE_MENU,
            BUBBLE_OPTIONS
        };

        void update_layout_position();
        void open_own_userinfo();
        void draw_bubble(Point<int16_t> at, int16_t height) const;
        Rectangle<int16_t> bubble_bounds() const;
        void set_bubble(Bubble which);
        float getexppercent() const;
        float gethppercent() const;
        float getmppercent() const;

        enum Buttons : uint16_t
        {
            BT_WHISPER,
            BT_CALLGM,
            BT_CASHSHOP,
            BT_MENU,
            BT_CHANNEL,
            BT_OPTIONS,
            BT_CHARACTER,
            BT_STATS,
            BT_QUEST,
            BT_INVENTORY,
            BT_EQUIPS,
            BT_SKILL,
            BT_KEYSETTING,

            // Entries of the popup bubbles. Each block is kept contiguous so it
            // can be shown and hidden as a range, and both start after every
            // button belonging to the bar itself so the two can be told apart
            // when drawing.
            BT_MENU_ITEM,
            BT_MENU_EQUIP,
            BT_MENU_STAT,
            BT_MENU_SKILL,
            BT_MENU_QUEST,
            BT_MENU_MSN,

            BT_OPTION_CHANNEL,
            BT_OPTION_KEYSETTING,
            BT_OPTION_GAMEOPTION,
            BT_OPTION_SYSTEMOPTION,
            BT_OPTION_QUIT
        };

        // Placement of the system button row, given as the top left corner of
        // each button relative to the bar's anchor. Moving one of these moves
        // its artwork and its clickable area together, so they can be nudged
        // individually without touching anything else. The buttons are 75 wide,
        // hence the wider step before the last one, which leaves the small
        // separator the bar artwork draws at that point.
        static constexpr Point<int16_t> BT_CASHSHOP_POS = {  57, -36 };
        static constexpr Point<int16_t> BT_MENU_POS     = { 134, -36 };
        static constexpr Point<int16_t> BT_CHANNEL_POS  = { 211, -36 };
        static constexpr Point<int16_t> BT_OPTIONS_POS  = { 292, -36 };

        // Sits on the row of small buttons rather than the system row, so it
        // takes their y. Its x is taken from the system button that follows the
        // separator, so the two stay flush with each other if that one is moved.
        static constexpr int16_t SMALL_BUTTON_Y = -58;
        static constexpr Point<int16_t> BT_KEYSETTING_POS = {
            BT_OPTIONS_POS.x(), SMALL_BUTTON_Y
        };

        // Popup bubbles. Both are built from the same three piece column: a
        // head, a single row of body meant to be stretched, and a foot. One
        // entry fills each row of the body, so a bubble is only ever as tall as
        // the entries stacked inside it.
        static constexpr int16_t BUBBLE_WIDTH = 79;
        static constexpr int16_t BUBBLE_HEAD_HEIGHT = 34;
        static constexpr int16_t BUBBLE_FOOT_HEIGHT = 41;
        static constexpr int16_t BUBBLE_ENTRY_HEIGHT = 25;
        static constexpr int16_t BUBBLE_ENTRY_INSET = 8;

        // Room left above the first entry and below the last. The head and foot
        // are mostly rounded cap and tail, so the entries tuck into them instead
        // of starting where the head's bitmap happens to end, which otherwise
        // leaves a band of empty bubble at each end. These two are the values to
        // change if the entries sit too near or too far from the ends.
        static constexpr int16_t BUBBLE_ENTRY_TOP = 22;
        static constexpr int16_t BUBBLE_ENTRY_BOTTOM = 18;

        static constexpr int16_t MENU_ENTRY_COUNT = BT_MENU_MSN - BT_MENU_ITEM + 1;
        static constexpr int16_t OPTIONS_ENTRY_COUNT = BT_OPTION_QUIT - BT_OPTION_CHANNEL + 1;

        static constexpr int16_t MENU_HEIGHT =
            BUBBLE_ENTRY_TOP + BUBBLE_ENTRY_HEIGHT * MENU_ENTRY_COUNT + BUBBLE_ENTRY_BOTTOM;
        static constexpr int16_t OPTIONS_HEIGHT =
            BUBBLE_ENTRY_TOP + BUBBLE_ENTRY_HEIGHT * OPTIONS_ENTRY_COUNT + BUBBLE_ENTRY_BOTTOM;

        // Top left of each bubble, centred over the button it belongs to and
        // resting directly on top of it.
        static constexpr Point<int16_t> MENU_POS = {
            static_cast<int16_t>(BT_MENU_POS.x() - 2),
            static_cast<int16_t>(BT_MENU_POS.y() - MENU_HEIGHT)
        };
        static constexpr Point<int16_t> OPTIONS_POS = {
            static_cast<int16_t>(BT_OPTIONS_POS.x() - 2),
            static_cast<int16_t>(BT_OPTIONS_POS.y() - OPTIONS_HEIGHT)
        };

        static constexpr Point<int16_t> POSITION  = {  512, 590 };
        static constexpr Point<int16_t> DIMENSION = { 1366, 80  };
        // How long escape has to be held before an open popup is dismissed.
        static constexpr time_t BUBBLE_ESCAPE_HOLD = 100;

        static constexpr time_t MESSAGE_COOLDOWN = 1'000;

        const CharStats& stats;

        EnumMap<Messages::Type, time_t> message_cooldowns;

        UIChatbar chatbar;
        Gauge expbar;
        Gauge hpbar;
        Gauge mpbar;
        Charset statset;
        Charset levelset;
        Text namelabel;
        Text joblabel;
        Animation hpanimation;
        Animation mpanimation;

        Texture bubble_head;
        Texture bubble_body;
        Texture bubble_foot;
        Bubble open_bubble;
        time_t escape_held;
    };
}
