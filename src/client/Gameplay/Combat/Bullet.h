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
#include "../Physics/PhysicsObject.h"

#include "../../Graphics/Animation.h"
#include "../../Template/Point.h"

namespace jrc
{
    // Represents a projectile on a map.
    class Bullet
    {
    public:
        Bullet(Animation animation, Point<int16_t> origin, bool toleft);

        void draw(double viewx, double viewy, float alpha) const;
        // Aim the shot. flighttime is how long it should take to arrive, in ms;
        // zero leaves it at the speed band a projectile flies at by default.
        // Returns true if it is already on top of its target, in which case it
        // should not be spawned at all.
        bool settarget(Point<int16_t> target, uint16_t flighttime = 0);
        bool update(Point<int16_t> target);

    private:
        // Point the sprite along the line it is flying.
        void set_pitch(double xdelta, double ydelta);

        Animation animation;
        MovingObject moveobj;
        bool flip = false;
        float pitch = 0.0f;
    };
}
