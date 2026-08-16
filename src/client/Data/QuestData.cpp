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
#include "QuestData.h"

#include "nlnx/nx.hpp"
#include "nlnx/node.hpp"

#include <algorithm>
#include <unordered_map>

namespace jrc
{
    namespace
    {
        // The images a quest is spread across. Kept here so the traversals
        // below read as the tree they are walking.
        nl::node quest_image(const char* image, int16_t id)
        {
            return nl::nx::quest[image][std::to_string(id)];
        }

        nl::node stage_node(const char* image, int16_t id, QuestData::Stage stage)
        {
            return quest_image(image, id)[std::to_string(static_cast<int32_t>(stage))];
        }

        // Reads the numbered children of a node as consecutive pages of text.
        // Numbering is dense in the game files, but a page is skipped rather
        // than assumed when one is missing, since a hole would otherwise turn
        // into an empty dialogue page the player has to click through.
        std::vector<std::string> read_pages(nl::node parent)
        {
            std::vector<std::string> pages;
            for (int32_t i = 0; ; ++i)
            {
                nl::node page = parent[std::to_string(i)];
                if (!page)
                {
                    break;
                }

                if (page.data_type() == nl::node::type::string)
                {
                    pages.push_back(page.get_string());
                }
            }
            return pages;
        }

        // The first line of a refusal branch. Some branches hold several pages
        // but the reference client only ever shows one before closing, so the
        // rest would never be reached.
        std::string read_stop_line(nl::node parent, const char* reason)
        {
            nl::node branch = parent[reason];
            if (!branch)
            {
                return {};
            }

            std::vector<std::string> pages = read_pages(branch);
            return pages.empty() ? std::string{} : pages.front();
        }

        std::vector<QuestData::Item> read_items(nl::node parent)
        {
            std::vector<QuestData::Item> items;
            for (auto entry : parent["item"])
            {
                QuestData::Item item;
                item.id = entry["id"];
                item.count = entry["count"];
                item.prop = entry["prop"];
                item.job = entry["job"];
                // Absent means the item is offered to both genders, which is
                // not the same as the zero a missing node would otherwise read.
                nl::node gender = entry["gender"];
                item.gender = gender ? static_cast<int32_t>(gender) : 2;
                items.push_back(item);
            }
            return items;
        }
    }

    QuestData::QuestData(int32_t id)
        : questid(static_cast<int16_t>(id)),
          area(0),
          order(0),
          autostart(false),
          autocomplete(false),
          valid(false),
          infoloaded(false)
    {
        for (size_t stage = 0; stage < NUM_STAGES; ++stage)
        {
            requirementsloaded[stage] = false;
            rewardsloaded[stage] = false;
            dialoguesloaded[stage] = false;
        }
    }

    void QuestData::load_info() const
    {
        if (infoloaded)
        {
            return;
        }
        infoloaded = true;

        nl::node src = quest_image("QuestInfo.img", questid);
        if (!src)
        {
            return;
        }

        valid = true;
        name = src["name"].get_string();
        parent = src["parent"].get_string();
        demandsummary = src["demandSummary"].get_string();
        rewardsummary = src["rewardSummary"].get_string();
        area = static_cast<int16_t>(src["area"].get_integer());
        order = static_cast<int16_t>(src["order"].get_integer());
        autostart = src["autoStart"].get_bool();
        // The game files mark self-completing quests two ways and the server
        // accepts either, so a quest flagged only autoPreComplete must not be
        // offered for a manual hand-in here.
        autocomplete = src["autoComplete"].get_bool() || src["autoPreComplete"].get_bool();

        for (size_t i = 0; i < 3; ++i)
        {
            descs[i] = src[std::to_string(i)].get_string();
        }
    }

    void QuestData::load_requirements(Stage stage) const
    {
        if (requirementsloaded[stage])
        {
            return;
        }
        requirementsloaded[stage] = true;

        nl::node src = stage_node("Check.img", questid, stage);
        if (!src)
        {
            return;
        }

        Requirements& reqs = requirements[stage];
        reqs.npc = src["npc"];
        reqs.lvmin = static_cast<int16_t>(src["lvmin"].get_integer());
        reqs.lvmax = static_cast<int16_t>(src["lvmax"].get_integer());
        // The two stages spell the meso requirement differently: starting a
        // quest costs "money", handing one in costs "endmeso".
        reqs.mesos = static_cast<int32_t>(
            src["money"].get_integer(src["endmeso"].get_integer())
        );
        reqs.scripted = src["startscript"] || src["endscript"];
        reqs.infonumber = static_cast<int16_t>(src["infoNumber"].get_integer());

        for (auto job : src["job"])
        {
            reqs.jobs.push_back(job);
        }

        reqs.items = read_items(src);

        for (auto mob : src["mob"])
        {
            reqs.mobs.push_back({ mob["id"], mob["count"] });
        }

        for (auto quest : src["quest"])
        {
            QuestState required;
            required.id = static_cast<int16_t>(quest["id"].get_integer());
            required.state = static_cast<int8_t>(quest["state"].get_integer());
            reqs.quests.push_back(required);
        }
    }

    void QuestData::load_rewards(Stage stage) const
    {
        if (rewardsloaded[stage])
        {
            return;
        }
        rewardsloaded[stage] = true;

        nl::node src = stage_node("Act.img", questid, stage);
        if (!src)
        {
            return;
        }

        Rewards& acts = rewards[stage];
        acts.exp = src["exp"];
        acts.mesos = src["money"];
        acts.fame = src["pop"];
        acts.nextquest = static_cast<int16_t>(src["nextQuest"].get_integer());
        acts.items = read_items(src);
    }

    void QuestData::load_dialogue(Stage stage) const
    {
        if (dialoguesloaded[stage])
        {
            return;
        }
        dialoguesloaded[stage] = true;

        nl::node src = stage_node("Say.img", questid, stage);
        if (!src)
        {
            return;
        }

        Dialogue& say = dialogues[stage];
        say.pages = read_pages(src);
        say.accepted = read_pages(src["yes"]);
        say.declined = read_pages(src["no"]);
        say.asks = src["ask"].get_bool();

        nl::node stop = src["stop"];
        say.stops.resize(NUM_STOP_REASONS);
        say.stops[STOP_ITEM] = read_stop_line(stop, "item");
        say.stops[STOP_MOB] = read_stop_line(stop, "mob");
        say.stops[STOP_NPC] = read_stop_line(stop, "npc");
        say.stops[STOP_QUEST] = read_stop_line(stop, "quest");
        say.stops[STOP_INFO] = read_stop_line(stop, "info");
        say.stops[STOP_DEFAULT] = read_stop_line(stop, "default");
    }

    bool QuestData::is_valid() const
    {
        load_info();
        return valid;
    }

    QuestData::operator bool() const
    {
        return is_valid();
    }

    int16_t QuestData::get_id() const
    {
        return questid;
    }

    const std::string& QuestData::get_name() const
    {
        load_info();
        return name;
    }

    const std::string& QuestData::get_parent() const
    {
        load_info();
        return parent;
    }

    int16_t QuestData::get_area() const
    {
        load_info();
        return area;
    }

    int16_t QuestData::get_order() const
    {
        load_info();
        return order;
    }

    const std::string& QuestData::get_desc(size_t index) const
    {
        load_info();
        static const std::string nodesc;
        return index < 3 ? descs[index] : nodesc;
    }

    const std::string& QuestData::get_demand_summary() const
    {
        load_info();
        return demandsummary;
    }

    const std::string& QuestData::get_reward_summary() const
    {
        load_info();
        return rewardsummary;
    }

    bool QuestData::is_auto_start() const
    {
        load_info();
        return autostart;
    }

    bool QuestData::is_auto_complete() const
    {
        load_info();
        return autocomplete;
    }

    const QuestData::Requirements& QuestData::get_requirements(Stage stage) const
    {
        load_requirements(stage);
        return requirements[stage];
    }

    const QuestData::Rewards& QuestData::get_rewards(Stage stage) const
    {
        load_rewards(stage);
        return rewards[stage];
    }

    const QuestData::Dialogue& QuestData::get_dialogue(Stage stage) const
    {
        load_dialogue(stage);
        return dialogues[stage];
    }

    int32_t QuestData::get_npc(Stage stage) const
    {
        return get_requirements(stage).npc;
    }

    const std::vector<int16_t>& QuestData::all_ids()
    {
        // QuestInfo.img is the one image every quest has an entry in, so it is
        // what defines the set. The children come out in string order, which
        // puts "10000" before "1001", hence the sort.
        static std::vector<int16_t> ids = []
        {
            std::vector<int16_t> collected;
            for (auto entry : nl::nx::quest["QuestInfo.img"])
            {
                try
                {
                    collected.push_back(static_cast<int16_t>(std::stoi(entry.name())));
                }
                catch (const std::exception&)
                {
                    // A child whose name is not a quest id is not a quest.
                }
            }
            std::sort(collected.begin(), collected.end());
            return collected;
        }();

        return ids;
    }

    const std::vector<QuestData::NpcStage>& QuestData::stages_at_npc(int32_t npcid)
    {
        using Index = std::unordered_map<int32_t, std::vector<NpcStage>>;

        static const Index index = []
        {
            Index built;
            for (auto quest : nl::nx::quest["Check.img"])
            {
                int16_t questid = 0;
                try
                {
                    questid = static_cast<int16_t>(std::stoi(quest.name()));
                }
                catch (const std::exception&)
                {
                    continue;
                }

                for (size_t stage = 0; stage < NUM_STAGES; ++stage)
                {
                    nl::node npc = quest[std::to_string(stage)]["npc"];
                    if (npc)
                    {
                        built[npc].push_back({ questid, static_cast<Stage>(stage) });
                    }
                }
            }

            // Quests are reached in string order, so put each npc's list back
            // into quest order - the order the player expects to be offered a
            // questline in.
            for (auto& entry : built)
            {
                std::sort(
                    entry.second.begin(),
                    entry.second.end(),
                    [](const NpcStage& left, const NpcStage& right)
                    {
                        return left.questid < right.questid;
                    }
                );
            }

            return built;
        }();

        static const std::vector<NpcStage> none;

        auto found = index.find(npcid);
        return found == index.end() ? none : found->second;
    }
}
