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
#include "QuestHandlers.h"

#include "../../Audio/Audio.h"
#include "../../Data/QuestData.h"
#include "../../Gameplay/Stage.h"
#include "../../IO/UI.h"
#include "../../IO/UITypes/UIQuestLog.h"
#include "../../IO/UITypes/UIStatusMessenger.h"

namespace jrc
{
    namespace
    {
        void show_status(Text::Color color, const std::string& message)
        {
            if (auto messenger = UI::get().get_element<UIStatusMessenger>())
            {
                messenger->show_status(color, message);
            }
        }
    }

    void QuestClearHandler::handle(InPacket& recv) const
    {
        int16_t questid = recv.read_short();

        Stage::get().get_player().show_effect_id(CharEffect::QUEST_CLEAR);
        Sound(Sound::QUESTCLEAR).play();

        const QuestData& quest = QuestData::get(questid);
        if (quest.is_valid())
        {
            show_status(Text::YELLOW, "Quest Complete! - " + quest.get_name());
        }
    }

    void UpdateQuestInfoHandler::handle(InPacket& recv) const
    {
        // Every branch here shares one opcode and is told apart by a leading
        // subtype byte. The timer branches lead with a count that the server
        // only ever sends as one, but it is read rather than assumed so the
        // packet stays parsed correctly if that changes.
        constexpr int8_t ADD_TIMER = 6;
        constexpr int8_t REMOVE_TIMER = 7;
        constexpr int8_t NPC_UPDATE = 8;
        constexpr int8_t QUEST_ERROR = 0x0A;
        constexpr int8_t NO_MESOS = 0x0B;
        constexpr int8_t ITEM_WORN = 0x0D;
        constexpr int8_t MISSING_ITEM = 0x0E;
        constexpr int8_t QUEST_EXPIRED = 0x0F;

        int8_t subtype = recv.read_byte();

        switch (subtype)
        {
        case ADD_TIMER:
        {
            int16_t count = recv.read_short();
            for (int16_t i = 0; i < count; ++i)
            {
                int16_t questid = recv.read_short();
                int32_t seconds = recv.read_int();
                if (auto questlog = UI::get().get_element<UIQuestLog>())
                {
                    questlog->set_timer(questid, seconds);
                }
            }
            break;
        }
        case REMOVE_TIMER:
        {
            int16_t count = recv.read_short();
            for (int16_t i = 0; i < count; ++i)
            {
                int16_t questid = recv.read_short();
                if (auto questlog = UI::get().get_element<UIQuestLog>())
                {
                    questlog->clear_timer(questid);
                }
            }
            break;
        }
        case NPC_UPDATE:
            // Tells the client which npc a started quest should now point at.
            // The quest log derives that from the game files instead, so the
            // ids are read only to consume the packet.
            recv.read_short(); // quest id
            recv.read_int();   // npc id
            break;
        case QUEST_ERROR:
        {
            int16_t questid = recv.read_short();
            const QuestData& quest = QuestData::get(questid);
            std::string name = quest.is_valid() ? quest.get_name() : std::to_string(questid);
            show_status(Text::RED, "Could not proceed with the quest: " + name);
            break;
        }
        case NO_MESOS:
            show_status(Text::RED, "You do not have enough mesos.");
            break;
        case ITEM_WORN:
            show_status(Text::RED, "You cannot hand in an item you are wearing.");
            break;
        case MISSING_ITEM:
            show_status(Text::RED, "You do not have the required item.");
            break;
        case QUEST_EXPIRED:
        {
            int16_t questid = recv.read_short();
            const QuestData& quest = QuestData::get(questid);
            std::string name = quest.is_valid() ? quest.get_name() : std::to_string(questid);
            show_status(Text::RED, "Time is up for the quest: " + name);
            break;
        }
        default:
            break;
        }
    }
}
