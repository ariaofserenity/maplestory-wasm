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
#include "../../Character/QuestLog.h"
#include "../../Graphics/Text.h"
#include "../../Graphics/Texture.h"

#include <cstdint>
#include <string>
#include <vector>

namespace jrc
{
    // One laid-out piece of the game's rich text: a run of words, a picture,
    // or a marker. This is the reference client's CT_INFO, field for field.
    //
    // The layout is decided up front and stored here, so drawing is a matter
    // of blitting each piece at the offset it was given.
    struct CTInfo
    {
        // Reference client nType. Only TEXT and ICON are drawn by the quest
        // window; the dialogue draws PICTURE as well.
        enum Type : int32_t
        {
            TEXT = 0,
            ICON = 1,
            PICTURE = 2,
            CONTROL = 3,
            LINK = 4
        };

        int32_t type = TEXT;
        // The item a tooltip would describe. Only set when the token was
        // spelled with the trailing colon, which is what marks it as one the
        // player can point at.
        int32_t itemid = 0;
        int32_t line = 0;
        Text::Font font = Text::A11M;
        Text::Color color = Text::DARKGREY;
        std::string text;
        // The same text, laid out. Kept beside the string because the fitting
        // pass rewrites the string and has to re-measure it.
        Text render;
        Texture icon;
        int16_t left = 0;
        int16_t top = 0;
        int16_t width = 0;
        int16_t height = 0;
        // Which selectable option this piece belongs to, or -1 for none.
        int32_t select = -1;
        int16_t underline = 0;
        bool linechange = false;
        // Which control code this is, for CONTROL pieces: #E, #I, #S, #K in
        // that order.
        int32_t funccode = 0;
        // Set while the #w toggle is on, which is how a reward list marks the
        // part of itself that is the reward proper.
        bool reward = false;
        int32_t npcid = 0;
        int32_t mapid = 0;
    };

    // The reference client's CTextAnalyzer.
    //
    // Quest text and npc dialogue are markup, not plain text. A demand for
    // thirty branches is stored as "#i4000003:# #t4000003:# #c4000003# / 30",
    // which reads as the item's icon, its name, how many the character holds,
    // and then the literal "/ 30". Stripping the markup throws away the icon
    // and the count and leaves a line that reads like a mistake.
    //
    // This is a port of the client's own parser rather than an approximation
    // of it, so the same text breaks into the same pieces at the same places.
    // The three passes it runs are the client's: split the source into
    // phrases and turn each into a piece, fit the pieces across lines, then
    // hang each line's pieces from a common baseline.
    class TextAnalyzer
    {
    public:
        // A pixel margin kept at both ends of every line, and the width the
        // text is laid out to. The quest window uses 1 and 255; the npc
        // dialogue uses 8 and 341, or 8 and 210 when it shows no npc.
        TextAnalyzer(int16_t margin, int16_t width);

        // Lay the markup out, replacing whatever was in pieces.
        //
        // defaultcolor is what plain text is drawn in and what #k goes back
        // to, since the reference client swaps that font per panel rather
        // than fixing it. questid is the quest the text belongs to, which the
        // progress tokens are read against. listmode suppresses the marker
        // #L reserves room for, for the dialogue's own selection layout.
        //
        // Returns the height the laid-out text came to.
        int16_t analyze(const std::string& source, std::vector<CTInfo>& pieces,
            Text::Color defaultcolor, int16_t questid, bool listmode) const;

        int16_t get_margin() const;
        int16_t get_width() const;

    private:
        // Pass one: source text to pieces, one phrase at a time.
        void tokenize(const std::string& source, std::vector<CTInfo>& pieces,
            Text::Color defaultcolor, int16_t questid, bool listmode) const;
        // Pass two: assign every piece an x and a line, breaking lines where
        // they no longer fit.
        void fit_lines(std::vector<CTInfo>& pieces) const;
        // Pass three: assign every piece a y.
        int16_t stack_lines(std::vector<CTInfo>& pieces) const;

        // Take the next phrase off the front of source: one run of ordinary
        // characters, one #token, one line break, or nothing when spent.
        static std::string get_phrase(const std::string& source, size_t& cursor);
        // The phrase kinds the reference client tells apart. The numbering is
        // its own, because the token handlers are indexed by it.
        static int32_t get_phrase_type(const std::string& phrase);
        // The number a token carries, which is however much of the phrase
        // after the token letter reads as one.
        static int32_t get_parameter_no(const std::string& phrase);

        // How much of the text is a fragment that must not be left to start a
        // line on its own - a plural s, a possessive, or trailing punctuation.
        // Zero when it is not one.
        static int32_t is_suffix(const std::string& text);
        // Where the text could be broken instead, or zero when it could not.
        static int32_t is_dilimiter(const std::string& text);

        // Split the piece at index in two, the tail going to the next line.
        static void separate_line_text(std::vector<CTInfo>& pieces, size_t index,
            const std::string& head, const std::string& tail);
        // Move the piece at index and everything after it down a line.
        static void separate_line_icon(std::vector<CTInfo>& pieces, size_t index);

        // Where a line starts and ends and how tall it is, or first and last
        // of -1 when nothing is on it.
        static void get_line(const std::vector<CTInfo>& pieces, int32_t line,
            int32_t& first, int32_t& last, int16_t& height);
        static void adjust_height(std::vector<CTInfo>& pieces, int32_t first,
            int32_t last, int16_t y, int16_t lineheight);

        int16_t margin;
        int16_t width;
    };
}
