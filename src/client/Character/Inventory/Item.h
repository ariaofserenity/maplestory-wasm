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
#include <cstdint>
#include <string>

namespace jrc
{
    class Item
    {
    public:
        Item(int32_t itemid, int64_t expiration, const std::string& owner, int16_t flags);

        // Return the item's expiration as the filetime the server sent.
        int64_t get_expiration() const;
        // Return the attribute bits the server sent with the item.
        int16_t get_flags() const;

    private:
        int32_t item_id;
        int64_t expiration;
        std::string owner;
        int16_t flags;
    };
}

