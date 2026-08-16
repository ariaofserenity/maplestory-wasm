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
#include "TextAnalyzer.h"

#include "../../Data/ItemData.h"
#include "../../Data/QuestData.h"
#include "../../Data/SkillData.h"
#include "../../Gameplay/Stage.h"
#include "../../Util/Misc.h"

#include "nlnx/nx.hpp"
#include "nlnx/node.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace jrc
{
    namespace
    {
        // The gap left between one line's baseline and the next.
        constexpr int16_t LINE_GAP = 2;
        // No line is ever shorter than this, however small the pieces on it.
        constexpr int16_t MIN_LINE_HEIGHT = 16;
        // How much room a selection marker reserves, when the caller is not
        // laying the options out itself.
        constexpr int16_t LINK_MARKER_WIDTH = 18;
        // A selectable piece hangs this much lower than the rest of its line.
        constexpr int16_t SELECT_DROP = 10;
        // Slack the wrapping pass keeps back before it measures, so a broken
        // line never ends flush against the margin.
        constexpr int16_t WRAP_SLACK = 10;
        // Wide enough that measuring never wraps, so the advances a layout
        // reports are one run of increasing x positions.
        constexpr uint16_t MEASURE_WIDTH = 30000;

        // Phrase kinds, numbered as the reference client numbers them.
        enum PhraseType : int32_t
        {
            PT_PLAIN = 0,
            PT_LINK = 1,        // #L
            PT_FUNC_E = 2,      // #E
            PT_FUNC_I = 3,      // #I
            PT_FUNC_S = 4,      // #S
            PT_FUNC_K = 5,      // #K
            PT_REWARD = 6,      // #w
            PT_ITEM_ICON = 7,   // #i #v
            PT_SKILL = 10,      // #s
            PT_PICTURE = 11,    // #F #f
            PT_BOX = 13,        // #B
            PT_JOB = 14,        // #j
            PT_QUESTION = 15,   // #Q
            PT_DURATION = 16,   // #D
            PT_HEADING = 17,    // #W
            PT_PARAM = 18       // everything else that carries a number
        };

        // The token letters that take their argument up to the closing hash,
        // reading it character by character.
        bool takes_run(char c)
        {
            switch (c)
            {
            case '@': case 'B': case 'F': case 'L': case 'M': case '_':
            case 'a': case 'c': case 'f': case 'h': case 'i': case 'm':
            case 'o': case 'p': case 'q': case 's': case 't': case 'u':
            case 'v': case 'x': case 'y': case 'z':
                return true;
            default:
                return false;
            }
        }

        // The token letters that take everything up to the closing hash in
        // one go, so their argument may hold anything but a hash.
        bool takes_span(char c)
        {
            switch (c)
            {
            case 'D': case 'Q': case 'R': case 'W': case 'j':
                return true;
            default:
                return false;
            }
        }

        Text make_text(const std::string& str, Text::Font font, Text::Color color)
        {
            // Unformatted: the markup has already been taken apart, and
            // letting the layout read it again would eat a stray hash.
            return { font, Text::LEFT, color, str, MEASURE_WIDTH, false };
        }

        // How many characters of the text fit in the given width. The client
        // asks its font the same question before it decides where to break.
        size_t calc_longest_text(const Text& text, int32_t available)
        {
            if (available <= 0)
            {
                return 0;
            }

            size_t length = text.length();
            size_t fits = 0;
            while (fits < length &&
                static_cast<int32_t>(text.advance(fits + 1)) <= available)
            {
                fits++;
            }

            return fits;
        }

        std::string trim_left(const std::string& str)
        {
            size_t first = str.find_first_not_of(' ');
            return first == std::string::npos ? std::string{} : str.substr(first);
        }

        // The last place the character could be found, or -1.
        int32_t find_reverse(const std::string& str, char c)
        {
            size_t at = str.find_last_of(c);
            return at == std::string::npos ? -1 : static_cast<int32_t>(at);
        }

        std::string item_name(int32_t itemid)
        {
            const ItemData& data = ItemData::get(itemid);
            return data.is_valid() ? data.get_name() : std::string{};
        }

        std::string string_table_name(const char* image, const std::string& key)
        {
            nl::node name = nl::nx::string[image][key]["name"];
            return name ? name.get_string() : std::string{};
        }

        std::string map_name(int32_t mapid)
        {
            const NxHelper::Map::MapInfo info = NxHelper::Map::get_map_info_by_id(mapid);
            return info.full_name.empty() ? info.name : info.full_name;
        }

        // The heading pictures a reward list is introduced by, named in the
        // markup exactly as they are in the game files.
        Texture summary_heading(const std::string& name)
        {
            return nl::nx::ui["UIWindow2.img"]["Quest"]["quest_info"]["summary_icon"][name];
        }

        // A picture named by its full path, which is how a few quests point
        // at art that has no token of its own.
        Texture picture_at(const std::string& path)
        {
            nl::node node = nl::nx::ui;
            size_t start = 0;
            // The paths are written from the file down, so a leading "UI/"
            // names the file this already is.
            if (path.compare(0, 3, "UI/") == 0)
            {
                start = 3;
            }

            while (start < path.size() && node)
            {
                size_t end = path.find('/', start);
                if (end == std::string::npos)
                {
                    end = path.size();
                }
                node = node[path.substr(start, end - start)];
                start = end + 1;
            }

            return node;
        }

        // How far along a quest's kill counter for one of its mobs is, read
        // back as the client words it.
        std::string quest_mob_count(const Questlog& quests, int32_t packed)
        {
            int16_t owner = static_cast<int16_t>(packed / 10);
            size_t slot = static_cast<size_t>(packed % 10);
            if (slot == 0)
            {
                return {};
            }

            size_t mobindex = slot - 1;

            const QuestData::Requirements& reqs =
                QuestData::get(owner).get_requirements(QuestData::END);

            int32_t needed = mobindex < reqs.mobs.size()
                ? reqs.mobs[mobindex].count
                : 0;
            int32_t killed = quests.get_mob_progress(owner, mobindex);

            return std::to_string(killed) + " / " + std::to_string(needed);
        }

        std::string quest_state_name(const Questlog& quests, int16_t questid)
        {
            switch (quests.get_state(questid))
            {
            case Questlog::STARTED:
                return "In Progress";
            case Questlog::COMPLETED:
                return "Complete";
            default:
                return "Not Started";
            }
        }
    }

    TextAnalyzer::TextAnalyzer(int16_t m, int16_t w) : margin(m), width(w) {}

    int16_t TextAnalyzer::get_margin() const
    {
        return margin;
    }

    int16_t TextAnalyzer::get_width() const
    {
        return width;
    }

    std::string TextAnalyzer::get_phrase(const std::string& source, size_t& cursor)
    {
        if (cursor >= source.size())
        {
            return {};
        }

        char first = source[cursor];

        // A line break is the break character and whatever follows it, so
        // both "\r\n" and the two characters backslash and n - which some
        // quest text spells its breaks as - come out as one phrase.
        if (first == '\r' || first == '\\')
        {
            std::string phrase(1, first);
            cursor++;
            if (cursor < source.size())
            {
                phrase.push_back(source[cursor]);
                cursor++;
            }
            return phrase;
        }

        if (first != '#')
        {
            // Ordinary text runs until something that starts a phrase.
            size_t start = cursor;
            while (cursor < source.size())
            {
                char c = source[cursor];
                if (c == '\r' || c == '#' || c == '\\')
                {
                    break;
                }
                cursor++;
            }
            return source.substr(start, cursor - start);
        }

        cursor++;
        if (cursor >= source.size())
        {
            return "#";
        }

        char letter = source[cursor];
        cursor++;

        // A doubled hash is the way to write one.
        if (letter == '#')
        {
            return "#";
        }

        std::string phrase = "#";
        phrase.push_back(letter);

        if (takes_run(letter))
        {
            while (cursor < source.size())
            {
                char c = source[cursor];
                cursor++;
                if (c == '\r' || c == '#' || c == '\\')
                {
                    // The closing hash belongs to the token, not the text.
                    break;
                }
                phrase.push_back(c);
            }
        }
        else if (takes_span(letter))
        {
            size_t close = source.find('#', cursor);
            if (close != std::string::npos)
            {
                phrase += source.substr(cursor, close - cursor);
                cursor = close + 1;
            }
        }

        return phrase;
    }

    int32_t TextAnalyzer::get_phrase_type(const std::string& phrase)
    {
        if (phrase.size() < 2 || phrase[0] != '#')
        {
            return PT_PLAIN;
        }

        switch (phrase[1])
        {
        case 'E': return PT_FUNC_E;
        case 'I': return PT_FUNC_I;
        case 'K': return PT_FUNC_K;
        case 'S': return PT_FUNC_S;
        case 'w': return PT_REWARD;
        default: break;
        }

        // Everything else needs an argument to mean anything, so a bare two
        // character token is text as far as this is concerned. The colour and
        // weight codes are caught before this is ever reached.
        if (phrase.size() <= 2)
        {
            return PT_PLAIN;
        }

        switch (phrase[1])
        {
        case 'B': return PT_BOX;
        case 'D': return PT_DURATION;
        case 'F':
        case 'f': return PT_PICTURE;
        case 'L': return PT_LINK;
        case 'Q': return PT_QUESTION;
        case 'W': return PT_HEADING;
        case 'i':
        case 'v': return PT_ITEM_ICON;
        case 'j': return PT_JOB;
        case 's': return PT_SKILL;
        default: return PT_PARAM;
        }
    }

    int32_t TextAnalyzer::get_parameter_no(const std::string& phrase)
    {
        // Whatever follows the token letter that reads as a number. The
        // trailing colon some tokens carry ends it on its own.
        return phrase.size() > 2 ? std::atoi(phrase.c_str() + 2) : 0;
    }

    int32_t TextAnalyzer::is_suffix(const std::string& text)
    {
        int32_t length = static_cast<int32_t>(text.size());
        if (length < 1)
        {
            return 0;
        }

        auto ends_here = [&](int32_t at)
        {
            return length == at || text[at] == ' ';
        };

        switch (text[0])
        {
        case '!':
        case ',':
        case '-':
        case '.':
        case '?':
            return ends_here(1) ? 1 : 0;
        case '\'':
        {
            // A possessive holds on to whatever it is attached to until the
            // next space.
            size_t space = text.find(' ');
            return space == std::string::npos ? 0 : static_cast<int32_t>(space);
        }
        case 'e':
            if (length > 1 && text[1] == 's')
            {
                if (length < 3 || text[2] == ' ')
                {
                    return 2;
                }

                switch (text[2])
                {
                case '!':
                case ',':
                case '.':
                case '?':
                    return ends_here(3) ? 3 : 0;
                default:
                    break;
                }
            }
            return 0;
        case 's':
            if (length < 2 || text[1] == ' ')
            {
                return 1;
            }

            switch (text[1])
            {
            case '!':
            case ',':
            case '.':
            case '?':
                return ends_here(2) ? 2 : 0;
            default:
                return 0;
            }
        default:
            return 0;
        }
    }

    int32_t TextAnalyzer::is_dilimiter(const std::string& text)
    {
        int32_t length = static_cast<int32_t>(text.size());
        int32_t at = length;

        for (char c : { ' ', '-', '(' })
        {
            int32_t found = find_reverse(text, c);
            if (found > 0 && found < at)
            {
                at = found;
            }
        }

        return at < length && at > 0 ? at : 0;
    }

    void TextAnalyzer::separate_line_text(std::vector<CTInfo>& pieces, size_t index,
        const std::string& head, const std::string& tail)
    {
        if (head.empty())
        {
            // Nothing left to keep on this line, so the piece goes down whole
            // along with everything after it.
            for (size_t i = index; i < pieces.size(); ++i)
            {
                pieces[i].line++;
            }
            return;
        }

        pieces[index].text = head;
        pieces[index].render = make_text(head, pieces[index].font, pieces[index].color);
        pieces[index].width = pieces[index].render.width();

        std::string rest = trim_left(tail);
        if (!rest.empty())
        {
            CTInfo carried;
            carried.type = CTInfo::TEXT;
            carried.line = pieces[index].line;
            carried.font = pieces[index].font;
            carried.color = pieces[index].color;
            carried.text = rest;
            carried.render = make_text(rest, carried.font, carried.color);
            carried.left = 0;
            carried.top = 0;
            carried.width = carried.render.width();
            carried.height = carried.render.height();
            carried.select = pieces[index].select;
            carried.underline = 0;
            carried.linechange = pieces[index].linechange;

            using difference = std::vector<CTInfo>::difference_type;
            pieces.insert(
                pieces.begin() + static_cast<difference>(index + 1),
                std::move(carried));
        }

        for (size_t i = index + 1; i < pieces.size(); ++i)
        {
            pieces[i].line++;
        }
    }

    void TextAnalyzer::separate_line_icon(std::vector<CTInfo>& pieces, size_t index)
    {
        for (size_t i = index; i < pieces.size(); ++i)
        {
            pieces[i].line++;
        }
    }

    void TextAnalyzer::get_line(const std::vector<CTInfo>& pieces, int32_t line,
        int32_t& first, int32_t& last, int16_t& height)
    {
        height = 0;
        int32_t lowest = std::numeric_limits<int32_t>::max();
        int32_t highest = std::numeric_limits<int32_t>::min();

        for (size_t i = 0; i < pieces.size(); ++i)
        {
            if (pieces[i].line != line)
            {
                continue;
            }

            int32_t at = static_cast<int32_t>(i);
            lowest = std::min(lowest, at);
            highest = std::max(highest, at);
            height = std::max(height, pieces[i].height);
        }

        if (lowest == std::numeric_limits<int32_t>::max() ||
            highest == std::numeric_limits<int32_t>::min())
        {
            first = -1;
            last = -1;
            return;
        }

        first = lowest;
        last = highest;
        if (height < MIN_LINE_HEIGHT)
        {
            height = MIN_LINE_HEIGHT;
        }
    }

    void TextAnalyzer::adjust_height(std::vector<CTInfo>& pieces, int32_t first,
        int32_t last, int16_t y, int16_t lineheight)
    {
        for (int32_t i = first; i <= last; ++i)
        {
            CTInfo& piece = pieces[static_cast<size_t>(i)];
            // Pieces hang from the line's baseline rather than its top edge,
            // so an item icon and the words beside it sit on the same line
            // however much taller than them it is.
            int16_t drop = piece.select != -1 ? SELECT_DROP : 0;

            piece.top = static_cast<int16_t>(y + lineheight - piece.height + drop);
            piece.underline = static_cast<int16_t>(piece.height + 1);
        }
    }

    void TextAnalyzer::tokenize(const std::string& source, std::vector<CTInfo>& pieces,
        Text::Color defaultcolor, int16_t questid, bool listmode) const
    {
        // The reference client passes the quest a text belongs to so that the
        // tokens this client does not model yet - #B, #j, #Q, #D - can read
        // its record. Nothing that is modelled needs it: the progress tokens
        // name their own quest.
        (void)questid;

        const Player& player = Stage::get().get_player();
        const Questlog& quests = player.get_quests();

        Text::Color color = defaultcolor;
        bool bold = false;
        int32_t select = -1;
        int32_t line = 0;
        bool reward = false;

        size_t cursor = 0;
        for (;;)
        {
            std::string phrase = get_phrase(source, cursor);
            if (phrase.empty())
            {
                break;
            }

            char lead = phrase[0];
            if (lead == '\r' || lead == '\\')
            {
                if (!pieces.empty())
                {
                    pieces.back().linechange = true;
                }
                line++;
                continue;
            }

            // Colour and weight carry no argument and produce no piece; they
            // change what the pieces after them look like.
            if (phrase == "#k") { color = defaultcolor;  continue; }
            if (phrase == "#r") { color = Text::RED;     continue; }
            if (phrase == "#g") { color = Text::GREEN;   continue; }
            if (phrase == "#b") { color = Text::BLUE;    continue; }
            if (phrase == "#d") { color = Text::VIOLET;  continue; }
            if (phrase == "#e") { bold = true;           continue; }
            if (phrase == "#n") { bold = false;          continue; }
            if (phrase == "#l") { select = -1;           continue; }

            int32_t type = get_phrase_type(phrase);
            if (type == PT_REWARD)
            {
                reward = !reward;
                continue;
            }

            pieces.emplace_back();
            CTInfo& piece = pieces.back();
            piece.line = line;
            piece.select = select;
            piece.linechange = false;

            // Whether this piece is text, and so gets the current font,
            // colour and measured width once the token has had its say.
            bool istext = false;
            std::string body;

            switch (type)
            {
            case PT_LINK:
                // The marker an option is introduced by. The dialogue lays
                // its own options out and asks for no room to be kept.
                piece.type = CTInfo::LINK;
                piece.width = listmode ? 0 : LINK_MARKER_WIDTH;
                piece.height = make_text(" ", Text::A11M, defaultcolor).height();
                piece.select = get_parameter_no(phrase);
                select = piece.select;
                continue;

            case PT_FUNC_E:
            case PT_FUNC_I:
            case PT_FUNC_S:
            case PT_FUNC_K:
                piece.type = CTInfo::CONTROL;
                piece.funccode = type - PT_FUNC_E;
                continue;

            case PT_ITEM_ICON:
            {
                piece.type = CTInfo::ICON;
                int32_t id = get_parameter_no(phrase);
                // The colon is what marks a token as one the player can point
                // at, so it decides whether a tooltip has anything to show.
                piece.itemid = phrase.find(':') != std::string::npos ? id : 0;
                piece.text = item_name(id);
                // CItemInfo::GetItemIcon(id, 1, 0): the second argument picks
                // the framed inventory icon over the raw art, and the token
                // handler passes 1. The raw art is a different size and hangs
                // from a different origin, so it never lined up.
                piece.icon = ItemData::get(id).get_icon(false);
                break;
            }

            case PT_SKILL:
            {
                piece.type = CTInfo::ICON;
                int32_t id = get_parameter_no(phrase);
                const SkillData& skill = SkillData::get(id);
                piece.text = skill.get_name();
                piece.icon = skill.get_icon(SkillData::NORMAL);
                break;
            }

            case PT_PICTURE:
                piece.type = CTInfo::PICTURE;
                piece.icon = picture_at(phrase.substr(2));
                break;

            case PT_HEADING:
                piece.type = CTInfo::ICON;
                piece.icon = summary_heading(phrase.substr(2));
                break;

            case PT_PARAM:
            {
                istext = true;
                int32_t value = get_parameter_no(phrase);

                switch (phrase[1])
                {
                case 'h':
                    body = player.get_stats().get_name();
                    break;
                case 't':
                    body = item_name(value);
                    piece.itemid = phrase.find(':') != std::string::npos ? value : 0;
                    break;
                case 'z':
                    body = item_name(value);
                    piece.itemid = value;
                    break;
                case 'c':
                    body = std::to_string(
                        player.get_inventory().get_total_item_count(value));
                    break;
                case 'o':
                    body = string_table_name("Mob.img", std::to_string(value));
                    break;
                case 'p':
                    body = string_table_name("Npc.img", std::to_string(value));
                    break;
                case '@':
                    body = string_table_name("Npc.img", std::to_string(value));
                    piece.npcid = value;
                    break;
                case 'm':
                    body = map_name(value);
                    piece.mapid = value;
                    break;
                case 'q':
                    body = string_table_name("Skill.img", string_format::extend_id(value, 7));
                    break;
                case 'a':
                    body = quest_mob_count(quests, value);
                    break;
                case 'y':
                    body = QuestData::get(static_cast<int16_t>(value)).get_name();
                    break;
                case 'u':
                    body = quest_state_name(quests, static_cast<int16_t>(value));
                    break;
                case 'R':
                {
                    const std::string& progress =
                        quests.get_progress(static_cast<int16_t>(value));
                    body = progress.empty() ? "(empty)" : progress;
                    break;
                }
                default:
                    // A token this client does not model - #M and #x, which
                    // read per-character quest state the server never sends,
                    // and anything the reference client also ignores. Saying
                    // nothing beats printing its markup.
                    break;
                }
                break;
            }

            case PT_PLAIN:
            default:
                istext = true;
                body = phrase;
                break;
            }

            if (istext)
            {
                piece.type = CTInfo::TEXT;
                piece.font = bold ? Text::A11B : Text::A11M;
                piece.color = color;
                piece.text = body;
                piece.render = make_text(body, piece.font, piece.color);
                piece.width = piece.render.width();
                piece.height = piece.render.height();
                piece.reward = reward;
            }
            else
            {
                piece.width = piece.icon.width();
                piece.height = piece.icon.height();
                piece.reward = reward;
            }
        }

        if (!pieces.empty())
        {
            pieces.back().linechange = true;
        }
    }

    void TextAnalyzer::fit_lines(std::vector<CTInfo>& pieces) const
    {
        int16_t x = margin;
        int32_t current = pieces.front().line;

        for (size_t i = 0; i < pieces.size(); )
        {
            if (pieces[i].line != current)
            {
                current = pieces[i].line;
                x = margin;
            }

            int32_t type = pieces[i].type;
            if (type == CTInfo::ICON || type == CTInfo::PICTURE || type == CTInfo::LINK)
            {
                int16_t piecewidth = pieces[i].width;
                // A picture too wide to ever fit is placed anyway rather than
                // pushed down a line it would not fit on either.
                bool never_fits = piecewidth + margin > width;
                bool fits_here = x + piecewidth + margin <= width;

                if (!never_fits && !fits_here)
                {
                    separate_line_icon(pieces, i);
                    continue;
                }

                pieces[i].left = x;
                x = static_cast<int16_t>(x + piecewidth);
            }

            // A line that would start with a plural s, a possessive or a
            // stray comma reads as a mistake, so the break is moved back into
            // the piece before it.
            if (i > 0 && x == margin && pieces[i - 1].type == CTInfo::TEXT &&
                is_suffix(pieces[i].text) > 0)
            {
                int32_t at = is_dilimiter(pieces[i - 1].text);
                if (at > 0)
                {
                    const std::string& whole = pieces[i - 1].text;
                    std::string head = whole.substr(0, static_cast<size_t>(at));
                    std::string tail = trim_left(whole.substr(head.size()));

                    if (!tail.empty())
                    {
                        separate_line_text(pieces, i - 1, head, tail);

                        pieces[i].line++;
                        pieces[i].left = margin;
                        x = static_cast<int16_t>(margin + pieces[i].width);
                        for (size_t j = i; j < pieces.size(); ++j)
                        {
                            pieces[j].line--;
                        }

                        i++;
                    }
                }
                else
                {
                    // Nowhere to break it, so the whole piece before comes
                    // down to keep the fragment company.
                    pieces[i - 1].line++;
                    pieces[i].line = pieces[i - 1].line;
                    current = pieces[i].line;
                    pieces[i - 1].left = margin;
                    x = static_cast<int16_t>(x + pieces[i - 1].width);
                }
            }

            if (pieces[i].type == CTInfo::TEXT)
            {
                if (x + pieces[i].width + margin <= width)
                {
                    pieces[i].left = x;
                    x = static_cast<int16_t>(x + pieces[i].width);
                }
                else
                {
                    int32_t available = width - x - margin - WRAP_SLACK;
                    const std::string& whole = pieces[i].text;
                    size_t fits = calc_longest_text(pieces[i].render, available);

                    if (whole.size() > fits)
                    {
                        // Back up to a word boundary, unless the word is the
                        // whole line and has to be split somewhere.
                        size_t at = fits;
                        while (at > 0 && whole[at - 1] != ' ' && whole[at] != ' ')
                        {
                            at--;
                        }
                        if (at == 0 && x == margin)
                        {
                            at = fits;
                        }
                        fits = at;
                    }

                    std::string head = whole.substr(0, fits);
                    std::string tail = whole.substr(head.size());
                    separate_line_text(pieces, i, head, tail);

                    pieces[i].left = x;
                    x = static_cast<int16_t>(x + pieces[i].width);

                    if (head.empty())
                    {
                        // Nothing fit in what was left of the line, so the
                        // piece went down whole. Take it again from the start
                        // of its new line, where there is room to break it.
                        continue;
                    }
                }
            }

            i++;
        }
    }

    int16_t TextAnalyzer::stack_lines(std::vector<CTInfo>& pieces) const
    {
        int32_t lastline = 0;
        for (const CTInfo& piece : pieces)
        {
            lastline = std::max(lastline, piece.line);
        }

        int16_t emptyheight = make_text(" ", Text::A11M, Text::BLACK).height();
        int16_t y = margin;
        int32_t last = -1;

        for (int32_t line = 0; line <= lastline; ++line)
        {
            int32_t first = -1;
            int16_t lineheight = 0;
            get_line(pieces, line, first, last, lineheight);

            if (first != -1 && last != -1)
            {
                adjust_height(pieces, first, last, y, lineheight);
                y = static_cast<int16_t>(y + lineheight + LINE_GAP);
            }
            else
            {
                // A line with nothing on it still takes up a line's room.
                y = static_cast<int16_t>(y + emptyheight + LINE_GAP);
            }

            if (last != -1 && last >= static_cast<int32_t>(pieces.size()) - 1)
            {
                break;
            }
        }

        return static_cast<int16_t>(margin + y);
    }

    int16_t TextAnalyzer::analyze(const std::string& source, std::vector<CTInfo>& pieces,
        Text::Color defaultcolor, int16_t questid, bool listmode) const
    {
        pieces.clear();

        tokenize(source, pieces, defaultcolor, questid, listmode);
        if (pieces.empty())
        {
            return 0;
        }

        fit_lines(pieces);
        return stack_lines(pieces);
    }
}
