//////////////////////////////////////////////////////////////////////////////
// This file is part of the Journey MMORPG client                           //
//////////////////////////////////////////////////////////////////////////////
#pragma once

#include "Keyboard.h"

#include "../Graphics/Texture.h"

#include <cstdint>
#include <map>
#include <vector>

namespace jrc
{
    // Key bindings are edited from two places: the key config window, which
    // collects changes and sends them when the player confirms, and the
    // quickslot on the status bar, which applies a change the moment an icon is
    // dropped on it. Both need the same answers to what a binding looks like,
    // which keys one key cap stands for, and how a set of bindings reaches the
    // server, so those answers live here instead of in either of them.
    namespace KeyBinding
    {
        // Artwork a binding is drawn with. Skills and items carry icons of their
        // own; everything else is one of the fixed actions, which are pictured
        // in the key config artwork. An unbound mapping yields an empty texture.
        Texture icon_for(const Keyboard::Mapping& mapping);

        // Every key a single key cap stands for. The modifiers sit on both sides
        // of a keyboard but appear once in the ui, so binding one has to bind
        // its twin as well; otherwise the side the player did not aim at keeps
        // the old action and the two halves of the key disagree.
        std::vector<int32_t> paired_keys(int32_t keycode);

        // Makes the given bindings the current ones. Only keys that actually
        // differ are sent, so this can be handed the full set every time without
        // filling the wire with bindings that did not move.
        void apply(const std::map<int32_t, Keyboard::Mapping>& mappings);

        // Binds the mapping to the key, first taking it off whatever key held it
        // before so it is never reachable from two places at once.
        void bind(int32_t keycode, const Keyboard::Mapping& mapping);

        // Takes the mapping off every key currently holding it.
        void unbind(const Keyboard::Mapping& mapping);
    }
}
