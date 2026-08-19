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
#include "Drop.h"

#include "../../Audio/Audio.h"
#include "../../Constants.h"

#include <algorithm>
#include <cmath>

namespace jrc
{
    namespace
    {
        // A drop does not fall under the map's gravity. It follows a scripted
        // arc from whoever dropped it to the spot the server picked, which is
        // what keeps the motion identical on every machine. Speeds below are
        // in pixels per second and are the ones the v95 client uses.

        // How fast the arc leaves the dropper, and the coefficient of t² that
        // brings it back down. The v95 client uses 400 for both, which throws
        // the drop 100px up over a second; these are that pair scaled to three
        // quarters, for an arc a quarter shallower that still takes a second.
        // Both have to move together: the arc only comes back down to the
        // height it left at after RISE_SPEED / FALL_ACCEL seconds, and the
        // fall below depends on it landing there exactly.
        constexpr double RISE_SPEED = 300.0;
        constexpr double FALL_ACCEL = 300.0;
        // Height the duration formula assumes the arc can reach, which is
        // where RISE_SPEED runs out: RISE_SPEED² / (4 * FALL_ACCEL).
        constexpr double ARC_HEIGHT = 75.0;
        // A full arc, and the part of it spent covering the first half of the
        // horizontal gap. The rest of the gap is covered over what is left.
        constexpr double ARC_TIME = 1000.0;
        constexpr double SPREAD_TIME = 500.0;

        // Explosive drops are thrown further, on an arc scaled up by this much.
        constexpr double EXPLODE_SCALE = 1.8;
        constexpr int8_t EXPLODE_PICKUP = 3;

        // A resting drop bobs by this many pixels, once every two seconds.
        constexpr double FLOAT_AMPLITUDE = 3.0;
        constexpr double FLOAT_PERIOD = 2000.0;

        // A thrown item tumbles once on the way out and comes to rest upright.
        constexpr double SPIN_TIME = 300.0;

        // Drops sit half an icon above the point the server names.
        constexpr int16_t ICON_OFFSET = 4;

        // Half the width of the box a drop can be picked up from.
        constexpr int16_t DROP_HALFWIDTH = 16;

        // Shortest gap between two landing sounds. A mob that drops six items
        // at once should thud once, not six times.
        constexpr int32_t SOUND_INTERVAL = 300;

        // The mode the server uses for a drop that is being thrown right now,
        // as opposed to one that was already lying there when we walked up.
        constexpr int8_t MODE_AUDIBLE = 1;
        constexpr int8_t MODE_ONGROUND = 2;
        // The mode for something the player is not allowed to keep, such as a
        // quest item. The throw is played and the item thins out as it goes.
        constexpr int8_t MODE_VANISH = 3;

        // How long a drop that was never really placed hangs around if it
        // somehow never reaches the ground.
        constexpr int32_t UNREAL_LIFETIME = 3000;

        // How long the fade takes, which is the length of the arc, so the
        // item is gone at the moment it would have touched down.
        constexpr double FADE_TIME = 1000.0;

        constexpr double PI2 = 6.28318530717958647692;
    }

    int32_t Drop::sfxcooldown = 0;

    Drop::Drop(int32_t id, int32_t own, Point<int16_t> from, Point<int16_t> to,
        int8_t type, int8_t mode, int16_t dly, bool pldrp) : MapObject(id) {

        owner = own;
        pickuptype = type;
        playerdrop = pldrp;

        start = { from.x(), static_cast<int16_t>(from.y() - ICON_OFFSET) };
        dest = { to.x(), static_cast<int16_t>(to.y() - ICON_OFFSET) };
        basey = dest.y();

        set_position(start);

        angle.set(0.0f);
        opacity.set(1.0f);
        floatangle = 0.0;
        looter = nullptr;

        spinning = false;
        spun = 0;
        // Tumble the way the drop is being thrown.
        spindir = (to.x() < from.x()) ? -1.0f : 1.0f;

        // The arc is driven by hand, so the physics engine is only asked which
        // platform the drop is over, for the sake of the drawing order.
        phobj.type = PhysicsObject::FIXATED;

        scale = (pickuptype == EXPLODE_PICKUP) ? EXPLODE_SCALE : 1.0;
        delay = std::max<int32_t>(0, dly);
        duration = arc_duration(start.y(), dest.y());
        elapsed = 0;
        announce = mode == MODE_AUDIBLE;
        // Anything the server did not add to the map is thrown for show only.
        real = mode == MODE_AUDIBLE || mode == MODE_ONGROUND;
        fading = mode == MODE_VANISH;
        lifetime = 0;

        if (mode == MODE_ONGROUND)
        {
            // Already lying there before we could see it.
            land();
        }
        else
        {
            state = PENDING;
        }
    }

    int32_t Drop::arc_duration(int16_t starty, int16_t desty) const
    {
        int32_t full = static_cast<int32_t>(ARC_TIME * scale);

        // Landing at or below where it started leaves the whole arc available.
        if (starty <= desty)
            return full;

        // Otherwise the drop has to be caught at the point it has climbed far
        // enough, so the arc is cut short by however much it has to gain.
        double climb = ARC_HEIGHT * scale + desty - starty;
        if (climb < 0.0)
            climb = 0.0;

        // Number of 30ms steps needed to fall that far, under the 2 *
        // FALL_ACCEL the arc comes down at.
        int32_t steps = static_cast<int32_t>(std::sqrt(1000.0 / FALL_ACCEL * climb)) + 1;
        int32_t shortened = steps * 30 + static_cast<int32_t>(SPREAD_TIME * scale);
        return std::min(full, shortened);
    }

    bool Drop::claim_sound()
    {
        if (sfxcooldown > 0)
            return false;

        sfxcooldown = SOUND_INTERVAL;
        return true;
    }

    void Drop::tick_sound_cooldown()
    {
        if (sfxcooldown > 0)
            sfxcooldown -= Constants::TIMESTEP;
    }

    bool Drop::can_be_looted() const
    {
        return real && state == FLOATING;
    }

    void Drop::promote(int8_t mode)
    {
        if (mode == MODE_AUDIBLE || mode == MODE_ONGROUND)
        {
            real = true;
            fading = false;
            opacity.set(1.0f);
        }
    }

    Point<int16_t> Drop::icon_offset(Point<int16_t> origin, Point<int16_t> dimensions)
    {
        return { static_cast<int16_t>(origin.x() - dimensions.x() / 2), 0 };
    }

    void Drop::enable_spin()
    {
        // A drop that was already lying there when it came into view is
        // upright and stays that way; only one being thrown tumbles.
        spinning = state != FLOATING;
    }

    void Drop::land()
    {
        state = FLOATING;
        elapsed = 0;
        angle.set(0.0f);
        set_position(dest);
    }

    int8_t Drop::update(const Physics& physics)
    {
        if (state == ARCING || state == FALLING)
        {
            // A fixated object keeps the first platform it was matched with.
            // While the drop is travelling it can cross onto another one, so
            // the match is thrown away and looked up again each step.
            phobj.fhid = 0;
            phobj.onground = false;
        }

        // FIXATED, so this only refreshes the foothold the drop is drawn over.
        // The pickup animation below is the one case that still wants real
        // physics, and that leaves the object NORMAL.
        physics.move_object(phobj);

        if (spinning)
        {
            // The turn is started the moment the drop appears and runs on its
            // own clock, so it is over well before the arc is.
            spun += Constants::TIMESTEP;
            if (spun >= SPIN_TIME)
            {
                spinning = false;
                angle.set(0.0f);
            }
            else
            {
                angle = spindir * static_cast<float>(PI2 * spun / SPIN_TIME);
            }
        }

        if (!real && state != PENDING)
        {
            // Landing is the end of it. The throw was only ever an animation
            // for something the server has already taken away.
            lifetime += Constants::TIMESTEP;

            if (fading)
            {
                float left = 1.0f - static_cast<float>(lifetime / FADE_TIME);
                opacity = std::max(left, 0.0f);
            }

            if (state == FLOATING || lifetime > UNREAL_LIFETIME)
            {
                MapObject::deactivate();
                return -1;
            }
        }

        switch (state)
        {
        case PENDING:
            elapsed += Constants::TIMESTEP;
            if (elapsed >= delay)
            {
                elapsed = 0;
                state = ARCING;

                if (announce && claim_sound())
                {
                    Sound(Sound::DROP).play();
                }
            }
            break;

        case ARCING:
        {
            elapsed += Constants::TIMESTEP;

            // Half the horizontal gap is covered quickly, the rest over
            // whatever is left of the arc.
            double spread = SPREAD_TIME * scale;
            double early = std::min(elapsed / spread, 1.0);
            double late = (duration > spread)
                ? std::min(std::max((elapsed - spread) / (duration - spread), 0.0), 1.0)
                : 1.0;
            double travelled = 0.5 * (dest.x() - start.x()) * (early + late);

            double seconds = elapsed / 1000.0;
            double height = start.y()
                - RISE_SPEED * scale * seconds
                + FALL_ACCEL * seconds * seconds;

            phobj.x = start.x() + travelled;
            phobj.y = height;

            if (elapsed >= duration)
            {
                if (height < dest.y())
                {
                    // The arc bottomed out in mid air, which happens when the
                    // dropper was well above the ground it lands on.
                    state = FALLING;
                    elapsed = 0;
                }
                else
                {
                    land();
                }
            }
            break;
        }

        case FALLING:
        {
            elapsed += Constants::TIMESTEP;

            // The arc ends moving downwards at the speed it left with, and the
            // remaining distance is covered at exactly that speed.
            double height = start.y() + RISE_SPEED * scale * (elapsed / 1000.0);
            if (height >= dest.y())
            {
                land();
            }
            else
            {
                phobj.x = dest.x();
                phobj.y = height;
            }
            break;
        }

        case FLOATING:
            floatangle += PI2 * Constants::TIMESTEP / FLOAT_PERIOD;
            if (floatangle > PI2)
                floatangle -= PI2;

            phobj.x = dest.x();
            phobj.y = basey + std::sin(floatangle) * FLOAT_AMPLITUDE;
            break;

        case PICKEDUP:
        {
            static const uint16_t PICKUPTIME = 48;
            static const float OPCSTEP = 1.0f / PICKUPTIME;

            if (looter)
            {
                double hdelta = looter->x - phobj.x;
                phobj.hspeed = looter->hspeed / 2.0 + (hdelta - 16.0) / PICKUPTIME;
            }

            opacity -= OPCSTEP;
            if (opacity.last() <= OPCSTEP)
            {
                opacity.set(1.0f);

                MapObject::deactivate();
                return -1;
            }
            break;
        }
        }

        return phobj.fhlayer;
    }

    void Drop::expire(int8_t type, const PhysicsObject* lt)
    {
        spinning = false;

        switch (type)
        {
        case 0:
            state = PICKEDUP;
            break;
        case 1:
            deactivate();
            break;
        case 2:
            angle.set(0.0f);
            state = PICKEDUP;
            looter = lt;
            phobj.vspeed = -4.5f;
            phobj.type = PhysicsObject::NORMAL;
            break;
        }
    }

    Rectangle<int16_t> Drop::bounds() const
    {
        // Centred on the drop, to match where the icon is drawn.
        auto lt = get_position() - Point<int16_t>(DROP_HALFWIDTH, 0);
        auto rb = lt + Point<int16_t>(DROP_HALFWIDTH * 2, 32);
        return Rectangle<int16_t>(lt, rb);
    }
}
