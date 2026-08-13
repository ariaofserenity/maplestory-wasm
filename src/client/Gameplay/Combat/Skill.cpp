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
#include "Skill.h"

#include "../../Character/SkillId.h"
#include "../../Data/BulletData.h"
#include "../../Data/SkillData.h"
#include "../../Util/Misc.h"
#include "../../Util/Randomizer.h"

#include "nlnx/node.hpp"
#include "nlnx/nx.hpp"

#include <algorithm>


namespace jrc
{
    namespace
    {
        const Randomizer randomizer;

        // How far a swing shoves a monster, in pixels. The reference client
        // reads this off the level's x, which the v83 knock-back skills do not
        // carry, so the distance is tuned rather than sourced.
        constexpr int16_t KNOCKBACK_DISTANCE = 70;

        // Show one of a skill's animations on the caster. The z the node carries
        // decides whether it is drawn in front of or behind the character.
        void show_on(Char& user, nl::node src)
        {
            if (!src)
                return;

            Animation animation = src;
            user.show_attack_effect(animation, src["z"]);
        }
    }

    // A skill that draws no projectile yet brings its own weapon afterimage is
    // swung, not fired: the afterimage is the trail the weapon leaves. Power
    // Knock-Back is the bow line's one example, and its afterimage lives under
    // swingT1/swingT3 - the two-handed swing stances - which is exactly what
    // the character has to be put into.
    bool swings_weapon(int32_t skillid)
    {
        return shoot_hides_bullet(skillid)
            && SkillData::get(skillid).has_afterimage();
    }

    int16_t resolve_shoot_reach(const Char& user)
    {
        // The client picks the mastery skill by whether the job is a Cygnus
        // one. Asking which of the two the character actually has a level in
        // comes to the same thing and does not need the job here, where only
        // the look and the skill levels of a remote character are known.
        ShootReach reach = default_shoot_reach(user.get_weapontype(), false);
        ShootReach cygnus = default_shoot_reach(user.get_weapontype(), true);

        for (int32_t bonus : { reach.bonusskill, cygnus.bonusskill })
        {
            int32_t level = bonus ? user.get_skilllevel(bonus) : 0;
            if (level > 0)
            {
                reach.base += static_cast<int16_t>(
                    SkillData::get(bonus).get_stats(level).reach
                );
                break;
            }
        }
        return reach.base;
    }

    Skill::Skill(int32_t id)
        : skillid(id)
    {
        std::string strid;
        if (skillid < 10000000)
        {
            strid = string_format::extend_id(skillid, 7);
        }
        else
        {
            strid = std::to_string(skillid);
        }

        sound = std::make_unique<SingleSkillSound>(strid);
    }

    bool Skill::has_projectile(const Char& user) const
    {
        // Asked per swing rather than cached, because which levels and brackets
        // apply depends on the character and answering it up front would mean
        // reading every one of them.
        const SkillData& data = SkillData::get(skillid);
        int32_t level = user.get_skilllevel(skillid);
        return data.get_ball(user.get_level(), level, false)
            || data.get_ball(user.get_level(), level, true);
    }

    void Skill::apply_useeffects(Char& user) const
    {
        // Iron Body and Magic Armor draw their shield over the character rather
        // than playing an animation from the skill's own node.
        switch (skillid)
        {
        case SkillId::IRON_BODY:
        case SkillId::MAGIC_ARMOR:
            user.show_iron_body();
            break;
        default:
            show_on(user, SkillData::get(skillid).get_effect(
                user.get_level(), user.get_skilllevel(skillid)
            ));
        }

        sound->play_use();
    }

    void Skill::apply_actions(Char& user, Attack::Type type) const
    {
        std::string action = SkillData::get(skillid).get_action(
            user.get_skilllevel(skillid), randomizer.next_int<size_t>(1024)
        );

        if (!action.empty())
        {
            user.attack(action);
            return;
        }

        // A skill that swings the bow always brings it overhead rather than
        // picking between the two swings a bare-handed one would.
        if (swings_weapon(skillid))
        {
            user.attack(Stance::SWINGT1);
            return;
        }

        // No action of the skill's own: swing the weapon as usual. A ranged
        // weapon used for a close attack has no proper stance for it, which is
        // what degenerate means here.
        bool degenerate;
        switch (user.get_weapontype())
        {
        case Weapon::BOW:
        case Weapon::CROSSBOW:
        case Weapon::CLAW:
        case Weapon::GUN:
            degenerate = type != Attack::RANGED;
            break;
        default:
            degenerate = false;
        }
        user.attack(degenerate);
    }

    void Skill::apply_stats(const Char& user, Attack& attack) const
    {
        attack.skill = skillid;

        int32_t level = user.get_skilllevel(skillid);
        const SkillData::Stats& stats = SkillData::get(skillid).get_stats(level);

        if (stats.fixdamage)
        {
            attack.fixdamage  = stats.fixdamage;
            attack.damagetype = Attack::DMG_FIXED;
        }
        else if (stats.matk)
        {
            attack.matk += stats.matk;
            attack.damagetype = Attack::DMG_MAGIC;
        }
        else
        {
            attack.mindamage *= stats.damage;
            attack.maxdamage *= stats.damage;
            attack.damagetype = Attack::DMG_WEAPON;
        }
        attack.critical  += stats.critical;
        attack.ignoredef += stats.ignoredef;
        attack.mobcount = stats.mobcount;
        attack.hrange   = stats.hrange;

        switch (attack.type)
        {
        case Attack::RANGED:
            attack.hitcount = stats.bulletcount;
            break;
        default:
            attack.hitcount = stats.attackcount;
        }

        if (!stats.range.empty())
            attack.range = stats.range;

        // Anything fired rather than swung reaches as far as the level's scalar
        // range says, or as far as the weapon throws when it says nothing. The
        // rect describes the band the shot sweeps and the area a burst covers,
        // not how far it travels - using it as the reach is what left skills
        // like Inferno hitting only what was already next to the caster.
        if (attack.type != Attack::CLOSE)
        {
            attack.reach = (stats.reach > 0)
                ? static_cast<int16_t>(stats.reach)
                : resolve_shoot_reach(user);
        }

        // A shoot skill picks its targets off the line the muzzle draws unless
        // it is one of the few the client points at the level's own rect.
        if (attack.type == Attack::RANGED)
        {
            attack.rectsearch = shoot_searches_rect(skillid);
            attack.splash = shoot_splashes(skillid);
            attack.vadjust = shoot_vertical_adjust(skillid);

            if (swings_weapon(skillid))
            {
                // A swing sweeps a box in front of the caster. The v95 tables
                // send Power Knock-Back to its level's rect, but the v83 data
                // has no lt/rb for it to use - what it does carry is a range
                // and a swing afterimage, so the box runs that far forward
                // over the band the trail covers. Written towards -x, the way
                // every rect in the game files is.
                Rectangle<int16_t> trail = user.get_afterimage().get_range();
                int16_t top = trail.t();
                int16_t bottom = trail.b();
                if (top == bottom)
                {
                    // No trail to measure: fall back to a body-height band, or
                    // the box would be flat and catch nothing.
                    top = -60;
                    bottom = 0;
                }

                attack.linear = false;
                attack.rectsearch = true;
                attack.splash = false;
                attack.range = {
                    static_cast<int16_t>(-attack.reach), 0, top, bottom
                };
                attack.reach = 0;
            }
            else if (attack.vadjust == 0 && shoot_area_from_data(skillid))
            {
                // The level's own rect replaces the line outright, reach and
                // all - these skills land where they are aimed rather than
                // carrying along a shot.
                attack.linear = false;
                attack.reach = 0;
            }
        }

        const bool projectile = has_projectile(user);

        if (projectile && !attack.bullet)
        {
            switch (skillid)
            {
            case SkillId::THREE_SNAILS:
                switch (level)
                {
                case 1:
                    attack.bullet = 4000019;
                    break;
                case 2:
                    attack.bullet = 4000000;
                    break;
                case 3:
                    attack.bullet = 4000016;
                    break;
                }
                break;
            default:
                attack.bullet = skillid;
            }
        }

        // A skill with no action of its own leaves the weapon's regular attack
        // animation in place, and therefore keeps its stance and reach too.
        if (SkillData::get(skillid).get_action(level, 0).empty())
        {
            attack.stance = user.get_look().get_stance();
            if (attack.type == Attack::CLOSE && !projectile)
            {
                attack.range = user.get_afterimage().get_range();
            }
        }
    }

    void Skill::apply_hiteffects(const AttackUser& user, Mob& target) const
    {
        // Outside a small hardcoded list the reference client picks a hit
        // animation at random for every skill, so that is what happens here.
        nl::node src = SkillData::get(skillid).get_random_hit(
            user.level, user.skilllevel
        );
        if (src)
        {
            Animation animation = src;
            target.show_effect(animation, src["pos"], src["z"], user.flip);
        }

        // A skill that swings the weapon to shove monsters off does exactly
        // that, away from the caster.
        if (swings_weapon(skillid))
        {
            target.push_back(KNOCKBACK_DISTANCE, !user.flip);
        }

        sound->play_hit();
    }

    Animation Skill::get_bullet(const Char& user, int32_t bulletid) const
    {
        nl::node ball = SkillData::get(skillid).get_ball(
            user.get_level(), user.get_skilllevel(skillid), user.getflip()
        );
        if (ball)
            return ball;

        // A skill without a ball throws the equipped ammunition.
        return BulletData::get(bulletid).get_animation();
    }

    SpecialMove::AreaEffect Skill::get_area_effect(const Char& user) const
    {
        const SkillData& data = SkillData::get(skillid);
        int32_t level = user.get_skilllevel(skillid);

        AreaEffect out;
        out.tile = data.get_tile();

        nl::node special = data.get_special(user.get_level(), level);
        if (!special)
            return out;

        // Which registrar a skill uses, where its box sits and how far the
        // caller shifts that box are all decided by skill id in the reference
        // client's attack handlers, so the same table is reproduced here rather
        // than guessed at from the node's shape.
        switch (skillid)
        {
        case SkillId::INFERNO:
        case SkillId::BLIZZARD_SNIPER:
            out.kind = EMIT_FALLING;
            out.anchor = ANCHOR_IMPACT;
            out.lifttop = 50;
            out.liftbottom = 170;
            break;
        case SkillId::ARROW_RAIN:
            out.kind = EMIT_FALLING;
            out.anchor = ANCHOR_CASTER;
            out.lifttop = 250;
            out.liftbottom = 250;
            break;
        case SkillId::ARROW_ERUPTION:
            out.kind = EMIT_FALLING;
            out.anchor = ANCHOR_PER_MOB;
            break;
        case SkillId::EXPLOSION:
            out.kind = EMIT_EXPLOSION;
            out.anchor = ANCHOR_CASTER;
            break;
        case SkillId::BIG_BANG_FP:
        case SkillId::BIG_BANG_IL:
        case SkillId::BIG_BANG_BISHOP:
        case SkillId::BLIZZARD_ARCHMAGE:
            out.kind = EMIT_EXPLOSION;
            out.anchor = ANCHOR_CASTER;
            out.lifttop = 40;
            out.liftbottom = 40;
            break;
        default:
            // Anything the client does not dispatch specially shows its special
            // once where the move landed.
            out.kind = EMIT_ONCE;
            out.special = special;
            out.lifetime = static_cast<uint16_t>(
                std::min(data.get_stats(level).duration, 60000)
            );
            return out;
        }

        for (int32_t i = 0; nl::node sub = special[std::to_string(i)]; i++)
        {
            out.variants.push_back(sub);
        }
        if (out.variants.empty())
        {
            out.kind = EMIT_NONE;
            return out;
        }

        out.count    = special["count"];
        out.interval = special["interval"];
        out.start    = special["start"];
        out.duration = special["duration"];
        out.travelx  = special["x"];
        out.travely  = special["y"];
        out.falltime = special["fall"];
        out.alpha    = special["a"];
        out.spawnbox = data.get_stats(level).range;
        out.lifetime = static_cast<uint16_t>(
            std::min(data.get_stats(level).duration, 60000)
        );

        if (out.count <= 0)
        {
            out.kind = EMIT_ONCE;
            out.special = special;
        }
        return out;
    }

    SpecialMove::CastKind Skill::get_cast_kind() const
    {
        const SkillData& data = SkillData::get(skillid);
        if (data.is_keydown())
            return CAST_KEYDOWN;
        if (data.has_prepare())
            return CAST_PREPARE;
        return CAST_INSTANT;
    }

    uint16_t Skill::get_prepare_time() const
    {
        return SkillData::get(skillid).get_prepare_time();
    }

    uint16_t Skill::get_ball_delay() const
    {
        return SkillData::get(skillid).get_ball_delay();
    }

    nl::node Skill::get_prepare_effect() const
    {
        return SkillData::get(skillid).get_prepare();
    }

    nl::node Skill::get_keydown_effect() const
    {
        return SkillData::get(skillid).get_keydown();
    }

    nl::node Skill::get_keydown_end_effect() const
    {
        return SkillData::get(skillid).get_keydown_end();
    }

    nl::node Skill::get_finish_effect() const
    {
        return SkillData::get(skillid).get_finish();
    }

    int32_t Skill::get_cooldown(int32_t level) const
    {
        return SkillData::get(skillid).get_stats(level).cooldown;
    }

    bool Skill::is_attack() const
    {
        return SkillData::get(skillid).is_attack();
    }

    bool Skill::is_skill() const
    {
        return true;
    }

    int32_t Skill::get_id() const
    {
        return skillid;
    }

    SpecialMove::ForbidReason Skill::can_use(int32_t level, Weapon::Type weapon,
        const Job& job, uint16_t hp, uint16_t mp, uint16_t bullets) const {

        const SkillData& data = SkillData::get(skillid);

        if (level <= 0 || level > data.get_masterlevel())
            return FBR_OTHER;

        if (!job.can_use(skillid))
            return FBR_OTHER;

        const SkillData::Stats& stats = data.get_stats(level);

        if (hp <= stats.hpcost)
            return FBR_HPCOST;

        if (mp < stats.mpcost)
            return FBR_MPCOST;

        // Both weapon fields are consulted, so a skill usable with either of two
        // weapon types is not rejected for the second one.
        if (!data.is_correct_weapon(weapon))
            return FBR_WEAPONTYPE;

        switch (weapon)
        {
        case Weapon::BOW:
        case Weapon::CROSSBOW:
        case Weapon::CLAW:
        case Weapon::GUN:
            return (bullets >= stats.bulletcost) ? FBR_NONE : FBR_BULLETCOST;
        default:
            return FBR_NONE;
        }
    }
}
