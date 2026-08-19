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

#include "../../Template/Interpolated.h"
#include "../../Template/Rectangle.h"

namespace jrc
{
    class Drop : public MapObject
    {
    public:
        virtual int8_t update(const Physics& physics) override;

        void init(int8_t);
        void expire(int8_t, const PhysicsObject*);

        Rectangle<int16_t> bounds() const;

        // Whether the drop is settled on the ground and is a real map object,
        // which is what the server requires before it will hand it over.
        bool can_be_looted() const;

        // A second announcement of the same drop. The server sends one to
        // everyone and another to the players near enough to watch it land;
        // the latter is what turns it into an object that stays on the map.
        void promote(int8_t mode);

        // Age the shared cooldown that keeps a burst of drops from stacking
        // their landing sounds. Called once per update by the map's drop pool.
        static void tick_sound_cooldown();

    protected:
        Drop(int32_t oid, int32_t owner, Point<int16_t> start,
            Point<int16_t> dest, int8_t type, int8_t mode, int16_t delay, bool playerdrop);

        // Let the drop tumble on its way out. Items do, mesos do not.
        void enable_spin();

        // Where to draw an icon so it sits centred on the drop, given the
        // anchor and size baked into the artwork. The original client hangs
        // the icon off its own middle and ignores that anchor, so without
        // this a drop settles half an icon to the right of the spot the
        // server named, which reads as being off to one side of the player.
        static Point<int16_t> icon_offset(Point<int16_t> origin, Point<int16_t> dimensions);

        Linear<float> opacity;
        Linear<float> angle;

    private:
        enum State
        {
            // Spawned, but the server asked for the arc to start a little
            // later so the drop lines up with the death animation.
            PENDING,
            // Following the scripted arc away from whoever dropped it.
            ARCING,
            // The arc ran out before the ground did, so it keeps sinking.
            FALLING,
            // Resting on the ground, bobbing gently.
            FLOATING,
            PICKEDUP
        };

        // Length of the arc for a drop that starts at 'starty' and has to
        // finish at 'desty', in milliseconds.
        int32_t arc_duration(int16_t starty, int16_t desty) const;
        void land();
        // Claim the shared landing sound if nothing else claimed it recently.
        static bool claim_sound();

        int32_t owner;
        int8_t pickuptype;
        bool playerdrop;
        // A drop the server never actually placed on the map. It plays the
        // throw and then vanishes; it can never be picked up.
        bool real;
        // Whether the drop thins out as it is thrown, which is how the server
        // says the player was never going to be allowed to keep it.
        bool fading;
        int32_t lifetime;

        const PhysicsObject* looter;
        State state;

        Point<int16_t> start;
        Point<int16_t> dest;
        // How fast the arc leaves the dropper and how long it lasts. Explosive
        // drops use a longer, higher arc, which is the only thing that varies.
        double scale;
        int32_t delay;
        int32_t duration;
        int32_t elapsed;
        bool announce;

        double basey;
        double floatangle;

        bool spinning;
        int32_t spun;
        float spindir;

        static int32_t sfxcooldown;
    };
}
