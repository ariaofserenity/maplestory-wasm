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
#include "CharStats.h"

#include <initializer_list>
#include <unordered_map>
#include <memory>
#include <vector>

namespace jrc
{
    // Interface for passive buffs.
    class PassiveBuff
    {
    public:
        virtual ~PassiveBuff() {}

        virtual bool is_applicable(CharStats& stats, nl::node level) const = 0;
        virtual void apply_to(CharStats& stats, nl::node level) const = 0;
    };


    // Abstract base for passives without conditions.
    class ConditionlessBuff : public PassiveBuff
    {
    public:
        bool is_applicable(CharStats& stats, nl::node level) const final override;
    };


    // Buff for any passive that simply adds a level's numbers to equip stats.
    // Which property feeds which stat is the whole of the skill, so the mapping
    // is data rather than a class per skill; a property may appear twice where
    // one number raises two stats.
    class StatBonusBuff : public ConditionlessBuff
    {
    public:
        struct Bonus
        {
            const char* property;
            Equipstat::Id stat;
        };

        StatBonusBuff(std::initializer_list<Bonus> bonuses);

        void apply_to(CharStats& stats, nl::node level) const override;

    private:
        std::vector<Bonus> bonuses;
    };


    template <Equipstat::Id XSTAT, Weapon::Type...W>
    // Buff for Mastery skills, which raise the damage floor of one weapon and
    // add a bonus of their own on top. The bonus is accuracy for the second-job
    // masteries and weapon attack for the fourth-job ones, but it is always the
    // level's x.
    class WeaponMasteryBuff : public PassiveBuff
    {
    public:
        bool is_applicable(CharStats& stats, nl::node level) const override;
        void apply_to(CharStats& stats, nl::node level) const override;
    };


    // Buff for Critical Shot. Its prop is how often a critical lands, on top of
    // the small chance every character starts with, and its damage is the whole
    // of what one is worth rather than a bonus over a normal hit.
    class CriticalShotBuff : public ConditionlessBuff
    {
    public:
        void apply_to(CharStats& stats, nl::node level) const override;
    };


    // Buff for Achilles.
    class AchillesBuff : public ConditionlessBuff
    {
    public:
        void apply_to(CharStats& stats, nl::node level) const override;
    };


    // Buff for Berserk.
    class BerserkBuff : public PassiveBuff
    {
    public:
        bool is_applicable(CharStats& stats, nl::node level) const override;
        void apply_to(CharStats& stats, nl::node level) const override;
    };


    // Collection of passive buffs.
    class PassiveBuffs
    {
    public:
        // Register all effects.
        PassiveBuffs();

        // Apply a passive skill effect to the character stats.
        void apply_buff(CharStats& stats, int32_t skill_id, int32_t skill_level) const;

    private:
        std::unordered_map<int32_t, std::unique_ptr<PassiveBuff>> buffs;
    };
}
