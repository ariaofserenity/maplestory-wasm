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
#include "QuestDialogue.h"

#include "Stage.h"

#include "MapleMap/Npc.h"

#include "../IO/UI.h"
#include "../IO/UITypes/UINpcTalk.h"
#include "../Net/Packets/NpcInteractionPackets.h"
#include "../Net/Packets/QuestPackets.h"

#include "nlnx/nx.hpp"
#include "nlnx/node.hpp"

#include <algorithm>

namespace jrc
{
    namespace
    {
        // Which requirement of a stage the player has not met yet, expressed as
        // the line the npc should say about it. Every requirement the client
        // can see is checked, because the point is to tell the player what is
        // still missing rather than to decide anything - the server checks all
        // of this again and is the only one whose answer counts.
        bool requirements_met(
            const QuestData::Requirements& reqs,
            QuestData::StopReason& reason
        )
        {
            const Player& player = Stage::get().get_player();
            const CharStats& stats = player.get_stats();
            const Inventory& inventory = player.get_inventory();
            const Questlog& quests = player.get_quests();

            int16_t level = static_cast<int16_t>(stats.get_stat(Maplestat::LEVEL));
            if (level < reqs.lvmin || (reqs.lvmax > 0 && level > reqs.lvmax))
            {
                reason = QuestData::STOP_DEFAULT;
                return false;
            }

            if (!reqs.jobs.empty())
            {
                int32_t job = stats.get_stat(Maplestat::JOB);
                if (std::find(reqs.jobs.begin(), reqs.jobs.end(), job) == reqs.jobs.end())
                {
                    reason = QuestData::STOP_DEFAULT;
                    return false;
                }
            }

            for (const QuestData::QuestState& required : reqs.quests)
            {
                if (quests.get_state(required.id) != required.state)
                {
                    reason = QuestData::STOP_QUEST;
                    return false;
                }
            }

            for (const QuestData::Item& item : reqs.items)
            {
                if (inventory.get_total_item_count(item.id) < item.count)
                {
                    reason = QuestData::STOP_ITEM;
                    return false;
                }
            }

            if (reqs.mesos > 0 && inventory.get_meso() < reqs.mesos)
            {
                reason = QuestData::STOP_DEFAULT;
                return false;
            }

            return true;
        }

        // The mob part of a hand-in, which unlike the rest is read out of the
        // quest's own progress record rather than out of the character.
        //
        // The counters in that record run in the order the server collects the
        // quest's mobs, which is the start stage's list followed by the end
        // stage's. No quest in the game files names mobs on its start stage, so
        // the end stage's list lines up with the record from the front.
        bool mobs_defeated(int16_t questid, const QuestData::Requirements& reqs)
        {
            const Questlog& quests = Stage::get().get_player().get_quests();

            for (size_t i = 0; i < reqs.mobs.size(); ++i)
            {
                if (quests.get_mob_progress(questid, i) < reqs.mobs[i].count)
                {
                    return false;
                }
            }

            return true;
        }

        // Whether a started quest could be handed in right now. Both halves of
        // the hand-in are checked, because the balloon has to agree with what
        // the npc will actually say when clicked.
        bool can_hand_in(int16_t questid)
        {
            const QuestData::Requirements& reqs =
                QuestData::get(questid).get_requirements(QuestData::END);

            QuestData::StopReason ignored = QuestData::STOP_DEFAULT;
            return requirements_met(reqs, ignored) && mobs_defeated(questid, reqs);
        }

        // What the npc says when it is not saying anything in particular.
        // The game files keep these as d0, d1, ... beside the npc's name; the
        // first is the one the reference client opens a quest list with.
        std::string default_line(int32_t npcid)
        {
            nl::node strings = nl::nx::string["Npc.img"][std::to_string(npcid)];
            for (int32_t i = 0; ; ++i)
            {
                nl::node line = strings["d" + std::to_string(i)];
                if (!line)
                {
                    break;
                }

                if (line.data_type() == nl::node::type::string)
                {
                    return line.get_string();
                }
            }

            return {};
        }

        Point<int16_t> player_position()
        {
            return Stage::get().get_player().get_position();
        }
    }

    QuestDialogue::QuestDialogue(int16_t in_questid, int32_t in_npcid, int32_t in_npcoid,
        QuestData::Stage in_stage)
        : questid(in_questid), npcid(in_npcid), npcoid(in_npcoid), stage(in_stage) {}

    void QuestDialogue::fall_through() const
    {
        TalkToNPCPacket(npcoid).dispatch();
    }

    const QuestData& QuestDialogue::data() const
    {
        return QuestData::get(questid);
    }

    const QuestData::Dialogue& QuestDialogue::dialogue() const
    {
        return data().get_dialogue(stage);
    }

    std::vector<QuestDialogue::Offer> QuestDialogue::offers_at(int32_t npcid)
    {
        const Questlog& quests = Stage::get().get_player().get_quests();
        std::vector<Offer> found;

        // Hand-ins come before offers: a player standing at an npc holding a
        // finished quest wants to turn it in, not to be shown the next one.
        for (QuestData::Stage wanted : { QuestData::END, QuestData::START })
        {
            for (const QuestData::NpcStage& entry : QuestData::stages_at_npc(npcid))
            {
                if (entry.stage != wanted)
                {
                    continue;
                }

                const QuestData& quest = QuestData::get(entry.questid);
                if (!quest.is_valid() || quest.get_name().empty())
                {
                    continue;
                }

                const QuestData::Requirements& reqs = quest.get_requirements(entry.stage);

                if (wanted == QuestData::END)
                {
                    // Quests the server hands in by itself are never offered
                    // here; clicking the npc would ask for something it has
                    // already done.
                    if (quest.is_auto_complete() || !quests.is_started(entry.questid))
                    {
                        continue;
                    }
                }
                else
                {
                    if (quest.is_auto_start() ||
                        quests.get_state(entry.questid) != Questlog::NOT_STARTED)
                    {
                        continue;
                    }

                    // An offer the player cannot take is not shown at all,
                    // which is what keeps every npc from listing every quest
                    // in the game the moment it is clicked.
                    QuestData::StopReason ignored = QuestData::STOP_DEFAULT;
                    if (!requirements_met(reqs, ignored))
                    {
                        continue;
                    }
                }

                // A quest with neither dialogue of its own nor a script has
                // nothing for the client to play, so the npc is left to its
                // own conversation.
                if (!reqs.scripted && quest.get_dialogue(entry.stage).pages.empty())
                {
                    continue;
                }

                found.push_back({ entry.questid, entry.stage });
            }
        }

        return found;
    }

    void QuestDialogue::refresh_markers()
    {
        const Questlog& quests = Stage::get().get_player().get_quests();
        MapObjects* npcs = Stage::get().get_npcs().get_npcs();

        for (auto& entry : *npcs)
        {
            Npc* npc = static_cast<Npc*>(entry.second.get());
            if (!npc)
            {
                continue;
            }

            // The balloon follows a fixed order of precedence: a quest ready
            // to hand in outranks one that can be started, which outranks one
            // merely under way. Only quests this npc actually performs count.
            Npc::QuestMarker marker = Npc::MARKER_NONE;
            bool available = false;
            bool inprogress = false;

            for (const QuestData::NpcStage& stage : QuestData::stages_at_npc(npc->get_id()))
            {
                const QuestData& quest = QuestData::get(stage.questid);
                if (!quest.is_valid() || quest.get_name().empty())
                {
                    continue;
                }

                if (stage.stage == QuestData::END)
                {
                    if (!quests.is_started(stage.questid) || quest.is_auto_complete())
                    {
                        continue;
                    }

                    if (can_hand_in(stage.questid))
                    {
                        marker = Npc::MARKER_COMPLETE;
                        break;
                    }

                    inprogress = true;
                }
                else
                {
                    if (quest.is_auto_start() ||
                        quests.get_state(stage.questid) != Questlog::NOT_STARTED)
                    {
                        continue;
                    }

                    QuestData::StopReason ignored = QuestData::STOP_DEFAULT;
                    if (requirements_met(quest.get_requirements(QuestData::START), ignored))
                    {
                        available = true;
                    }
                }
            }

            if (marker == Npc::MARKER_NONE)
            {
                if (available)
                {
                    marker = Npc::MARKER_AVAILABLE;
                }
                else if (inprogress)
                {
                    marker = Npc::MARKER_IN_PROGRESS;
                }
            }

            npc->set_quest_marker(marker);
        }
    }

    void QuestDialogue::show_menu(const std::vector<Offer>& offers, int32_t npcid, int32_t npcoid)
    {
        // The reference client opens this window with whatever the npc says
        // when it has nothing particular to say, and hangs the list of quests
        // underneath it. Without that the npc appears to have been skipped
        // over and the menu reads as if it came from nowhere.
        std::string prompt = default_line(npcid);

        // The npc sorts what it has to offer into the same buckets its head
        // balloon uses, and heads each one with a picture rather than a
        // caption. Both the pictures and the line format below are the
        // reference client's own, out of its string pool.
        static const char* const HEADING_HAND_IN =
            "\r\n\r\n#fUI/UIWindow2.img/UtilDlgEx/list3#\r\n";
        static const char* const HEADING_AVAILABLE =
            "\r\n\r\n#fUI/UIWindow2.img/UtilDlgEx/list1#\r\n";
        static const char* const HEADING_IN_PROGRESS =
            "\r\n\r\n#fUI/UIWindow2.img/UtilDlgEx/list0#\r\n";

        std::vector<size_t> handin;
        std::vector<size_t> available;
        std::vector<size_t> inprogress;

        for (size_t i = 0; i < offers.size(); ++i)
        {
            if (offers[i].stage != QuestData::END)
            {
                available.push_back(i);
            }
            else if (can_hand_in(offers[i].questid))
            {
                handin.push_back(i);
            }
            else
            {
                inprogress.push_back(i);
            }
        }

        // The index in the link is the offer's own, so which entry was picked
        // still reads straight out of the list however it was grouped.
        auto append = [&](const char* heading, const std::vector<size_t>& group)
        {
            if (group.empty())
            {
                return;
            }

            prompt += heading;
            for (size_t i : group)
            {
                prompt += "#d#L" + std::to_string(i) + "# ";
                prompt += QuestData::get(offers[i].questid).get_name();
                prompt += "#l#k\r\n";
            }
        };

        // Hand-ins first, the way the client lists them.
        append(HEADING_HAND_IN, handin);
        append(HEADING_AVAILABLE, available);
        append(HEADING_IN_PROGRESS, inprogress);

        UI::get().emplace<UINpcTalk>();
        UI::get().enable();

        auto npctalk = UI::get().get_element<UINpcTalk>();
        if (!npctalk)
        {
            return;
        }

        std::vector<Offer> picked = offers;

        UINpcTalk::LocalCallbacks callbacks;
        callbacks.select = [picked, npcid, npcoid](int32_t choice)
        {
            if (choice < 0 || static_cast<size_t>(choice) >= picked.size())
            {
                return;
            }

            auto conversation = std::make_shared<QuestDialogue>(
                picked[choice].questid, npcid, npcoid, picked[choice].stage
            );
            conversation->begin();
        };
        // End Chat ends the conversation and nothing more. Handing the npc
        // back to the server here is what made some of them start talking
        // again the moment the window was closed.
        callbacks.dismiss = nullptr;

        npctalk->show_local(
            npcid,
            prompt,
            UINpcTalk::LocalPrompt::TEXT,
            false,
            std::move(callbacks)
        );
    }

    bool QuestDialogue::offer(int32_t npcid, int32_t npcoid)
    {
        std::vector<Offer> found = offers_at(npcid);
        if (found.empty())
        {
            return false;
        }

        if (found.size() > 1)
        {
            show_menu(found, npcid, npcoid);
            return true;
        }

        auto conversation = std::make_shared<QuestDialogue>(
            found.front().questid, npcid, npcoid, found.front().stage
        );
        conversation->begin();
        return true;
    }

    void QuestDialogue::begin()
    {
        const QuestData::Requirements& reqs = data().get_requirements(stage);

        if (reqs.scripted)
        {
            // The conversation for this one is a server script. Asking for it
            // is all the client does; the reply arrives as ordinary npc
            // dialogue packets.
            if (stage == QuestData::START)
            {
                QuestActionPacket::scripted_start(questid, npcid, player_position()).dispatch();
            }
            else
            {
                QuestActionPacket::scripted_complete(questid, npcid, player_position()).dispatch();
            }
            return;
        }

        if (stage == QuestData::END)
        {
            QuestData::StopReason reason = QuestData::STOP_DEFAULT;
            if (!requirements_met(reqs, reason))
            {
                show_stop(reason);
                return;
            }

            if (!mobs_defeated(questid, reqs))
            {
                show_stop(QuestData::STOP_MOB);
                return;
            }
        }

        UI::get().emplace<UINpcTalk>();
        UI::get().enable();
        show_page(0);
    }

    void QuestDialogue::show_page(size_t index)
    {
        auto npctalk = UI::get().get_element<UINpcTalk>();
        if (!npctalk)
        {
            return;
        }

        const std::vector<std::string>& pages = dialogue().pages;
        if (index >= pages.size())
        {
            return;
        }

        bool last = index + 1 == pages.size();
        // Starting a quest is always the player's choice. Handing one in only
        // asks when the quest itself poses a question on its last page.
        bool asks = last && (stage == QuestData::START || dialogue().asks);

        auto self = shared_from_this();

        UINpcTalk::LocalCallbacks callbacks;
        // As above: dismissing a page closes it rather than passing the npc
        // on to its own script.
        callbacks.dismiss = nullptr;

        if (asks)
        {
            callbacks.accept = [self]() { self->accept(); };
            callbacks.decline = [self]() { self->decline(); };
        }
        else if (last)
        {
            // The last page of a hand-in that asks nothing is the hand-in.
            callbacks.advance = [self]() { self->accept(); };
        }
        else
        {
            callbacks.advance = [self, index]() { self->show_page(index + 1); };
        }

        npctalk->show_local(
            npcid,
            pages[index],
            asks ? UINpcTalk::LocalPrompt::YES_NO : UINpcTalk::LocalPrompt::TEXT,
            !last,
            std::move(callbacks)
        );
    }

    void QuestDialogue::show_answer_page(size_t index, bool accepted)
    {
        auto npctalk = UI::get().get_element<UINpcTalk>();
        if (!npctalk)
        {
            return;
        }

        const std::vector<std::string>& pages =
            accepted ? dialogue().accepted : dialogue().declined;
        if (index >= pages.size())
        {
            return;
        }

        bool last = index + 1 == pages.size();
        auto self = shared_from_this();

        UINpcTalk::LocalCallbacks callbacks;
        if (!last)
        {
            callbacks.advance = [self, index, accepted]()
            {
                self->show_answer_page(index + 1, accepted);
            };
        }
        else if (!accepted)
        {
            // Nothing was asked of the server, so the npc's own conversation
            // is still worth having.
            callbacks.advance = [self]() { self->fall_through(); };
            callbacks.dismiss = nullptr;
        }

        npctalk->show_local(
            npcid,
            pages[index],
            UINpcTalk::LocalPrompt::TEXT,
            !last,
            std::move(callbacks)
        );
    }

    void QuestDialogue::show_stop(QuestData::StopReason reason)
    {
        const std::vector<std::string>& stops = dialogue().stops;

        std::string line;
        if (static_cast<size_t>(reason) < stops.size())
        {
            line = stops[reason];
        }
        if (line.empty() && static_cast<size_t>(QuestData::STOP_DEFAULT) < stops.size())
        {
            line = stops[QuestData::STOP_DEFAULT];
        }
        if (line.empty())
        {
            // Quests are not obliged to state a refusal line. Saying nothing
            // would look like the click was lost, so the npc's own
            // conversation is shown instead.
            fall_through();
            return;
        }

        UI::get().emplace<UINpcTalk>();
        UI::get().enable();

        auto npctalk = UI::get().get_element<UINpcTalk>();
        if (!npctalk)
        {
            return;
        }

        auto self = shared_from_this();

        UINpcTalk::LocalCallbacks callbacks;
        callbacks.advance = [self]() { self->fall_through(); };
        callbacks.dismiss = nullptr;

        npctalk->show_local(
            npcid,
            line,
            UINpcTalk::LocalPrompt::TEXT,
            false,
            std::move(callbacks)
        );
    }

    void QuestDialogue::accept()
    {
        if (stage == QuestData::START)
        {
            QuestActionPacket::start(questid, npcid, player_position()).dispatch();
        }
        else
        {
            QuestActionPacket::complete(questid, npcid, player_position()).dispatch();
        }

        show_answer_page(0, true);
    }

    void QuestDialogue::decline()
    {
        show_answer_page(0, false);

        // A quest with nothing to say about being turned down leaves the npc
        // free to say what it would have said anyway.
        if (dialogue().declined.empty())
        {
            fall_through();
        }
    }
}
