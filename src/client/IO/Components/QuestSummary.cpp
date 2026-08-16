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
#include "QuestSummary.h"

#include <algorithm>

namespace jrc
{
    QuestSummary::QuestSummary() : totalheight(0) {}

    bool QuestSummary::has_pictures(const std::string& source)
    {
        for (size_t i = 0; i + 1 < source.size(); ++i)
        {
            if (source[i] != '#')
            {
                continue;
            }

            switch (source[i + 1])
            {
            case 'i':
            case 'v':
            case 's':
            case 'f':
            case 'F':
            case 'W':
                return true;
            default:
                break;
            }
        }

        return false;
    }

    bool QuestSummary::empty() const
    {
        return pieces.empty();
    }

    int16_t QuestSummary::height() const
    {
        return totalheight;
    }

    const std::vector<CTInfo>& QuestSummary::get_pieces() const
    {
        return pieces;
    }

    void QuestSummary::parse(const std::string& source, int16_t questid,
        int16_t width, int16_t margin, Text::Color defaultcolor)
    {
        TextAnalyzer analyzer(margin, width);
        totalheight = analyzer.analyze(source, pieces, defaultcolor, questid, false);
    }

    void QuestSummary::draw(Point<int16_t> position, int32_t upto_piece,
        int32_t upto_chars) const
    {
        for (size_t i = 0; i < pieces.size(); ++i)
        {
            int32_t at = static_cast<int32_t>(i);
            if (at > upto_piece)
            {
                break;
            }

            const CTInfo& piece = pieces[i];
            Point<int16_t> where = position + Point<int16_t>(piece.left, piece.top);

            if (at < upto_piece || piece.type != CTInfo::TEXT)
            {
                draw_piece(piece, where);
                continue;
            }

            // The piece being shown right now, cut to what has arrived.
            size_t shown = std::min<size_t>(
                piece.text.size(), static_cast<size_t>(std::max(0, upto_chars)));
            if (shown == 0)
            {
                continue;
            }

            Text partial{ piece.font, Text::LEFT, piece.color,
                piece.text.substr(0, shown), 0, false };
            partial.draw(where);
        }
    }

    void QuestSummary::draw(Point<int16_t> position) const
    {
        for (const CTInfo& piece : pieces)
        {
            draw_piece(piece, position + Point<int16_t>(piece.left, piece.top));
        }
    }

    void QuestSummary::draw_piece(const CTInfo& piece, Point<int16_t> at)
    {
        switch (piece.type)
        {
        case CTInfo::TEXT:
            piece.render.draw(at);
            break;
        case CTInfo::ICON:
        case CTInfo::PICTURE:
            if (piece.icon.is_valid())
            {
                // The reference client blits the canvas with its top left
                // corner at the piece's offset, which is also what the piece
                // was measured as. Texture::draw hangs a bitmap from its
                // origin instead, so the origin goes back on.
                piece.icon.draw(at + piece.icon.get_origin());
            }
            break;
        default:
            // Selection markers and control codes are structure, not
            // something to put on screen.
            break;
        }
    }
}
