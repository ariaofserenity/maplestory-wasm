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
#include "../PacketHandler.h"

namespace jrc
{
    // Spawn a character on the stage.
    // Opcode: SPAWN_CHAR(160)
    class SpawnCharHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Remove a character from the stage.
    // Opcode: REMOVE_CHAR(161)
    class RemoveCharHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Spawn a pet on the stage.
    // Opcode: SPAWN_PET(168)
    class SpawnPetHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Move a character.
    // Opcode: CHAR_MOVED(185)
    class CharMovedHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Update the look of a character.
    // Opcode: UPDATE_CHARLOOK(197)
    class UpdateCharLookHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Detailed info about a character, which opens the user info window.
    // Opcode: CHAR_INFO(61)
    class CharInfoHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Display an effect on a character.
    // Opcode: SHOW_FOREIGN_EFFECT(198)
    class ShowForeignEffectHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Spawn a mob on the stage.
    // Opcode: SPAWN_MOB(236)
    class SpawnMobHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Remove a map from the stage, either by killing it or making it invisible.
    // Opcode: KILL_MOB(237)
    class KillMobHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Spawn a mob on the stage and take control of it.
    // Opcode: SPAWN_MOB_C(238)
    class SpawnMobControllerHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Update mob state and position with the client.
    // Opcode: MOB_MOVED(239)
    class MobMovedHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Updates a mob's hp with the client.
    // Opcode: SHOW_MOB_HP(250)
    class ShowMobHpHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Damage dealt to a monster by something other than a swing - Power Guard
    // and Mana Reflection bouncing a hit back, a mob hurting itself. Carries no
    // attacker, so it only puts a number over the monster.
    // Opcode: DAMAGE_MONSTER(246)
    class DamageMonsterHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Spawn an npc on the current stage.
    // Opcode: SPAWN_NPC(257)
    class SpawnNpcHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Spawn an npc on the current stage and take control of it.
    // Opcode: SPAWN_NPC_C(259)
    class SpawnNpcControllerHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Drop a lootable item on the stage.
    // Opcode: DROP_LOOT(268)
    class DropLootHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Remove an item from the stage.
    // Opcode: REMOVE_LOOT(269)
    class RemoveLootHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Parse a ReactorSpawn and send it to the Stage spawn queue.
    // Opcode: SPAWN_REACTOR(279)
    class SpawnReactorHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Remove a reactor from the stage.
    // Opcode: REMOVE_REACTOR(280)
    class RemoveReactorHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Put a summon on the map.
    // Opcode: SPAWN_SUMMON(175)
    class SpawnSummonHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Take a summon off the map.
    // Opcode: REMOVE_SUMMON(176)
    class RemoveSummonHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Move somebody else's summon.
    // Opcode: MOVE_SUMMON(177)
    class MoveSummonHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Show somebody else's summon swinging.
    // Opcode: SUMMON_ATTACK(178)
    class SummonAttackHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Show a summon taking damage.
    // Opcode: DAMAGE_SUMMON(179)
    class DamageSummonHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };
}
