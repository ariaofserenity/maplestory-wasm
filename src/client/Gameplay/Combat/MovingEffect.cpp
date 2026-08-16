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
#include "MovingEffect.h"

#include "../../Constants.h"

namespace jrc
{
    MovingEffect::MovingEffect(Animation animation, Point<int16_t> origin,
        Point<int16_t> travel, uint16_t duration, bool flip)
        : animation(animation), origin(origin), travel(travel),
          duration(duration), elapsed(0), flip(flip) {}

    float MovingEffect::opacity() const
    {
        if (duration <= 0)
            return 1.0f;

        constexpr int32_t FADE_IN_MS = 80;

        if (elapsed < FADE_IN_MS)
            return static_cast<float>(elapsed) / FADE_IN_MS;

        return 1.0f;
    }

    void MovingEffect::draw(double viewx, double viewy, float alpha) const
    {
        // A copy with no travel time does not move, so it stays at its origin.
        float progress = (duration > 0)
            ? static_cast<float>(elapsed) / duration
            : 0.0f;
        if (progress > 1.0f)
            progress = 1.0f;

        Point<int16_t> at{
            static_cast<int16_t>(origin.x() + travel.x() * progress),
            static_cast<int16_t>(origin.y() + travel.y() * progress)
        };

        // The view offsets are added, matching MovingObject::get_absolute.
        Point<int16_t> onscreen{
            static_cast<int16_t>(at.x() + viewx),
            static_cast<int16_t>(at.y() + viewy)
        };
        animation.draw({ onscreen, flip, opacity() }, alpha);
    }

    bool MovingEffect::update()
    {
        // A copy with no stated travel time lasts exactly as long as its own
        // animation, which is how the one-shot burst copies behave.
        bool finished = animation.update();

        elapsed += Constants::TIMESTEP;
        return (duration > 0) ? (elapsed >= duration) : finished;
    }
}
