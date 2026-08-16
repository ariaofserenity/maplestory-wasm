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
#include "PassiveBuffs.h"

#include "../Character/SkillId.h"
#include "../Util/Misc.h"

#include "nlnx/node.hpp"
#include "nlnx/nx.hpp"

namespace jrc
{
    namespace
    {
        std::unique_ptr<PassiveBuff> stat_bonus(
            std::initializer_list<StatBonusBuff::Bonus> bonuses) {

            return std::make_unique<StatBonusBuff>(bonuses);
        }
    }

    bool ConditionlessBuff::is_applicable(CharStats&, nl::node) const
    {
        return true;
    }


    StatBonusBuff::StatBonusBuff(std::initializer_list<Bonus> in_bonuses)
        : bonuses(in_bonuses) {}

    void StatBonusBuff::apply_to(CharStats& stats, nl::node level) const
    {
        for (const Bonus& bonus : bonuses)
        {
            stats.add_value(bonus.stat, level[bonus.property]);
        }
    }


    template <Equipstat::Id XSTAT, Weapon::Type...W>
    bool WeaponMasteryBuff<XSTAT, W...>::is_applicable(CharStats& stats, nl::node) const
    {
        Weapon::Type held = stats.get_weapontype();
        return ((held == W) || ...);
    }

    template <Equipstat::Id XSTAT, Weapon::Type...W>
    void WeaponMasteryBuff<XSTAT, W...>::apply_to(CharStats& stats, nl::node level) const
    {
        // WZ stores mastery in 5% steps above the 10% unmastered floor:
        // e.g. 1 -> 15%, 10 -> 60%. Bow Expert continues the same scale from
        // 11, which is where its 65% starts.
        float mastery = 0.1f + static_cast<float>(level["mastery"]) * 0.05f;
        stats.raise_mastery(mastery);
        stats.add_value(XSTAT, level["x"]);
    }


    void CriticalShotBuff::apply_to(CharStats& stats, nl::node level) const
    {
        stats.add_critical(static_cast<float>(level["prop"]) / 100);
        stats.set_critdamage(static_cast<float>(level["damage"]) / 100);
    }


    void AchillesBuff::apply_to(CharStats& stats, nl::node level) const
    {
        float reducedamage = static_cast<float>(level["x"]) / 1000;
        stats.set_reducedamage(reducedamage);
    }


    bool BerserkBuff::is_applicable(CharStats& stats, nl::node level) const
    {
        float hp_percent = static_cast<float>(level["x"]) / 100;
        int32_t hp_threshold = static_cast<int32_t>(stats.get_total(Equipstat::HP) * hp_percent);
        int32_t hp_current = stats.get_stat(Maplestat::HP);
        return hp_current <= hp_threshold;
    }

    void BerserkBuff::apply_to(CharStats& stats, nl::node level) const
    {
        float damagepercent = static_cast<float>(level["damage"]) / 100;
        stats.set_damagepercent(damagepercent);
    }


    PassiveBuffs::PassiveBuffs()
    {
        // Beginner
        buffs[SkillId::ANGEL_BLESSING] = stat_bonus({
            { "x", Equipstat::WATK },
            { "y", Equipstat::MAGIC },
            { "z", Equipstat::ACC },
            { "z", Equipstat::AVOID }
        });

        // Fighter
        buffs[SkillId::SWORD_MASTERY_FIGHTER] = std::make_unique<WeaponMasteryBuff<Equipstat::ACC, Weapon::SWORD_1H, Weapon::SWORD_2H>>();
        buffs[SkillId::AXE_MASTERY] = std::make_unique<WeaponMasteryBuff<Equipstat::ACC, Weapon::AXE_1H, Weapon::AXE_2H>>();

        // Crusader

        // Hero
        buffs[SkillId::ACHILLES_HERO] = std::make_unique<AchillesBuff>();

        // Page
        buffs[SkillId::SWORD_MASTERY_PAGE] = std::make_unique<WeaponMasteryBuff<Equipstat::ACC, Weapon::SWORD_1H, Weapon::SWORD_2H>>();
        buffs[SkillId::BW_MASTERY] = std::make_unique<WeaponMasteryBuff<Equipstat::ACC, Weapon::MACE_1H, Weapon::MACE_2H>>();

        // White Knight

        // Paladin
        buffs[SkillId::ACHILLES_PALADIN] = std::make_unique<AchillesBuff>();

        // Spearman
        buffs[SkillId::SPEAR_MASTERY] = std::make_unique<WeaponMasteryBuff<Equipstat::ACC, Weapon::SPEAR>>();
        buffs[SkillId::PA_MASTERY] = std::make_unique<WeaponMasteryBuff<Equipstat::ACC, Weapon::POLEARM>>();

        // Dragon Knight

        // Dark Knight
        buffs[SkillId::ACHILLES_DK] = std::make_unique<AchillesBuff>();
        buffs[SkillId::BERSERK] = std::make_unique<BerserkBuff>();

        // Bowman
        buffs[SkillId::BLESSING_OF_AMAZON] = stat_bonus({ { "x", Equipstat::ACC } });
        buffs[SkillId::CRITICAL_SHOT] = std::make_unique<CriticalShotBuff>();

        // Hunter
        buffs[SkillId::BOW_MASTERY] = std::make_unique<WeaponMasteryBuff<Equipstat::ACC, Weapon::BOW>>();

        // Ranger
        buffs[SkillId::THRUST] = stat_bonus({ { "speed", Equipstat::SPEED } });

        // Bowmaster
        buffs[SkillId::BOW_EXPERT] = std::make_unique<WeaponMasteryBuff<Equipstat::WATK, Weapon::BOW>>();

        // Crossbowman
        buffs[SkillId::CROSSBOW_MASTERY] = std::make_unique<WeaponMasteryBuff<Equipstat::ACC, Weapon::CROSSBOW>>();

        // Marksman. The crossbow's fourth-job mastery, and the counterpart of
        // Bow Expert down to raising weapon attack rather than accuracy.
        buffs[SkillId::MARKSMAN_BOOST] = std::make_unique<WeaponMasteryBuff<Equipstat::WATK, Weapon::CROSSBOW>>();

        // Cygnus Knights. The client picks between these and the explorer
        // masteries by job; asking which of the two the character has a level
        // in comes to the same thing, so both are simply registered.
        buffs[SkillId::BOW_MASTERY_WA] = std::make_unique<WeaponMasteryBuff<Equipstat::ACC, Weapon::BOW>>();
        buffs[SkillId::BOW_EXPERT_WA] = std::make_unique<WeaponMasteryBuff<Equipstat::WATK, Weapon::BOW>>();
        buffs[SkillId::SWORD_MASTERY_DW] = std::make_unique<WeaponMasteryBuff<Equipstat::ACC, Weapon::SWORD_1H, Weapon::SWORD_2H>>();
        buffs[SkillId::CLAW_MASTERY_NW] = std::make_unique<WeaponMasteryBuff<Equipstat::ACC, Weapon::CLAW>>();
        buffs[SkillId::KNUCKLER_MASTERY_TB] = std::make_unique<WeaponMasteryBuff<Equipstat::ACC, Weapon::KNUCKLE>>();

        // Assassin
        buffs[SkillId::CLAW_MASTERY] = std::make_unique<WeaponMasteryBuff<Equipstat::ACC, Weapon::CLAW>>();

        // Bandit
        buffs[SkillId::DAGGER_MASTERY] = std::make_unique<WeaponMasteryBuff<Equipstat::ACC, Weapon::DAGGER>>();

        // Brawler
        buffs[SkillId::KNUCKLER_MASTERY] = std::make_unique<WeaponMasteryBuff<Equipstat::ACC, Weapon::KNUCKLE>>();

        // Gunslinger
        buffs[SkillId::GUN_MASTERY] = std::make_unique<WeaponMasteryBuff<Equipstat::ACC, Weapon::GUN>>();

        // Aran
        buffs[SkillId::POLEARM_MASTERY_ARAN] = std::make_unique<WeaponMasteryBuff<Equipstat::ACC, Weapon::POLEARM>>();
        buffs[SkillId::HIGH_MASTERY_ARAN] = std::make_unique<WeaponMasteryBuff<Equipstat::ACC, Weapon::POLEARM>>();
    }

    void PassiveBuffs::apply_buff(CharStats& stats, int32_t skill_id, int32_t skill_level) const
    {
        auto iter = buffs.find(skill_id);
        if (iter == buffs.end())
            return;

        bool wrong_job = !stats.get_job().can_use(skill_id);
        if (wrong_job)
            return;

        std::string strid;
        if (skill_id < 10000000)
        {
            strid = string_format::extend_id(skill_id, 7);
        }
        else
        {
            strid = std::to_string(skill_id);
        }
        nl::node src = nl::nx::skill[strid.substr(0, 3) + ".img"]["skill"][strid]["level"][skill_level];

        const PassiveBuff* buff = iter->second.get();
        if (buff && buff->is_applicable(stats, src))
        {
            buff->apply_to(stats, src);
        }
    }
}
