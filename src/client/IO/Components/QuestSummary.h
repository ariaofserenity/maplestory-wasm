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
#include "TextAnalyzer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace jrc
{
    // A blurb of the game's rich text, laid out and ready to draw.
    //
    // All the parsing lives in TextAnalyzer, which is the reference client's
    // own parser; this is the drawable that owns a laid-out result.
    class QuestSummary
    {
    public:
        QuestSummary();

        // Lay the blurb out to the given width.
        //
        // The margin and default colour are the panel's, since the reference
        // client keeps a margin at both ends of every line and swaps the
        // colour plain text is drawn in per panel. The quest window's are the
        // defaults here; the npc dialogue passes its own.
        void parse(const std::string& source, int16_t questid, int16_t width,
            int16_t margin = 1, Text::Color defaultcolor = Text::DARKGREY);

        void draw(Point<int16_t> position) const;

        // Draw only as much as has been shown so far: every piece before
        // upto_piece whole, and upto_chars characters of the one at it. This
        // is how CUtilDlgEx::Draw brings an npc's page in a character at a
        // time.
        void draw(Point<int16_t> position, int32_t upto_piece, int32_t upto_chars) const;

        // Whether the text carries a token that only this renderer can show.
        // Plain text rendering is cheaper, so it stays the default for the
        // dialogue that needs no pictures.
        static bool has_pictures(const std::string& source);

        // How tall the laid-out blurb came out, so a caller can stack things
        // underneath it.
        int16_t height() const;
        bool empty() const;

        // The pieces themselves, for a caller that needs more than a picture
        // of them - which option a piece belongs to, say, or which item it
        // would show a tooltip for.
        const std::vector<CTInfo>& get_pieces() const;

    private:
        static void draw_piece(const CTInfo& piece, Point<int16_t> at);

        std::vector<CTInfo> pieces;
        int16_t totalheight;
    };
}
