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
#include "../UIDragElement.h"

#include "../../Character/QuestLog.h"
#include "../../Graphics/Text.h"
#include "../../Graphics/Texture.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace jrc
{
    // The quest tracker, the reference client's CUIQuestAlarm.
    //
    // A small window that lists the quests being followed and how far along
    // each of their demands is. It holds at most five, and a quest is only
    // worth tracking if it asks for something countable - an item, a mob,
    // meso, or another quest first.
    //
    // Everything the server knows about progress is already in the questlog,
    // so nothing here is sent or received; the window is a view of it.
    class UIQuestTracker : public UIDragElement<PosQUESTTRACKER>
    {
    public:
        static constexpr Type TYPE = QUESTTRACKER;
        static constexpr bool FOCUSED = false;
        static constexpr bool TOGGLED = true;

        UIQuestTracker(const Questlog& quests);

        void draw(float alpha) const override;

        bool is_in_range(Point<int16_t> cursorpos) const override;
        CursorResult send_cursor(bool clicked, Point<int16_t> cursorpos) override;

        // Follow a quest. Called for every quest that starts, which is the
        // reference client's automatic path; a call the player asked for
        // passes automatic false and ignores both the auto-register setting
        // and the list of quests they have dropped before.
        void track(int16_t questid, bool automatic);
        // Whether the tracker would take another quest, which is what decides
        // if the quest window's own track button is offered.
        bool has_room() const;
        // Stop following one.
        void drop(int16_t questid);
        bool is_tracked(int16_t questid) const;
        // Whether the quest asks for anything this window could count.
        static bool worth_tracking(int16_t questid);

        // Rebuild the rows, for when a quest record changed.
        void refresh();

    protected:
        Button::State button_pressed(uint16_t buttonid) override;

    private:
        enum Buttons : uint16_t
        {
            // The reference client numbers these 0x7d1, 0x7d0, 0x7d7 and
            // 0x7d8, with one delete button per tracked quest at 0x7d2
            // upwards. The order here is its order.
            BT_MIN,
            BT_MAX,
            BT_AUTO,
            BT_QUESTLOG,
            BT_DELETE0
        };

        // One line of the window. The reference client gives every one of
        // these the same height and lays them out from the same left edge.
        struct Row
        {
            // Whether this is the quest's name rather than one of its demands.
            bool title = false;
            Text label;
            Text progress;
            // 0 to 100, which is what picks the progress colour.
            int32_t percent = 0;
        };

        // Pick up quests that were already under way.
        void adopt_started();
        void rebuild_rows();
        // Where a tracked quest's delete button sits.
        Point<int16_t> delete_button_pos(size_t which) const;
        // The title's y, which never depends on the window's state.
        int16_t title_y() const;
        void resize();
        // The colour a figure is written in, by how far along it is.
        static Text::Color progress_color(int32_t percent);

        // Off CUIQuestAlarm::Create and ::GetHeight: the window is 180 wide,
        // its rows start below the 25 pixel title bar and are 18 apart, and
        // the frame adds a 5 pixel footer. Shut, it is just the 20 pixel
        // minimised bar.
        static constexpr int16_t WIDTH = 180;
        static constexpr int16_t TITLE_HEIGHT = 25;
        static constexpr int16_t ROW_HEIGHT = 18;
        static constexpr int16_t FOOTER_HEIGHT = 5;
        static constexpr int16_t MIN_HEIGHT = 20;
        static constexpr int16_t ROW_TOP = 25;
        static constexpr int16_t ROW_LEFT = 10;
        // Where a progress figure ends. The client works this out as 0xa0
        // plus 15 against a 180 wide window, which is its right margin.
        static constexpr int16_t ROW_RIGHT = 175;
        static constexpr int16_t TITLE_TEXT_X = 19;
        static constexpr int16_t TITLE_TEXT_Y = 5;
        // BtQ, BtAuto, BtMin and BtMax place themselves: each carries its
        // spot in its art's origin, negated - (-4,-4), (-118,-4) and
        // (-150,-4) - so nothing here needs to position them. BtQ's and
        // BtAuto's agree exactly with the coordinates their CreateCtrl calls
        // pass in OnCreate, which is the cross-check that they are placement
        // and not something else.
        // A quest's delete button follows its name by fifteen pixels, which
        // is how Draw places it: CalcTextWidth(name) + 15.
        static constexpr int16_t DELETE_GAP = 15;
        // RegisterQuest refuses a sixth quest.
        static constexpr size_t MAX_TRACKED = 5;
        // ... and refuses this category outright.
        static constexpr int16_t UNTRACKED_CATEGORY = 51;

        const Questlog& quests;

        Texture backgrnd_max;
        Texture backgrnd_min;
        Texture backgrnd_center;
        Texture backgrnd_bottom;

        Text title;

        std::vector<int16_t> tracked;
        // Quests the player took off the list, so following them again is
        // something they have to ask for.
        std::set<int16_t> dropped;
        std::vector<Row> rows;

        bool maximized;
        bool autoregister;
    };
}
