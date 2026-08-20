#include "Char.h"

#include <cmath>

#include <algorithm>
#include <math.h>

#include "../Constants.h"
#include "../Data/WeaponData.h"
#include "../Util/Misc.h"

#include "nlnx/nx.hpp"
#include "nlnx/node.hpp"

namespace jrc
{
    namespace
    {
        // Combo Attack's orbs. ShowSkillEffect hands the character's own vector
        // controller to Effect_SkillUse as the parent, so the ring is centred
        // on the character rather than hung over their head - it wraps the
        // body, which is why this sits at roughly mid-height rather than up
        // with the name tag and the chat balloon.
        constexpr int16_t COMBO_ORB_HEIGHT = 33;
        // The orbit itself. The original leaves each orb to a COM layer that
        // spins under its own Rotate, with no per-frame routine in the binary
        // to copy, so the span and the pace are chosen rather than lifted - but
        // the size is not free. state/0, the ring that sits on the character,
        // is 72x73 about a centred origin, so it reaches 36 pixels out; an orb
        // is 42x42 about its own centre, so it reaches 21. Anything under 57
        // has the orbs clipping the ring, which is what a radius of 36 was
        // doing. These clear it with a little room to spare, barely flattened,
        // so the ring keeps the middle to itself.
        constexpr float COMBO_ORB_RX = 56.0f;
        constexpr float COMBO_ORB_RY = 50.0f;
        constexpr float COMBO_ORB_PERIOD = 1800.0f;
        // The ring turns on the spot rather than travelling, so it gets a pace of
        // its own - slower than the orbits, so the two read as separate motions.
        constexpr float COMBO_RING_PERIOD = 2400.0f;
        constexpr float COMBO_PI = 3.14159265f;
    }

    Char::Char(int32_t o, const CharLook& lk, const std::string& name)
        : LivingObject(o, { false, true, true }),
          look(lk),
          namelabel({ Text::A13M, Text::CENTER, Text::WHITE, Text::NAMETAG, name }),
          partyhp(-1),
          partymaxhp(0) {}

    void Char::draw(double viewx, double viewy, float alpha) const
    {
        Point<int16_t> absp = phobj.get_absolute(viewx, viewy, alpha);

        effects.drawbelow(absp, alpha);

        Color color;
        if (invincible)
        {
            float phi = invincible.alpha() * 30;
            float rgb = 0.9f - 0.5f * std::abs(sinf(phi));
            color = { rgb, rgb, rgb, 1.0f };
        }
        else if (is_hidden())
        {
            color = { 1.0f, 1.0f, 1.0f, 0.4f };
        }
        else
        {
            color = Color::WHITE;
        }
        look.draw({ absp, color }, alpha);

        afterimage.draw(look.get_frame(), { absp, flip }, alpha);

        if (ironbody)
        {
            float ibalpha = ironbody.alpha();
            float scale   = 1.0f + ibalpha;
            float opacity = 1.0f - ibalpha;
            look.draw({ absp, scale, scale, opacity }, alpha);
        }

        for (auto& pet : pets)
        {
            if (pet.get_itemid())
            {
                pet.draw(viewx, viewy, alpha);
            }
        }

        if (partymaxhp > 0 && partyhp >= 0)
        {
            int32_t clamped_hp = std::clamp(partyhp, 0, partymaxhp);
            int16_t hp_percent = static_cast<int16_t>((100 * clamped_hp) / partymaxhp);
            partyhpbar.draw(absp + Point<int16_t>(0, -55), hp_percent);
        }

        namelabel.draw(absp);
        chatballoon.draw(absp - Point<int16_t>(0, 85));

        effects.drawabove(absp, alpha);

        if (comboorbs > 0 && !comboorbsprites.empty())
        {
            // The buff counts from one, so a value of one is the combo running
            // with nothing banked - that shows the ring alone, and each orb
            // after it takes the next sprite along.
            auto orbs = static_cast<size_t>(comboorbs) - 1;

            // Centred on the character, the way the parent vector puts it.
            Point<int16_t> centre = absp - Point<int16_t>(
                0, COMBO_ORB_HEIGHT
            );

            comboorbsprites[0].draw({ comboringangle, centre, false, 1.0f });

            size_t shown = std::min(orbs, comboorbsprites.size() - 1);
            for (size_t i = 0; i < shown; i++)
            {
                // Spread evenly round the orbit and carried along by the shared
                // angle, so they hold their spacing as they turn.
                float step = static_cast<float>(2.0 * COMBO_PI * i / shown);
                float theta = comboangle + step;

                auto ox = static_cast<int16_t>(COMBO_ORB_RX * std::cos(theta));
                auto oy = static_cast<int16_t>(COMBO_ORB_RY * std::sin(theta));

                comboorbsprites[i + 1].draw({ centre + Point<int16_t>(ox, oy) });
            }
        }

        for (auto& number : damagenumbers)
        {
            number.draw(viewx, viewy, alpha);
        }
    }

    bool Char::update(const Physics& physics, float speed)
    {
        damagenumbers.remove_if([](DamageNumber& number) {
            return number.update();
        });
        effects.update();
        chatballoon.update();
        invincible.update();
        ironbody.update();

        if (comboorbs > 0)
        {
            constexpr float TWO_PI = static_cast<float>(2.0 * COMBO_PI);
            constexpr float TURN = TWO_PI * Constants::TIMESTEP / COMBO_ORB_PERIOD;
            constexpr float SPIN = TWO_PI * Constants::TIMESTEP / COMBO_RING_PERIOD;

            comboangle += TURN;
            if (comboangle > TWO_PI)
            {
                comboangle -= TWO_PI;
            }

            comboringangle += SPIN;
            if (comboringangle > TWO_PI)
            {
                comboringangle -= TWO_PI;
            }
        }

        for (auto& pet : pets)
        {
            if (pet.get_itemid())
            {
                switch (state)
                {
                case LADDER:
                case ROPE:
                    pet.set_stance(PetLook::HANG);
                    break;
                case SWIM:
                    pet.set_stance(PetLook::FLY);
                    break;
                default:
                    if (pet.get_stance() == PetLook::HANG || pet.get_stance() == PetLook::FLY)
                        pet.set_stance(PetLook::STAND);
                }
                pet.update(physics, get_position());
            }
        }

        uint16_t stancespeed = 0;
        if (speed >= 1.0f / Constants::TIMESTEP)
        {
            stancespeed = static_cast<uint16_t>(Constants::TIMESTEP * speed);
        }
        afterimage.update(look.get_frame(), stancespeed);
        return look.update(stancespeed);
    }

    float Char::get_stancespeed() const
    {
        if (attacking)
            return get_real_attackspeed();

        switch (state)
        {
        case WALK:
            return static_cast<float>(std::abs(phobj.hspeed));
        case LADDER:
        case ROPE:
            return static_cast<float>(std::abs(phobj.vspeed));
        default:
            return 1.0f;
        }
    }

    float Char::get_real_attackspeed() const
    {
        int8_t speed = get_integer_attackspeed();
        return 1.7f - static_cast<float>(speed) / 10;
    }

    uint16_t Char::get_attackdelay(size_t no) const
    {
        uint8_t first_frame = afterimage.get_first_frame();
        uint16_t delay = look.get_attackdelay(no, first_frame);
        float fspeed = get_real_attackspeed();
        return static_cast<uint16_t>(delay / fspeed);
    }

    uint16_t Char::get_attackdelay(size_t no, size_t total_hits) const
    {
        // An action usually marks more hittable frames than the skill has hits
        // (Brandish: five frames, two hits), so asking for indices 0..n-1 packs
        // every hit into the opening of the swing. Spread them over the whole
        // action instead, one per visible strike.
        size_t frames = look.get_attackframecount();
        if (frames > 1 && total_hits > 1 && total_hits <= frames)
        {
            return get_attackdelay(no * (frames - 1) / (total_hits - 1));
        }
        return get_attackdelay(no);
    }

    int8_t Char::update(const Physics& physics)
    {
        update(physics, 1.0f);
        return get_layer();
    }

    int8_t Char::get_layer() const
    {
        return is_climbing() ? 7 : phobj.fhlayer;
    }

    void Char::show_attack_effect(Animation toshow, int8_t z)
    {
        float attackspeed = get_real_attackspeed();
        effects.add(toshow, { flip }, z, attackspeed);
    }

    void Char::show_attack_effect(Animation toshow, int8_t z, Point<int16_t> offset)
    {
        float attackspeed = get_real_attackspeed();
        effects.add(toshow, { offset, flip }, z, attackspeed);
    }

    void Char::show_effect_id(CharEffect::Id toshow)
    {
        effects.add(
            chareffects[toshow]
        );
    }

    void Char::set_combo_orbs(int32_t orbs)
    {
        comboorbs = orbs;
    }

    void Char::show_iron_body()
    {
        ironbody.set_for(500);
    }

    void Char::show_damage(int32_t damage)
    {
        int16_t start_y = phobj.get_y() - 60;
        int16_t x = phobj.get_x() - 10;
        damagenumbers.emplace_back(DamageNumber::TOPLAYER, damage, start_y, x);

        look.set_alerted(5000);
        invincible.set_for(2000);
    }

    void Char::show_recovery(int32_t amount)
    {
        int16_t start_y = phobj.get_y() - 60;
        int16_t x = phobj.get_x() - 10;
        damagenumbers.emplace_back(DamageNumber::RECOVERY, amount, start_y, x);
    }

    void Char::speak(const std::string& line)
    {
        chatballoon.change_text(line);
    }

    void Char::set_party_hp(int32_t hp, int32_t max_hp)
    {
        if (max_hp <= 0)
        {
            clear_party_hp();
            return;
        }

        partymaxhp = max_hp;
        partyhp = std::clamp(hp, 0, max_hp);
    }

    void Char::clear_party_hp()
    {
        partyhp = -1;
        partymaxhp = 0;
    }

    void Char::change_look(Maplestat::Id stat, int32_t id)
    {
        switch (stat)
        {
        case Maplestat::SKIN:
            look.set_body(id);
            break;
        case Maplestat::FACE:
            look.set_face(id);
            break;
        case Maplestat::HAIR:
            look.set_hair(id);
            break;
        default:
            break;
        }
    }

    void Char::set_state(uint8_t statebyte)
    {
        if (statebyte % 2 == 1)
        {
            set_direction(false);
            statebyte -= 1;
        }
        else
        {
            set_direction(true);
        }

        Char::State newstate = by_value(statebyte);
        set_state(newstate);
    }

    void Char::set_expression(int32_t expid)
    {
        Expression::Id expression = Expression::byaction(expid);
        look.set_expression(expression);
    }

    void Char::attack(const std::string& action)
    {
        look.set_action(action);

        attacking = true;
        look.set_alerted(5000);
    }

    void Char::attack(Stance::Id stance)
    {
        look.attack(stance);

        attacking = true;
        look.set_alerted(5000);
    }

    void Char::attack(bool degenerate)
    {
        look.attack(degenerate);

        attacking = true;
        look.set_alerted(5000);
    }

    void Char::set_afterimage(int32_t skill_id)
    {
        int32_t weapon_id = look.get_equips().get_weapon();
        if (weapon_id <= 0)
            return;

        const WeaponData& weapon = WeaponData::get(weapon_id);

        std::string stance_name = Stance::names[look.get_stance()];
        int16_t weapon_level = weapon.get_equipdata().get_reqstat(Maplestat::LEVEL);
        const std::string& ai_name = weapon.get_afterimage();
        afterimage = { skill_id, ai_name, stance_name, weapon_level };
    }

    const Afterimage& Char::get_afterimage() const
    {
        return afterimage;
    }

    void Char::set_direction(bool f)
    {
        flip = f;
        look.set_direction(f);
    }

    void Char::set_state(State st)
    {
        state = st;

        Stance::Id stance = Stance::by_state(state);
        look.set_stance(stance);
    }

    void Char::add_pet(uint8_t index, int32_t iid, const std::string& name,
        int32_t uniqueid, Point<int16_t> pos, uint8_t stance, int32_t fhid) {

        if (index > 2)
            return;

        pets[index] = PetLook(iid, name, uniqueid, pos, stance, fhid);
    }

    void Char::remove_pet(uint8_t index, bool hunger)
    {
        if (index > 2)
            return;

        pets[index] = PetLook();
        if (hunger)
        {

        }
    }

    bool Char::is_invincible() const
    {
        return invincible == true;
    }

    bool Char::is_sitting() const
    {
        return state == SIT;
    }

    bool Char::is_climbing() const
    {
        return state == LADDER || state == ROPE;
    }

    bool Char::is_standing() const
    {
        return state == STAND;
    }

    bool Char::is_twohanded() const
    {
        return look.get_equips().is_twohanded();
    }

    Weapon::Type Char::get_weapontype() const
    {
        int32_t weapon_id = look.get_equips().get_weapon();
        if (weapon_id <= 0)
            return Weapon::NONE;

        return WeaponData::get(weapon_id)
            .get_type();
    }

    bool Char::inrange(Point<int16_t> cursorpos, Point<int16_t> viewpos) const
    {
        if (!active)
        {
            return false;
        }

        Rectangle<int16_t> bounds = look.get_body_rect();
        if (bounds.straight())
        {
            return false;
        }

        Point<int16_t> absp = get_position() + viewpos;
        return Rectangle<int16_t>(bounds.getlt() + absp, bounds.getrb() + absp)
            .contains(cursorpos);
    }

    bool Char::getflip() const
    {
        return flip;
    }

    std::string Char::get_name() const
    {
        return namelabel.get_text();
    }

    CharLook& Char::get_look()
    {
        return look;
    }

    const CharLook& Char::get_look() const
    {
        return look;
    }

    PhysicsObject& Char::get_phobj()
    {
        return phobj;
    }

    const PhysicsObject& Char::get_phobj() const
    {
        return phobj;
    }


    void Char::init()
    {
        CharLook::init();

        nl::node src = nl::nx::effect["BasicEff.img"];
        for (auto iter : CharEffect::PATHS)
        {
            chareffects.emplace(iter.first, src.resolve(iter.second));
        }

        // Combo Attack keeps its orb art with the skill rather than in
        // BasicEff, one numbered child per count the buff can reach.
        nl::node combosrc = nl::nx::skill["111.img"]["skill"]["1111002"]["state"];
        for (nl::node frame : combosrc)
        {
            comboorbsprites.emplace_back(frame);
        }
    }

    EnumMap<CharEffect::Id, Animation> Char::chareffects;
    std::vector<Texture> Char::comboorbsprites;
}
