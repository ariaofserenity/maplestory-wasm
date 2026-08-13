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
#include <vector>

namespace jrc
{
    namespace Buffstat
    {
        enum Id
        {
            NONE,

            MORPH,
            RECOVERY,
            MAPLE_WARRIOR,
            STANCE,
            SHARP_EYES,
            MANA_REFLECTION,
            SHADOW_CLAW,
            INFINITY_,
            HOLY_SHIELD,
            HAMSTRING,
            BLIND,
            CONCENTRATE,
            ECHO_OF_HERO,
            GHOST_MORPH,
            AURA,
            CONFUSE,
            BERSERK_FURY,
            DIVINE_BODY,
            SPARK,
            FINALATTACK,
            BATTLESHIP,
            WATK,
            WDEF,
            MATK,
            MDEF,
            ACC,
            AVOID,
            HANDS,
            SHOWDASH,
            SPEED,
            JUMP,
            MAGIC_GUARD,
            DARKSIGHT,
            BOOSTER,
            POWERGUARD,
            HYPERBODYHP,
            HYPERBODYMP,
            INVINCIBLE,
            SOULARROW,
            STUN,
            POISON,
            SEAL,
            DARKNESS,
            COMBO,
            SUMMON,
            WK_CHARGE,
            DRAGONBLOOD,
            HOLY_SYMBOL,
            MESOUP,
            SHADOWPARTNER,
            PICKPOCKET,
            PUPPET,
            MESOGUARD,
            WEAKEN,

            DASH,
            DASH2,
            ELEMENTAL_RESET,
            ARAN_COMBO,
            COMBO_DRAIN,
            COMBO_BARRIER,
            BODY_PRESSURE,
            SMART_KNOCKBACK,
            PYRAMID_PQ,
            ENERGY_CHARGE,
            MONSTER_RIDING,
            HOMING_BEACON,
            SPEED_INFUSION,

            LENGTH
        };

        // One buff stat as it appears on the wire. The server announces which
        // stats a packet carries with two bitmasks and then writes their values
        // back to back, so a stat is identified by which of the two masks it
        // belongs to and the bit it sets there.
        struct Code
        {
            Id stat;
            uint64_t mask;
            // Whether the bit belongs to the first of the two masks.
            bool first;
        };

        // Every stat the client understands, in the order the server writes
        // their values. This ordering is load-bearing: the values are an
        // unlabelled run of fixed-size records, so reading them in any other
        // order hands each one to the wrong stat. Concentrate is the case that
        // exposes it - it arrives as a weapon attack bonus followed by its own
        // mana-saving value, and swapping the two turns +26 attack into +50.
        extern const std::vector<Code> codes;
    }

    struct Buff
    {
        Buffstat::Id stat;
        int16_t value;
        int32_t skillid;
        int32_t duration;

        constexpr Buff(Buffstat::Id stat, int16_t value, int32_t skillid, int32_t duration)
            : stat(stat), value(value), skillid(skillid), duration(duration) {}

        constexpr Buff()
            : Buff(Buffstat::NONE, 0, 0, 0) {}
    };
}
