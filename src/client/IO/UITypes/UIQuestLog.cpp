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
#include "UIQuestLog.h"

#include "../Components/MapleButton.h"
#include "../Components/TwoSpriteButton.h"

#include "../../Constants.h"
#include "../../Data/QuestData.h"
#include "../../Graphics/GraphicsGL.h"
#include "../../Net/Packets/QuestPackets.h"

#include "../UI.h"
#include "UIQuestTracker.h"

#include "nlnx/nx.hpp"

#include <algorithm>

namespace jrc
{
    namespace
    {
        // Stands in for a blurb a quest does not state.
        const std::string EMPTY;

        // Regions by the number a quest's "area" states.
        //
        // The reference client walks Etc/QuestCategory.img with the array
        // index as the key, so in its data the entry's own name is the number
        // a quest states. Here the entries have been renumbered and carry
        // that original number in a "category" field instead, with the name
        // moved to "title" - so the match is on "category", and the entry's
        // own name is left as the order to list regions in.
        //
        // Checked against the quests themselves rather than assumed: area 12
        // is Luminous and its quests are Luminous skills, area 29 is Nautilus
        // and its quests are the Nautilus ones, and so on. Matching on the
        // entry name instead puts Luminous quests under Aran.
        const std::map<int32_t, std::string>& categories()
        {
            static const std::map<int32_t, std::string> table = []
            {
                std::map<int32_t, std::string> built;

                for (nl::node entry : nl::nx::etc["QuestCategory.img"])
                {
                    int32_t area = 0;
                    std::string title;

                    if (entry["title"])
                    {
                        area = static_cast<int32_t>(entry["category"].get_integer());
                        title = entry["title"].get_string();
                    }
                    else
                    {
                        // The older shape: the entry is the name itself, and
                        // its own number is what a quest states.
                        try
                        {
                            area = std::stoi(entry.name());
                        }
                        catch (const std::exception&)
                        {
                            continue;
                        }
                        title = entry.get_string();
                    }

                    if (!title.empty())
                    {
                        built[area] = title;
                    }
                }

                return built;
            }();

            return table;
        }

        const std::string* find_category(int32_t area)
        {
            const std::map<int32_t, std::string>& table = categories();
            auto found = table.find(area);
            return found == table.end() ? nullptr : &found->second;
        }

        std::string format_time_left(int64_t milliseconds)
        {
            int64_t total = std::max<int64_t>(0, milliseconds / 1000);
            int64_t minutes = total / 60;
            int64_t seconds = total % 60;

            std::string padded = std::to_string(seconds);
            if (padded.size() < 2)
            {
                padded.insert(padded.begin(), '0');
            }

            return std::to_string(minutes) + ":" + padded;
        }
    }

    UIQuestLog::UIQuestLog(const CharStats& in_stats, const Questlog& in_quests)
        : UIDragElement({ LIST_WIDTH, 20 }),
          stats(in_stats),
          quests(in_quests),
          body_scroll(0),
          summary_scroll(0),
          pressedrow(-1),
          wasclicked(false),
          tab(AVAILABLE),
          offset(0),
          selected(-1)
    {
        nl::node list = nl::nx::ui["UIWindow2.img"]["Quest"]["list"];
        nl::node info = nl::nx::ui["UIWindow2.img"]["Quest"]["quest_info"];

        sprites.emplace_back(list["backgrnd"]);
        sprites.emplace_back(list["backgrnd2"]);

        // BtMin is shown while a region is open and shuts it; BtMax is
        // shown while it is shut and opens it again.
        nl::node quest = nl::nx::ui["UIWindow2.img"]["Quest"];
        category_open = quest["BtMin"]["normal"]["0"];
        category_shut = quest["BtMax"]["normal"]["0"];

        notices[AVAILABLE] = list["notice0"];
        notices[IN_PROGRESS] = list["notice1"];
        notices[COMPLETED] = list["notice2"];

        detail_frame.emplace_back(info["backgrnd"]);
        detail_frame.emplace_back(info["backgrnd2"]);
        detail_frame.emplace_back(info["backgrnd3"]);
        detail_summary_frame = info["summary"];

        nl::node tabe = list["Tab"]["enabled"];
        nl::node tabd = list["Tab"]["disabled"];

        // Every piece of this window carries the origin it belongs at, so the
        // buttons are added without a position and land where the art says.
        for (uint16_t i = BT_TAB_AVAILABLE; i <= BT_TAB_COMPLETED; ++i)
        {
            uint16_t tabid = i - BT_TAB_AVAILABLE;
            buttons[i] = std::make_unique<TwoSpriteButton>(tabd[tabid], tabe[tabid]);
        }

        buttons[BT_CLOSE] = std::make_unique<MapleButton>(
            nl::nx::ui["Basic.img"]["BtClose"], Point<int16_t>(277, 11)
        );

        // The detail pane's own buttons live one pane to the right; their
        // origins then place them within it.
        Point<int16_t> detail_origin(DETAIL_X, 0);
        buttons[BT_GIVEUP] = std::make_unique<MapleButton>(info["BtGiveup"], detail_origin);
        buttons[BT_DETAIL_CLOSE] = std::make_unique<MapleButton>(info["BtClose"], detail_origin);
        // BtArlim is the reference client's register-alarm button: it puts
        // the quest on the tracker.
        buttons[BT_TRACK] = std::make_unique<MapleButton>(info["BtArlim"], detail_origin);

        detail_name = { Text::A12B, Text::LEFT, Text::WHITE, "", DETAIL_NAME_WIDTH };

        body_slider = {
            11,
            { DETAIL_BODY_CLIP_Y,
              static_cast<int16_t>(DETAIL_BODY_CLIP_Y + DETAIL_BODY_LEN_FULL
                  - SLIDER_ARROW_HEIGHT) },
            static_cast<int16_t>(DETAIL_X + DETAIL_SLIDER_X), 1, 1,
            [&](bool upwards)
            {
                body_scroll = static_cast<int16_t>(
                    body_scroll + (upwards ? -DETAIL_SCROLL_STEP : DETAIL_SCROLL_STEP));
            }
        };
        body_slider.setenabled(false);

        summary_slider = {
            11,
            { DETAIL_SUMMARY_CLIP_Y,
              static_cast<int16_t>(DETAIL_SUMMARY_CLIP_Y + DETAIL_SUMMARY_CLIP_HEIGHT
                  - SLIDER_ARROW_HEIGHT) },
            static_cast<int16_t>(DETAIL_X + DETAIL_SLIDER_X), 1, 1,
            [&](bool upwards)
            {
                summary_scroll = static_cast<int16_t>(
                    summary_scroll + (upwards ? -DETAIL_SCROLL_STEP : DETAIL_SCROLL_STEP));
            }
        };
        summary_slider.setenabled(false);

        slider = {
            11,
            { LIST_TOP, static_cast<int16_t>(LIST_TOP + ROWS * ROW_HEIGHT) },
            SLIDER_X, ROWS, 0,
            [&](bool upwards)
            {
                int16_t shift = upwards ? -1 : 1;
                if (offset + shift >= 0)
                {
                    change_offset(static_cast<uint16_t>(offset + shift));
                }
            }
        };

        dimension = { LIST_WIDTH, LIST_HEIGHT };

        update_quests();
    }

    bool UIQuestLog::detail_open() const
    {
        return selected >= 0 && static_cast<size_t>(selected) < current_list().size() &&
            !current_list()[selected].header;
    }

    const std::vector<UIQuestLog::Row>& UIQuestLog::current_list() const
    {
        return lists[tab];
    }

    bool UIQuestLog::is_collapsed(Tab which, int32_t area) const
    {
        return collapsed.count({ static_cast<int32_t>(which), area }) > 0;
    }

    void UIQuestLog::build_rows(Tab which, std::vector<int16_t> questids)
    {
        std::vector<Row>& rows = lists[which];
        rows.clear();

        // Sort into regions, and by the order the game files give inside one.
        // The reference client calls that key sortkey; this data spells it
        // order.
        std::map<int32_t, std::vector<int16_t>> grouped;
        for (int16_t questid : questids)
        {
            grouped[QuestData::get(questid).get_area()].push_back(questid);
        }

        // The area number is the index the reference client keeps its region
        // names under, so counting up through them is the order it lists them
        // in. Regions the files do not name fall at the end on their own.
        for (auto& entry : grouped)
        {
            int32_t area = entry.first;
            std::vector<int16_t>& group = entry.second;
            std::sort(group.begin(), group.end(), [](int16_t left, int16_t right)
            {
                int16_t lorder = QuestData::get(left).get_order();
                int16_t rorder = QuestData::get(right).get_order();
                return lorder != rorder ? lorder < rorder : left < right;
            });

            Row heading;
            heading.header = true;
            heading.area = area;
            heading.count = static_cast<int32_t>(group.size());
            rows.push_back(heading);

            // A shut region keeps its heading and drops its quests, which is
            // what InsertQuestInfo does when IsMinimizedCategory says so.
            if (is_collapsed(which, area))
            {
                continue;
            }

            for (int16_t questid : group)
            {
                Row row;
                row.questid = questid;
                row.area = area;
                rows.push_back(row);
            }
        }
    }

    void UIQuestLog::toggle_category(size_t rowindex)
    {
        const std::vector<Row>& rows = current_list();
        if (rowindex >= rows.size() || !rows[rowindex].header)
        {
            return;
        }

        std::pair<int32_t, int32_t> key{ static_cast<int32_t>(tab), rows[rowindex].area };
        if (!collapsed.erase(key))
        {
            collapsed.insert(key);
        }

        // Rebuilding the tab puts the list back at the top, which would throw
        // the player away from the heading they just clicked.
        uint16_t was = offset;
        update_quests();

        int16_t rowcount = static_cast<int16_t>(current_list().size());
        if (rowcount > ROWS)
        {
            change_offset(std::min(was, static_cast<uint16_t>(rowcount - ROWS)));
        }
    }

    bool UIQuestLog::can_take(int16_t questid) const
    {
        const QuestData& quest = QuestData::get(questid);
        const QuestData::Requirements& reqs = quest.get_requirements(QuestData::START);

        int16_t level = static_cast<int16_t>(stats.get_stat(Maplestat::LEVEL));
        if (level < reqs.lvmin || (reqs.lvmax > 0 && level > reqs.lvmax))
        {
            return false;
        }

        if (!reqs.jobs.empty())
        {
            int32_t job = stats.get_stat(Maplestat::JOB);
            if (std::find(reqs.jobs.begin(), reqs.jobs.end(), job) == reqs.jobs.end())
            {
                return false;
            }
        }

        for (const QuestData::QuestState& required : reqs.quests)
        {
            if (quests.get_state(required.id) != required.state)
            {
                return false;
            }
        }

        return true;
    }

    void UIQuestLog::update_quests()
    {
        build_rows(IN_PROGRESS, quests.get_started());
        build_rows(COMPLETED, quests.get_completed());

        std::vector<int16_t> available;
        for (int16_t questid : QuestData::all_ids())
        {
            if (quests.get_state(questid) != Questlog::NOT_STARTED)
            {
                continue;
            }

            const QuestData& quest = QuestData::get(questid);
            // A quest with no name has nothing to show in a list, which is how
            // the game files carry entries that are not quests the player takes.
            if (!quest.is_valid() || quest.get_name().empty())
            {
                continue;
            }

            // Only the ones an npc actually offers. The rest are started by
            // the server on its own and would just be noise here.
            if (quest.is_auto_start() || quest.get_npc(QuestData::START) == 0)
            {
                continue;
            }

            if (can_take(questid))
            {
                available.push_back(questid);
            }
        }

        build_rows(AVAILABLE, std::move(available));

        // The selection is an index, so anything that changed the list length
        // has invalidated it.
        change_tab(tab);
    }

    void UIQuestLog::change_tab(Tab newtab)
    {
        tab = newtab;
        offset = 0;
        selected = -1;

        for (uint16_t i = BT_TAB_AVAILABLE; i <= BT_TAB_COMPLETED; ++i)
        {
            buttons[i]->set_state(
                i - BT_TAB_AVAILABLE == tab ? Button::PRESSED : Button::NORMAL
            );
        }

        int16_t rows = static_cast<int16_t>(current_list().size());
        slider.setrows(ROWS, rows);
        slider.setenabled(rows > ROWS);

        labels.clear();
        for (const Row& row : current_list())
        {
            if (!row.header)
            {
                labels.emplace_back(
                    Text::A11M, Text::LEFT, Text::BLACK,
                    QuestData::get(row.questid).get_name(), LIST_TEXT_WIDTH, false
                );
                continue;
            }

            // A region the files do not name still gets a heading, so its
            // quests are not silently folded in with another region's.
            const std::string* named = find_category(row.area);
            std::string title = named != nullptr
                ? *named
                : "Area " + std::to_string(row.area);

            // The client formats a heading as "%s (%d)" and draws it white,
            // in an eleven pixel font, over the band.
            std::string caption = title + " (" + std::to_string(row.count) + ")";

            labels.emplace_back(
                Text::A11M, Text::LEFT, Text::WHITE,
                caption, LIST_TEXT_WIDTH, false
            );
        }

        refresh_details();
    }

    void UIQuestLog::change_offset(uint16_t newoffset)
    {
        int16_t rows = static_cast<int16_t>(current_list().size());
        if (newoffset + ROWS > rows)
        {
            return;
        }

        offset = newoffset;
    }

    void UIQuestLog::select(int32_t index)
    {
        if (index < 0 || static_cast<size_t>(index) >= current_list().size())
        {
            return;
        }

        // A heading is not a quest: clicking it opens or shuts its region.
        if (current_list()[index].header)
        {
            toggle_category(static_cast<size_t>(index));
            return;
        }

        // The picked quest is the one drawn in blue, so the labels of both the
        // old and the new pick have to be restyled.
        if (detail_open())
        {
            labels[selected].change_color(Text::BLACK);
        }

        selected = index;
        labels[selected].change_color(Text::BLUE);

        refresh_details();
    }

    void UIQuestLog::refresh_details()
    {
        // The detail pane and its buttons only exist while a quest is picked.
        buttons[BT_DETAIL_CLOSE]->set_active(detail_open());
        // SetButton shows Forfeit and Quest Helper for any quest that is
        // under way or already done - it hides them only for one that has not
        // been taken - and greys them out rather than taking them away. The
        // two calls it makes are different: +0x24 shows or hides, +0x1c
        // enables or disables.
        bool started_or_done = detail_open() &&
            (tab == IN_PROGRESS || tab == COMPLETED);

        buttons[BT_GIVEUP]->set_active(started_or_done);
        buttons[BT_TRACK]->set_active(started_or_done);

        // Giving up is disabled once the quest is done.
        giveup_enabled = started_or_done && tab == IN_PROGRESS;
        buttons[BT_GIVEUP]->set_state(
            giveup_enabled ? Button::NORMAL : Button::DISABLED);

        // Quest Helper is disabled for a quest that asks for nothing
        // countable, once the tracker holds five, and for one it already
        // holds - but it stays on screen through all three.
        track_enabled = false;
        if (started_or_done && tab == IN_PROGRESS)
        {
            int16_t showing = current_list()[selected].questid;
            if (UIQuestTracker::worth_tracking(showing))
            {
                if (auto tracker = UI::get().get_element<UIQuestTracker>())
                {
                    track_enabled = tracker->has_room() && !tracker->is_tracked(showing);
                }
            }
        }
        buttons[BT_TRACK]->set_state(
            track_enabled ? Button::NORMAL : Button::DISABLED);

        if (!detail_open())
        {
            detail_name.change_text("");
            detail_body.parse(EMPTY, 0, DETAIL_TEXT_WIDTH);
            detail_demand.parse(EMPTY, 0, DETAIL_TEXT_WIDTH);
            detail_reward.parse(EMPTY, 0, DETAIL_TEXT_WIDTH);
            return;
        }

        int16_t questid = current_list()[selected].questid;
        const QuestData& quest = QuestData::get(questid);

        detail_name.change_text(quest.get_name());

        // The npc the quest is taken from, which is the one SetNPC shows.
        detail_npc = {};
        int32_t giver = quest.get_npc(QuestData::START);
        if (giver == 0)
        {
            giver = quest.get_npc(QuestData::END);
        }
        if (giver != 0)
        {
            std::string strid = std::to_string(giver);
            strid.insert(0, 7 - strid.size(), '0');
            strid.append(".img");

            nl::node src = nl::nx::npc[strid];
            std::string link = src["info"]["link"];
            if (!link.empty())
            {
                src = nl::nx::npc[link + ".img"];
            }

            detail_npc = src["stand"];
        }

        // Which description applies follows the quest's state, the same way
        // the three descriptions are laid out in the game files.
        size_t descindex = 0;
        switch (tab)
        {
        case IN_PROGRESS:
            descindex = 1;
            break;
        case COMPLETED:
            descindex = 2;
            break;
        default:
            break;
        }

        std::string body = quest.get_desc(descindex);

        auto timer = timers.find(questid);
        if (timer != timers.end())
        {
            body += "\n\nTime left: " + format_time_left(timer->second);
        }

        detail_body.parse(body, questid, DETAIL_TEXT_WIDTH);

        // The reward box states what the quest wants and what it pays, both
        // written as markup that names items and mobs by picture as well as
        // by word.
        const std::string& demand = tab == COMPLETED ? EMPTY : quest.get_demand_summary();
        detail_demand.parse(demand, questid, DETAIL_TEXT_WIDTH);
        detail_reward.parse(quest.get_reward_summary(), questid, DETAIL_TEXT_WIDTH);

        refresh_detail_scroll();
    }

    bool UIQuestLog::has_summary() const
    {
        return !detail_demand.empty() || !detail_reward.empty();
    }

    int16_t UIQuestLog::body_view_height() const
    {
        // The description box gives up its lower half to the reward box, and
        // takes the whole pane back for a quest that states no reward.
        return has_summary() ? DETAIL_BODY_LEN_BOXED : DETAIL_BODY_LEN_FULL;
    }

    int16_t UIQuestLog::summary_content_height() const
    {
        return static_cast<int16_t>(detail_demand.height() + detail_reward.height());
    }

    void UIQuestLog::refresh_detail_scroll()
    {
        auto fit = [](Slider& bar, int16_t content, int16_t view)
        {
            int16_t overflow = static_cast<int16_t>(content - view);
            if (overflow <= 0)
            {
                bar.setenabled(false);
                bar.setrows(0, 1, 1);
                return false;
            }

            // One notch of the bar is one line of text, so a bar is as long
            // as the text has lines that do not fit.
            int16_t steps = static_cast<int16_t>(
                (overflow + DETAIL_SCROLL_STEP - 1) / DETAIL_SCROLL_STEP);
            bar.setenabled(true);
            bar.setrows(0, 1, static_cast<int16_t>(steps + 1));
            return true;
        };

        body_scroll = 0;
        summary_scroll = 0;

        // The client's scrollbar runs from its y for exactly its length; a
        // Slider here puts its lower arrow *past* the range it is given, so
        // the range has to stop an arrow short or the bar runs down over the
        // buttons along the bottom of the pane.
        int16_t view = body_view_height();
        body_slider.setvertical({
            DETAIL_BODY_CLIP_Y,
            static_cast<int16_t>(DETAIL_BODY_CLIP_Y + view - SLIDER_ARROW_HEIGHT)
        });
        fit(body_slider, detail_body.height(), view);
        fit(summary_slider, summary_content_height(), DETAIL_SUMMARY_CLIP_HEIGHT);
    }

    void UIQuestLog::set_timer(int16_t questid, int32_t seconds)
    {
        timers[questid] = static_cast<int64_t>(seconds) * 1000;
        refresh_details();
    }

    void UIQuestLog::clear_timer(int16_t questid)
    {
        timers.erase(questid);
        refresh_details();
    }

    void UIQuestLog::update()
    {
        UIElement::update();

        if (detail_open())
        {
            detail_npc.update();
        }

        if (timers.empty())
        {
            return;
        }

        for (auto& timer : timers)
        {
            timer.second = std::max<int64_t>(0, timer.second - Constants::TIMESTEP);
        }

        // Only the quest on screen needs its countdown redrawn, and only when
        // the displayed second actually changed.
        if (detail_open())
        {
            auto timer = timers.find(current_list()[selected].questid);
            if (timer != timers.end() && timer->second % 1000 < Constants::TIMESTEP)
            {
                refresh_details();
            }
        }
    }

    void UIQuestLog::draw(float alpha) const
    {
        if (detail_open())
        {
            // Behind the list pane, so the two frames overlap the way stacked
            // windows do rather than the detail sitting on top of the tabs.
            Point<int16_t> detailpos = position + Point<int16_t>(DETAIL_X, 0);
            for (const Texture& piece : detail_frame)
            {
                piece.draw(detailpos);
            }
            if (has_summary())
            {
                detail_summary_frame.draw(detailpos);
            }

            detail_name.draw(detailpos + Point<int16_t>(DETAIL_NAME_X, DETAIL_NAME_Y));
            detail_npc.draw(
                DrawArgument(detailpos + Point<int16_t>(DETAIL_NPC_X, DETAIL_NPC_Y)), alpha);

            GraphicsGL& graphics = GraphicsGL::get();

            // Both panels are cut to their box, so a line scrolled half out
            // of view is cut rather than drawn across the frame.
            graphics.push_cliprect(
                static_cast<int16_t>(detailpos.x() + DETAIL_CLIP_X),
                static_cast<int16_t>(detailpos.y() + DETAIL_BODY_CLIP_Y),
                DETAIL_CLIP_WIDTH, static_cast<int16_t>(body_view_height() - 2)
            );
            detail_body.draw(detailpos + Point<int16_t>(
                DETAIL_TEXT_X, static_cast<int16_t>(DETAIL_BODY_Y - body_scroll)));
            graphics.pop_cliprect();

            if (has_summary())
            {
                graphics.push_cliprect(
                    static_cast<int16_t>(detailpos.x() + DETAIL_CLIP_X),
                    static_cast<int16_t>(detailpos.y() + DETAIL_SUMMARY_CLIP_Y),
                    DETAIL_CLIP_WIDTH, DETAIL_SUMMARY_CLIP_HEIGHT
                );

                // The two blurbs are consecutive entries of the quest's
                // summary, stacked by their own heights the way the client
                // stacks them, with nothing added between.
                int16_t blurby = static_cast<int16_t>(DETAIL_SUMMARY_Y - summary_scroll);
                detail_demand.draw(detailpos + Point<int16_t>(DETAIL_TEXT_X, blurby));
                blurby = static_cast<int16_t>(blurby + detail_demand.height());
                detail_reward.draw(detailpos + Point<int16_t>(DETAIL_TEXT_X, blurby));

                graphics.pop_cliprect();
            }

            // The scrollbars carry the detail pane's offset in their own x,
            // so they are drawn against the window rather than the pane.
            body_slider.draw(position);
            if (has_summary())
            {
                summary_slider.draw(position);
            }
        }

        UIElement::draw(alpha);

        const std::vector<Row>& list = current_list();
        if (list.empty())
        {
            notices[tab].draw(position);
            return;
        }

        int16_t rowy = LIST_TOP;
        for (size_t i = offset; i < list.size() && i < offset + ROWS; ++i)
        {
            if (list[i].header)
            {
                // The band the heading sits on, in the grey the client
                // fills it with.
                GraphicsGL::get().drawrectangle(
                    static_cast<int16_t>(position.x() + CATEGORY_BAND_X),
                    static_cast<int16_t>(position.y() + rowy),
                    CATEGORY_BAND_WIDTH, CATEGORY_BAND_HEIGHT,
                    0x73 / 255.0f, 0x75 / 255.0f, 0x73 / 255.0f, 1.0f
                );

                // Both the picture and the name sit centred in the band.
                // Hanging them off a fixed offset left the text half out of
                // it, because a label is taller than the eleven pixel font
                // the client measures it as.
                const Texture& mark = is_collapsed(tab, list[i].area)
                    ? category_shut
                    : category_open;

                int16_t markdy = static_cast<int16_t>(
                    (CATEGORY_BAND_HEIGHT - mark.height()) / 2);
                mark.draw(position + Point<int16_t>(
                    CATEGORY_ICON_X, static_cast<int16_t>(rowy + markdy))
                    + mark.get_origin());

                int16_t textdy = static_cast<int16_t>(
                    (CATEGORY_BAND_HEIGHT - labels[i].height()) / 2);
                labels[i].draw(position + Point<int16_t>(
                    CATEGORY_TEXT_X, static_cast<int16_t>(rowy + textdy)));
            }
            else
            {
                labels[i].draw(position + Point<int16_t>(LIST_LEFT, rowy));
            }

            rowy += ROW_HEIGHT;
        }

        slider.draw(position);
    }

    int32_t UIQuestLog::row_at(Point<int16_t> cursorpos) const
    {
        Point<int16_t> relative = cursorpos - position;
        if (relative.x() < LIST_LEFT || relative.x() > LIST_LEFT + LIST_TEXT_WIDTH)
        {
            return -1;
        }

        if (relative.y() < LIST_TOP)
        {
            return -1;
        }

        int16_t row = static_cast<int16_t>((relative.y() - LIST_TOP) / ROW_HEIGHT);
        if (row >= ROWS)
        {
            return -1;
        }

        size_t index = offset + row;
        return index < current_list().size() ? static_cast<int32_t>(index) : -1;
    }

    Button::State UIQuestLog::button_pressed(uint16_t buttonid)
    {
        switch (buttonid)
        {
        case BT_TAB_AVAILABLE:
        case BT_TAB_IN_PROGRESS:
        case BT_TAB_COMPLETED:
            change_tab(static_cast<Tab>(buttonid - BT_TAB_AVAILABLE));
            return Button::PRESSED;
        case BT_GIVEUP:
            if (!giveup_enabled)
            {
                return Button::DISABLED;
            }
            if (detail_open())
            {
                // The record is not dropped here. The server answers a
                // forfeit with a quest record update, and that is what moves
                // the quest out of this list.
                QuestActionPacket::forfeit(current_list()[selected].questid).dispatch();
            }
            return Button::NORMAL;
        case BT_TRACK:
            if (!track_enabled)
            {
                // Greyed out: on screen, but it does nothing.
                return Button::DISABLED;
            }
            if (detail_open())
            {
                if (auto tracker = UI::get().get_element<UIQuestTracker>())
                {
                    // Asked for by hand, so neither the auto-register setting
                    // nor an earlier drop stands in the way.
                    tracker->track(current_list()[selected].questid, false);
                }
                refresh_details();
            }
            // Whatever comes back here is what the button is left showing, so
            // it has to be the state refresh_details just worked out.
            return track_enabled ? Button::NORMAL : Button::DISABLED;
        case BT_DETAIL_CLOSE:
            if (detail_open())
            {
                labels[selected].change_color(Text::BLACK);
            }
            selected = -1;
            refresh_details();
            return Button::NORMAL;
        case BT_CLOSE:
            active = false;
            return Button::NORMAL;
        default:
            return Button::PRESSED;
        }
    }

    void UIQuestLog::send_scroll(double yoffset)
    {
        // A wheel notch arrives without a position, so it goes to whichever
        // panel the cursor was last over.
        if (detail_open() && lastcursor.x() - position.x() >= DETAIL_X)
        {
            int16_t y = static_cast<int16_t>(lastcursor.y() - position.y());
            if (has_summary() && y >= DETAIL_SUMMARY_CLIP_Y)
            {
                summary_slider.send_scroll(yoffset);
            }
            else
            {
                body_slider.send_scroll(yoffset);
            }
            return;
        }

        slider.send_scroll(yoffset);
    }

    void UIQuestLog::send_key(int32_t, bool pressed, bool escape)
    {
        if (pressed && escape)
        {
            active = false;
        }
    }

    bool UIQuestLog::is_in_range(Point<int16_t> cursorpos) const
    {
        // The detail pane is drawn outside the element's own bounds, so it
        // would otherwise take no clicks at all.
        Point<int16_t> extent = dimension;
        if (detail_open())
        {
            extent.shift_x(DETAIL_WIDTH);
        }

        return Rectangle<int16_t>(position, position + extent).contains(cursorpos);
    }

    bool UIQuestLog::remove_cursor(bool clicked, Point<int16_t> cursorpos)
    {
        if (!clicked)
        {
            wasclicked = false;
            pressedrow = -1;
        }

        if (slider.remove_cursor(clicked) ||
            body_slider.remove_cursor(clicked) ||
            summary_slider.remove_cursor(clicked))
        {
            return true;
        }

        return UIDragElement::remove_cursor(clicked, cursorpos);
    }

    UIElement::CursorResult UIQuestLog::send_cursor(bool clicked, Point<int16_t> cursorpos)
    {
        lastcursor = cursorpos;

        // The scrollbars work in the window's own coordinates, which is what
        // every other window hands them and what this one did not.
        Point<int16_t> relative = cursorpos - position;

        if (slider.isenabled())
        {
            Cursor::State state = slider.send_cursor(relative, clicked);
            if (state != Cursor::IDLE)
            {
                return { state, true };
            }
        }

        if (detail_open())
        {
            for (Slider* bar : { &body_slider, &summary_slider })
            {
                if (!bar->isenabled())
                {
                    continue;
                }

                Cursor::State state = bar->send_cursor(relative, clicked);
                if (state != Cursor::IDLE)
                {
                    return { state, true };
                }
            }
        }

        // A row acts like a button, the way the client's own do: the press
        // remembers which row it landed on and the release only acts when it
        // comes up over that same row. Acting while the button is merely held
        // down is what made dragging across a heading open and shut it over
        // and over.
        int32_t row = row_at(cursorpos);
        if (clicked && !wasclicked)
        {
            pressedrow = row;
        }
        else if (!clicked && wasclicked)
        {
            if (row >= 0 && row == pressedrow)
            {
                select(row);
            }
            pressedrow = -1;
        }
        wasclicked = clicked;

        if (row >= 0)
        {
            return { clicked ? Cursor::CLICKING : Cursor::CANCLICK, true };
        }

        return UIDragElement::send_cursor(clicked, cursorpos);
    }
}
