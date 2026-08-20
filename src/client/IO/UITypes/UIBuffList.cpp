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
#include "UIBuffList.h"

#include "../../Data/ItemData.h"
#include "../../Util/Misc.h"
#include "../../Net/Packets/PlayerPackets.h"

#include "nlnx/nx.hpp"
#include "nlnx/node.hpp"

namespace jrc
{
    BuffIcon::BuffIcon(int32_t buff, int32_t dur)
        : cover(IconCover::BUFF, dur - FLASH_TIME) {

        buffid = buff;
        duration = dur;
        opacity.set(1.0f);
        opcstep = -0.05f;

        if (buffid >= 0)
        {
            std::string strid = string_format::extend_id(buffid, 7);
            nl::node src = nl::nx::skill[strid.substr(0, 3) + ".img"]["skill"][strid];
            icon = src["icon"];
        }
        else
        {
            icon = ItemData::get(-buffid)
                .get_icon(true);
        }
    }

    void BuffIcon::draw(Point<int16_t> position, float alpha) const
    {
        icon.draw({ position, opacity.get(alpha) });
        cover.draw(position + Point<int16_t>(1, -31), alpha);
    }

    void BuffIcon::refresh(int32_t dur)
    {
        duration = dur;
        cover = IconCover(IconCover::BUFF, dur - FLASH_TIME);
        opacity.set(1.0f);
        opcstep = -0.05f;
    }

    Rectangle<int16_t> BuffIcon::bounds(Point<int16_t> position) const
    {
        // draw() hands the anchor straight to the texture, which offsets it by
        // its own origin - the bottom left for an icon - so the box sits above
        // the anchor rather than below it.
        Point<int16_t> topleft = position - icon.get_origin();
        return { topleft, topleft + icon.get_dimensions() };
    }

    int32_t BuffIcon::get_id() const
    {
        return buffid;
    }

    bool BuffIcon::update()
    {
        if (duration <= FLASH_TIME)
        {
            opacity += opcstep;

            bool fadedout = opcstep < 0.0f && opacity.last() <= 0.0f;
            bool fadedin = opcstep > 0.0f && opacity.last() >= 1.0f;
            if (fadedout || fadedin)
                opcstep = -opcstep;
        }

        cover.update();

        duration -= Constants::TIMESTEP;
        return duration < Constants::TIMESTEP;
    }


    UIBuffList::UIBuffList()
    {
        update_screen(Constants::viewwidth(), Constants::viewheight());
        active = true;
    }

    void UIBuffList::draw(float alpha) const
    {
        Point<int16_t> icpos = position;
        for (auto& icon : icons)
        {
            icon.second.draw(icpos, alpha);
            icpos.shift_x(-32);
        }
    }

    void UIBuffList::update()
    {
        for (auto iter = icons.begin(); iter != icons.end();)
        {
            bool expired = iter->second.update();
            if (expired)
            {
                iter = icons.erase(iter);
            }
            else
            {
                iter++;
            }
        }
    }

    void UIBuffList::update_screen(int16_t new_width, int16_t)
    {
        position = {
            static_cast<int16_t>(new_width - 50),
            40
        };
        dimension = { 32, 32 };
    }

    UIElement::CursorResult UIBuffList::send_cursor(bool pressed, Point<int16_t> cursorposition)
    {
        return UIElement::send_cursor(pressed, cursorposition);
    }

    bool UIBuffList::is_in_range(Point<int16_t> cursorpos) const
    {
        // The base check covers one icon's worth of space at the anchor, while
        // the row grows leftwards from it, so everything but the newest buff
        // sat outside the element and never saw the click.
        return icon_at(cursorpos) != nullptr;
    }

    const BuffIcon* UIBuffList::icon_at(Point<int16_t> cursorpos) const
    {
        Point<int16_t> icpos = position;
        for (auto& icon : icons)
        {
            if (icon.second.bounds(icpos).contains(cursorpos))
            {
                return &icon.second;
            }
            icpos.shift_x(-32);
        }
        return nullptr;
    }

    void UIBuffList::rightclick(Point<int16_t> cursorpos)
    {
        const BuffIcon* icon = icon_at(cursorpos);
        if (!icon)
        {
            return;
        }

        // Item buffs are keyed by the negative of the item's id, and the server
        // reads this as a skill id and looks it up unconditionally, so sending
        // one would fault it. Only a skill can be dropped this way.
        int32_t buffid = icon->get_id();
        if (buffid <= 0)
        {
            return;
        }

        // The icon stays until the server answers with the cancel, so a buff it
        // declines to drop keeps both its effect and its icon.
        CancelBuffPacket(buffid).dispatch();
    }

    void UIBuffList::add_buff(int32_t buffid, int32_t duration)
    {
        // emplace keeps the entry already there, which for a recast meant the
        // icon kept counting the old cast down and disappeared while the buff
        // itself was still running on the refreshed timer.
        auto iter = icons.find(buffid);
        if (iter != icons.end())
        {
            iter->second.refresh(duration);
            return;
        }

        icons.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(buffid),
            std::forward_as_tuple(buffid, duration)
        );
    }

    void UIBuffList::remove_buff(int32_t buffid)
    {
        icons.erase(buffid);
    }
}
