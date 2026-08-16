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
#include "../../Template/Rectangle.h"

#include <cstdint>
#include <utility>
#include <vector>


namespace jrc
{
    struct Attack
    {
        enum Type
        {
            CLOSE,
            RANGED,
            MAGIC
        };

        enum DamageType
        {
            DMG_WEAPON,
            DMG_MAGIC,
            DMG_FIXED
        };

        Type type             = CLOSE;
        DamageType damagetype = DMG_WEAPON;

        double mindamage    = 1.0;
        double maxdamage    = 1.0;
        float critical      = 0.0f;
        // What a critical hit is worth, as a multiple of a normal one.
        float critdamage    = 1.0f;
        float ignoredef     = 0.0f;
        int32_t matk        = 0;
        int32_t accuracy    = 0;
        int32_t fixdamage   = 0;
        int16_t playerlevel = 1;

        uint8_t hitcount = 0;
        uint8_t mobcount = 0;
        uint8_t speed    = 0;
        uint8_t stance   = 0;
        int32_t skill    = 0;
        int32_t bullet   = 0;

        Point<int16_t> origin;
        Rectangle<int16_t> range;
        float hrange = 1.0f;
        // How far forward a ranged or magic attack reaches, in pixels. The
        // rect only describes the band it sweeps; for anything fired rather
        // than swung the reference client takes the distance from the level's
        // scalar range instead, falling back to a per-weapon default. Zero
        // means the rect alone decides, which is the case for close attacks.
        int16_t reach = 0;
        // A fired attack does not sweep the skill's rect at all. The reference
        // client lays a one-pixel-tall line from the muzzle out to the reach
        // and asks which monsters cross it, which still catches anything whose
        // body spans that height. A handful of skills widen the line, and a
        // handful replace it with the level's own rect; those clear this.
        bool linear = false;
        // A fired attack normally searches a wedge spreading out from the
        // muzzle and hits the single nearest monster inside it. This makes it
        // sweep the line described above instead.
        bool rectsearch = false;
        // Whether, having found that one monster, it spreads to whatever else
        // stands inside the level's rect around it.
        bool splash = false;
        // How far above and below the line a widened one reaches.
        int16_t vadjust = 0;
        bool toleft  = false;

        // How high above the caster's feet a shot leaves.
        static constexpr int16_t MUZZLE_HEIGHT = 28;
    };


    struct MobAttack
    {
        Attack::Type type = Attack::CLOSE;
        int32_t watk      = 0;
        int32_t matk      = 0;
        int32_t mobid     = 0;
        int32_t oid       = 0;
        Point<int16_t> origin;
        bool valid = false;

        // Create a mob attack for touch damage.
        MobAttack(int32_t watk,
                  Point<int16_t> origin,
                  int32_t mobid,
                  int32_t oid)
            : type(Attack::CLOSE), watk(watk), mobid(mobid), oid(oid),
              origin(origin),      valid(true)
            {}

        MobAttack()
            : valid(false) {}

        explicit operator bool() const
        {
            return valid;
        }
    };


    struct MobAttackResult
    {
        int32_t damage;
        int32_t mobid;
        int32_t oid;
        uint8_t direction;

        MobAttackResult(const MobAttack& attack,
                        int32_t damage,
                        uint8_t direction)
            : damage(damage), mobid(attack.mobid), oid(attack.oid),
              direction(direction)
            {}
    };


    struct AttackResult
    {
        AttackResult(const Attack& attack)
            : type(attack.type),     hitcount(attack.hitcount),
              skill(attack.skill),   bullet(attack.bullet),
              stance(attack.stance), speed(attack.speed),
              reach(attack.reach), toleft(attack.toleft)
            {}

        AttackResult() = default;

        Attack::Type type;
        int32_t attacker = 0;
        uint8_t mobcount = 0;
        uint8_t hitcount = 1;
        int32_t skill    = 0;
        // Whether the skill charges while its key is held, and for how long it
        // had been held when this volley went off. The server expects the
        // number from every charging skill, so the flag has to travel with the
        // result rather than being worked out again when the packet is built -
        // an attack arriving over the wire has no skill data to consult.
        bool keydown     = false;
        int32_t charge   = 0;
        int32_t bullet   = 0;
        uint8_t level    = 0;
        uint8_t display  = 0;
        uint8_t stance   = 0;
        uint8_t speed    = 0;
        // How far the shot flies. Only the caster's own attacks carry it; one
        // arriving over the wire leaves it zero and the reach is worked out
        // from the character instead.
        int16_t reach    = 0;
        bool toleft      = false;
        // Damage lines per target, in the order the targets were selected
        // (nearest first). Ordering is load-bearing: multi-target skills
        // stagger their hits outward from the attacker, so an unordered
        // container would randomise the visual sweep.
        std::vector<std::pair<int32_t, std::vector<std::pair<int32_t, bool>>>> damagelines;
        int32_t first_oid = 0;
        int32_t last_oid  = 0;
    };

    struct AttackUser
    {
        int32_t skilllevel;
        uint16_t level;
        bool secondweapon;
        bool flip;
    };
}
