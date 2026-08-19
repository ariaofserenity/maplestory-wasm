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
#include "Attack.h"
#include "GroundEffect.h"
#include "MovingEffect.h"
#include "RegularAttack.h"
#include "Skill.h"

#include "../MapleMap/MapChars.h"
#include "../MapleMap/MapMobs.h"
#include "../Physics/Physics.h"

#include "../../Character/Player.h"
#include "../../Template/TimedQueue.h"

#include <unordered_map>

namespace jrc
{
    class Combat
    {
    public:
        Combat(Player& player, MapChars& chars, MapMobs& mobs,
            Physics& physics);

        // Draw bullets, damage numbers etc.
        void draw(double viewx, double viewy, float alpha) const;
        // Poll attacks, damage effects, etc.
        void update();

        // Make the player use a special move. Returns true if the move was used
        // or started charging.
        bool use_move(int32_t move_id);
        // Let go of a move's key. A charging move goes off; anything else is
        // ignored, so this is safe to call for every key release.
        void release_move(int32_t move_id);
        // Reset transient state (e.g. on map change).
        void clear();

        // Add an attack to the attack queue.
        void push_attack(const AttackResult& attack);
        // Show what one of the player's summons just hit: a damage number over
        // every monster it caught, applied on the cadence the client hits them
        // at rather than all on the frame the swing began.
        void apply_summon_damage(const AttackResult& result,
            const AttackUser& user, uint16_t attackafter);
        // Show a buff effect.
        void show_buff(int32_t cid, int32_t skillid, int8_t level);
        // Show a buff effect.
        void show_player_buff(int32_t skillid);

    private:
        struct DamageEffect
        {
            AttackUser user;
            DamageNumber number;
            int32_t damage;
            bool toleft;
            int32_t target_oid;
            int32_t move_id;
        };

        // An area animation waiting to appear.
        struct SpecialEffect
        {
            int32_t user_oid;
            Animation animation;
            // World position the effect was cast at, so it stays put if the
            // caster walks away mid-cast.
            Point<int16_t> origin;
            // Non-zero means the copy travels this far while it plays, over
            // duration ms, anchored to the map rather than to the caster.
            Point<int16_t> travel;
            uint16_t duration;
            // Non-zero means the effect stays on the terrain this long instead
            // of playing once.
            uint16_t lifetime;
            int8_t z;
            // The opacity the skill states for this copy, out of 255; zero when
            // it states none.
            int16_t alpha;
            bool flip;
            // Whether origin is a map position or an offset from the caster.
            // Travel alone cannot tell these apart: an explosion copy is
            // anchored to the map yet does not move.
            bool onmap;
        };

        // A projectile in flight. It carries no damage: the reference client
        // applies each target's damage on that target's own delay and leaves
        // the shot as scenery, so an arrow through six monsters does not land
        // all six on the frame it happens to arrive.
        struct BulletEffect
        {
            Bullet bullet;
            Point<int16_t> target;
            // What the shot was aimed at, so a homing one can follow it.
            int32_t target_oid = 0;
            // How long the shot takes to arrive, in ms. It is timed to land
            // with the damage it was fired for.
            uint16_t flighttime = 0;
            // A straight shot flies where it was aimed instead of following its
            // target around.
            bool straight = false;
        };

        // A move partway through being cast. Instant moves never enter this
        // state; a prepare move sits here for its wind-up and a keydown move for
        // as long as its key is held.
        struct Cast
        {
            int32_t move_id = 0;
            SpecialMove::CastKind kind = SpecialMove::CAST_INSTANT;
            // Wind-up left to run, in ms.
            int32_t remaining = 0;
            // Time until this keydown move fires again, in ms. Only a move that
            // keeps firing while held uses it.
            int32_t nextshot = 0;
            // How long the key has been held, in ms. This is the charge the
            // server is told about when the move finally goes off.
            int32_t held = 0;
            bool active = false;
        };

        void apply_attack(const AttackResult& attack);
        // Returns whether the move itself was used. A shot that turns into a
        // swing at point-blank range reports false: the swing is a plain
        // attack, so the skill was never spent.
        bool apply_move(const SpecialMove& move, int32_t charge = 0);
        // Swing a move that deals damage: roll its targets, show its effects and
        // tell the server. Split out because a final attack goes through the
        // same path without being something the player asked for.
        // charge is how long a charging move was held for; zero for everything
        // else, and for the volleys a move looses while still being held.
        bool apply_attack_move(const SpecialMove& move, int32_t charge = 0);
        // Whether a monster is close enough that a bow should be swung at it
        // rather than fired.
        bool monster_within_reach(const SpecialMove& move) const;
        // Roll whether the move that was just swung sets off a follow-up attack.
        void try_register_final_attack(const SpecialMove& move);
        // Run a pending follow-up attack forward one tick.
        void update_final_attack();
        // Begin the cast of a move that does not go off immediately.
        void begin_cast(const SpecialMove& move, SpecialMove::CastKind kind);
        // Run the current cast forward one tick.
        void update_cast();
        // End the current cast, playing its closing animations.
        void finish_cast();
        // Show one of a move's own animations on the player.
        void show_cast_effect(nl::node src);
        // Start the cooldown a move imposes, if any.
        void start_cooldown(const SpecialMove& move);
        // Tell the server a skill was used, with where it was cast when the
        // skill needs the server to know.
        void send_use_skill(const SpecialMove& move);
        void apply_use_movement(const SpecialMove& move);
        void apply_result_movement(const SpecialMove& move, const AttackResult& result);
        void apply_rush(const AttackResult& result);
        void apply_bullet_effect(const BulletEffect& effect);
        void apply_special_effect(const SpecialEffect& effect);
        void apply_damage_effect(const DamageEffect& effect);
        void extract_effects(const Char& user, const SpecialMove& move, const AttackResult& result);
        // When each struck target takes its damage, in ms after the attack is
        // applied - one entry per damage line, in the order they arrive. Empty
        // for a close attack, whose cadence has not been reversed.
        std::vector<uint16_t> hit_delays(const Char& user,
            const AttackResult& result, int32_t shootdelay) const;
        // Lay a skill's tiles across the ground the area effect covers.
        void lay_tiles(const Char& user, const SpecialMove::AreaEffect& area,
            Rectangle<int16_t> box, uint16_t delay, bool flip);
        std::vector<DamageNumber> place_numbers(int32_t oid, const std::vector<std::pair<int32_t, bool>>& damagelines);
        const SpecialMove& get_move(int32_t move_id);

        static bool is_teleport_skill(int32_t skillid);
        bool apply_teleport(const SpecialMove& move);
        Point<int16_t> find_teleport_target(int16_t range);
        Point<int16_t> find_teleport_target_vertical(bool up, int16_t range);
        Point<int16_t> find_teleport_target_horizontal(int16_t range);

        Player& player;
        MapChars& chars;
        MapMobs& mobs;
        Physics& physics;

        std::unordered_map<int32_t, Skill> skills;
        // Remaining cooldown per move, in ms.
        std::unordered_map<int32_t, int32_t> cooldowns;
        RegularAttack regularattack;
        Cast cast;
        int16_t teleport_cooldown = 0;
        // The follow-up attack a swing set off, and how long until it goes off.
        // Zero means none is pending.
        int32_t finalattack_skill = 0;
        int32_t finalattack_delay = 0;

        TimedQueue<AttackResult> attackresults;
        TimedQueue<BulletEffect> bulleteffects;
        TimedQueue<DamageEffect> damageeffects;
        TimedQueue<SpecialEffect> specialeffects;

        std::list<BulletEffect> bullets;
        std::list<MovingEffect> movingeffects;
        std::list<GroundEffect> groundeffects;
        std::list<DamageNumber> damagenumbers;
    };
}
