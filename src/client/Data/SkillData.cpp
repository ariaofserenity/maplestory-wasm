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
#include "SkillData.h"

#include "../Util/Misc.h"

#include <unordered_set>

#include "nlnx/nx.hpp"
#include "nlnx/node.hpp"

namespace jrc
{
    namespace
    {
        // Skills whose level data carries a damage field but which are not
        // direct attacks: buffs that grant magic attack, and persistent area
        // skills the client has no support for yet.
        //
        // Listing the exceptions instead of the rule matters. A skill missing
        // from this set still behaves as a plain attack, which is wrong but
        // visible; a skill missing from a whitelist does nothing at all, which
        // is how ~135 skills came to be silently dead.
        bool is_not_direct_attack(int32_t id)
        {
            static const std::unordered_set<int32_t> exceptions = {
                2101001,  // Meditation (F/P)
                2201001,  // Meditation (I/L)
                12101000, // Meditation (Flame Wizard)
                9101003,  // Bless (GM)
                13101006, // Wind Walk
                2111003,  // Poison Mist
                12111005, // Flame Gear
                4121004,  // Ninja Ambush (Hermit)
                4221004   // Ninja Ambush (Night Lord)
            };
            return exceptions.count(id) > 0;
        }
    }

    SkillData::SkillData(int32_t id)
    {
        // Locate sources
        std::string strid = string_format::extend_id(id, 7);
        nl::node src = nl::nx::skill[strid.substr(0, 3) + ".img"]["skill"][strid];
        nl::node strsrc = nl::nx::string["Skill.img"][strid];


        // Load icons
        icons = { src["icon"], src["iconDisabled"], src["iconMouseOver"] };


        // Load strings
        name = strsrc["name"].get_string();
        desc = strsrc["desc"].get_string();

        for (int32_t level = 1; nl::node sub = strsrc["h" + std::to_string(level)]; level++)
        {
            levels.emplace(level, sub);
        }


        // Load stats
        bool deals_damage = false;
        nl::node levelsrc = src["level"];
        for (auto sub : levelsrc)
        {
            // The level data is the authority on whether a skill is offensive:
            // anything that specifies damage output attacks. Deriving this beats
            // a hand-kept id table, which silently disables every skill nobody
            // remembered to add and cannot be verified against the game files.
            if (sub["damage"] || sub["mad"] || sub["fixdamage"])
                deals_damage = true;

            float damage = (float)sub["damage"] / 100;
            int32_t matk = sub["mad"];
            int32_t fixdamage = sub["fixdamage"];
            int32_t mastery = sub["mastery"];
            uint8_t  attackcount = (uint8_t)sub["attackCount"].get_integer(1);
            uint8_t  mobcount = (uint8_t)sub["mobCount"].get_integer(1);
            uint8_t bulletcount = (uint8_t)sub["bulletCount"].get_integer(1);
            int16_t bulletcost = (int16_t)sub["bulletConsume"].get_integer(bulletcount);
            int32_t hpcost = sub["hpCon"];
            int32_t mpcost = sub["mpCon"];
            float chance = (float)sub["prop"].get_real(100.0) / 100;
            float critical = 0.0f;
            float ignoredef = 0.0f;
            float hrange = (float)sub["range"].get_real(100.0) / 100;
            Rectangle<int16_t> range = sub;
            int32_t level = string_conversion::or_default<int32_t>(sub.name(), -1);
            stats.emplace(
                std::piecewise_construct,
                std::forward_as_tuple(level),
                std::forward_as_tuple(damage, matk, fixdamage, mastery, attackcount, mobcount,
                    bulletcount, bulletcost, hpcost, mpcost, chance, critical, ignoredef, hrange, range)
            );
        }

        element = src["elemAttr"].get_string();
        reqweapon = Weapon::by_value(100 + (int32_t)src["weapon"]);
        masterlevel = static_cast<int32_t>(stats.size());
        passive = (id % 10000) / 1000 == 0;
        // A summon node means the damage belongs to the summoned creature, not
        // to a swing the player makes, so those are not direct attacks either.
        attacking = !passive
            && deals_damage
            && !src["summon"]
            && !is_not_direct_attack(id);
        invisible = src["invisible"].get_bool();
    }

    bool SkillData::is_passive() const
    {
        return passive;
    }

    bool SkillData::is_attack() const
    {
        return attacking;
    }

    bool SkillData::is_invisible() const
    {
        return invisible;
    }

    int32_t SkillData::get_masterlevel() const
    {
        return masterlevel;
    }

    Weapon::Type SkillData::get_required_weapon() const
    {
        return reqweapon;
    }

    const SkillData::Stats& SkillData::get_stats(int32_t level) const
    {
        auto iter = stats.find(level);
        if (iter == stats.end())
        {
            static constexpr Stats null_stats{ 0.0f, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, {} };
            return null_stats;
        }
        return iter->second;
    }

    const std::string& SkillData::get_name() const
    {
        return name;
    }

    const std::string& SkillData::get_desc() const
    {
        return desc;
    }

    const std::string& SkillData::get_level_desc(int32_t level) const
    {
        auto iter = levels.find(level);
        if (iter == levels.end())
        {
            static const std::string null_level = "Missing level description.";
            return null_level;
        }
        else
        {
            return iter->second;
        }
    }

    const Texture& SkillData::get_icon(Icon icon) const
    {
        return icons[icon];
    }
}
