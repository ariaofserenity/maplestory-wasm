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
#include "SpecialMove.h"

#include "SkillSound.h"

#include <memory>

namespace jrc
{
    // The skill implementation of special move.
    //
    // All animation choices are delegated to SkillData, which resolves them the
    // way the reference client does. This class only decides when to ask.
    class Skill : public SpecialMove
    {
    public:
        Skill(int32_t skillid);

        void apply_useeffects(Char& user) const override;
        void apply_actions(Char& user, Attack::Type type) const override;
        void apply_stats(const Char& user, Attack& attack) const override;
        void apply_hiteffects(const AttackUser& user, Mob& target) const override;
        Animation get_bullet(const Char& user, int32_t bulletid) const override;
        AreaEffect get_area_effect(const Char& user) const override;

        CastKind get_cast_kind() const override;
        uint16_t get_prepare_time() const override;
        uint16_t get_ball_delay() const override;
        nl::node get_prepare_effect() const override;
        nl::node get_keydown_effect() const override;
        nl::node get_keydown_end_effect() const override;
        nl::node get_finish_effect() const override;
        int32_t get_cooldown(int32_t level) const override;

        bool is_attack() const override;
        bool is_skill() const override;
        bool is_passive() const override;
        bool needs_position() const override;
        int32_t get_id() const override;

        ForbidReason can_use(int32_t level, Weapon::Type weapon,
            const Job& job, uint16_t hp, uint16_t mp, uint16_t bullets) const override;

    private:
        // Whether the skill has a projectile of its own. A skill without one
        // throws whatever the weapon is loaded with.
        bool has_projectile(const Char& user) const;

        std::unique_ptr<SkillSound> sound;

        int32_t skillid;
    };
}
