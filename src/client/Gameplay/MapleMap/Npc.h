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
#include "MapObject.h"

#include "../Physics/PhysicsObject.h"

#include "../../Graphics/Animation.h"
#include "../../Graphics/Text.h"
#include "../../Util/Randomizer.h"

#include <map>

namespace jrc
{
    // Represents a npc on the current map.
    // Implements the 'Mapobject' interface to be used in a 'Mapobjects' template.
    class Npc : public MapObject
    {
    public:
        // Constructs an npc by combining data from game files with
        // data sent by the server.
        Npc(int32_t npcid, int32_t oid, bool mirrored, uint16_t fhid,
            bool control, Point<int16_t> position);

        // Draws the current animation and name/function tags.
        void draw(double viewx, double viewy, float alpha) const override;
        // Updates the current animation and physics.
        int8_t update(const Physics& physics) override;

        // Changes stance and resets animation.
        void set_stance(const std::string& stance);

        // The marker floating over a quest npc's head.
        //
        // The reference client keeps five buckets of quest ids per npc and
        // picks one balloon from whichever is occupied, in a fixed order of
        // precedence: a quest ready to be handed in outranks one that can be
        // started, which outranks one merely under way. The values are the
        // indices it looks up under UIWindow2/QuestIcon.
        enum QuestMarker : int32_t
        {
            // A quest that can be started right now.
            MARKER_AVAILABLE = 0,
            // A quest under way whose conditions are not met yet.
            MARKER_IN_PROGRESS = 1,
            // A quest under way that can now be handed in. Takes precedence
            // over everything else.
            MARKER_COMPLETE = 2,
            // A startable quest the client considers beneath the character.
            MARKER_LOW_VALUE = 3,
            // Nothing to show.
            MARKER_NONE = 6
        };

        // Note which balloon this npc should float, if any.
        void set_quest_marker(QuestMarker marker);

        // Check wether this is a server-sided npc.
        bool isscripted() const;
        // Check if the npc is in range of the cursor.
        bool inrange(Point<int16_t> cursorpos, Point<int16_t> viewpos) const;

        // The npc's id in the game files, as opposed to the object id the
        // server tracks this particular instance of it by.
        int32_t get_id() const
        {
            return npcid;
        }

        const std::string& get_name() const
        {
            return name;
        }

        const std::string& get_func() const
        {
            return func;
        }

    private:
        std::map<std::string, Animation> animations;
        std::map<std::string, std::vector<std::string>> lines;
        std::vector<std::string> states;
        std::string name;
        std::string func;
        bool hidename;
        bool scripted;
        bool mouseonly;

        int32_t npcid;
        bool flip;
        std::string stance;
        bool control;

        Randomizer random;
        Text namelabel;
        Text funclabel;

        // The balloon currently shown, and its animation. Kept apart from the
        // npc's own animations because it belongs to the ui rather than to the
        // npc's artwork.
        QuestMarker questmarker;
        Animation markeranimation;
    };
}
