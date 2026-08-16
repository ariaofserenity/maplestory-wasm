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
#include "UIQuestTracker.h"

#include "../Components/MapleButton.h"

#include "../UI.h"
#include "../KeyType.h"
#include "UIQuestLog.h"

#include "../../Data/ItemData.h"
#include "../../Data/QuestData.h"
#include "../../Gameplay/Stage.h"

#include "nlnx/nx.hpp"

#include <algorithm>

namespace jrc
{
    namespace
    {
        std::string mob_name(int32_t mobid)
        {
            nl::node name = nl::nx::string["Mob.img"][std::to_string(mobid)]["name"];
            return name ? name.get_string() : std::string{};
        }

        std::string count_of(int32_t have, int32_t need)
        {
            return std::to_string(std::min(have, need)) + " / " + std::to_string(need);
        }

        // The client's DrawTextA y is the top of an eleven pixel cell; a
        // Text here is positioned by the top of a box a line-space tall with
        // its baseline at the bottom. Lining the two baselines up is the only
        // exact way to carry one of its coordinates across.
        constexpr int16_t CLIENT_FONT_ASCENT = 11;

        int16_t at_client_y(int16_t client_y, const Text& text)
        {
            return static_cast<int16_t>(client_y + CLIENT_FONT_ASCENT - text.height());
        }

        // Labels are cut to the room they have rather than wrapped. A wrapped
        // one is two lines tall inside an eighteen pixel row, and its width
        // then runs to the wrap limit, which threw the delete button that
        // follows it clean out of the window.
        Text fitted(const std::string& what, Text::Alignment align,
            Text::Color color, int16_t room)
        {
            Text text{ Text::A11M, align, color, what, 0, false };
            if (text.width() <= room || what.empty())
            {
                return text;
            }

            std::string cut = what;
            while (cut.size() > 1 && text.width() > room)
            {
                cut.pop_back();
                text.change_text(cut + "..");
            }

            return text;
        }

        int32_t percent_of(int64_t have, int64_t need)
        {
            if (need <= 0)
            {
                return 100;
            }

            int64_t done = std::min(have, need) * 100 / need;
            return static_cast<int32_t>(std::clamp<int64_t>(done, 0, 100));
        }
    }

    UIQuestTracker::UIQuestTracker(const Questlog& in_quests)
        : UIDragElement({ WIDTH, MIN_HEIGHT }),
          quests(in_quests),
          maximized(true),
          autoregister(true)
    {
        nl::node src = nl::nx::ui["UIWindow2.img"]["QuestAlarm"];

        backgrnd_max = src["backgrndmax"];
        backgrnd_min = src["backgrndmin"];
        backgrnd_center = src["backgrndcenter"];
        backgrnd_bottom = src["backgrndbottom"];

        buttons[BT_MIN] = std::make_unique<MapleButton>(src["BtMin"]);
        buttons[BT_MAX] = std::make_unique<MapleButton>(src["BtMax"]);
        buttons[BT_AUTO] = std::make_unique<MapleButton>(src["BtAuto"]);
        buttons[BT_QUESTLOG] = std::make_unique<MapleButton>(src["BtQ"]);

        // One per quest that can be followed, the way Draw makes one per row.
        for (size_t i = 0; i < MAX_TRACKED; ++i)
        {
            buttons[static_cast<uint16_t>(BT_DELETE0 + i)] =
                std::make_unique<MapleButton>(src["BtDelete"]);
        }

        // StringPool 3660, formatted with how many quests are on the list.
        title = { Text::A11M, Text::LEFT, Text::WHITE, "", WIDTH };

        resize();
    }

    bool UIQuestTracker::worth_tracking(int16_t questid)
    {
        const QuestData& quest = QuestData::get(questid);
        if (!quest.is_valid())
        {
            return false;
        }

        // The reference client leaves this category off the list outright.
        if (quest.get_area() == UNTRACKED_CATEGORY)
        {
            return false;
        }

        const QuestData::Requirements& reqs = quest.get_requirements(QuestData::END);

        // A quest asking for nothing countable has nothing to show here.
        return !reqs.items.empty() || !reqs.mobs.empty() || reqs.mesos > 0 ||
            !reqs.quests.empty();
    }

    Text::Color UIQuestTracker::progress_color(int32_t percent)
    {
        // CUIQuestAlarm::GetProgressFont, thresholds and all. The colours
        // are the ARGB its four fonts are created with - 0xff2020, 0xff3399,
        // 0xff9900 and 0x28c99b - not the nearest ones already to hand.
        if (percent < 33)
        {
            return Text::BRIGHTRED;
        }
        if (percent < 66)
        {
            return Text::REDVIOLET;
        }
        if (percent < 100)
        {
            return Text::AMBER;
        }
        return Text::MINT;
    }

    int16_t UIQuestTracker::title_y() const
    {
        // Draw puts the title at y 5, once, before it branches on whether the
        // window is open - so this can never depend on which background is
        // showing.
        return at_client_y(TITLE_TEXT_Y, title);
    }

    bool UIQuestTracker::has_room() const
    {
        return tracked.size() < MAX_TRACKED;
    }

    bool UIQuestTracker::is_tracked(int16_t questid) const
    {
        return std::find(tracked.begin(), tracked.end(), questid) != tracked.end();
    }

    void UIQuestTracker::track(int16_t questid, bool automatic)
    {
        if (automatic && !autoregister)
        {
            return;
        }

        if (is_tracked(questid) || !worth_tracking(questid))
        {
            return;
        }

        // A quest the player took off the list stays off until they ask for
        // it again themselves.
        if (automatic && dropped.count(questid))
        {
            return;
        }

        if (tracked.size() >= MAX_TRACKED)
        {
            return;
        }

        dropped.erase(questid);
        tracked.push_back(questid);

        // Following a quest opens the window if it was shut, the way
        // RegisterQuest does.
        maximized = true;
        active = true;
        refresh();
    }

    void UIQuestTracker::drop(int16_t questid)
    {
        auto found = std::find(tracked.begin(), tracked.end(), questid);
        if (found == tracked.end())
        {
            return;
        }

        tracked.erase(found);
        dropped.insert(questid);
        refresh();
    }

    void UIQuestTracker::adopt_started()
    {
        // The reference client reads its list back out of the options file.
        // Nothing saves it here yet, so on entering a map the quests already
        // under way are picked up the same way a fresh one would be - which
        // still respects the cap and anything the player has dropped.
        if (!autoregister)
        {
            return;
        }

        for (int16_t questid : quests.get_started())
        {
            if (tracked.size() >= MAX_TRACKED)
            {
                break;
            }

            if (!is_tracked(questid) && !dropped.count(questid) &&
                worth_tracking(questid))
            {
                tracked.push_back(questid);
            }
        }
    }

    void UIQuestTracker::refresh()
    {
        // A quest that is no longer under way has nothing left to follow.
        tracked.erase(
            std::remove_if(tracked.begin(), tracked.end(),
                [this](int16_t questid) { return !quests.is_started(questid); }),
            tracked.end());

        adopt_started();
        rebuild_rows();
        resize();

        // The quest window's Quest Helper button turns on whether this list
        // holds the quest and how full it is, so it is told whenever either
        // changes - which is what RegisterQuest and DeleteQuestByIndex do
        // through ResetInfo.
        if (auto questlog = UI::get().get_element<UIQuestLog>())
        {
            questlog->refresh_details();
        }

        title.change_text("Quest Helper (" + std::to_string(tracked.size()) + "/5)");
    }

    void UIQuestTracker::rebuild_rows()
    {
        rows.clear();

        const Inventory& inventory = Stage::get().get_player().get_inventory();

        for (size_t i = 0; i < tracked.size(); ++i)
        {
            int16_t questid = tracked[i];
            const QuestData& quest = QuestData::get(questid);
            const QuestData::Requirements& reqs = quest.get_requirements(QuestData::END);

            auto add = [&](bool istitle, const std::string& label,
                const std::string& figure, int32_t percent)
            {
                Row row;
                row.title = istitle;
                row.percent = percent;
                // A quest's name shares its row with the delete button that
                // follows it; a demand shares its row with the figure on the
                // right. Either way the label gets what is left.
                int16_t room = istitle
                    ? static_cast<int16_t>(ROW_RIGHT - ROW_LEFT - DELETE_GAP - 11)
                    : static_cast<int16_t>(ROW_RIGHT - ROW_LEFT - 48);
                row.label = fitted(label, Text::LEFT, Text::WHITE, room);
                if (!figure.empty())
                {
                    row.progress = fitted(figure, Text::RIGHT,
                        progress_color(percent), 48);
                }
                rows.push_back(std::move(row));
            };

            add(true, quest.get_name(), {}, 0);

            for (const QuestData::Item& item : reqs.items)
            {
                int32_t have = inventory.get_total_item_count(item.id);
                add(false, ItemData::get(item.id).get_name(),
                    count_of(have, item.count), percent_of(have, item.count));
            }

            for (size_t m = 0; m < reqs.mobs.size(); ++m)
            {
                int32_t have = quests.get_mob_progress(questid, m);
                add(false, mob_name(reqs.mobs[m].id),
                    count_of(have, reqs.mobs[m].count),
                    percent_of(have, reqs.mobs[m].count));
            }

            if (reqs.mesos > 0)
            {
                int64_t have = inventory.get_meso();
                add(false, "Meso",
                    count_of(static_cast<int32_t>(std::min<int64_t>(have, reqs.mesos)),
                        reqs.mesos),
                    percent_of(have, reqs.mesos));
            }

            for (const QuestData::QuestState& required : reqs.quests)
            {
                bool met = quests.get_state(required.id) == required.state;
                add(false, QuestData::get(required.id).get_name(),
                    met ? "1 / 1" : "0 / 1", met ? 100 : 0);
            }

            // A blank line between quests, but not after the last.
            if (i + 1 < tracked.size())
            {
                rows.emplace_back();
            }
        }
    }

    void UIQuestTracker::resize()
    {
        if (!maximized || rows.empty())
        {
            dimension = { WIDTH, MIN_HEIGHT };
        }
        else
        {
            dimension = { WIDTH, static_cast<int16_t>(
                TITLE_HEIGHT + static_cast<int16_t>(rows.size()) * ROW_HEIGHT
                    + FOOTER_HEIGHT) };
        }

        // BtMin, BtMax, BtAuto and BtQ carry their own placement in the
        // art's origin - which is what CLayoutMan::AddButton means by a
        // CCtrlOriginButton, and why that call had no coordinates to find.
        // Setting a position as well would add to it, so they are left alone.
        buttons[BT_MIN]->set_active(maximized && !rows.empty());
        buttons[BT_MAX]->set_active(!maximized && !rows.empty());
        buttons[BT_AUTO]->set_active(maximized && !rows.empty());
        buttons[BT_QUESTLOG]->set_active(true);

        // A delete button rides on its quest's own name row, fifteen pixels
        // past the end of it.
        for (size_t i = 0; i < MAX_TRACKED; ++i)
        {
            Button* button = buttons[static_cast<uint16_t>(BT_DELETE0 + i)].get();
            if (!maximized || i >= tracked.size())
            {
                button->set_active(false);
                continue;
            }

            button->set_active(true);
            button->set_position(delete_button_pos(i));
        }
    }

    Point<int16_t> UIQuestTracker::delete_button_pos(size_t which) const
    {
        int16_t y = ROW_TOP;
        size_t seen = 0;

        for (const Row& row : rows)
        {
            if (row.title)
            {
                if (seen == which)
                {
                    int16_t x = static_cast<int16_t>(
                        ROW_LEFT + row.label.width() + DELETE_GAP);
                    return {
                        std::min<int16_t>(x, static_cast<int16_t>(ROW_RIGHT - 11)),
                        static_cast<int16_t>(y + (ROW_HEIGHT - 11) / 2)
                    };
                }
                seen++;
            }

            y += ROW_HEIGHT;
        }

        return { 0, 0 };
    }

    void UIQuestTracker::draw(float alpha) const
    {
        if (!maximized || rows.empty())
        {
            backgrnd_min.draw(position);
            title.draw(position + Point<int16_t>(TITLE_TEXT_X, title_y()));
            UIElement::draw(alpha);
            return;
        }

        backgrnd_max.draw(position);

        int16_t y = ROW_TOP;
        for (const Row& row : rows)
        {
            backgrnd_center.draw(position + Point<int16_t>(0, y));

            // Draw puts every one of these at x 10, whether it is a quest's
            // name or one of its demands, and the figure against the right
            // margin. Both sit inside the row's own band.
            if (!row.label.empty())
            {
                row.label.draw(position + Point<int16_t>(
                    ROW_LEFT, at_client_y(y, row.label)));
            }

            if (!row.progress.empty())
            {
                row.progress.draw(position + Point<int16_t>(
                    ROW_RIGHT, at_client_y(y, row.progress)));
            }

            y += ROW_HEIGHT;
        }

        backgrnd_bottom.draw(position + Point<int16_t>(0, y));
        title.draw(position + Point<int16_t>(TITLE_TEXT_X, title_y()));

        UIElement::draw(alpha);
    }

    bool UIQuestTracker::is_in_range(Point<int16_t> cursorpos) const
    {
        return Rectangle<int16_t>(position, position + dimension).contains(cursorpos);
    }

    UIElement::CursorResult UIQuestTracker::send_cursor(bool clicked,
        Point<int16_t> cursorpos)
    {
        return UIDragElement::send_cursor(clicked, cursorpos);
    }

    Button::State UIQuestTracker::button_pressed(uint16_t buttonid)
    {
        switch (buttonid)
        {
        case BT_MIN:
        case BT_MAX:
            // ToggleQuestAlarmState: the window is rebuilt around the new
            // state rather than merely redrawn.
            maximized = !maximized;
            resize();
            return Button::NORMAL;
        case BT_AUTO:
            autoregister = !autoregister;
            return Button::NORMAL;
        case BT_QUESTLOG:
            // 0x7d8 toggles the quest window.
            UI::get().send_menu(KeyAction::QUESTLOG);
            return Button::NORMAL;
        default:
            if (buttonid >= BT_DELETE0 && buttonid < BT_DELETE0 + MAX_TRACKED)
            {
                size_t which = buttonid - BT_DELETE0;
                if (which < tracked.size())
                {
                    drop(tracked[which]);
                }
            }
            return Button::NORMAL;
        }
    }
}
