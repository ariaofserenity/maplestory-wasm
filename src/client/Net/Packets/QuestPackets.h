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

#include "../../Template/Point.h"

namespace jrc
{
    // Packet which tells the server what the player did with a quest.
    // Opcode: QUEST_ACTION(107)
    //
    // Quest dialogue lives in the game files rather than on the server, so for
    // most quests the client plays the conversation itself and this packet is
    // the only thing the server hears about it. The server re-checks every
    // requirement before acting, so a client that asks for something it should
    // not get is simply ignored.
    class QuestActionPacket : public OutPacket
    {
    public:
        // Requests starting a quest at an npc. The position is sent so the
        // server can confirm the player is actually standing at that npc.
        static QuestActionPacket start(int16_t questid, int32_t npcid, Point<int16_t> position)
        {
            return at_npc(START, questid, npcid, position);
        }

        // Requests handing a quest in at an npc.
        static QuestActionPacket complete(int16_t questid, int32_t npcid, Point<int16_t> position)
        {
            return at_npc(COMPLETE, questid, npcid, position);
        }

        // Requests handing a quest in where the reward is one of several items
        // the player picks between.
        static QuestActionPacket complete(int16_t questid, int32_t npcid, Point<int16_t> position,
            int16_t selection)
        {
            QuestActionPacket packet = at_npc(COMPLETE, questid, npcid, position);
            packet.write_short(selection);
            return packet;
        }

        // Requests giving a quest up.
        static QuestActionPacket forfeit(int16_t questid)
        {
            QuestActionPacket packet(FORFEIT, questid);
            return packet;
        }

        // Requests that the server play a quest's start dialogue. Used for the
        // quests whose conversation is a server script instead of game data.
        static QuestActionPacket scripted_start(int16_t questid, int32_t npcid, Point<int16_t> position)
        {
            return at_npc(SCRIPTED_START, questid, npcid, position);
        }

        // Requests that the server play a quest's hand-in dialogue.
        static QuestActionPacket scripted_complete(int16_t questid, int32_t npcid, Point<int16_t> position)
        {
            return at_npc(SCRIPTED_COMPLETE, questid, npcid, position);
        }

        // Requests a replacement for a quest item the player lost.
        static QuestActionPacket restore_item(int16_t questid, int32_t itemid)
        {
            QuestActionPacket packet(RESTORE_ITEM, questid);
            packet.write_int(1); // number of items to restore
            packet.write_int(itemid);
            return packet;
        }

    private:
        enum Mode : int8_t
        {
            RESTORE_ITEM = 0,
            START = 1,
            COMPLETE = 2,
            FORFEIT = 3,
            SCRIPTED_START = 4,
            SCRIPTED_COMPLETE = 5
        };

        QuestActionPacket(Mode mode, int16_t questid) : OutPacket(QUEST_ACTION)
        {
            write_byte(mode);
            write_short(questid);
        }

        static QuestActionPacket at_npc(Mode mode, int16_t questid, int32_t npcid,
            Point<int16_t> position)
        {
            QuestActionPacket packet(mode, questid);
            packet.write_int(npcid);
            packet.write_short(position.x());
            packet.write_short(position.y());
            return packet;
        }
    };
}
