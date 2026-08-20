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
#include "../UIElement.h"

#include "../Components/IconCover.h"

#include "../../Constants.h"
#include "../../Graphics/Texture.h"
#include "../../Template/Rectangle.h"

#include <unordered_map>

namespace jrc
{
    class BuffIcon
    {
    public:
        BuffIcon(int32_t buff, int32_t dur);

        void draw(Point<int16_t> position, float alpha) const;
        bool update();
        // Restart the countdown. Recasting a buff replaces the server's timer
        // rather than stacking with it, so the icon has to follow suit instead
        // of running the old one down and vanishing under a live buff.
        void refresh(int32_t dur);
        // The box the icon covers when drawn at the given anchor, taken from
        // the texture rather than assumed, since the anchor is its bottom left.
        Rectangle<int16_t> bounds(Point<int16_t> position) const;
        int32_t get_id() const;

    private:
        static const uint16_t FLASH_TIME = 3'000;

        Texture icon;
        IconCover cover;
        int32_t buffid;
        int32_t duration;
        Linear<float> opacity;
        float opcstep;
    };


    class UIBuffList : public UIElement
    {
    public:
        static constexpr Type TYPE = BUFFLIST;
        static constexpr bool FOCUSED = false;
        static constexpr bool TOGGLED = false;

        UIBuffList();

        void draw(float inter) const override;
        void update() override;
        void update_screen(int16_t new_width, int16_t new_height) override;
        CursorResult send_cursor(bool pressed, Point<int16_t> position) override;
        bool is_in_range(Point<int16_t> cursorpos) const override;
        // Right-clicking an icon asks the server to drop that buff.
        void rightclick(Point<int16_t> cursorpos) override;

        void add_buff(int32_t buffid, int32_t duration);
        // Drop an icon once the server confirms the buff is gone.
        void remove_buff(int32_t buffid);

    private:
        // The icon drawn under the cursor, or null when none is.
        const BuffIcon* icon_at(Point<int16_t> cursorpos) const;

        std::unordered_map<int32_t, BuffIcon> icons;
    };
}
