#pragma once
#include "../UIDragElement.h"

#include "../Components/Slider.h"

#include "../../Character/Look/CharLook.h"
#include "../../Graphics/Text.h"
#include "../../Graphics/Texture.h"

#include <string>
#include <vector>

namespace jrc
{
    // The 'Character Info' window, opened by double clicking a character on
    // the map. Built from UIWindow2.img/UserInfo/character.
    class UIUserInfo : public UIDragElement<PosUSERINFO>
    {
    public:
        static constexpr Type TYPE = USERINFO;
        static constexpr bool FOCUSED = false;
        // The server sends a fresh dump every time, so the window is always
        // rebuilt instead of toggling the previous target off.
        static constexpr bool TOGGLED = false;

        // Everything the CHAR_INFO packet tells us about the target.
        struct Info
        {
            int32_t     cid     = 0;
            std::string name;
            uint16_t    level   = 0;
            int16_t     job     = 0;
            int16_t     fame    = 0;
            std::string guild;
            std::string alliance;
            bool        married = false;
        };

        UIUserInfo(const Info& info, const CharLook& look);

        void draw(float alpha) const override;
        void update() override;
        void send_key(int32_t keycode, bool pressed, bool escape) override;
        void send_scroll(double yoffset) override;
        CursorResult send_cursor(bool clicked, Point<int16_t> cursorpos) override;
        bool remove_cursor(bool clicked, Point<int16_t> cursorpos) override;

        // Apply a fame change the server confirmed, as long as it is about
        // the character this window is showing.
        void update_fame(const std::string& target, int16_t fame);

        // Who the window is currently showing, which is how a caller can
        // tell whether it already has the character it wants on screen.
        int32_t get_cid() const;

    protected:
        Button::State button_pressed(uint16_t buttonid) override;

    private:
        // Fame can only be given by, and to, a character of at least this
        // level, which is what greys the two fame buttons out.
        static constexpr uint16_t FAME_MIN_LEVEL = 15;

        enum Buttons
        {
            BT_CLOSE,
            BT_FAMILY,
            BT_PARTY,
            BT_TRADE,
            BT_ITEM,
            BT_WISH,
            BT_FAME_UP,
            BT_FAME_DOWN,
            BT_PERSONALITY,
            BT_COLLECT,
            BT_RIDE,
            BT_PET,
            NUM_BUTTONS
        };

        enum Label
        {
            NAME,
            LEVEL,
            JOB,
            FAME,
            GUILD,
            ALLIANCE,
            NUM_LABELS
        };

        // One line of the item list: what the target wears in a single slot.
        struct EquipEntry
        {
            Texture icon;
            Text name;
            Text reqlevel;
        };

        void send_fame(bool raise);
        // Collect the target's equipment, in slot order.
        void load_equips();
        void load_own_equips();
        void load_remote_equips();
        void add_equip(int32_t itemid);
        void toggle_equips();
        void draw_equips() const;

        Info info;
        CharLook look;
        Texture gender;
        Texture married;

        Text labels[NUM_LABELS];
        Point<int16_t> labeloffsets[NUM_LABELS];

        // The item list is a panel of its own that the game hangs off the
        // right edge of this window, so it moves with it when dragged.
        bool show_equips;
        Texture equipbg;
        Texture equipbg2;
        std::vector<EquipEntry> equips;
        int16_t equipoffset;
        Slider equipslider;
    };
}
