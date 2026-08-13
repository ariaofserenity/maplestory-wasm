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
#include "Buff.h"

namespace jrc
{
    namespace Buffstat
    {
        // The order below is the order the server appends a buff's stats, not
        // the numeric order of their bits: the barrier stat first, then the
        // block of plain equip bonuses every buff shares, then whatever the
        // individual skill contributes, and the morphs last.
        //
        // A mask appears once. Several stats share a bit - a summon and a combo
        // count are indistinguishable on the wire, as are a puppet and the
        // thief's pickpocket in the client's own tables - and reading such a
        // bit twice would consume two records where the server wrote one and
        // desynchronise everything after it.
        const std::vector<Code> codes =
        {
            { AURA, 0x40000, false },

            // Shared by every buff that raises a plain equip stat, and always
            // written before the skill's own contribution.
            { WATK, 0x100000000L, false },
            { WDEF, 0x200000000L, false },
            { MATK, 0x400000000L, false },
            { MDEF, 0x800000000L, false },
            { ACC, 0x1000000000L, false },
            { AVOID, 0x2000000000L, false },
            { HANDS, 0x4000000000L, false },
            { SPEED, 0x8000000000L, false },
            { JUMP, 0x10000000000L, false },

            // Per-skill stats. Only one or two of these ever accompany a single
            // buff, so their order relative to each other only matters for the
            // pairs - hyper body's two halves and dash's two halves.
            { RECOVERY, 0x4, false },
            { MAPLE_WARRIOR, 0x8, false },
            { STANCE, 0x10, false },
            { SHARP_EYES, 0x20, false },
            { MANA_REFLECTION, 0x40, false },
            { SHADOW_CLAW, 0x100, false },
            { INFINITY_, 0x200, false },
            { HOLY_SHIELD, 0x400, false },
            { HAMSTRING, 0x800, false },
            { BLIND, 0x1000, false },
            { CONCENTRATE, 0x2000, false },
            { PUPPET, 0x4000, false },
            { ECHO_OF_HERO, 0x8000, false },
            { CONFUSE, 0x80000, false },
            { BERSERK_FURY, 0x8000000, false },
            { DIVINE_BODY, 0x10000000, false },
            { SPARK, 0x20000000L, false },
            { FINALATTACK, 0x80000000L, false },
            { MAGIC_GUARD, 0x20000000000L, false },
            { DARKSIGHT, 0x40000000000L, false },
            { BOOSTER, 0x80000000000L, false },
            { POWERGUARD, 0x100000000000L, false },
            { HYPERBODYHP, 0x200000000000L, false },
            { HYPERBODYMP, 0x400000000000L, false },
            { INVINCIBLE, 0x800000000000L, false },
            { SOULARROW, 0x1000000000000L, false },
            { STUN, 0x2000000000000L, false },
            { POISON, 0x4000000000000L, false },
            { SEAL, 0x8000000000000L, false },
            { DARKNESS, 0x10000000000000L, false },
            // Also the bit a summon sets; the two cannot be told apart.
            { COMBO, 0x20000000000000L, false },
            { WK_CHARGE, 0x40000000000000L, false },
            { DRAGONBLOOD, 0x80000000000000L, false },
            { HOLY_SYMBOL, 0x100000000000000L, false },
            { MESOUP, 0x200000000000000L, false },
            { SHADOWPARTNER, 0x400000000000000L, false },
            { PICKPOCKET, 0x800000000000000L, false },
            { MESOGUARD, 0x1000000000000000L, false },
            { WEAKEN, 0x4000000000000000L, false },

            // The first mask carries stats that only ever arrive on their own,
            // or in a pair of their own, so they can all sit at the end.
            { ELEMENTAL_RESET, 0x200000000L, true },
            { ARAN_COMBO, 0x1000000000L, true },
            { COMBO_DRAIN, 0x2000000000L, true },
            { COMBO_BARRIER, 0x4000000000L, true },
            { BODY_PRESSURE, 0x8000000000L, true },
            { SMART_KNOCKBACK, 0x10000000000L, true },
            { PYRAMID_PQ, 0x20000000000L, true },
            { ENERGY_CHARGE, 0x4000000000000L, true },
            { DASH2, 0x8000000000000L, true },
            { DASH, 0x10000000000000L, true },
            { MONSTER_RIDING, 0x20000000000000L, true },
            { HOMING_BEACON, 0x80000000000000L, true },
            { SPEED_INFUSION, 0x100000000000000L, true },

            // Written after everything the skill or item contributed.
            { MORPH, 0x2, false },
            { GHOST_MORPH, 0x20000, false }
        };
    }
}
