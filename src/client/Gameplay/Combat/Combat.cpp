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
#include "Combat.h"

#include "../../Character/SkillId.h"
#include "../../Data/SkillData.h"
#include "../../IO/KeyAction.h"
#include "../../IO/Messages.h"
#include "../../Net/Packets/AttackAndSkillPackets.h"

#include "nlnx/nx.hpp"

#include "../../Util/Randomizer.h"

#include <algorithm>
#include <cmath>

namespace jrc
{
    namespace
    {
        // Teleport cooldown in update ticks (~44 * 8ms ≈ 350ms).
        constexpr int16_t TELEPORT_COOLDOWN_TICKS = 44;
        // Default teleport range when skill data has no hrange.
        constexpr int16_t TELEPORT_DEFAULT_RANGE = 130;
        // Step size (px) for horizontal sweep collision checks.
        constexpr int16_t TELEPORT_STEP = 8;
        // Max ground-height delta (px) before treating a step as a wall.
        constexpr int16_t TELEPORT_WALL_THRESHOLD = 35;

        // Multi-target close attacks resolve their targets in sequence rather
        // than on a single frame. TryDoingMeleeAttack's cadence has not been
        // reversed, so the step is still a tuned constant.
        constexpr uint16_t TARGET_STAGGER_MS = 75;

        // How long a shot takes to cover a pixel. Both reversed attack paths
        // multiply the horizontal gap to the target by this to work out when
        // that target is hit.
        constexpr double SHOT_MS_PER_PIXEL = 1.5;

        // What the magic path charges per target when the skill is not one of
        // the ids it singles out.
        constexpr int32_t MAGIC_TARGET_STEP = 50;

        // Ids the two reversed paths single out when they decide when a target
        // is hit. Only some of these have an established name, and the client
        // lists them numerically too.

        // Shoot path: hit a flat 400 ms into the swing, every target at once.
        constexpr int32_t SHOOT_FLAT_400_WA = 13111000;
        // Shoot path: targets after the first trail it by 200 ms.
        constexpr int32_t SHOOT_TRAIL_200_WH = 33101007;
        // Shoot path: hit the moment the swing's own frame comes up.
        constexpr int32_t SHOOT_ON_EVENT_MECH = 35121012;
        // Magic path: hit the moment the cast's own frame comes up.
        constexpr int32_t MAGIC_ON_EVENT[] = {
            2121007, 2221007, 2321008, 12111003, 32121004
        };
        // Magic path: hit as the spell reaches each target.
        constexpr int32_t MAGIC_TRAVELS[] = {
            2101004, 2121003, 12111006, 22101000, 32111003
        };

        template <size_t N>
        bool listed(const int32_t (&ids)[N], int32_t skillid)
        {
            return std::find(std::begin(ids), std::end(ids), skillid)
                != std::end(ids);
        }

        // How long after a swing lands its final attack goes off, as a fraction
        // of the delay the swing itself took. The client places it a third of
        // the way from that frame to the end of the action, and an action runs
        // roughly twice as long as it takes to land.
        constexpr int32_t FINAL_ATTACK_DELAY_NUM = 4;
        constexpr int32_t FINAL_ATTACK_DELAY_DEN = 3;

        // How far apart a summon's swing spreads its targets. The client stacks
        // this on top of the delay the skill's own data states.
        constexpr int32_t SUMMON_TARGET_STAGGER = 50;

        const Randomizer randomizer;

        // Delay before a given target is hit. Divided by the attack speed
        // factor for the same reason Char::get_attackdelay is, so that faster
        // weapons sweep their targets proportionally faster.
        uint16_t target_stagger(const Char& user, size_t target_index)
        {
            return static_cast<uint16_t>(
                target_index * TARGET_STAGGER_MS / user.get_real_attackspeed()
            );
        }
    }

    Combat::Combat(Player& in_player,
        MapChars& in_chars, MapMobs& in_mobs,
        Physics& in_physics) :
        player(in_player),
        chars(in_chars),
        mobs(in_mobs),
        physics(in_physics),
        attackresults([&](const AttackResult& attack) {
            apply_attack(attack);
        }),
        bulleteffects([&](const BulletEffect& effect) {
            apply_bullet_effect(effect);
        }),
        damageeffects([&](const DamageEffect& effect) {
            apply_damage_effect(effect);
        }),
        specialeffects([&](const SpecialEffect& effect) {
            apply_special_effect(effect);
        }) {}

    void Combat::draw(double viewx, double viewy, float alpha) const
    {
        for (auto& be : bullets)
        {
            be.bullet.draw(viewx, viewy, alpha);
        }
        // Ground effects sit under everything else that is drawn here.
        for (auto& ge : groundeffects)
        {
            ge.draw(viewx, viewy, alpha);
        }
        for (auto& me : movingeffects)
        {
            me.draw(viewx, viewy, alpha);
        }
        for (auto& dn : damagenumbers)
        {
            dn.draw(viewx, viewy, alpha);
        }
    }

    void Combat::update()
    {
        if (teleport_cooldown > 0)
            teleport_cooldown--;

        for (auto iter = cooldowns.begin(); iter != cooldowns.end(); )
        {
            iter->second -= Constants::TIMESTEP;
            iter = (iter->second <= 0) ? cooldowns.erase(iter) : std::next(iter);
        }

        update_cast();
        update_final_attack();

        attackresults.update();
        bulleteffects.update();
        damageeffects.update();
        specialeffects.update();

        bullets.remove_if([&](BulletEffect& mb) {
            // A homing shot re-aims each tick; a straight one keeps the line it
            // was loosed along.
            if (!mb.straight && mobs.contains(mb.target_oid))
                mb.target = mobs.get_mob_body_position(mb.target_oid);

            return mb.bullet.update(mb.target);
        });
        movingeffects.remove_if([](MovingEffect& me) {
            return me.update();
        });
        groundeffects.remove_if([](GroundEffect& ge) {
            return ge.update();
        });
        damagenumbers.remove_if([](DamageNumber& dn) {
            return dn.update();
        });
    }

    void Combat::clear()
    {
        teleport_cooldown = 0;
        // A cast cannot survive the map it was started on.
        cast = {};
        // Neither can a follow-up attack aimed at monsters on it.
        finalattack_skill = 0;
        // Map-anchored effects mean nothing on the next map.
        movingeffects.clear();
        groundeffects.clear();
    }

    bool Combat::use_move(int32_t move_id)
    {
        if (!player.can_attack())
            return false;

        // A move already charging owns the character until it is let go of.
        if (cast.active)
            return false;

        if (is_teleport_skill(move_id) && teleport_cooldown > 0)
            return false;

        const SpecialMove& move = get_move(move_id);

        // A passive skill has nothing to use. It carries no action of its own,
        // so putting it through the cast path made the character swing at thin
        // air and told the server about an attack that never happened. This is
        // checked here rather than only where the skill book is clicked,
        // because a key can still be bound to one from a stored key layout.
        if (move.is_passive())
            return false;

        Weapon::Type weapontype = player.get_stats().get_weapontype();
        if (cooldowns.count(move_id) > 0)
        {
            ForbidSkillMessage(SpecialMove::FBR_COOLDOWN, weapontype).drop();
            return false;
        }

        SpecialMove::ForbidReason reason = player.can_use(move);
        if (reason != SpecialMove::FBR_NONE)
        {
            ForbidSkillMessage(reason, weapontype).drop();
            return false;
        }

        SpecialMove::CastKind kind = move.get_cast_kind();
        if (kind != SpecialMove::CAST_INSTANT)
        {
            begin_cast(move, kind);
            return true;
        }

        if (apply_move(move))
        {
            start_cooldown(move);
        }
        return true;
    }

    void Combat::release_move(int32_t move_id)
    {
        // Only the move that is actually charging responds to its key coming up.
        if (!cast.active || cast.move_id != move_id)
            return;

        if (cast.kind == SpecialMove::CAST_KEYDOWN)
        {
            // A skill that fires while it is held has already loosed everything
            // it is going to; one that only charges goes off now, and reports
            // how long it charged for.
            if (keydown_repeat_interval(cast.move_id) == 0)
            {
                const SpecialMove& charged = get_move(cast.move_id);
                int32_t charge = std::clamp<int32_t>(
                    cast.held, KEYDOWN_MIN_GAUGE, keydown_max_gauge(cast.move_id)
                );
                if (player.can_use(charged) == SpecialMove::FBR_NONE)
                    apply_move(charged, charge);
            }

            finish_cast();
            return;
        }

        // A charged skill goes off when its key comes up, whether or not the
        // wind-up had finished. Cancelling instead made the five prepare skills
        // - Monster Magnet, Explosion and Chakra among them - impossible to use
        // with an ordinary keypress, since a tap never outlasts the wind-up.
        const SpecialMove& move = get_move(cast.move_id);
        cast = {};

        if (apply_move(move))
        {
            start_cooldown(move);
        }
        show_cast_effect(move.get_finish_effect());
    }

    void Combat::begin_cast(const SpecialMove& move, SpecialMove::CastKind kind)
    {
        cast = {};
        cast.move_id = move.get_id();
        cast.kind = kind;
        cast.active = true;

        // Both kinds of cast are mirrored to everyone else in the map so their
        // clients can show the same wind-up. Every skill in the game files that
        // has a prepare or keydown node is one the server recognises here.
        int32_t level = player.get_skills().get_level(cast.move_id);
        SkillEffectPacket(
            cast.move_id, level,
            static_cast<uint8_t>(player.get_integer_attackspeed())
        ).dispatch();

        if (kind == SpecialMove::CAST_PREPARE)
        {
            show_cast_effect(move.get_prepare_effect());
            cast.remaining = move.get_prepare_time();
            return;
        }

        show_cast_effect(move.get_keydown_effect());
        // The first volley of a skill that keeps firing goes off immediately;
        // the rest follow at that skill's own cadence. A skill that only
        // charges never reaches the check that reads this.
        cast.nextshot = 0;
    }

    void Combat::update_cast()
    {
        if (!cast.active)
            return;

        // A character that can no longer act drops whatever it was charging.
        if (!player.can_attack() && cast.kind == SpecialMove::CAST_PREPARE)
        {
            cast = {};
            return;
        }

        const SpecialMove& move = get_move(cast.move_id);

        if (cast.kind == SpecialMove::CAST_PREPARE)
        {
            cast.remaining -= Constants::TIMESTEP;
            if (cast.remaining > 0)
                return;

            // The wind-up is over, so the skill goes off now.
            int32_t move_id = cast.move_id;
            cast = {};
            if (apply_move(move))
            {
                start_cooldown(move);
            }
            show_cast_effect(get_move(move_id).get_finish_effect());
            return;
        }

        cast.held += Constants::TIMESTEP;

        const int32_t interval = keydown_repeat_interval(cast.move_id);
        if (interval == 0)
        {
            // Nothing to do but keep the gauge running: this one fires when the
            // key comes up.
            return;
        }

        cast.nextshot -= Constants::TIMESTEP;
        if (cast.nextshot > 0)
            return;

        if (player.can_use(move) != SpecialMove::FBR_NONE || !player.can_shoot())
        {
            // Out of mp or ammunition part way through, or off the ground with
            // a bow in hand: stop charging rather than firing shots that should
            // not be leaving.
            finish_cast();
            return;
        }

        // A volley loosed mid-hold reports no charge at all - the gauge only
        // means something to the skills that spend it all on one shot.
        apply_move(move);
        cast.nextshot = interval;
    }

    void Combat::finish_cast()
    {
        if (!cast.active)
            return;

        const SpecialMove& move = get_move(cast.move_id);
        cast = {};

        show_cast_effect(move.get_keydown_end_effect());
        show_cast_effect(move.get_finish_effect());
        start_cooldown(move);
    }

    void Combat::show_cast_effect(nl::node src)
    {
        if (!src)
            return;

        Animation animation = src;
        player.show_attack_effect(animation, src["z"]);
    }

    void Combat::start_cooldown(const SpecialMove& move)
    {
        int32_t move_id = move.get_id();
        int32_t level = player.get_skills().get_level(move_id);
        int32_t cooldown = move.get_cooldown(level);
        if (cooldown > 0)
            cooldowns[move_id] = cooldown;
    }

    bool Combat::apply_move(const SpecialMove& move, int32_t charge)
    {
        if (move.is_attack())
        {
            return apply_attack_move(move, charge);
        }
        else if (is_teleport_skill(move.get_id()))
        {
            if (apply_teleport(move))
            {
                move.apply_useeffects(player);
                move.apply_actions(player, Attack::MAGIC);

                send_use_skill(move);
            }
        }
        else
        {
            move.apply_useeffects(player);
            move.apply_actions(player, Attack::MAGIC);

            apply_use_movement(move);

            send_use_skill(move);
        }

        return true;
    }

    void Combat::send_use_skill(const SpecialMove& move)
    {
        int32_t moveid = move.get_id();
        int32_t level = player.get_skills().get_level(moveid);

        if (move.needs_position())
        {
            UseSkillPacket(moveid, level, player.get_position()).dispatch();
            return;
        }

        UseSkillPacket(moveid, level).dispatch();
    }

    bool Combat::apply_attack_move(const SpecialMove& move, int32_t charge)
    {
        // A bow brought against a monster is swung, and the swing is a plain
        // attack and nothing more: the reference client throws the melee with
        // no skill attached at all and abandons the skill outright once it
        // connects. Letting the skill through anyway is what had a point-blank
        // Strafe still loosing an arrow and still charging for it.
        const bool closerange = monster_within_reach(move);
        const SpecialMove& swung = closerange ? regularattack : move;

        Attack attack = player.prepare_attack(swung.is_skill(), closerange);

        swung.apply_useeffects(player);
        swung.apply_actions(player, attack.type);

        player.set_afterimage(swung.get_id());

        swung.apply_stats(player, attack);

        AttackResult result = mobs.send_attack(attack);
        result.attacker = player.get_oid();
        result.keydown = swung.reports_keydown();
        result.charge = charge;
        extract_effects(player, swung, result);

        apply_use_movement(swung);
        apply_result_movement(swung, result);

        AttackPacket(result).dispatch();

        try_register_final_attack(swung);

        return !closerange;
    }

    bool Combat::monster_within_reach(const SpecialMove& move) const
    {
        // A skill the client keeps firing at point blank never becomes a swing.
        if (shoot_fires_point_blank(move.get_id()))
            return false;

        switch (player.get_stats().get_weapontype())
        {
        case Weapon::BOW:
        case Weapon::CROSSBOW:
            break;
        default:
            return false;
        }

        // How far the swing reaches is the trail the weapon leaves, which is
        // the same box a bow already swings when it has nothing to fire.
        return mobs.has_mob_in_reach(
            player.get_afterimage().get_range(),
            player.get_position(),
            !player.getflip()
        );
    }

    void Combat::try_register_final_attack(const SpecialMove& move)
    {
        // Only a skill chains into a final attack. The reference client reads
        // the chain off the skill that was swung, so a plain attack - which has
        // no skill data to read it from - never sets one off.
        if (!move.is_skill())
            return;

        Weapon::Type weapon = player.get_stats().get_weapontype();
        int32_t skillid = SkillData::get(move.get_id()).get_final_attack(weapon);
        if (skillid == 0)
            return;

        int32_t level = player.get_skills().get_level(skillid);
        if (level <= 0)
            return;

        // A final attack's whole level data is its chance and its damage; prop
        // is how often the swing sets it off. TryRegisterFinalAttack rolls it
        // over 101 values and lets a tie through, so a level lands a shade more
        // often than the flat percentage its tooltip quotes - 3 in 101 at level
        // one rather than 2 in 100, which is half again as often down there.
        auto prop = static_cast<int32_t>(
            std::lround(SkillData::get(skillid).get_stats(level).chance * 100)
        );
        if (randomizer.next_int(101) > prop)
            return;

        finalattack_skill = skillid;
        // The client fires it a third of the way from the frame the swing lands
        // to the end of the action. Only the landing frame is exposed here, so
        // the same fraction is taken off that instead of off the remainder.
        finalattack_delay = player.get_attackdelay(0) * FINAL_ATTACK_DELAY_NUM
            / FINAL_ATTACK_DELAY_DEN;
    }

    void Combat::update_final_attack()
    {
        if (finalattack_skill == 0)
            return;

        finalattack_delay -= Constants::TIMESTEP;
        if (finalattack_delay > 0)
            return;

        int32_t skillid = finalattack_skill;
        finalattack_skill = 0;

        // Nothing is loosed from a ladder or a chair the character grabbed in
        // the moment between the swing and its follow-up. can_use does not
        // cover it here: a final attack is passive skill data, so it does not
        // read as an attack to the checks that gate one.
        if (player.is_climbing() || player.is_sitting())
            return;

        const SpecialMove& move = get_move(skillid);
        // The follow-up is a shot of its own and spends its own arrow, which the
        // server charges for as well, so it is dropped rather than fired when
        // the swing that set it off emptied the quiver.
        if (player.can_use(move) != SpecialMove::FBR_NONE)
            return;

        apply_attack_move(move);
    }

    void Combat::apply_use_movement(const SpecialMove& move)
    {
        switch (move.get_id())
        {
        case SkillId::FLASH_JUMP:
            break;
        }
    }

    bool Combat::is_teleport_skill(int32_t skillid)
    {
        return skillid == SkillId::TELEPORT_FP
            || skillid == SkillId::IL_TELEPORT
            || skillid == SkillId::PRIEST_TELEPORT;
    }

    bool Combat::apply_teleport(const SpecialMove& move)
    {
        int32_t skillid = move.get_id();
        int32_t level = player.get_skilllevel(skillid);
        const SkillData::Stats& stats = SkillData::get(skillid).get_stats(level);
        int16_t range = static_cast<int16_t>(stats.hrange * 100.0f);
        if (range <= 0)
            range = TELEPORT_DEFAULT_RANGE;

        Point<int16_t> current = player.get_position();
        Point<int16_t> target = find_teleport_target(range);

        if (target == current)
            return false;

        static Animation tp_effect(
            nl::nx::effect["BasicEff.img"]["Teleport"]
        );
        player.show_attack_effect(tp_effect, 0);

        player.set_position(target);
        PhysicsObject& phobj = player.get_phobj();
        phobj.hspeed = 0.0;
        phobj.vspeed = 0.0;
        phobj.fhid = 0;

        teleport_cooldown = TELEPORT_COOLDOWN_TICKS;
        return true;
    }

    Point<int16_t> Combat::find_teleport_target(int16_t range)
    {
        bool up = player.is_key_down(KeyAction::UP);
        bool down = player.is_key_down(KeyAction::DOWN);
        bool left = player.is_key_down(KeyAction::LEFT);
        bool right = player.is_key_down(KeyAction::RIGHT);

        if (left || right)
            return find_teleport_target_horizontal(range);

        if (up || down)
            return find_teleport_target_vertical(up, range);

        return find_teleport_target_horizontal(range);
    }

    Point<int16_t> Combat::find_teleport_target_vertical(bool up, int16_t range)
    {
        Point<int16_t> current = player.get_position();

        if (up)
        {
            // Search from above: get_y_below finds the nearest foothold
            // at or below (y - range), i.e. a platform above us.
            Point<int16_t> above = physics.get_y_below(
                Point<int16_t>(current.x(), current.y() - range)
            );
            // 5px dead-zone avoids teleporting onto the same platform.
            if (above.y() < current.y() - 5)
                return above;
        }
        else
        {
            // +3px skips the current foothold (ground contact is ~y+1)
            // so get_y_below finds the next platform below.
            Point<int16_t> below = physics.get_y_below(
                Point<int16_t>(current.x(), current.y() + 3)
            );
            // 5px dead-zone, and cap at teleport range.
            if (below.y() > current.y() + 5
                && below.y() - current.y() <= range)
                return below;
        }

        return current;
    }

    Point<int16_t> Combat::find_teleport_target_horizontal(int16_t range)
    {
        Point<int16_t> current = player.get_position();

        int16_t direction = player.getflip() ? 1 : -1;
        if (player.is_key_down(KeyAction::LEFT))
            direction = -1;
        else if (player.is_key_down(KeyAction::RIGHT))
            direction = 1;

        Range<int16_t> map_walls = physics.get_fht().get_walls();
        int16_t prev_ground = current.y();
        Point<int16_t> target = current;

        for (int16_t i = 1; i <= range / TELEPORT_STEP; i++)
        {
            int16_t test_x = static_cast<int16_t>(
                std::clamp<int32_t>(
                    current.x() + direction * i * TELEPORT_STEP,
                    map_walls.first(), map_walls.second()
                )
            );

            // -30px searches from above the previous ground level so we
            // snap to the same foothold layer, not one far below.
            int16_t ground_y = physics.get_y_below(
                Point<int16_t>(test_x, prev_ground - 30)
            ).y();

            if (std::abs(static_cast<int32_t>(ground_y)
                       - static_cast<int32_t>(prev_ground)) > TELEPORT_WALL_THRESHOLD)
                break;

            target = Point<int16_t>(test_x, ground_y);
            prev_ground = ground_y;
        }

        return target;
    }

    void Combat::apply_result_movement(const SpecialMove& move, const AttackResult& result)
    {
        switch (move.get_id())
        {
        case SkillId::RUSH_HERO:
        case SkillId::RUSH_PALADIN:
        case SkillId::RUSH_DK:
            apply_rush(result);
            break;
        }
    }

    void Combat::apply_rush(const AttackResult& result)
    {
        if (result.mobcount == 0)
            return;

        Point<int16_t> mob_position = mobs.get_mob_position(result.last_oid);
        int16_t targetx = mob_position.x();
        player.rush(targetx);
    }

    void Combat::apply_bullet_effect(const BulletEffect& effect)
    {
        bullets.push_back(effect);
        if (bullets.back().bullet.settarget(effect.target, effect.flighttime))
        {
            // Already touching the target: there is no flight to show. The
            // damage is on its own timer and lands regardless.
            bullets.pop_back();
        }
    }

    void Combat::apply_special_effect(const SpecialEffect& effect)
    {
        if (effect.onmap)
        {
            // An emitted copy plays out where the skill was aimed, so it holds a
            // map position rather than an offset from the caster.
            movingeffects.emplace_back(
                effect.animation, effect.origin, effect.travel,
                effect.duration, effect.flip, effect.alpha
            );
            return;
        }

        if (effect.lifetime > 0)
        {
            groundeffects.emplace_back(
                effect.animation, effect.origin, effect.lifetime, effect.flip
            );
            return;
        }

        if (effect.user_oid == player.get_oid())
        {
            player.show_attack_effect(effect.animation, effect.z, effect.origin);
        }
        else if (Optional<OtherChar> ouser = chars.get_char(effect.user_oid))
        {
            ouser->show_attack_effect(effect.animation, effect.z, effect.origin);
        }
    }

    void Combat::apply_damage_effect(const DamageEffect& effect)
    {
        Point<int16_t> body_position = mobs.get_mob_body_position(effect.target_oid);
        damagenumbers.push_back(effect.number);
        damagenumbers.back().set_x(body_position.x());

        const SpecialMove& move = get_move(effect.move_id);
        mobs.apply_damage(effect.target_oid, effect.damage, effect.toleft, effect.user, move);
    }

    void Combat::push_attack(const AttackResult& attack)
    {
        attackresults.push(400, attack);
    }

    void Combat::apply_summon_damage(const AttackResult& result,
        const AttackUser& user, uint16_t attackafter) {

        size_t target_index = 0;
        for (auto& line : result.damagelines)
        {
            const int32_t oid = line.first;
            if (!mobs.contains(oid))
            {
                target_index++;
                continue;
            }

            // The swing lands its damage a stated distance into its own
            // animation, and sweeps outward from there one target at a time.
            const uint16_t lands = static_cast<uint16_t>(
                attackafter + target_index * SUMMON_TARGET_STAGGER
            );

            std::vector<DamageNumber> numbers = place_numbers(oid, line.second);
            for (size_t i = 0; i < numbers.size(); i++)
            {
                damageeffects.emplace(
                    lands, user, numbers[i], line.second[i].first,
                    result.toleft, oid, result.skill
                );
            }

            target_index++;
        }
    }

    void Combat::apply_attack(const AttackResult& attack)
    {
        if (Optional<OtherChar> ouser = chars.get_char(attack.attacker))
        {
            OtherChar& user = *ouser;
            user.update_skill(attack.skill, attack.level);
            user.update_speed(attack.speed);

            const SpecialMove& move = get_move(attack.skill);
            move.apply_useeffects(user);

            if (Stance::Id stance = Stance::by_id(attack.stance))
            {
                user.attack(stance);
            }
            else
            {
                move.apply_actions(user, attack.type);
            }

            user.set_afterimage(attack.skill);

            extract_effects(user, move, attack);
        }
    }

    std::vector<uint16_t> Combat::hit_delays(const Char& user,
        const AttackResult& result, int32_t shootdelay) const
    {
        std::vector<uint16_t> delays;

        // Only the shoot and magic paths have been reversed. A close attack
        // keeps the cadence we approximate for it.
        if (result.type == Attack::CLOSE)
            return delays;

        const int32_t base = shootdelay;
        const int16_t from = user.get_position().x();

        // What the client charges most skills on both paths: the time the shot
        // needs to cross the gap to its target.
        auto travel = [&](int32_t oid) {
            int16_t at = mobs.contains(oid)
                ? mobs.get_mob_body_position(oid).x()
                : from;
            int32_t distance = std::abs(at - from);
            return base + static_cast<int32_t>(distance * SHOT_MS_PER_PIXEL);
        };

        delays.reserve(result.damagelines.size());
        for (size_t i = 0; i < result.damagelines.size(); i++)
        {
            const int32_t oid = result.damagelines[i].first;
            const int32_t step = static_cast<int32_t>(i);
            int32_t delay;

            if (result.type == Attack::MAGIC)
            {
                if (listed(MAGIC_ON_EVENT, result.skill))
                    delay = base;
                else if (listed(MAGIC_TRAVELS, result.skill))
                    delay = travel(oid);
                else
                    delay = base + step * MAGIC_TARGET_STEP;
            }
            else
            {
                switch (result.skill)
                {
                case SkillId::ARROW_RAIN:
                case SkillId::ARROW_ERUPTION:
                case SHOOT_FLAT_400_WA:
                    // The arrows land on their own schedule, the same one for
                    // every target, however far off it is.
                    delay = base + 400;
                    break;
                case SHOOT_ON_EVENT_MECH:
                    delay = base;
                    break;
                case SkillId::ARROW_BOMB:
                case SHOOT_TRAIL_200_WH:
                    // The shot itself connects; the blast catches everything
                    // else 200 ms later.
                    delay = (i == 0) ? travel(oid) : delays.front() + 200;
                    break;
                case SkillId::INFERNO:
                case SkillId::BLIZZARD_SNIPER:
                    // Likewise, except the spread cascades outwards from where
                    // the shot connected.
                    delay = (i == 0)
                        ? travel(oid)
                        : delays.front() + 120 + step * 30;
                    break;
                default:
                    delay = travel(oid);
                }
            }

            delays.push_back(static_cast<uint16_t>(std::max(delay, 0)));
        }
        return delays;
    }

    void Combat::extract_effects(const Char& user, const SpecialMove& move, const AttackResult& result)
    {
        AttackUser attackuser = {
            user.get_skilllevel(move.get_id()),
            user.get_level(),
            user.is_twohanded(),
            !result.toleft
        };
        // The area animations belong where the skill landed rather than where
        // the caster is standing, but on the caster's floor so a tile sits on
        // the ground rather than in the air.
        Point<int16_t> anchor = user.get_position();
        if (mobs.contains(result.first_oid))
        {
            anchor = {
                mobs.get_mob_position(result.first_oid).x(),
                anchor.y()
            };
        }

        SpecialMove::AreaEffect area = move.get_area_effect(user);

        // When the shot leaves the caster, and the point every other delay is
        // measured from. The client normally takes how far into the action the
        // frame flagged as its event sits, scaled by the length the animation
        // actually runs for - which is what our attack-frame delay already is.
        // A skill that states its own ball delay replaces that outright rather
        // than adding to it; adding was what left Dragon's Breath loosing its
        // arrow as the draw-back animation ended instead of partway through it.
        const uint16_t ball_delay = move.get_ball_delay();
        const uint16_t area_delay = (ball_delay > 0)
            ? ball_delay
            : static_cast<uint16_t>(user.get_attackdelay(0));

        // When each target takes its damage.
        const std::vector<uint16_t> delays = hit_delays(user, result, area_delay);
        // When the area animation starts. The client hands the registrar the
        // first target's hit delay, so the stream of copies runs in step with
        // the damage rather than the moment the attack was applied - that gap
        // is what made Arrow Rain's arrows fall ahead of everything else.
        const uint16_t hit_delay = delays.empty() ? area_delay : delays.front();

        // Every one of the shoot path's falling emitters sits behind a check
        // that the swing found something, so a volley that connects with
        // nothing paints no arrows.
        if (area.kind == SpecialMove::EMIT_FALLING && result.damagelines.empty())
            area.kind = SpecialMove::EMIT_NONE;

        // Where the emitter's box sits. The client picks this per skill: on
        // the caster, on the point the shot first connected, or once per
        // struck monster.
        std::vector<Point<int16_t>> anchors;
        switch (area.anchor)
        {
        case SpecialMove::ANCHOR_IMPACT:
            if (mobs.contains(result.first_oid))
                anchors.push_back(mobs.get_mob_body_position(result.first_oid));
            break;
        case SpecialMove::ANCHOR_PER_MOB:
            for (auto& line : result.damagelines)
            {
                if (mobs.contains(line.first))
                    anchors.push_back(mobs.get_mob_body_position(line.first));
            }
            break;
        default:
            anchors.push_back(user.get_position());
        }

        for (Point<int16_t> at : anchors)
        {
            Rectangle<int16_t> box{
                static_cast<int16_t>(at.x() + area.spawnbox.l()),
                static_cast<int16_t>(at.x() + area.spawnbox.r()),
                static_cast<int16_t>(at.y() + area.spawnbox.t() - area.lifttop),
                static_cast<int16_t>(at.y() + area.spawnbox.b() - area.liftbottom)
            };

            int16_t boxw = box.width();
            int16_t boxh = box.height();
            const int32_t step = std::max<int32_t>(area.interval, 1);

            // A falling emitter lights the ground copy by copy, above, so only
            // the kinds that drop nothing lay a row across the area instead.
            // The row uses the box before it is lifted: the lift exists to
            // start copies above the area they fall into, and searching for
            // ground inside a box raised off it finds none.
            if (area.kind != SpecialMove::EMIT_FALLING)
            {
                lay_tiles(
                    user, area,
                    Rectangle<int16_t>{
                        box.l(), box.r(),
                        static_cast<int16_t>(at.y() + area.spawnbox.t()),
                        static_cast<int16_t>(at.y() + area.spawnbox.b())
                    },
                    hit_delay, !result.toleft
                );
            }

            if (area.kind == SpecialMove::EMIT_FALLING)
            {
                // count copies every interval until duration has elapsed. Each
                // spawns at a random point in the box and travels (x, y) over
                // fall ms, with the jitter the client applies - the jitter is
                // what makes the stream read as rain rather than a volley.
                // start and duration are both offsets from the hit delay: the
                // registrar adds the caller's delay to each of them.
                const int16_t reachx = result.toleft
                    ? static_cast<int16_t>(-area.travelx)
                    : area.travelx;

                const int32_t first = hit_delay + area.start;
                const int32_t last = hit_delay + area.duration;

                for (int32_t tick = first; tick <= last; tick += step)
                {
                    for (int16_t i = 0; i < area.count; i++)
                    {
                        Point<int16_t> from{
                            static_cast<int16_t>(box.l()
                                + (boxw > 0 ? randomizer.next_int<int32_t>(boxw) : 0)),
                            static_cast<int16_t>(box.t()
                                + (boxh > 0 ? randomizer.next_int<int32_t>(boxh) : 0))
                        };

                        int32_t dy = area.travely;
                        if (area.travelx != 0 && area.travely != 0)
                        {
                            dy = (20 - randomizer.next_int<int32_t>(41)) * area.travelx
                                 / area.travely + area.travely;
                        }

                        int32_t fall = area.falltime
                            + randomizer.next_int<int32_t>(300) - 150;
                        if (fall < 1)
                            fall = 1;

                        nl::node variant = area.variants[
                            randomizer.next_int<size_t>(area.variants.size())
                        ];

                        specialeffects.emplace(
                            static_cast<uint16_t>(tick), user.get_oid(),
                            Animation{ variant }, from,
                            Point<int16_t>{ reachx, static_cast<int16_t>(dy) },
                            static_cast<uint16_t>(fall), uint16_t{ 0 },
                            static_cast<int8_t>(variant["z"]), area.alpha,
                            !result.toleft, true
                        );

                        // The ground is lit by the copies reaching it, so each
                        // one leaves its own tile where it comes down and at
                        // the moment it does. A copy travelling upwards never
                        // touches the floor and leaves nothing.
                        if (!area.tiles.empty() && area.lifetime > 0 && dy > 0)
                        {
                            const int16_t landx =
                                static_cast<int16_t>(from.x() + reachx);
                            const int16_t ground = physics.get_y_below(
                                Point<int16_t>(landx, from.y())
                            ).y();

                            nl::node tile = area.tiles[
                                randomizer.next_int<size_t>(area.tiles.size())
                            ];

                            specialeffects.emplace(
                                static_cast<uint16_t>(tick + fall),
                                user.get_oid(), Animation{ tile },
                                Point<int16_t>(landx, static_cast<int16_t>(
                                    ground + randomizer.next_int<int32_t>(5) - 2
                                )),
                                Point<int16_t>{ 0, 0 }, uint16_t{ 0 },
                                area.lifetime, int8_t{ 0 }, int16_t{ 0 },
                                !result.toleft, true
                            );
                        }
                    }
                }
            }
            else if (area.kind == SpecialMove::EMIT_EXPLOSION)
            {
                // The copies burst outward: the band they may appear in starts
                // at a sixth of the box and widens every tick, and each copy
                // plays once where it lands rather than travelling. The
                // explosion registrar takes no delay from its caller, so this
                // one starts the moment the attack is applied rather than with
                // the damage.
                int32_t curw = boxw * 5 / 6;
                int32_t curh = boxh * 5 / 6;
                Point<int16_t> centre{
                    static_cast<int16_t>((box.l() + box.r()) / 2),
                    static_cast<int16_t>((box.t() + box.b()) / 2)
                };

                for (int32_t tick = area.start; tick <= area.duration; tick += step)
                {
                    int32_t dw = boxw - curw;
                    int32_t dh = boxh - curh;
                    curw = curw * 3 / 4;
                    curh = curh * 3 / 4;

                    for (int16_t i = 0; i < area.count; i++)
                    {
                        Point<int16_t> from{
                            static_cast<int16_t>(centre.x()
                                + (dw > 0 ? randomizer.next_int<int32_t>(dw) - dw / 2 : 0)),
                            static_cast<int16_t>(centre.y()
                                + (dh > 0 ? randomizer.next_int<int32_t>(dh) - dh / 2 : 0))
                        };

                        nl::node variant = area.variants[
                            randomizer.next_int<size_t>(area.variants.size())
                        ];

                        // Zero travel and zero duration means it sits where it
                        // appeared and lasts as long as its own animation.
                        specialeffects.emplace(
                            static_cast<uint16_t>(tick), user.get_oid(),
                            Animation{ variant }, from,
                            Point<int16_t>{ 0, 0 }, uint16_t{ 0 }, uint16_t{ 0 },
                            static_cast<int8_t>(variant["z"]), int16_t{ 0 },
                            !result.toleft, true
                        );
                    }
                }
            }
            else if (area.kind == SpecialMove::EMIT_ONCE && area.special)
            {
                specialeffects.emplace(
                    hit_delay, user.get_oid(), Animation{ area.special },
                    at - user.get_position(),
                    Point<int16_t>{ 0, 0 }, uint16_t{ 0 }, uint16_t{ 0 },
                    static_cast<int8_t>(area.special["z"]), int16_t{ 0 },
                    !result.toleft, false
                );
            }
        }

        // A skill that draws no projectile of its own still arms itself with an
        // arrow and still spends one - Arrow Rain paints its own volley, so a
        // stray arrow flying off forwards is the client's shot leaking through.
        const bool bullet = result.bullet && !shoot_hides_bullet(move.get_id());

        if (bullet)
        {
            // Shots of one volley are fanned apart vertically, or they overlap
            // exactly and a two-arrow skill reads as a single arrow.
            auto bullet_fan = [](Point<int16_t> base, size_t index, size_t count) {
                int32_t steps = static_cast<int32_t>(index) * 2
                    - static_cast<int32_t>(count > 0 ? count - 1 : 0);
                return base + Point<int16_t>(0, static_cast<int16_t>(steps * 7));
            };

            // Most volleys leave together; the few that do not have their own
            // gap, and it does not depend on the attack animation's frames.
            const uint16_t volley_gap = shoot_bullet_delay(move.get_id(), result.bullet);

            auto bullet_delay = [&](size_t index, size_t) {
                return static_cast<uint16_t>(area_delay + index * volley_gap);
            };

            // A ranged attack looses one projectile per shot the skill fires -
            // its bullet count - and not one per monster it happens to reach.
            // An arrow that passes through six mobs is still a single arrow.
            const size_t shots = std::max<size_t>(result.hitcount, 1);

            size_t target_index = 0;
            for (auto& line : result.damagelines)
            {
                int32_t oid = line.first;
                if (!mobs.contains(oid))
                {
                    target_index++;
                    continue;
                }

                const uint16_t when = target_index < delays.size()
                    ? delays[target_index]
                    : area_delay;

                std::vector<DamageNumber> numbers = place_numbers(oid, line.second);
                for (size_t i = 0; i < numbers.size(); i++)
                {
                    // Each line lands as the shot that carries it passes. The
                    // client keeps one delay per monster and pops all of them
                    // together, but a volley that leaves in sequence and lands
                    // in one lump does not read as four arrows hitting. A mob
                    // struck more often than the skill has shots folds the
                    // extra hits onto the last one.
                    const size_t shot = std::min(i, shots - 1);
                    const uint16_t lands = static_cast<uint16_t>(
                        when + shot * volley_gap
                    );

                    damageeffects.emplace(
                        lands, attackuser, numbers[i], line.second[i].first,
                        result.toleft, oid, move.get_id()
                    );
                }

                target_index++;
            }

            // The shot is aimed at the monster the attack found, at that
            // monster's own height - which is what angles it up or down a
            // slope. With nothing found it flies flat to the far end of what
            // the attack reaches, so a shot that stops short is the honest
            // picture of a reach that does not get there.
            const int16_t reach = result.reach > 0
                ? result.reach
                : resolve_shoot_reach(user);
            const Point<int16_t> muzzle{
                user.get_position().x(),
                static_cast<int16_t>(user.get_position().y() - Attack::MUZZLE_HEIGHT)
            };

            // Only a shot that hunted through the wedge for one target follows
            // that target. The reference client hands the bullet a homing
            // anchor solely on that path; a shot that swept a band leaves the
            // anchor empty and flies flat to the far end of its reach. An arrow
            // meant to pass through a row of monsters would otherwise curve
            // onto the first of them and stop looking like it pierced anything.
            const bool homes = !shoot_searches_rect(move.get_id());
            const bool connects = homes && mobs.contains(result.first_oid);
            const Point<int16_t> aim = connects
                ? mobs.get_mob_body_position(result.first_oid)
                : muzzle + Point<int16_t>(
                    result.toleft ? static_cast<int16_t>(-reach) : reach, 0
                );

            for (size_t i = 0; i < shots; i++)
            {
                Point<int16_t> destination = bullet_fan(aim, i, shots);
                Point<int16_t> flightpath = destination - muzzle;
                const double distance = std::sqrt(
                    static_cast<double>(flightpath.x()) * flightpath.x()
                    + static_cast<double>(flightpath.y()) * flightpath.y()
                );

                Bullet bullet{
                    move.get_bullet(user, result.bullet),
                    user.get_position(),
                    result.toleft
                };

                bulleteffects.emplace(
                    bullet_delay(i, shots),
                    bullet,
                    destination,
                    connects ? result.first_oid : 0,
                    // The same 1.5 ms per pixel the damage delays are charged
                    // at, so a shot reaches a monster on the frame that
                    // monster takes its damage.
                    static_cast<uint16_t>(distance * SHOT_MS_PER_PIXEL),
                    !connects
                );
            }

        }
        else
        {
            size_t target_index = 0;
            for (auto& line : result.damagelines)
            {
                int32_t oid = line.first;
                if (mobs.contains(oid))
                {
                    std::vector<DamageNumber> numbers = place_numbers(oid, line.second);

                    // A reversed path knows exactly when this monster is hit,
                    // and every line it takes lands on that one frame. A close
                    // attack has no such table, so its hits are still spread
                    // over the swing and cascade outwards target by target.
                    const bool timed = target_index < delays.size();
                    uint16_t stagger = target_stagger(user, target_index);

                    size_t i = 0;
                    for (auto& number : numbers)
                    {
                        damageeffects.emplace(
                            timed
                                ? delays[target_index]
                                : static_cast<uint16_t>(
                                    user.get_attackdelay(i, numbers.size()) + stagger),
                            attackuser,
                            number,
                            line.second[i].first,
                            result.toleft,
                            oid,
                            move.get_id()
                        );

                        i++;
                    }
                }
                target_index++;
            }
        }
    }

    void Combat::lay_tiles(const Char& user, const SpecialMove::AreaEffect& area,
        Rectangle<int16_t> box, uint16_t delay, bool flip) {

        if (area.tiles.empty() || area.lifetime == 0 || area.tilestep <= 0)
            return;

        // The shoot path lays its tiles on the first target's hit delay; the
        // magic path passes the foothold registrar a flat 180 ms.
        const uint16_t tile_delay = (area.kind == SpecialMove::EMIT_EXPLOSION)
            ? uint16_t{ 180 }
            : delay;

        // The client walks the affected width in steps of effectDistance and
        // drops a tile on whatever ground it finds under each column. It can
        // also nudge each step forward by up to a third of one, but only when
        // the caller asks for it, and the shoot path does not - so the row sits
        // on an even grid. The variant and a couple of pixels of height are
        // still rolled per tile.
        for (int32_t x = box.l(); x < box.r(); x += area.tilestep)
        {
            int16_t at = static_cast<int16_t>(x);

            // The reference client collects every foothold crossing this column
            // between the box's top and bottom and lays a tile on each. Only
            // the nearest one below is available here, so a stack of platforms
            // inside the box gets the lowest of them rather than all.
            int16_t ground = physics.get_y_below(
                Point<int16_t>(at, box.t())
            ).y();
            if (ground > box.b())
                continue;

            nl::node variant = area.tiles[
                randomizer.next_int<size_t>(area.tiles.size())
            ];

            specialeffects.emplace(
                tile_delay, user.get_oid(), Animation{ variant },
                Point<int16_t>(
                    at,
                    static_cast<int16_t>(ground + randomizer.next_int<int32_t>(5) - 2)
                ),
                Point<int16_t>{ 0, 0 }, uint16_t{ 0 },
                area.lifetime, int8_t{ 0 }, int16_t{ 0 }, flip, true
            );
        }
    }

    void Combat::show_bounce_damage(int32_t oid, int32_t damage)
    {
        Point<int16_t> body = mobs.get_mob_body_position(oid);
        damagenumbers.emplace_back(DamageNumber::NORMAL, damage, body.y());
        damagenumbers.back().set_x(body.x());
    }

    std::vector<DamageNumber> Combat::place_numbers(int32_t oid, const std::vector<std::pair<int32_t, bool>>& damagelines)
    {
        std::vector<DamageNumber> numbers;
        int16_t body = mobs.get_mob_body_position(oid).y();
        for (auto& line : damagelines)
        {
            int32_t amount = line.first;
            bool critical = line.second;
            DamageNumber::Type type = critical ? DamageNumber::CRITICAL : DamageNumber::NORMAL;
            numbers.emplace_back(type, amount, body);

            body -= DamageNumber::rowheight(critical);
        }
        return numbers;
    }

    void Combat::show_buff(int32_t cid, int32_t skillid, int8_t level)
    {
        if (Optional<OtherChar> ouser = chars.get_char(cid))
        {
            OtherChar& user = *ouser;
            user.update_skill(skillid, level);

            const SpecialMove& move = get_move(skillid);
            move.apply_useeffects(user);
            move.apply_actions(user, Attack::MAGIC);
        }
    }

    void Combat::show_player_buff(int32_t skillid)
    {
        get_move(skillid)
            .apply_useeffects(player);
    }

    const SpecialMove& Combat::get_move(int32_t move_id)
    {
        if (move_id == 0)
            return regularattack;

        auto iter = skills.find(move_id);
        if (iter == skills.end())
        {
            iter = skills.emplace(move_id, move_id).first;
        }
        return iter->second;
    }
}
