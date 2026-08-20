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
#include "MapInfo.h"

#include "../../Constants.h"

#include <algorithm>

namespace jrc
{
    MapInfo::MapInfo(nl::node src, Range<int16_t> walls, Range<int16_t> borders)
    {
        nl::node info = src["info"];
        if (info["VRLeft"].data_type() == nl::node::type::integer)
        {
            mapwalls = { info["VRLeft"], info["VRRight"] };
            mapborders = { info["VRTop"], info["VRBottom"] };
            mapborders = {
                mapborders.first() + Constants::VIEWYOFFSET,
                mapborders.second() - Constants::VIEWYOFFSET
            };
        }
        else
        {
            mapwalls = walls;
            mapborders = borders;
        }

        std::string bgmpath = info["bgm"];
        size_t split = bgmpath.find('/');
        bgm = bgmpath.substr(0, split) + ".img/" + bgmpath.substr(split + 1);

        cloud = info["cloud"].get_bool();
        fieldlimit = info["fieldLimit"];
        hideminimap = info["hideMinimap"].get_bool();
        mapmark = info["mapMark"].get_string();
        swim = info["swim"].get_bool();
        town = info["town"].get_bool();
        recovery = static_cast<float>(info["recovery"].get_real(1.0));

        for (auto seat : src["seat"])
        {
            seats.push_back(seat);
        }

        for (auto ladder : src["ladderRope"])
        {
            ladders.push_back(ladder);
        }
    }

    MapInfo::MapInfo() {}

    bool MapInfo::is_underwater() const
    {
        return swim;
    }

    std::string MapInfo::get_bgm() const
    {
        return bgm;
    }

    Range<int16_t> MapInfo::get_walls() const
    {
        return mapwalls;
    }

    Range<int16_t> MapInfo::get_borders() const
    {
        return mapborders;
    }

    float MapInfo::get_recovery() const
    {
        return recovery;
    }

    Optional<const Seat> MapInfo::findseat(Point<int16_t> position) const
    {
        for (auto& seat : seats)
        {
            if (seat.inrange(position))
                return seat;
        }
        return nullptr;
    }

    Optional<const Ladder> MapInfo::findladder(Point<int16_t> position, bool upwards) const
    {
        for (auto& ladder : ladders)
        {
            if (ladder.inrange(position, upwards))
                return ladder;
        }
        return nullptr;
    }


    Seat::Seat(nl::node src)
    {
        pos = src;
    }

    bool Seat::inrange(Point<int16_t> position) const
    {
        auto hor = Range<int16_t>::symmetric(position.x(), 10);
        auto ver = Range<int16_t>::symmetric(position.y(), 10);
        return hor.contains(pos.x()) && ver.contains(pos.y());
    }

    Point<int16_t> Seat::getpos() const
    {
        return pos;
    }


    Ladder::Ladder(nl::node src)
    {
        x = src["x"];
        y1 = src["y1"];
        y2 = src["y2"];
        ladder = src["l"].get_bool();
    }

    bool Ladder::is_ladder() const
    {
        return ladder;
    }

    bool Ladder::inrange(Point<int16_t> position, bool upwards) const
    {
        auto hor = Range<int16_t>::symmetric(position.x(), 10);
        if (!hor.contains(x))
        {
            return false;
        }

        if (upwards)
        {
            // The reference client overlaps a span reaching 20 pixels above the
            // feet against the whole ladder rather than testing a single point,
            // which is what makes grabbing work while walking underneath: rope
            // bottoms usually hang some way above the platform they end at, and
            // a point only a few pixels up never reaches them.
            return position.y() - 20 <= y2 && y1 <= position.y();
        }

        // Climbing down is only offered while standing on top of the ladder,
        // within the same short reach the reference client reserves for it.
        return y1 >= position.y() && y1 <= position.y() + 10;
    }

    bool Ladder::felloff(int16_t y, bool downwards) const
    {
        int16_t dy = downwards ? y + 5 : y - 5;
        return dy > y2 || y + 5 < y1;
    }

    int16_t Ladder::get_x() const
    {
        return x;
    }

    int16_t Ladder::attach_y(int16_t y) const
    {
        // Pull the character onto the ladder's own extent. Grabbing from the
        // platform at the foot of a rope leaves the feet below the last rung,
        // and leaving them there would trip felloff() on the very next tick and
        // drop the character straight back off again.
        return std::clamp(y, y1, y2);
    }
}
