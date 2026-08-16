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
    // Play the animation for a quest that was just handed in.
    // Opcode: QUEST_CLEAR(49)
    class QuestClearHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };


    // Quest timers, the npc marker update, and the refusals the server sends
    // when it turns a quest action down.
    // Opcode: UPDATE_QUEST_INFO(211)
    class UpdateQuestInfoHandler : public PacketHandler
    {
        void handle(InPacket& recv) const override;
    };
}
