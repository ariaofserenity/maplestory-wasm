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
#include "../Character/Inventory/Weapon.h"
#include "../Graphics/Texture.h"
#include "../Template/Rectangle.h"
#include "../Template/Cache.h"

#include "nlnx/node.hpp"

#include <string>
#include <array>
#include <cstddef>
#include <unordered_map>
#include <utility>
#include <vector>

namespace jrc
{
    // Everything the client knows about one skill.
    //
    // The layout mirrors the reference client's SKILLENTRY: a set of animation
    // nodes on the skill itself, a table of per-skill-level data, and a table of
    // character-level brackets that override some of those nodes. Which of the
    // three a given animation comes from depends on the animation, and the rules
    // are not symmetric - see get_effect / get_hit / get_ball below, and
    // docs/ms-skill-model.md for where they were read out of the client.
    //
    // Nothing here infers meaning from the shape of a node. A node either exists
    // under its documented name or the skill simply has no such animation.
    //
    // Nothing is read from the game files until it is asked for. The client runs
    // on a lazily fetched filesystem where touching a node can block on a
    // download, and skill data is first constructed from inside a packet
    // handler; reading a whole skill up front stalls the connection long enough
    // for the server to drop it as idle. Everything below is therefore resolved
    // on demand and memoised.
    class SkillData : public Cache<SkillData>
    {
    public:
        // The data of one skill level.
        struct Stats
        {
            float damage = 0.0f;
            int32_t matk = 0;
            int32_t fixdamage = 0;
            int32_t mastery = 0;
            uint8_t attackcount = 0;
            uint8_t mobcount = 0;
            uint8_t bulletcount = 0;
            int16_t bulletcost = 0;
            int32_t hpcost = 0;
            int32_t mpcost = 0;
            int32_t itemcost = 0;
            int16_t itemcount = 0;
            int32_t mesocost = 0;
            float chance = 0.0f;
            float subchance = 0.0f;
            float critical = 0.0f;
            float ignoredef = 0.0f;
            float hrange = 0.0f;
            // The level's scalar range, in pixels. For a fired attack this is
            // how far forward it reaches; zero means the skill states none.
            int32_t reach = 0;
            // Generic tuning values; their meaning is per skill.
            int32_t x = 0;
            int32_t y = 0;
            int32_t z = 0;
            // Buff duration and cooldown, both in ms.
            int32_t duration = 0;
            int32_t subduration = 0;
            int32_t cooldown = 0;
            // The box the skill affects, relative to the caster.
            Rectangle<int16_t> range;
        };

        // Icon types
        enum Icon
        {
            NORMAL,
            DISABLED,
            MOUSEOVER,
            NUM_ICONS
        };

        // Return wether the skill is passive. Derived from the id alone, so this
        // never touches the game files.
        bool is_passive() const;
        // Return wether the skill is an attack skill.
        bool is_attack() const;
        // Return wether this skill is invisible in the skill book ui.
        bool is_invisible() const;
        // Whether the skill brings a weapon afterimage of its own, which is
        // what a swing leaves behind and a shot does not.
        bool has_afterimage() const;
        // Return the default masterlevel.
        int32_t get_masterlevel() const;
        // Return the required weapon.
        Weapon::Type get_required_weapon() const;
        // Return wether the skill can be used with this weapon. Unlike
        // get_required_weapon this also honours the secondary weapon field, so
        // skills usable with either of two weapon types answer correctly.
        bool is_correct_weapon(Weapon::Type weapon) const;
        // Return the data of one level.
        // If there is no data for that level, a default object is returned.
        const Stats& get_stats(int32_t level) const;
        // The skills that must already be trained before this one accepts
        // points, each mapped to the level it has to reach. Empty for a skill
        // that stands on its own.
        const std::unordered_map<int32_t, int32_t>& get_required_skills() const;

        // Return the name of the skill.
        const std::string& get_name() const;
        // Return the description of the skill.
        const std::string& get_desc() const;
        // Return the description of a level.
        // If there is no description for this level, a warning message is returned.
        const std::string& get_level_desc(int32_t level) const;

        // Return one of the skill icons.
        // Cannot fail if type is a valid enum.
        const Texture& get_icon(Icon icon) const;

        // ---- animation resolution -------------------------------------------
        //
        // The level arguments are the character's level and the level the
        // player has in this skill. Both are taken even where only one is used,
        // because which one indexes the bracket table depends on the skill.

        // Played on the caster. Bracket first, then the skill's own node.
        nl::node get_effect(int32_t charlevel, int32_t skilllevel) const;
        // Played on each mob that was struck. Skill level first, then the
        // bracket, then the skill's own nodes - the opposite order to the
        // effect, which is how the reference client resolves it.
        nl::node get_hit(int32_t charlevel, int32_t skilllevel, size_t index) const;
        // How many distinct hit animations the above can return.
        size_t count_hits(int32_t charlevel, int32_t skilllevel) const;
        // A hit animation chosen at random, which is what the client does for
        // every skill outside a small hardcoded list.
        nl::node get_random_hit(int32_t charlevel, int32_t skilllevel) const;
        // The projectile. Bracket first, then skill level, then the skill's own
        // node; the flipped variant is used only when it exists.
        nl::node get_ball(int32_t charlevel, int32_t skilllevel, bool flip) const;
        nl::node get_afterimage(int32_t charlevel, int32_t skilllevel) const;
        // Played over the area the skill affects.
        nl::node get_special(int32_t charlevel, int32_t skilllevel) const;

        // The action to play. A level may pin one; otherwise the skill's action
        // list is sampled with the given seed. An empty name means the skill has
        // no action of its own and the weapon's regular attack should be used.
        std::string get_action(int32_t skilllevel, size_t seed) const;

        // ---- plain animation nodes ------------------------------------------

        nl::node get_hit_root() const;
        nl::node get_screen() const;
        nl::node get_affected() const;
        nl::node get_special_affected() const;
        nl::node get_mob() const;
        nl::node get_tile() const;
        nl::node get_prepare() const;
        nl::node get_keydown() const;
        nl::node get_keydown_end() const;
        nl::node get_summon() const;
        nl::node get_finish() const;

        // ---- cast behaviour --------------------------------------------------

        // A skill with a prepare node is cast in two stages: the prepare
        // animation runs first and the skill only goes off afterwards.
        bool has_prepare() const;
        // A keydown skill charges for as long as its key is held.
        bool is_keydown() const;
        int32_t get_prepare_action() const;
        // How long the prepare stage runs, in ms.
        uint16_t get_prepare_time() const;
        // Delay between the cast and the projectile appearing, in ms.
        uint16_t get_ball_delay() const;
        int32_t get_special_action() const;
        // Frames the special action holds for.
        int32_t get_delay_frame() const;
        int32_t get_hold_frame() const;
        // Whether the caster's effect keeps playing rather than running once.
        bool is_continuous_effect() const;
        // The element the skill deals damage in, empty for physical skills.
        const std::string& get_element() const;

        // The follow-up attack this skill can set off when swung with the given
        // weapon, or zero when it can set off none. A skill names the final
        // attacks it chains into and the weapons each one applies to, which is
        // why a bow's Final Attack never fires off a crossbow skill.
        int32_t get_final_attack(Weapon::Type weapon) const;

    private:
        // Allow the cache to use the constructor.
        friend Cache<SkillData>;
        // Note the skill's id. No game file is opened here.
        SkillData(int32_t id);

        // The skill's node in Skill.nx, resolved on first use.
        nl::node src() const;
        // The node holding one skill level's data.
        nl::node level_node(int32_t level) const;
        // The character-level bracket covering these levels, or an empty node.
        nl::node bracket_node(int32_t charlevel, int32_t skilllevel) const;
        // The node whose numbered children are the hit animations that apply.
        nl::node hit_parent(int32_t charlevel, int32_t skilllevel) const;
        // Pick between a node's ball and flipBall children.
        static nl::node ball_of(nl::node owner, bool flip);
        // How many numbered children a node has, counted from zero.
        static size_t count_indexed(nl::node owner);

        // Read the handful of top-level fields the whole class needs.
        void load_info() const;
        void load_strings() const;

        int32_t skillid;
        bool passive;

        mutable nl::node root;
        mutable bool rootloaded;

        // Top-level fields, loaded together on first use.
        mutable std::string element;
        mutable Weapon::Type reqweapon;
        mutable Weapon::Type subweapon;
        mutable int32_t masterlevel;
        mutable bool attacking;
        mutable bool invisible;
        mutable bool bracketbyskilllevel;
        mutable bool infoloaded;

        mutable std::string name;
        mutable std::string desc;
        mutable std::unordered_map<int32_t, std::string> leveldescs;
        mutable bool stringsloaded;

        // Reference stability matters here: get_stats hands out a reference and
        // an unordered_map keeps its elements put across later insertions.
        mutable std::unordered_map<int32_t, Stats> stats;
        // A bracket's base level paired with the node name it came from.
        using Bracket = std::pair<int32_t, std::string>;
        // The bracket table, in ascending order of base level.
        mutable std::vector<Bracket> brackets;
        mutable bool bracketsloaded;
        mutable std::vector<std::string> actions;
        mutable bool actionsloaded;

        // Required skill id paired with the level it must reach.
        mutable std::unordered_map<int32_t, int32_t> reqskills;
        mutable bool reqskillsloaded;

        // One follow-up attack and the weapons it applies to.
        struct FinalAttack
        {
            int32_t skillid;
            std::vector<Weapon::Type> weapons;
        };
        mutable std::vector<FinalAttack> finalattacks;
        mutable bool finalattacksloaded;

        mutable std::array<Texture, NUM_ICONS> icons;
        mutable bool iconsloaded;
    };
}
