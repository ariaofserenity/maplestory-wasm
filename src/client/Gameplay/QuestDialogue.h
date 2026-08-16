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
#include "../Data/QuestData.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace jrc
{
    // Plays the conversation for one quest and reports the outcome.
    //
    // Quest dialogue is game data, not server script: the server sends nothing
    // when the player clicks a quest npc and waits to be told what the player
    // chose. So the client is what decides a quest is on offer, what the npc
    // says about it, and whether a hand-in is worth asking for. Everything
    // decided here is re-checked by the server, which is the only thing that
    // actually starts or completes a quest.
    //
    // The instance lives for as long as the conversation does. Its pages are
    // shown through the npc dialogue window, whose buttons hold a reference
    // back to it, so it stays alive until the window drops those callbacks.
    class QuestDialogue : public std::enable_shared_from_this<QuestDialogue>
    {
    public:
        // Offer whatever this npc has to offer for quests. The object id is
        // what the server knows the npc by, and is needed to hand the
        // conversation back to it.
        //
        // Returns whether the npc turned out to be a quest npc for something
        // the player can act on. When it returns false the caller should fall
        // back to the ordinary server-driven conversation, because the npc
        // very likely has one.
        static bool offer(int32_t npcid, int32_t npcoid);

        // Recompute the balloon over every npc on the map.
        //
        // Which balloon an npc floats follows from the quest states the client
        // already knows, so nothing asks the server for it. Worth calling
        // whenever a quest record changes or a new map is entered.
        static void refresh_markers();

        QuestDialogue(int16_t questid, int32_t npcid, int32_t npcoid, QuestData::Stage stage);

    private:
        // Put the conversation on screen at its first page.
        void begin();
        // Show one page of the quest's dialogue.
        void show_page(size_t index);
        // Show one of the follow-up pages after the player answered.
        void show_answer_page(size_t index, bool accepted);
        // Show the line the npc says when the hand-in cannot go through, or a
        // generic one when the quest states none for that reason.
        void show_stop(QuestData::StopReason reason);
        // Tell the server the player accepted, then play whatever the quest
        // says afterwards.
        void accept();
        // Play the decline lines, if the quest has any.
        void decline();
        // One stage of one quest this npc can act on right now.
        struct Offer
        {
            int16_t questid;
            QuestData::Stage stage;
        };
        // Everything the npc can act on, hand-ins first. Shared by the balloon
        // and by the menu, so both agree on what the npc has to say.
        static std::vector<Offer> offers_at(int32_t npcid);
        // Put up a menu of the given offers and act on whichever is picked.
        static void show_menu(const std::vector<Offer>& offers, int32_t npcid, int32_t npcoid);
        // Hand the conversation over to the npc's own dialogue. A quest npc is
        // very often a shop or a script npc as well, so turning a quest down
        // must not be a way to lock the player out of that.
        void fall_through() const;

        const QuestData& data() const;
        const QuestData::Dialogue& dialogue() const;

        int16_t questid;
        int32_t npcid;
        int32_t npcoid;
        QuestData::Stage stage;
    };
}
