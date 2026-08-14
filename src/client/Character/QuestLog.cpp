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
#include "QuestLog.h"

#include "../Data/QuestData.h"

namespace jrc
{
    namespace
    {
        // One mob counter is three digits wide in the progress string.
        constexpr size_t PROGRESS_DIGITS = 3;

        // The quest a given quest keeps its progress counter in, or zero when
        // it keeps its own. Either stage may name it.
        int16_t info_number_of(int16_t questid)
        {
            const QuestData& quest = QuestData::get(questid);
            if (int16_t infonumber = quest.get_requirements(QuestData::START).infonumber)
            {
                return infonumber;
            }
            return quest.get_requirements(QuestData::END).infonumber;
        }
    }

    void Questlog::apply_started(int16_t questid, const std::string& progress)
    {
        if (lastapplied != 0 && info_number_of(lastapplied) == questid)
        {
            infos[questid] = progress;
            // A counter record cannot own the record after it in turn, so the
            // next one starts a fresh pair.
            lastapplied = 0;
            return;
        }

        Record& record = records[questid];
        record.state = STARTED;
        record.progress = progress;
        record.completiontime = 0;
        lastapplied = questid;
    }

    void Questlog::add_completed(int16_t questid, int64_t completiontime)
    {
        Record& record = records[questid];
        record.state = COMPLETED;
        record.progress.clear();
        record.completiontime = completiontime;
        // A completion is sent on its own, never with a counter record behind it.
        lastapplied = 0;
    }

    void Questlog::forfeit(int16_t questid)
    {
        records.erase(questid);
        // A quest being reset is still followed by its counter record, which
        // the server deliberately leaves standing.
        lastapplied = questid;
    }

    void Questlog::reset_update_sequence()
    {
        lastapplied = 0;
    }

    const Questlog::Record* Questlog::find(int16_t questid) const
    {
        auto iter = records.find(questid);
        return iter == records.end() ? nullptr : &iter->second;
    }

    Questlog::State Questlog::get_state(int16_t questid) const
    {
        const Record* record = find(questid);
        return record ? record->state : NOT_STARTED;
    }

    bool Questlog::is_started(int16_t questid) const
    {
        return get_state(questid) == STARTED;
    }

    bool Questlog::is_completed(int16_t questid) const
    {
        return get_state(questid) == COMPLETED;
    }

    const std::string& Questlog::get_progress(int16_t questid) const
    {
        static const std::string noprogress;

        if (const Record* record = find(questid))
        {
            if (!record->progress.empty())
            {
                return record->progress;
            }
        }

        // A quest that keeps its counter in another record has nothing in its
        // own, so fall back to the record it named.
        auto iter = infos.find(questid);
        return iter == infos.end() ? noprogress : iter->second;
    }

    int32_t Questlog::get_mob_progress(int16_t questid, size_t mobindex) const
    {
        const std::string& progress = get_progress(questid);

        size_t start = mobindex * PROGRESS_DIGITS;
        if (start + PROGRESS_DIGITS > progress.size())
        {
            return 0;
        }

        int32_t count = 0;
        for (size_t i = start; i < start + PROGRESS_DIGITS; ++i)
        {
            char digit = progress[i];
            if (digit < '0' || digit > '9')
            {
                // The counters are fixed-width decimals. Anything else means
                // this quest tracks something other than kills in its progress
                // string, and reading on would invent a number.
                return 0;
            }
            count = count * 10 + (digit - '0');
        }

        return count;
    }

    int64_t Questlog::get_completion_time(int16_t questid) const
    {
        const Record* record = find(questid);
        return record ? record->completiontime : 0;
    }

    std::vector<int16_t> Questlog::get_started() const
    {
        std::vector<int16_t> started;
        for (const auto& entry : records)
        {
            if (entry.second.state == STARTED)
            {
                started.push_back(entry.first);
            }
        }
        return started;
    }

    std::vector<int16_t> Questlog::get_completed() const
    {
        std::vector<int16_t> completed;
        for (const auto& entry : records)
        {
            if (entry.second.state == COMPLETED)
            {
                completed.push_back(entry.first);
            }
        }
        return completed;
    }
}
