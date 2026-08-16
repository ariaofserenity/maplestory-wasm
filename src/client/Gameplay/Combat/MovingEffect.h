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
#include "../../Graphics/Animation.h"
#include "../../Template/Point.h"

namespace jrc
{
    // One copy emitted by a skill's special node, travelling from where it
    // appears to where it lands over a fixed time. Arrow Rain's arrows fall
    // from above the area onto it; the same shape covers Arrow Eruption rising
    // and the other emitters.
    //
    // Anchored to the map rather than to the caster, so the copies stay where
    // the skill was aimed even if the caster walks away mid-cast.
    class MovingEffect
    {
    public:
        MovingEffect(Animation animation, Point<int16_t> origin,
            Point<int16_t> travel, uint16_t duration, bool flip);

        void draw(double viewx, double viewy, float alpha) const;
        // Advance one tick. Returns true once the copy has landed.
        bool update();

    private:
        // Fade in so a copy arrives rather than popping into existence.
        float opacity() const;

        Animation animation;
        Point<int16_t> origin;
        Point<int16_t> travel;
        int32_t duration;
        int32_t elapsed;
        bool flip;
    };
}
