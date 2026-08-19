//////////////////////////////////////////////////////////////////////////////
// This file is part of the Journey MMORPG client                           //
//////////////////////////////////////////////////////////////////////////////
#include "KeyBinding.h"

#include "KeyConfig.h"
#include "UI.h"
#include "UITypes/UIKeyConfig.h"

#include "../Data/ItemData.h"
#include "../Data/SkillData.h"
#include "../Net/Packets/PlayerPackets.h"

#include "nlnx/nx.hpp"

#include <string>
#include <tuple>

namespace jrc
{
    namespace KeyBinding
    {
        namespace
        {
            void erase_mapping_value(std::map<int32_t, Keyboard::Mapping>& mappings, const Keyboard::Mapping& mapping)
            {
                for (auto it = mappings.begin(); it != mappings.end(); )
                {
                    it = it->second == mapping ? mappings.erase(it) : std::next(it);
                }
            }

            Keyboard::Mapping mapping_at(const std::map<int32_t, Keyboard::Mapping>& mappings, int32_t keycode)
            {
                auto it = mappings.find(keycode);
                return it != mappings.end() ? it->second : Keyboard::Mapping();
            }
        }

        Texture icon_for(const Keyboard::Mapping& mapping)
        {
            switch (mapping.type)
            {
            case KeyType::SKILL:
                return SkillData::get(mapping.action).get_icon(SkillData::NORMAL);
            // Cash items are ordinary items as far as the artwork goes; only
            // where they are stored differs, which does not matter here.
            case KeyType::ITEM:
            case KeyType::CASH:
                return ItemData::get(mapping.action).get_icon(false);
            case KeyType::MENU:
            case KeyType::ACTION:
            case KeyType::FACE:
            {
                // The legacy UIWindow node carries the original korean artwork,
                // so the localised set in UIWindow2 is the one to read, matching
                // what the key config window itself draws.
                nl::node icons = nl::nx::ui["UIWindow2.img"]["KeyConfig"]["icon"];
                nl::node icon = icons[std::to_string(mapping.action)];

                return icon ? Texture(icon) : Texture();
            }
            default:
                return {};
            }
        }

        std::vector<int32_t> paired_keys(int32_t keycode)
        {
            switch (keycode)
            {
            case KeyConfig::LEFT_CONTROL:
            case KeyConfig::RIGHT_CONTROL:
                return { KeyConfig::LEFT_CONTROL, KeyConfig::RIGHT_CONTROL };
            case KeyConfig::LEFT_ALT:
            case KeyConfig::RIGHT_ALT:
                return { KeyConfig::LEFT_ALT, KeyConfig::RIGHT_ALT };
            case KeyConfig::LEFT_SHIFT:
            case KeyConfig::RIGHT_SHIFT:
                return { KeyConfig::LEFT_SHIFT, KeyConfig::RIGHT_SHIFT };
            default:
                return { keycode };
            }
        }

        void apply(const std::map<int32_t, Keyboard::Mapping>& mappings)
        {
            Keyboard& keyboard = UI::get().get_keyboard();

            std::vector<std::tuple<int32_t, uint8_t, int32_t>> updates;
            std::map<int32_t, Keyboard::Mapping> changed;

            for (int32_t keycode = 1; keycode < KeyConfig::LENGTH; ++keycode)
            {
                Keyboard::Mapping wanted = mapping_at(mappings, keycode);
                if (wanted != keyboard.get_maple_mapping(keycode))
                {
                    updates.emplace_back(keycode, static_cast<uint8_t>(wanted.type), wanted.action);
                    changed[keycode] = wanted;
                }
            }

            if (updates.empty())
            {
                return;
            }

            ChangeKeyMapPacket(updates).dispatch();

            for (const auto& update : updates)
            {
                auto keycode = static_cast<uint8_t>(std::get<0>(update));
                uint8_t type = std::get<1>(update);

                if (type == KeyType::NONE)
                {
                    keyboard.remove(keycode);
                }
                else
                {
                    keyboard.assign(keycode, type, std::get<2>(update));
                }
            }

            // The key config window keeps its own copy of the bindings, which it
            // edits before sending. A change made anywhere else has to reach it,
            // or the two disagree until it is closed and opened again. Handing
            // it back its own changes is harmless, so this needs no test for
            // where the change came from.
            if (auto keyconfig = UI::get().get_element<UIKeyConfig>())
            {
                keyconfig->adopt_mappings(changed);
            }
        }

        void bind(int32_t keycode, const Keyboard::Mapping& mapping)
        {
            if (mapping.type == KeyType::NONE)
            {
                return;
            }

            std::map<int32_t, Keyboard::Mapping> mappings = UI::get().get_keyboard().get_maplekeys();

            // Whatever the key held is displaced rather than swapped with the
            // binding's old home: a key can only show one thing, and the player
            // dropped this one on it deliberately.
            erase_mapping_value(mappings, mapping);

            for (int32_t key : paired_keys(keycode))
            {
                mappings[key] = mapping;
            }

            apply(mappings);
        }

        void unbind(const Keyboard::Mapping& mapping)
        {
            if (mapping.type == KeyType::NONE)
            {
                return;
            }

            std::map<int32_t, Keyboard::Mapping> mappings = UI::get().get_keyboard().get_maplekeys();
            erase_mapping_value(mappings, mapping);

            apply(mappings);
        }
    }
}
