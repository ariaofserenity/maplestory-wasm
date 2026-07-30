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
#include "Attack.h"
#include "SkillSpecialEffect.h"

#include "../MapleMap/Mob.h"

#include "../../Character/Char.h"
#include "../../Character/Job.h"

namespace jrc
{
    // Base class for attacks and buffs.
    class SpecialMove
    {
    public:
        enum ForbidReason
        {
            FBR_NONE,
            FBR_WEAPONTYPE,
            FBR_HPCOST,
            FBR_MPCOST,
            FBR_BULLETCOST,
            FBR_COOLDOWN,
            FBR_OTHER
        };

        virtual ~SpecialMove() {}

        virtual void apply_useeffects(Char& user) const = 0;
        virtual void apply_actions(Char& user, Attack::Type type) const = 0;
        virtual void apply_stats(const Char& user, Attack& attack) const = 0;
        virtual void apply_hiteffects(const AttackUser& user, Mob& target) const = 0;
        virtual Animation get_bullet(const Char& user, int32_t bulletid) const = 0;

        // Animations shown over the area a move affects. Most moves have none.
        virtual std::vector<SpecialSpawn> get_special_spawns(const Char&) const
        {
            return {};
        }
        // Extra delay before this move's damage registers, for moves whose
        // effect lands some time after the cast.
        virtual uint16_t get_damage_delay() const
        {
            return 0;
        }
        // Whether the move replaces the equipped projectile with its own
        // area animation instead of throwing it.
        virtual bool suppresses_bullet() const
        {
            return false;
        }

        virtual bool is_attack() const = 0;
        virtual bool is_skill() const = 0;
        virtual int32_t get_id() const = 0;

        virtual ForbidReason can_use(int32_t level, Weapon::Type weapon,
            const Job& job, uint16_t hp, uint16_t mp, uint16_t bullets) const = 0;
    };
}
