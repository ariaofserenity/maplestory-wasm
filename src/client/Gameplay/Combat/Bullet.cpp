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
#include "Bullet.h"

#include "../../Constants.h"

#include <cmath>

namespace jrc
{
    Bullet::Bullet(Animation a, Point<int16_t> origin, bool toleft)
    {
        animation = a;

        moveobj.set_x(origin.x() + (toleft ? -30.0 : 30.0));
        moveobj.set_y(origin.y() - 26.0);
    }

    void Bullet::draw(double viewx, double viewy, float alpha) const
    {
        Point<int16_t> bulletpos = moveobj.get_absolute(viewx, viewy, alpha);
        // A shot points along the line it is flying. The reference client only
        // spins bullets that ask for it, by rotatePeriod, and never turns one
        // towards what it was fired at - but an arrow angled up a slope while
        // still drawn level reads as a bug.
        DrawArgument args(pitch, bulletpos, flip, 1.0f);
        animation.draw(args, alpha);
    }

    bool Bullet::settarget(Point<int16_t> target, uint16_t flighttime)
    {
        double xdelta = target.x() - moveobj.crnt_x();
        double ydelta = target.y() - moveobj.crnt_y();
        if (std::abs(xdelta) < 10.0)
            return true;

        flip = xdelta > 0.0;

        if (flighttime > 0)
        {
            // Told when to arrive, the shot covers the gap in that time and
            // ignores the speed band - it has to land with the damage it was
            // fired for, however far away that target is.
            double steps = static_cast<double>(flighttime) / Constants::TIMESTEP;
            if (steps < 1.0)
                steps = 1.0;

            moveobj.hspeed = xdelta / steps;
            moveobj.vspeed = moveobj.hspeed * ydelta / xdelta;
            set_pitch(xdelta, ydelta);
            return false;
        }

        moveobj.hspeed = xdelta / 32;
        if (xdelta > 0.0)
        {
            if (moveobj.hspeed < 3.0)
            {
                moveobj.hspeed = 3.0;
            }
            else if (moveobj.hspeed > 6.0)
            {
                moveobj.hspeed = 6.0;
            }
        }
        else if (xdelta < 0.0)
        {
            if (moveobj.hspeed > -3.0)
            {
                moveobj.hspeed = -3.0;
            }
            else if (moveobj.hspeed < -6.0)
            {
                moveobj.hspeed = -6.0;
            }
        }
        moveobj.vspeed = moveobj.hspeed * ydelta / xdelta;
        set_pitch(xdelta, ydelta);
        return false;
    }

    void Bullet::set_pitch(double xdelta, double ydelta)
    {
        // The quad turns about its own centre in screen space, where y grows
        // downwards, so a positive angle is a clockwise turn. Flying right that
        // drops the head; flying left the sprite is mirrored and the same turn
        // raises it, so the sign follows the direction of travel.
        double drop = std::atan2(ydelta, std::abs(xdelta));
        pitch = static_cast<float>(flip ? drop : -drop);
    }

    bool Bullet::update(Point<int16_t> target)
    {
        animation.update();
        moveobj.move();

        int16_t xdelta = target.x() - moveobj.get_x();
        return moveobj.hspeed > 0.0 ? xdelta < 10 : xdelta > 10;
    }
}
