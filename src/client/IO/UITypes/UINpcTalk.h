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

#include "../Components/QuestSummary.h"

#include "../../Graphics/Text.h"
#include "../../Graphics/Texture.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace jrc
{
    class UINpcTalk : public UIElement
    {
    public:
        static constexpr Type TYPE = NPCTALK;
        static constexpr bool FOCUSED = false;
        static constexpr bool TOGGLED = true;

        UINpcTalk();

        void draw(float inter) const override;
        bool is_in_range(Point<int16_t> cursorpos) const override;
        void send_key(int32_t keycode, bool pressed, bool escape) override;
        void send_scroll(double yoffset) override;
        CursorResult send_cursor(bool clicked, Point<int16_t> cursorpos) override;

        void change_text(
            int32_t npcid,
            int8_t msgtype,
            int16_t style,
            bool has_navigation_flags,
            int8_t speaker,
            const std::string& text
        );

        // Which buttons a locally driven page offers.
        enum class LocalPrompt
        {
            // Advance through a page of text.
            TEXT,
            // Ask the player to accept or decline.
            YES_NO
        };

        // What a locally driven page does when the player presses a button.
        // Any of these may be left empty, in which case the corresponding
        // button simply closes the window.
        struct LocalCallbacks
        {
            std::function<void()> advance;
            // Called with the index of the option the player picked, for a
            // page whose text carries #L..#l options.
            std::function<void(int32_t)> select;
            std::function<void()> accept;
            std::function<void()> decline;
            std::function<void()> dismiss;
        };

        // Show a page the client itself is driving rather than the server.
        //
        // Quest conversations are stored in the game files, so for those the
        // reference client plays the dialogue locally and only reports the
        // outcome. Such a page must not answer the server, which is what
        // separates this from change_text: nothing is sent, and the callbacks
        // decide what happens next. A page that is not the last one shows a
        // Next button; the last one shows OK or the accept/decline pair.
        void show_local(
            int32_t npcid,
            const std::string& text,
            LocalPrompt prompt,
            bool has_next,
            LocalCallbacks callbacks
        );

    protected:
        Button::State button_pressed(uint16_t buttonid) override;

    private:
        enum class DialogueMode
        {
            TEXT,
            YES_NO,
            ACCEPT_DECLINE,
            SELECTION,
            UNKNOWN
        };

        // Lay the window out around one page of text. Shared by the server
        // driven and the locally driven paths, which differ only in where the
        // page came from and what the buttons do.
        void apply_dialogue(
            int32_t npcid,
            int8_t speaker,
            const std::string& text,
            DialogueMode mode,
            bool has_prev,
            bool has_next
        );

        // Run one of the local callbacks. Closes the window unless the
        // callback put a new page up, which is how a multi-page conversation
        // keeps the same window open.
        void run_local(const std::function<void()>& callback);

        void parse_selections(const std::string& text, std::string& rendered_text,
            std::string& raw_text);
        static std::string strip_npc_tokens(const std::string& text);
        static std::string replace_macros(const std::string& source);
        static DialogueMode resolve_dialogue_mode(int8_t msgtype, bool has_navigation_flags);
        void refresh_selection_styles();
        int16_t get_selection_text_height() const;
        int16_t get_dialogue_content_height() const;
        int16_t get_dialogue_text_y() const;
        int16_t get_options_start_y() const;
        int32_t get_option_at(Point<int16_t> relative) const;
        // Height of the page body, whichever of the two renderers drew it.
        int16_t get_body_height() const;

        enum Buttons
        {
            OK,
            NEXT,
            PREV,
            END,
            YES,
            NO
        };

        Texture top;
        Texture fill;
        Texture bottom;
        Texture nametag;

        Text text;
        // Used in place of the plain text when the page names pictures, which
        // quest dialogue does whenever it talks about an item.
        QuestSummary richtext;
        bool use_richtext;
        Texture speaker;
        Text name;
        int16_t height;
        int16_t vtile;
        DialogueMode dialogue_mode;
        bool slider;

        int8_t type;
        bool end_confirms_dialogue;
        // Set while the window is showing a page the client drives itself.
        bool local;
        LocalCallbacks local_callbacks;
        // Counts the pages put up locally, so a callback that shows the next
        // page can be told apart from one that ends the conversation.
        uint64_t local_epoch;
        std::string prompttext;
        std::vector<std::string> selection_texts;
        std::vector<Text> selection_labels;
        std::vector<int32_t> selections;
        int32_t selected;
        int32_t hovered_selection;
        int16_t scroll_offset;
        int16_t max_scroll;
    };
}
