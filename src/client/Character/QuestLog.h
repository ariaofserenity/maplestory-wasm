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
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace jrc
{
    // The questlog of an individual character.
    //
    // Mirrors the server's view of it: every quest the character has touched
    // has a state, and a started quest additionally carries a progress string.
    // The server is the only writer - everything here arrives either in the
    // character's field-entry packet or in a quest record update - so nothing
    // in this class ever decides that a quest has advanced.
    //
    // The progress string is the concatenation of one zero-padded three-digit
    // counter per mob the quest tracks, in the order the game files list them.
    // That is the shape the server builds it in, and it is opaque otherwise;
    // get_mob_progress is the only thing that takes it apart.
    class Questlog
    {
    public:
        // The states a quest record can be in. These are the values the server
        // sends, not an invention of the client.
        enum State : int8_t
        {
            NOT_STARTED = 0,
            STARTED = 1,
            COMPLETED = 2
        };

        // Apply a record the server sent for a started quest.
        //
        // Not every such record is a quest of its own. A quest that keeps its
        // counter in another quest's record is sent as two records back to
        // back, the second one carrying the other quest's id, and the packet
        // marks both the same way. What tells them apart is that the second
        // one names the id the first one asked for, which is why these have to
        // go through one entry point in the order they arrived.
        void apply_started(int16_t questid, const std::string& progress);
        void add_completed(int16_t questid, int64_t completiontime);
        // Drop a record entirely, for a quest that was given up. The server
        // sends state zero for those and the quest goes back to being
        // available, so keeping an empty record around would be wrong.
        void forfeit(int16_t questid);
        // Forget which quest was last applied, so the first record of a fresh
        // questlog is never mistaken for a continuation of an older one.
        void reset_update_sequence();

        State get_state(int16_t questid) const;
        bool is_started(int16_t questid) const;
        bool is_completed(int16_t questid) const;

        // The raw progress string, empty when the quest has none.
        const std::string& get_progress(int16_t questid) const;
        // How many of the mob at the given index the character has killed for
        // this quest. The index is the mob's position in the quest's mob list.
        int32_t get_mob_progress(int16_t questid, size_t mobindex) const;
        // When the quest was handed in, in the server's time format. Zero for
        // a quest that has not been completed.
        int64_t get_completion_time(int16_t questid) const;

        // The quests in each state, in ascending id order.
        std::vector<int16_t> get_started() const;
        std::vector<int16_t> get_completed() const;

    private:
        struct Record
        {
            State state = NOT_STARTED;
            std::string progress;
            int64_t completiontime = 0;
        };

        const Record* find(int16_t questid) const;

        std::map<int16_t, Record> records;
        // Progress records that belong to a quest other than the one being
        // tracked. Kept apart so they never show up as started quests.
        std::map<int16_t, std::string> infos;
        // The quest the last record applied to, or zero. Only meaningful for
        // deciding whether the record that follows it is that quest's counter.
        int16_t lastapplied = 0;
    };
}
