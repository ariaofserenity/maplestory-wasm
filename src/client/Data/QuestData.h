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
#include "../Template/Cache.h"

#include <cstdint>
#include <string>
#include <vector>

namespace jrc
{
    // Everything the client knows about one quest, read from Quest.nx.
    //
    // A quest is described by four parallel trees keyed on the quest id:
    // QuestInfo.img holds what the quest log shows, Check.img the conditions
    // for starting and for handing in, Act.img what handing in awards, and
    // Say.img the dialogue the client itself plays when the player talks to
    // the quest npc. The two stages of a quest - start and end - are the
    // children "0" and "1" of the latter three, which is why almost everything
    // below is indexed by Stage.
    //
    // The server owns the authoritative copy of all of this; the client reads
    // it to know which npc to offer a quest at, what to draw in the quest log,
    // and which of the canned refusal lines to show when a hand-in is not yet
    // possible. Nothing here is trusted for anything the server decides.
    //
    // Like the other data classes this resolves nothing until it is asked for.
    // The client runs on a lazily fetched filesystem, so touching a node can
    // block on a download, and quest data is first reached from inside a packet
    // handler.
    class QuestData : public Cache<QuestData>
    {
    public:
        // The two halves of a quest. Both Check, Act and Say store their data
        // under a child named after these values.
        enum Stage
        {
            START = 0,
            END = 1,
            NUM_STAGES
        };

        // An item requirement or an item reward. A negative count in a reward
        // means the item is taken away rather than given.
        struct Item
        {
            int32_t id = 0;
            int32_t count = 0;
            // Chance weight when the reward is one of several random items.
            // Zero for a reward that is always given.
            int32_t prop = 0;
            // Restricts the reward to one job branch, zero when unrestricted.
            int32_t job = 0;
            // 0 female, 1 male, 2 both. Both is the default.
            int32_t gender = 2;
        };

        // A number of a given mob that has to be killed.
        struct Mob
        {
            int32_t id = 0;
            int32_t count = 0;
        };

        // Another quest that has to be in a given state, where the state is the
        // same 0/1/2 the server sends for quest records.
        struct QuestState
        {
            int16_t id = 0;
            int8_t state = 0;
        };

        // What has to be true before this stage of the quest can be performed.
        struct Requirements
        {
            // The npc the player has to talk to for this stage. Zero when the
            // stage names none, which is how auto-start quests are written.
            int32_t npc = 0;
            int16_t lvmin = 0;
            // Zero means the quest has no upper level bound.
            int16_t lvmax = 0;
            int32_t mesos = 0;
            // Jobs allowed to take the quest. Empty means every job.
            std::vector<int32_t> jobs;
            std::vector<Item> items;
            std::vector<Mob> mobs;
            std::vector<QuestState> quests;
            // Set when the server, not the client, plays the dialogue for this
            // stage. Only its presence matters, never its contents.
            bool scripted = false;
            // Quest whose record holds this quest's progress counter. Zero when
            // the quest counts progress in its own record.
            int16_t infonumber = 0;
        };

        // What handing in the stage awards.
        struct Rewards
        {
            int32_t exp = 0;
            int32_t mesos = 0;
            int32_t fame = 0;
            std::vector<Item> items;
            // Quest that opens up once this one is handed in, zero for none.
            int16_t nextquest = 0;
        };

        // One canned line the npc says when a hand-in is refused, keyed by
        // which kind of requirement was not met.
        enum StopReason
        {
            STOP_ITEM,
            STOP_MOB,
            STOP_NPC,
            STOP_QUEST,
            STOP_INFO,
            STOP_DEFAULT,
            NUM_STOP_REASONS
        };

        // The dialogue the client plays for one stage of the quest.
        struct Dialogue
        {
            // The pages shown in order. Empty when the quest has no dialogue of
            // its own for this stage.
            std::vector<std::string> pages;
            // Pages shown after the player accepts, then the quest action goes
            // out. Empty is normal - most quests say nothing after accepting.
            std::vector<std::string> accepted;
            // Pages shown after the player declines.
            std::vector<std::string> declined;
            // Refusal lines, indexed by StopReason. An empty string means the
            // quest gives no line for that reason.
            std::vector<std::string> stops;
            // Whether the last page asks the player a yes/no question rather
            // than just being read and acknowledged.
            bool asks = false;
        };

        // Whether the quest exists in the game files at all.
        bool is_valid() const;
        explicit operator bool() const;

        int16_t get_id() const;
        const std::string& get_name() const;
        // The questline this quest belongs to, empty when it stands alone.
        const std::string& get_parent() const;
        // The area code the quest log groups the quest under.
        int16_t get_area() const;
        // Where the quest sorts within its questline.
        int16_t get_order() const;
        // The three descriptions: the one shown before starting, the one shown
        // while in progress, and the one shown once completed.
        const std::string& get_desc(size_t index) const;
        // The pre-rendered demand and reward blurbs the quest log shows. Both
        // are empty for quests that state none.
        const std::string& get_demand_summary() const;
        const std::string& get_reward_summary() const;
        // Quests the server starts on its own once their conditions are met.
        // The client must not offer these at an npc.
        bool is_auto_start() const;
        // Quests the server hands in on its own. The game files spell this two
        // ways and the server treats either as auto-complete, so this does too.
        bool is_auto_complete() const;

        const Requirements& get_requirements(Stage stage) const;
        const Rewards& get_rewards(Stage stage) const;
        const Dialogue& get_dialogue(Stage stage) const;

        // The npc this stage is performed at. Convenience for the lookup that
        // decides whether a clicked npc has anything to offer.
        int32_t get_npc(Stage stage) const;

        // Every quest id present in the game files, in ascending order. Read
        // once and kept, because the quest log and the npc lookup both walk it.
        static const std::vector<int16_t>& all_ids();

        // One stage of one quest, as reached from the npc that performs it.
        struct NpcStage
        {
            int16_t questid;
            Stage stage;
        };

        // The quest stages the given npc performs, in ascending quest order.
        //
        // Answering this by asking every quest for its npc would mean opening
        // thousands of nodes on a filesystem that fetches them over the
        // network, and it is asked on every npc click. Check.img is therefore
        // walked once and inverted into an index the first time any npc is
        // clicked.
        static const std::vector<NpcStage>& stages_at_npc(int32_t npcid);

    private:
        // Allow the cache to use the constructor.
        friend Cache<QuestData>;
        // Note the quest's id. No game file is opened here.
        QuestData(int32_t id);

        // Read the QuestInfo.img fields, all of which are wanted together.
        void load_info() const;
        void load_requirements(Stage stage) const;
        void load_rewards(Stage stage) const;
        void load_dialogue(Stage stage) const;

        int16_t questid;

        mutable std::string name;
        mutable std::string parent;
        mutable std::string demandsummary;
        mutable std::string rewardsummary;
        mutable std::string descs[3];
        mutable int16_t area;
        mutable int16_t order;
        mutable bool autostart;
        mutable bool autocomplete;
        mutable bool valid;
        mutable bool infoloaded;

        mutable Requirements requirements[NUM_STAGES];
        mutable bool requirementsloaded[NUM_STAGES];

        mutable Rewards rewards[NUM_STAGES];
        mutable bool rewardsloaded[NUM_STAGES];

        mutable Dialogue dialogues[NUM_STAGES];
        mutable bool dialoguesloaded[NUM_STAGES];
    };
}
