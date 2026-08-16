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
#include "../OutPacket.h"

#include "../../Gameplay/Combat/Attack.h"
#include "../../Gameplay/Combat/SpecialMove.h"

namespace jrc
{
    // Notifies the server of an attack. The opcode is determined by the attack type.
    // Attack::CLOSE = CLOSE_ATTACK(44)
    // Attack::RANGED = RANGED_ATTACK(45)
    // Attack::MAGIC = MAGIC_ATTACK(46)
    class AttackPacket : public OutPacket
    {
    public:
        AttackPacket(const AttackResult& attack)
            : OutPacket(opcodefor(attack.type)) {

            skip(1);

            write_byte((attack.mobcount << 4) | attack.hitcount);
            write_int(attack.skill);

            // A charging skill reports how long its key was held. Which of the
            // packet's two slots that number goes in is per skill; see
            // keydown_reported_late.
            if (attack.keydown && !keydown_reported_late(attack.skill))
                write_int(attack.charge);

            skip(8);

            write_byte(attack.display);
            write_byte(attack.toleft);
            write_byte(attack.stance);

            skip(1);

            write_byte(attack.speed);

            if (attack.type == Attack::RANGED)
            {
                skip(1);
                write_byte(attack.toleft);
                skip(7);
                if (attack.keydown && keydown_reported_late(attack.skill))
                    write_int(attack.charge);
            }
            else
            {
                skip(4);
            }

            for (auto& damagetomob : attack.damagelines)
            {
                write_int(damagetomob.first);

                skip(14);

                for (auto& singledamage : damagetomob.second)
                {
                    write_int(singledamage.first);
                    // add critical here
                }

                if (attack.skill != 5221004)
                    skip(4);
            }
        }

    private:
        static OutPacket::Opcode opcodefor(Attack::Type type)
        {
            switch (type)
            {
            case Attack::CLOSE:
                return CLOSE_ATTACK;
            case Attack::RANGED:
                return RANGED_ATTACK;
            default:
                return MAGIC_ATTACK;
            }
        }
    };


    // Tells the server that the player took damage.
    // Opcode: TAKE_DAMAGE(48)
    class TakeDamagePacket : public OutPacket
    {
    public:
        enum From : int8_t
        {
            TOUCH = -1
        };

        TakeDamagePacket(int8_t from, uint8_t element, int32_t damage,
            int32_t mobid, int32_t oid, uint8_t direction) : OutPacket(TAKE_DAMAGE) {

            write_time();
            write_byte(from);
            write_byte(element);
            write_int(damage);
            write_int(mobid);
            write_int(oid);
            write_byte(direction);
        }

        // From mob attack result.
        TakeDamagePacket(const MobAttackResult& result, From from)
            : TakeDamagePacket(from, 0, result.damage, result.mobid, result.oid, result.direction) {}
    };


    // Packet which notifies the server of a skill usage.
    // Opcode: USE_SKILL(91)
    class UseSkillPacket : public OutPacket
    {
    public:
        UseSkillPacket(int32_t skillid, int32_t level) : OutPacket(USE_SKILL)
        {
            write_header(skillid, level);
        }

        // A skill that puts something on the ground says where. The server
        // decides whether a position was sent by counting the bytes left over
        // once it has read the header, and only accepts exactly five - the
        // point plus one trailing byte it never reads. Sending the point on its
        // own leaves four and is ignored, which is why a summon cast this way
        // produced no summon at all.
        UseSkillPacket(int32_t skillid, int32_t level, Point<int16_t> position)
            : OutPacket(USE_SKILL) {

            write_header(skillid, level);
            write_short(position.x());
            write_short(position.y());
            skip(1);
        }

    private:
        void write_header(int32_t skillid, int32_t level)
        {
            write_time();
            write_int(skillid);
            write_byte(static_cast<uint8_t>(level));

            // if monster magnet : some more bytes

            if (skillid % 10000000 == 1004)
                skip(2); // no idea what this could be
        }
    };


    // Tells the server a charging skill has started, so it can show the
    // charging animation to everyone else in the map.
    // Opcode: SKILL_EFFECT(93)
    class SkillEffectPacket : public OutPacket
    {
    public:
        SkillEffectPacket(int32_t skillid, int32_t level, uint8_t speed)
            : OutPacket(SKILL_EFFECT) {

            write_int(skillid);
            write_byte(static_cast<uint8_t>(level));
            // Direction and inventory flags, neither of which the server reads
            // for anything but the echo it sends back out.
            write_byte(0);
            write_byte(speed);
            write_byte(0);
        }
    };
}
