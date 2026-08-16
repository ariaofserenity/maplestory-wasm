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

#include "../Components/QuestSummary.h"
#include "../Components/Slider.h"

#include "../../Character/CharStats.h"
#include "../../Character/QuestLog.h"
#include "../../Graphics/Animation.h"
#include "../../Graphics/Text.h"
#include "../../Graphics/Texture.h"

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace jrc
{
    // The quest log window.
    //
    // Two panes, laid out the way the game files draw them: a list of quests
    // with a tab per state, and a detail pane that opens beside it once a
    // quest is picked. Both come out of UIWindow2, where every piece carries
    // the origin it belongs at, so almost nothing here is positioned by hand.
    //
    // The available list is derived rather than sent - the server never tells
    // the client which quests are on offer, so it is worked out from the quest
    // requirements and the character's own record.
    class UIQuestLog : public UIDragElement<PosQUEST>
    {
    public:
        static constexpr Type TYPE = QUESTLOG;
        static constexpr bool FOCUSED = false;
        static constexpr bool TOGGLED = true;

        UIQuestLog(const CharStats& stats, const Questlog& quests);

        void draw(float alpha) const override;
        void update() override;

        void send_scroll(double yoffset) override;
        void send_key(int32_t keycode, bool pressed, bool escape) override;
        bool is_in_range(Point<int16_t> cursorpos) const override;
        bool remove_cursor(bool clicked, Point<int16_t> cursorpos) override;
        CursorResult send_cursor(bool clicked, Point<int16_t> cursorpos) override;

        // Rebuild the lists. Called whenever a quest record changes, since any
        // change can move a quest between the three views.
        void update_quests();

        // Rebuild the detail pane for the quest on show. The tracker calls
        // this when its own list changes, the way CUIQuestAlarm calls
        // CUIQuestInfo::ResetInfo and CUIQuestInfoDetail::ResetInfo.
        void refresh_details();

        // Note the remaining time on a timed quest, in seconds.
        void set_timer(int16_t questid, int32_t seconds);
        void clear_timer(int16_t questid);

    protected:
        Button::State button_pressed(uint16_t buttonid) override;

    private:
        // The three lists, which are also the three tabs. The game files carry
        // a fourth, Recommended, which is left out: it lists what the server
        // suggests and the server never sends that.
        enum Tab : uint16_t
        {
            AVAILABLE,
            IN_PROGRESS,
            COMPLETED,
            NUM_TABS
        };

        enum Buttons : uint16_t
        {
            BT_TAB_AVAILABLE,
            BT_TAB_IN_PROGRESS,
            BT_TAB_COMPLETED,
            BT_CLOSE,
            BT_GIVEUP,
            BT_DETAIL_CLOSE,
            BT_TRACK
        };

        void change_tab(Tab newtab);
        void change_offset(uint16_t newoffset);
        void select(int32_t index);
        // Whether the character meets everything the quest asks before it can
        // be taken. Only quests that pass this are listed as available.
        bool can_take(int16_t questid) const;
        // The row the cursor is over, or -1.
        int32_t row_at(Point<int16_t> cursorpos) const;
        // Whether the detail pane is showing.
        bool detail_open() const;

        // One line of a tab's list: either a region heading or a quest under
        // one. The reference client keeps both in the same list and tells
        // them apart when it draws, which is what lets a heading scroll with
        // the quests beneath it.
        struct Row
        {
            bool header = false;
            int16_t questid = 0;
            int32_t area = 0;
            // Quests in the region, for a heading.
            int32_t count = 0;
        };

        const std::vector<Row>& current_list() const;
        // Whether the region is shut on the given tab.
        bool is_collapsed(Tab which, int32_t area) const;
        // Open or shut the region the row belongs to.
        void toggle_category(size_t rowindex);
        // Rebuild one tab's rows from the quests it holds.
        void build_rows(Tab which, std::vector<int16_t> questids);

        // The list pane. Its frame is 295x396 and its white inner panel starts
        // below the tab strip.
        static constexpr int16_t LIST_WIDTH = 295;
        static constexpr int16_t LIST_HEIGHT = 396;
        // These are measured against this data's art, not taken from v95.
        // GMSv95 states its own in CUIQuestInfo::GetQuestIdxFromMousePos -
        // rows from y 52 stepping 22, x 14 to 216, thirteen of them, with the
        // scrollbar at x 217 - but that describes a narrower window. What
        // ships here is a later revision of the same node, carrying a search
        // box, level and location filters and a recommended section, whose
        // strips (searchArea, recommendTitle, completeCount) are all 275 wide.
        // So v95's absolute positions do not transfer; only the proportions
        // of a row's contents below do.
        static constexpr int16_t ROWS = 17;
        static constexpr int16_t ROW_HEIGHT = 18;
        static constexpr int16_t LIST_TOP = 52;
        static constexpr int16_t LIST_LEFT = 14;
        static constexpr int16_t LIST_TEXT_WIDTH = 252;
        static constexpr int16_t SLIDER_X = 277;

        // A region heading, laid out the way CUIQuestInfo::Draw lays one out:
        // a band starting three pixels left of the row and running its full
        // width, the open/shut picture three pixels down, and the name four
        // pixels down and twenty in - which is the room the picture takes.
        static constexpr int16_t CATEGORY_BAND_X = LIST_LEFT - 3;
        static constexpr int16_t CATEGORY_BAND_WIDTH = LIST_TEXT_WIDTH;
        static constexpr int16_t CATEGORY_BAND_HEIGHT = ROW_HEIGHT;
        static constexpr int16_t CATEGORY_ICON_X = LIST_LEFT;
        static constexpr int16_t CATEGORY_TEXT_X = LIST_LEFT + 17;

        // The detail pane, drawn to the right of the list pane. Every number
        // below is the reference client's, off CUIQuestInfoDetail::Draw and
        // ::SetLayout, rather than measured off the art by eye.
        static constexpr int16_t DETAIL_X = LIST_WIDTH;
        static constexpr int16_t DETAIL_WIDTH = 296;
        // The quest's name, in the blue header box.
        static constexpr int16_t DETAIL_NAME_X = 35;
        static constexpr int16_t DETAIL_NAME_Y = 42;
        // The title is cut to this rather than wrapped: CUIQuestInfoDetail
        // ::Draw runs it through format_string with a width of 145 first.
        static constexpr int16_t DETAIL_NAME_WIDTH = 145;
        // Where SetNPC puts the quest giver, by RelMove(0xe7, 0x6e). The npc
        // hangs from its own origin, which is at its feet.
        static constexpr int16_t DETAIL_NPC_X = 231;
        static constexpr int16_t DETAIL_NPC_Y = 110;
        // Text is drawn one pixel left of where it is clipped, because the
        // layout keeps a margin of its own inside that.
        static constexpr int16_t DETAIL_TEXT_X = 17;
        static constexpr int16_t DETAIL_CLIP_X = 18;
        static constexpr int16_t DETAIL_CLIP_WIDTH = 253;
        // The description, in the white body box.
        static constexpr int16_t DETAIL_BODY_Y = 127;
        static constexpr int16_t DETAIL_BODY_CLIP_Y = 128;
        // How far the description box reaches, which depends on whether the
        // reward box is below it taking up room.
        static constexpr int16_t DETAIL_BODY_LEN_BOXED = 120;
        static constexpr int16_t DETAIL_BODY_LEN_FULL = 238;
        // The demand and reward blurbs, in the grey box beneath.
        static constexpr int16_t DETAIL_SUMMARY_Y = 257;
        static constexpr int16_t DETAIL_SUMMARY_CLIP_Y = 252;
        static constexpr int16_t DETAIL_SUMMARY_CLIP_HEIGHT = 111;
        static constexpr int16_t DETAIL_SLIDER_X = 275;
        // A Slider draws its lower arrow below the range it is given, where
        // the client counts that arrow inside the bar's own length.
        static constexpr int16_t SLIDER_ARROW_HEIGHT = 12;
        // How far one notch of either detail scrollbar moves the text.
        static constexpr int16_t DETAIL_SCROLL_STEP = 16;
        // The width the reference client lays quest text out to, alongside a
        // one pixel margin at each end of every line.
        static constexpr int16_t DETAIL_TEXT_WIDTH = 255;

        // How tall the description box is for the quest on show.
        int16_t body_view_height() const;
        // How tall the demand and reward blurbs come to together.
        int16_t summary_content_height() const;
        // Whether the quest states anything for the reward box to hold.
        bool has_summary() const;
        // Point the detail scrollbars at the text now in the pane.
        void refresh_detail_scroll();

        const CharStats& stats;
        const Questlog& quests;

        Slider slider;
        // The detail pane scrolls its description and its reward box apart
        // from one another, as two panels with a scrollbar each.
        Slider body_slider;
        Slider summary_slider;
        // Whether the two pane buttons do anything; they stay on screen
        // either way, greyed out, the way SetButton leaves them.
        bool track_enabled = false;
        bool giveup_enabled = false;

        int16_t body_scroll;
        int16_t summary_scroll;
        // Where the cursor was last seen, so a wheel notch - which arrives
        // without a position - can go to the panel it is over.
        Point<int16_t> lastcursor;
        // Which row the mouse went down on, so a row acts on release over
        // that same row and never while the button is dragged across others.
        int32_t pressedrow;
        bool wasclicked;

        // The detail pane's frame. Kept out of the sprite list because it is
        // only drawn while a quest is picked.
        std::vector<Texture> detail_frame;
        // The reward box, drawn only for a quest that states one, because
        // without it the description box runs the full height of the pane.
        Texture detail_summary_frame;
        // "There are no quests ..." for each tab, in tab order.
        std::array<Texture, NUM_TABS> notices;
        // The picture on a region heading: BtMin while the region is open,
        // BtMax while it is shut.
        Texture category_open;
        Texture category_shut;
        // The quest giver, shown beside the title. SetNPC prefers a canvas at
        // Npc/%07d.img/info/default and falls back to the npc's own frames,
        // which is what this data leaves it to.
        Animation detail_npc;

        Tab tab;
        uint16_t offset;
        // Index into the current list, or -1 when nothing is picked.
        int32_t selected;

        std::vector<Row> lists[NUM_TABS];
        std::vector<Text> labels;
        // Regions the player has shut, per tab. The reference client keeps
        // this in its options so it survives a relog; here it lasts as long
        // as the session.
        std::set<std::pair<int32_t, int32_t>> collapsed;

        Text detail_name;
        // The description, drawn through the same renderer as the blurbs: it
        // is written in the same markup and desynchronises any simpler scan.
        QuestSummary detail_body;
        // The demand and reward blurbs, drawn with the pictures they are
        // written to carry rather than as stripped-down text.
        QuestSummary detail_demand;
        QuestSummary detail_reward;

        // Time left on each timed quest, in milliseconds. The server states it
        // once when the timer starts and says nothing more until it expires,
        // so the countdown is run here.
        std::map<int16_t, int64_t> timers;
    };
}
