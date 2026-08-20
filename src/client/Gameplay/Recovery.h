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
#include "../Character/Player.h"

#include <cstdint>

namespace jrc
{
    // The hp and mp a character gets back for doing nothing, ported from
    // CWvsContext::TryRecovery.
    //
    // The server schedules none of this. It only bounds what arrives and hands
    // the result back as a stat update, so regeneration exists exactly as long
    // as the client keeps asking for it - which is why two swordman skills that
    // ride on this tick, Improved HP Recovery and Endure, live here too.
    class Recovery
    {
    public:
        // Advance both timers by one frame and report a tick if one came due.
        void update(Player& player, float maprate);
        // Drop accumulated progress, e.g. when changing maps.
        void reset();

    private:
        // How long hp has to wait, or 0 while the character is in a stance that
        // does not recover at all.
        int32_t hp_interval(const Player& player) const;
        // The hp one tick is worth, skill included.
        int32_t hp_amount(const Player& player, double rate) const;
        // The mp one tick is worth, skill included.
        int32_t mp_amount(const Player& player, double rate) const;

        int32_t hp_elapsed = 0;
        int32_t mp_elapsed = 0;
    };
}
