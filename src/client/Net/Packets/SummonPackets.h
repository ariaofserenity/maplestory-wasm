#pragma once
#include "MovementPacket.h"

#include "../../Gameplay/Combat/Attack.h"
#include "../../Template/Point.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace jrc
{
    // Tells the server where one of the player's summons has walked to. Only
    // the client that owns a summon sends this; everyone else is told by the
    // server.
    // Opcode: MOVE_SUMMON(175)
    class MoveSummonPacket : public MovementPacket
    {
    public:
        MoveSummonPacket(int32_t oid, Point<int16_t> start, const Movement& movement)
            : MovementPacket(MOVE_SUMMON) {

            write_int(oid);
            write_short(start.x());
            write_short(start.y());
            // One step per packet, the same way the player's own movement is
            // streamed.
            write_byte(1);
            writemovement(movement);
        }
    };


    // Reports a hit one of the player's summons soaked on their behalf. The
    // server subtracts it from the summon's own health and ends the buff once
    // that runs out, which is what takes the summon off everyone's screen.
    // Opcode: DAMAGE_SUMMON(177)
    class DamageSummonPacket : public OutPacket
    {
    public:
        DamageSummonPacket(int32_t oid, int32_t damage, int32_t mobid)
            : OutPacket(DAMAGE_SUMMON) {

            write_int(oid);
            // The attack index the hit came from. Walking into a summon is not
            // one of the monster's numbered attacks, so it is reported as none.
            write_byte(-1);
            write_int(damage);
            write_int(mobid);
        }
    };


    // Reports a swing one of the player's summons made and what it did.
    // Opcode: SUMMON_ATTACK(176)
    class SummonAttackPacket : public OutPacket
    {
    public:
        // One struck monster: its object id and the damage it took.
        using Target = std::pair<int32_t, int32_t>;

        SummonAttackPacket(int32_t oid, Point<int16_t> at, bool toleft,
            const std::vector<Target>& targets) : OutPacket(SUMMON_ATTACK) {

            write_int(oid);
            skip(4);
            write_byte(toleft ? 0 : 1);
            write_byte(static_cast<uint8_t>(targets.size()));

            // Where the summon was standing when it swung. The server reads
            // past this without using it, but the bytes have to be there or
            // everything after them is read at the wrong offset.
            write_short(at.x());
            write_short(at.y());
            skip(4);

            for (auto& target : targets)
            {
                write_int(target.first);
                skip(8);
                // Where the monster was and where it is going. Neither is used
                // for anything but the echo the server sends back out.
                write_short(0);
                write_short(0);
                write_short(0);
                write_short(0);
                write_short(0);
                write_int(target.second);
            }
        }
    };
}
