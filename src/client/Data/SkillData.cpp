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
#include "../Util/Randomizer.h"

#include <algorithm>
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
                4221004,  // Ninja Ambush (Night Lord)
                // The four that carry a hit animation without dealing damage
                // of their own. Heal is the awkward one: it is an attack, but
                // only against undead, and sending it as one would stop it
                // healing.
                2301002,  // Heal
                4121003,  // Taunt (Hermit)
                4221003,  // Taunt (Night Lord)
                5221009   // Hypnotize
            };
            return exceptions.count(id) > 0;
        }

        constexpr int32_t NO_ACTION = -1;

        const Randomizer randomizer;
    }

    SkillData::SkillData(int32_t id)
        : skillid(id)
    {
        // Only what can be answered without opening a file. Everything else
        // waits until something actually asks for it.
        passive = (id % 10000) / 1000 == 0;

        rootloaded = false;
        infoloaded = false;
        stringsloaded = false;
        bracketsloaded = false;
        actionsloaded = false;
        iconsloaded = false;

        reqweapon = Weapon::NONE;
        subweapon = Weapon::NONE;
        masterlevel = 0;
        attacking = false;
        invisible = false;
        bracketbyskilllevel = false;
    }

    nl::node SkillData::src() const
    {
        if (!rootloaded)
        {
            std::string strid = string_format::extend_id(skillid, 7);
            root = nl::nx::skill[strid.substr(0, 3) + ".img"]["skill"][strid];
            rootloaded = true;
        }
        return root;
    }

    void SkillData::load_info() const
    {
        if (infoloaded)
            return;
        infoloaded = true;

        nl::node s = src();

        element = s["elemAttr"].get_string();
        reqweapon = Weapon::by_value(100 + (int32_t)s["weapon"]);
        // subWeapon is absent for most skills, and 0 there would read as a real
        // weapon type, so only a present node produces a second option.
        subweapon = s["subWeapon"]
            ? Weapon::by_value(100 + (int32_t)s["subWeapon"])
            : Weapon::NONE;
        invisible = s["invisible"].get_bool();
        bracketbyskilllevel = s["skillLV"].get_bool();

        nl::node levels = s["level"];
        masterlevel = s["masterLevel"].get_integer(
            static_cast<int64_t>(levels.size())
        );

        // The level data is the authority on whether a skill is offensive:
        // anything that specifies damage output attacks. Deriving this beats a
        // hand-kept id table, which silently disables every skill nobody
        // remembered to add and cannot be verified against the game files.
        // Only the three damage fields are read, and the scan stops at the
        // first level that has one, so this stays far cheaper than parsing
        // every level.
        // A hit node is the animation played on a monster the skill strikes,
        // so a skill carrying one attacks even when no level of it states any
        // damage. Arrow Bomb, Snipe, Shadow Meso, Meso Explosion and the
        // event skills all keep their damage percentage in x instead, and
        // were dead without this.
        bool deals_damage = static_cast<bool>(s["hit"]);
        for (nl::node sub : levels)
        {
            if (deals_damage)
                break;

            if (sub["damage"] || sub["mad"] || sub["fixdamage"])
            {
                deals_damage = true;
            }
        }

        // A summon node means the damage belongs to the summoned creature, not
        // to a swing the player makes, so those are not direct attacks either.
        attacking = !passive
            && deals_damage
            && !s["summon"]
            && !is_not_direct_attack(skillid);
    }

    void SkillData::load_strings() const
    {
        if (stringsloaded)
            return;
        stringsloaded = true;

        std::string strid = string_format::extend_id(skillid, 7);
        nl::node strsrc = nl::nx::string["Skill.img"][strid];

        name = strsrc["name"].get_string();
        desc = strsrc["desc"].get_string();

        for (int32_t level = 1; nl::node sub = strsrc["h" + std::to_string(level)]; level++)
        {
            leveldescs.emplace(level, sub);
        }
    }

    nl::node SkillData::level_node(int32_t level) const
    {
        return src()["level"][std::to_string(level)];
    }

    nl::node SkillData::bracket_node(int32_t charlevel, int32_t skilllevel) const
    {
        nl::node table = src()["CharLevel"];
        if (!table)
            return {};

        if (!bracketsloaded)
        {
            bracketsloaded = true;
            // The child's own name is kept rather than reformatted from the
            // parsed number, so a bracket still resolves if the game files spell
            // it in a way std::to_string would not reproduce.
            for (nl::node sub : table)
            {
                brackets.emplace_back(
                    string_conversion::or_default<int32_t>(sub.name(), 0),
                    sub.name()
                );
            }
            std::sort(brackets.begin(), brackets.end(),
                [](const Bracket& a, const Bracket& b) {
                    return a.first < b.first;
                });
        }

        if (brackets.empty())
            return {};

        load_info();
        int32_t key = bracketbyskilllevel ? skilllevel : charlevel;

        // The brackets are half-open ranges: the match is the last one whose
        // base level has been reached.
        const Bracket* match = nullptr;
        for (const Bracket& bracket : brackets)
        {
            if (bracket.first > key)
                break;
            match = &bracket;
        }
        if (match == nullptr)
            return {};

        return table[match->second];
    }

    size_t SkillData::count_indexed(nl::node owner)
    {
        if (!owner)
            return 0;

        // Counted rather than taken from the child count: a node like "ball"
        // carries a "delay" alongside its frames, so only the numbered run
        // counts.
        size_t count = 0;
        while (owner[std::to_string(count)])
        {
            count++;
        }
        return count;
    }

    nl::node SkillData::hit_parent(int32_t charlevel, int32_t skilllevel) const
    {
        nl::node level = level_node(skilllevel)["hit"];
        if (level["0"])
            return level;

        nl::node bracket = bracket_node(charlevel, skilllevel)["hit"];
        if (bracket["0"])
            return bracket;

        return src()["hit"];
    }

    size_t SkillData::count_hits(int32_t charlevel, int32_t skilllevel) const
    {
        return count_indexed(hit_parent(charlevel, skilllevel));
    }

    nl::node SkillData::get_hit(int32_t charlevel, int32_t skilllevel,
        size_t index) const {

        nl::node parent = hit_parent(charlevel, skilllevel);
        size_t count = count_indexed(parent);
        if (count == 0)
            return {};

        return parent[std::to_string(index % count)];
    }

    nl::node SkillData::get_random_hit(int32_t charlevel, int32_t skilllevel) const
    {
        nl::node parent = hit_parent(charlevel, skilllevel);
        size_t count = count_indexed(parent);
        if (count == 0)
            return {};

        return parent[std::to_string(randomizer.next_int(count))];
    }

    nl::node SkillData::get_effect(int32_t charlevel, int32_t skilllevel) const
    {
        nl::node bracket = bracket_node(charlevel, skilllevel)["effect"];
        if (bracket)
            return bracket;

        return src()["effect"];
    }

    nl::node SkillData::ball_of(nl::node owner, bool flip)
    {
        if (flip)
        {
            if (nl::node flipped = owner["flipBall"])
                return flipped;
        }
        return owner["ball"];
    }

    nl::node SkillData::get_ball(int32_t charlevel, int32_t skilllevel,
        bool flip) const {

        if (nl::node bracket = ball_of(bracket_node(charlevel, skilllevel), flip))
            return bracket;

        if (nl::node level = ball_of(level_node(skilllevel), flip))
            return level;

        return ball_of(src(), flip);
    }

    nl::node SkillData::get_afterimage(int32_t charlevel, int32_t skilllevel) const
    {
        nl::node bracket = bracket_node(charlevel, skilllevel)["afterimage"];
        if (bracket)
            return bracket;

        return src()["afterimage"];
    }

    nl::node SkillData::get_special(int32_t charlevel, int32_t skilllevel) const
    {
        nl::node bracket = bracket_node(charlevel, skilllevel)["special"];
        if (bracket)
            return bracket;

        return src()["special"];
    }

    std::string SkillData::get_action(int32_t skilllevel, size_t seed) const
    {
        // A level may pin one action, which wins over the skill's own list.
        std::string pinned = level_node(skilllevel)["action"].get_string();
        if (!pinned.empty())
            return pinned;

        if (!actionsloaded)
        {
            actionsloaded = true;
            // The action node is a list, not a two-handed pair. Its entries are
            // alternatives the skill picks between at random.
            for (nl::node sub : src()["action"])
            {
                if (sub.data_type() == nl::node::type::string)
                    actions.push_back(sub.get_string());
            }
        }

        if (actions.empty())
            return {};

        return actions[seed % actions.size()];
    }

    nl::node SkillData::get_hit_root() const { return src()["hit"]; }
    nl::node SkillData::get_screen() const { return src()["screen"]; }
    nl::node SkillData::get_affected() const { return src()["affected"]; }
    nl::node SkillData::get_special_affected() const { return src()["specialAffected"]; }
    nl::node SkillData::get_mob() const { return src()["mob"]; }
    nl::node SkillData::get_tile() const { return src()["tile"]; }
    nl::node SkillData::get_prepare() const { return src()["prepare"]; }
    nl::node SkillData::get_keydown() const { return src()["keydown"]; }
    nl::node SkillData::get_keydown_end() const { return src()["keydownend"]; }
    nl::node SkillData::get_summon() const { return src()["summon"]; }
    nl::node SkillData::get_finish() const { return src()["finish"]; }

    bool SkillData::has_prepare() const
    {
        return src()["prepare"].size() > 0;
    }

    bool SkillData::is_keydown() const
    {
        return src()["keydown"].size() > 0;
    }

    int32_t SkillData::get_prepare_action() const
    {
        return src()["prepare"]["action"].get_integer(NO_ACTION);
    }

    uint16_t SkillData::get_prepare_time() const
    {
        return static_cast<uint16_t>(src()["prepare"]["time"].get_integer(0));
    }

    uint16_t SkillData::get_ball_delay() const
    {
        // Both projectile nodes carry the same delay; whichever is present wins.
        nl::node s = src();
        return static_cast<uint16_t>(
            s["ball"]["delay"].get_integer(s["flipBall"]["delay"].get_integer(0))
        );
    }

    int32_t SkillData::get_special_action() const
    {
        return src()["specialAction"].get_integer(NO_ACTION);
    }

    int32_t SkillData::get_delay_frame() const
    {
        return src()["specialActionFrame"]["delay"];
    }

    int32_t SkillData::get_hold_frame() const
    {
        return src()["specialActionFrame"]["hold"];
    }

    bool SkillData::is_continuous_effect() const
    {
        return src()["effectC"].get_bool();
    }

    const std::string& SkillData::get_element() const
    {
        load_info();
        return element;
    }

    bool SkillData::is_passive() const
    {
        return passive;
    }

    bool SkillData::is_attack() const
    {
        load_info();
        return attacking;
    }

    bool SkillData::is_invisible() const
    {
        load_info();
        return invisible;
    }

    bool SkillData::has_afterimage() const
    {
        return static_cast<bool>(src()["afterimage"]);
    }

    int32_t SkillData::get_masterlevel() const
    {
        load_info();
        return masterlevel;
    }

    Weapon::Type SkillData::get_required_weapon() const
    {
        load_info();
        return reqweapon;
    }

    bool SkillData::is_correct_weapon(Weapon::Type weapon) const
    {
        load_info();

        if (reqweapon == Weapon::NONE)
            return true;
        return weapon == reqweapon || (subweapon != Weapon::NONE && weapon == subweapon);
    }

    const SkillData::Stats& SkillData::get_stats(int32_t level) const
    {
        auto iter = stats.find(level);
        if (iter != stats.end())
            return iter->second;

        nl::node sub = level_node(level);
        if (!sub)
        {
            static const Stats null_stats;
            return null_stats;
        }

        Stats parsed;
        // A skill that states no damage rate deals a plain 100%, which is what
        // the server assumes for it too. Reading it as zero is what left Arrow
        // Bomb - whose level data has no damage field at all - hitting for 1.
        parsed.damage      = (float)sub["damage"].get_integer(100) / 100;
        parsed.matk        = sub["mad"];
        parsed.fixdamage   = sub["fixdamage"];
        parsed.mastery     = sub["mastery"];
        parsed.attackcount = (uint8_t)sub["attackCount"].get_integer(1);
        parsed.mobcount    = (uint8_t)sub["mobCount"].get_integer(1);
        parsed.bulletcount = (uint8_t)sub["bulletCount"].get_integer(1);
        parsed.bulletcost  = (int16_t)sub["bulletConsume"].get_integer(parsed.bulletcount);
        parsed.hpcost      = sub["hpCon"];
        parsed.mpcost      = sub["mpCon"];
        parsed.itemcost    = sub["itemCon"];
        parsed.itemcount   = (int16_t)sub["itemConNo"].get_integer(0);
        parsed.mesocost    = sub["moneyCon"];
        parsed.chance      = (float)sub["prop"].get_real(100.0) / 100;
        parsed.subchance   = (float)sub["subProp"].get_real(0.0) / 100;
        parsed.hrange      = (float)sub["range"].get_real(100.0) / 100;
        parsed.reach       = sub["range"].get_integer(0);
        parsed.x           = sub["x"];
        parsed.y           = sub["y"];
        parsed.z           = sub["z"];
        // The durations are given in seconds and used as ms everywhere else.
        parsed.duration    = sub["time"].get_integer(0) * 1000;
        parsed.subduration = sub["subTime"].get_integer(0) * 1000;
        parsed.cooldown    = sub["cooltime"].get_integer(0) * 1000;
        parsed.range       = sub;

        return stats.emplace(level, std::move(parsed)).first->second;
    }

    const std::string& SkillData::get_name() const
    {
        load_strings();
        return name;
    }

    const std::string& SkillData::get_desc() const
    {
        load_strings();
        return desc;
    }

    const std::string& SkillData::get_level_desc(int32_t level) const
    {
        load_strings();

        auto iter = leveldescs.find(level);
        if (iter == leveldescs.end())
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
        if (!iconsloaded)
        {
            iconsloaded = true;
            nl::node s = src();
            icons = { s["icon"], s["iconDisabled"], s["iconMouseOver"] };
        }
        return icons[icon];
    }
}
