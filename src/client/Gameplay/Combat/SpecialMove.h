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

#include "../MapleMap/Mob.h"

#include "../../Character/Char.h"
#include "../../Character/Job.h"

#include "../../Template/Rectangle.h"

#include "nlnx/node.hpp"

#include <vector>

namespace jrc
{
    // How far a weapon shoots when the skill itself states no range, and the
    // skill whose own range is added on top of it. Taken from the reference
    // client's shoot-range lookup: a bow reaches 300 plus however far The Eye
    // of Amazon extends it, a claw 200 plus Keen Eyes, a gun a flat 200.
    struct ShootReach
    {
        int16_t base = 0;
        // Zero when nothing extends this weapon's reach.
        int32_t bonusskill = 0;
    };

    inline ShootReach default_shoot_reach(Weapon::Type weapon, bool cygnus)
    {
        switch (weapon)
        {
        case Weapon::BOW:
        case Weapon::CROSSBOW:
            return { 300, cygnus ? 13000001 : 3000002 };
        case Weapon::CLAW:
            return { 200, cygnus ? 14000001 : 4000001 };
        case Weapon::GUN:
            return { 200, 0 };
        default:
            return { 0, 0 };
        }
    }

    // How far this character's shots reach when the skill states no range of
    // its own: the weapon's base distance plus whatever its mastery skill adds.
    int16_t resolve_shoot_reach(const Char& user);

    // How far above and below the muzzle a fired attack reaches. Zero leaves
    // the client's one-pixel line, which is what almost every shoot skill uses.
    inline int16_t shoot_vertical_adjust(int32_t skillid)
    {
        switch (skillid)
        {
        case 3001004:  // Arrow Blow
        case 3121003:  // Dragon's Breath
        case 3221001:  // Piercing Arrow
        case 3221003:  // Dragon's Breath (Marksman)
        case 33101001:
            return 20;
        case 3201005:  // Iron Arrow
        case 4121003:  // Taunt
        case 4221003:  // Taunt (Night Lord)
        case 13111006:
            return 10;
        case 4111005:  // Avenger
        case 14111002:
            return 36;
        case 33121005:
            return 12;
        case 11101004:
        case 15111007:
        case 21100004:
        case 21110004:
            return 60;
        case 15111006:
            return 150;
        default:
            return 0;
        }
    }

    // How quickly the wedge a shot searches widens: one pixel above and below
    // the muzzle line for every this many pixels of travel.
    constexpr int16_t SHOOT_WEDGE_SLOPE = 4;

    // Skills that sweep a rect instead of searching the wedge. Everything
    // else - the plain shot, Arrow Bomb, Inferno, Strafe, Hurricane - looks
    // for the nearest monster inside a wedge spreading out from the muzzle,
    // and hits that one.
    inline bool shoot_searches_rect(int32_t skillid)
    {
        switch (skillid)
        {
        case 3101003:  // Power Knock-Back
        case 3111004:  // Arrow Rain
        case 3121003:  // Dragon's Breath
        case 3201003:  // Power Knock-Back (Crossbowman)
        case 3201005:  // Iron Arrow
        case 3211004:  // Arrow Eruption
        case 3221001:  // Piercing Arrow
        case 3221003:  // Dragon's Breath (Marksman)
        case 4111005:  // Avenger
        case 4121003:  // Taunt
        case 4221003:  // Taunt (Night Lord)
        case 5201001:
        case 5211004:
        case 5211005:
        case 5221008:
        case 11101004:
        case 13101005:
        case 13111000:
        case 13111006:
        case 14101006:
        case 14111002:
        case 15111007:
        case 21100004:
        case 21110004:
        case 21120006:
        case 33101001:
        case 33101002:
        case 33121001:
        case 33121005:
        case 35001001:
        case 35101009:
        case 35111015:
        case 35121012:
            return true;
        default:
            return false;
        }
    }

    // Skills that, having found their target in the wedge, spread to whatever
    // else stands inside the level's rect around it.
    inline bool shoot_splashes(int32_t skillid)
    {
        switch (skillid)
        {
        case 3001004:  // Arrow Blow
        case 3101005:  // Arrow Bomb
        case 3111003:  // Inferno
        case 3211003:  // Blizzard (Sniper)
        case 33101007:
        case 35111004:
        case 35121005:
        case 35121013:
            return true;
        default:
            return false;
        }
    }

    // Skills that pick their targets out of the level's own rect instead of
    // the line the muzzle draws.
    inline bool shoot_area_from_data(int32_t skillid)
    {
        switch (skillid)
        {
        case 3101003:  // Power Knock-Back
        case 3111004:  // Arrow Rain
        case 3201003:  // Power Knock-Back (Crossbowman)
        case 3211004:  // Arrow Eruption
        case 5201001:
        case 5211004:
        case 5211005:
        case 5221008:
        case 13101005:
        case 13111000:
        case 14101006:
        case 21120006:
        case 33101002:
        case 33121001:
        case 35001001:
        case 35111015:
        case 35121012:
            return true;
        default:
            return false;
        }
    }

    // Skills that draw no projectile of their own, however the shot is armed.
    inline bool shoot_hides_bullet(int32_t skillid)
    {
        switch (skillid)
        {
        case 3101003:  // Power Knock-Back
        case 3111004:  // Arrow Rain
        case 3201003:  // Power Knock-Back (Crossbowman)
        case 3211004:  // Arrow Eruption
        case 5201001:
        case 5211004:
        case 5211005:
        case 13101005:
        case 13111000:
        case 14101006:
        case 21120006:
        case 33101002:
        case 33121001:
        case 35001001:
        case 35101009:
        case 35121012:
            return true;
        default:
            return false;
        }
    }

    // How long the client leaves between the shots of one volley. Zero looses
    // them together, which is what all but a handful of skills do.
    inline uint16_t shoot_bullet_delay(int32_t skillid, int32_t bulletid)
    {
        // A thrown star is paced by what is being thrown rather than by the
        // skill throwing it.
        if (bulletid / 10000 == 207 || bulletid / 1000 == 5021
            || skillid == 4111004 || skillid == 5221007)
        {
            return 120;
        }

        switch (skillid)
        {
        // Double Shot is v83-only; GMSv95 knows the id but gives it no gap of
        // its own, so this is the one value in the table applied by analogy.
        case 3001005:  // Double Shot
        case 3111006:  // Strafe
        case 3211006:  // Strafe (Sniper)
        case 13111001:
        case 33001000:
        case 33111001:
            return 60;
        case 35001004:
        case 35101010:
            return 90;
        case 5001003:
        case 5210000:
            return 240;
        default:
            return 0;
        }
    }

    // How often a charging skill looses another volley while its key is held,
    // in ms. Zero means the skill fires once, when the key comes up, which is
    // what all but these few do - Piercing Arrow and every charged cast are
    // held to build one shot rather than to keep shooting.
    inline uint16_t keydown_repeat_interval(int32_t skillid)
    {
        switch (skillid)
        {
        case 3121004:  // Hurricane
        case 13111002: // Hurricane (Wind Archer)
        case 33121009:
        case 5221004:  // Rapid Fire
            return 100;
        case 35001001:
        case 35101009:
            return 300;
        default:
            return 0;
        }
    }

    // The longest a skill's charge gauge runs, in ms. Holding the key beyond
    // this adds nothing, and the client reports the capped value rather than
    // the real one.
    inline uint16_t keydown_max_gauge(int32_t skillid)
    {
        switch (skillid)
        {
        case 3221001:  // Piercing Arrow
        case 33101005:
            return 900;
        case 2121001:  // Big Bang (F/P)
        case 2221001:  // Big Bang (I/L)
        case 2321001:  // Big Bang (Bishop)
        case 5101004:  // Corkscrew Blow
        case 15101003: // Corkscrew Blow (Thunder Breaker)
        case 5201002:  // Grenade
        case 14111006: // Poison Bomb
            return 1000;
        case 22121000: // Ice Breath
        case 22151001: // Fire Breath
            return 500;
        default:
            return 2000;
        }
    }

    // The shortest charge the client will report. A tap that barely registers
    // still counts as this much.
    constexpr uint16_t KEYDOWN_MIN_GAUGE = 30;

    // Skills that carry the animations of a charged cast without being one.
    // v83 fires these on the press and repeats them for as long as the key is
    // held, like any ordinary skill; the later client this was reversed from
    // holds them on a gauge instead. Holding one here used to leave the wind-up
    // looping with nothing ever leaving the bow.
    inline bool keydown_fires_on_press(int32_t skillid)
    {
        switch (skillid)
        {
        case 3221001:  // Piercing Arrow
            return true;
        default:
            return false;
        }
    }

    // How long a skill's tiles hold at full strength before they fade, in ms.
    // The reference client states this per skill where it registers them.
    inline uint16_t tile_hold_time(int32_t skillid)
    {
        switch (skillid)
        {
        case 3111003:  // Inferno
            return 500;
        case 33121005:
            return 3300;
        default:
            return 500;
        }
    }

    // A tile is revealed by fading up to an opacity rolled per tile, out of
    // 255, so a stretch of fire does not read as one flat sheet.
    constexpr int16_t TILE_ALPHA_MIN = 128;
    constexpr int16_t TILE_ALPHA_MAX = 255;
    // How long that fade takes: a multiple of this, one to five, per tile. The
    // spread is what staggers the tiles so each patch lights as an arrow
    // reaches it rather than all of them together.
    constexpr int32_t TILE_FADE_IN_STEP = 100;
    constexpr int32_t TILE_FADE_IN_STEPS = 5;
    // How long they take to fade away once their hold is over.
    constexpr int32_t TILE_FADE_OUT_MS = 500;

    // Where in an attack packet a charging skill's gauge is written. Almost
    // every one puts it directly behind the skill id; these four put it at the
    // end of the shoot block instead, and they are the four that keep firing
    // for as long as they are held rather than spending the charge on one shot.
    // Grenade and Poison Bomb are fired from a weapon too and still use the
    // first position, so the attack type cannot stand in for this list.
    inline bool keydown_reported_late(int32_t skillid)
    {
        switch (skillid)
        {
        case 3121004:  // Hurricane
        case 3221001:  // Piercing Arrow
        case 5221004:  // Rapid Fire
        case 13111002: // Hurricane (Wind Archer)
            return true;
        default:
            return false;
        }
    }

    // What a shot deals to the nth monster it reaches, as a share of what the
    // skill would otherwise do. The client keeps one table of fifteen rates per
    // skill - fifteen being the most monsters an attack can list - and scales
    // every damage line of that target by the entry for its place in the queue.
    //
    // Piercing Arrow is the one that matters here and it is the only one that
    // climbs: the arrow gathers force through each body it passes, so every
    // monster after the first takes a fifth again as much as the one before.
    // Past the sixth the table is zero, which is no loss because nothing it can
    // hit reaches that far.
    inline double pierce_damage_rate(int32_t skillid, size_t index)
    {
        switch (skillid)
        {
        case 3221001:  // Piercing Arrow
        case 33101001:
        {
            static constexpr double rates[] = {
                1.0, 1.2, 1.44, 1.728, 2.0736, 2.48832
            };
            return (index < 6) ? rates[index] : 0.0;
        }
        default:
            return 1.0;
        }
    }

    // Skills a bow or a crossbow keeps firing even with a monster right on top
    // of the character. Everything else it can throw is swung instead, because
    // the reference client tries the melee swing first and looses the arrow
    // only if that found nothing to hit.
    //
    // The list was read out of the reference client's own table, which lives in
    // a v95 binary and exempts every archer skill there is. v83 is stricter:
    // the plain single-target shots drop to a swing at point blank the same way
    // the ordinary attack does, and only the skills below hold their range.
    // What is left reads consistently - the ground-targeted volleys, the
    // piercing and long shots, the one that fires continuously, and the two
    // passives that are never thrown by hand.
    inline bool shoot_fires_point_blank(int32_t skillid)
    {
        switch (skillid)
        {
        case 3100001:  // Final Attack : Bow
        case 3101003:  // Power Knock-Back
        case 3110001:  // Mortal Blow
        case 3111004:  // Arrow Rain
        case 3121003:  // Dragon's Breath
        case 3121004:  // Hurricane
        case 3200001:  // Final Attack : Crossbow
        case 3201003:  // Power Knock-Back (Crossbowman)
        case 3210001:  // Mortal Blow (Sniper)
        case 3211004:  // Arrow Eruption
        case 3221001:  // Piercing Arrow
        case 3221003:  // Dragon's Breath (Marksman)
        case 3221007:  // Snipe
        case 13101002: // Final Attack (Wind Archer)
        case 13101005: // Storm Break
        case 13111000: // Arrow Rain (Wind Archer)
        case 13111002: // Hurricane (Wind Archer)
        case 13111006: // Wind Piercing
        case 13111007: // Wind Shot
            return true;
        default:
            // Arrow Blow, Double Shot, Arrow Bomb, Iron Arrow, Strafe, Inferno
            // and Blizzard are the v83 difference: the v95 table exempts them,
            // this one does not, so they are swung at point blank.
            return false;
        }
    }

    // Base class for attacks and buffs.
    class SpecialMove
    {
    public:
        enum ForbidReason
        {
            FBR_NONE,
            FBR_WEAPONTYPE,
            FBR_HPCOST,
            FBR_MPCOST,
            FBR_BULLETCOST,
            FBR_COOLDOWN,
            FBR_OTHER
        };

        // How a move is cast. A plain move goes off the moment it is used; the
        // other two run through extra stages first, which is what the prepare
        // and keydown animations exist for.
        enum CastKind
        {
            // Used and resolved on the same frame.
            CAST_INSTANT,
            // Plays a wind-up before resolving.
            CAST_PREPARE,
            // Charges for as long as its key is held.
            CAST_KEYDOWN
        };

        // What a move paints over the ground it affects: an animation shown
        // where it landed, and a tile left on the terrain. The animation may be
        // a single one played in place, or a stream of copies emitted across
        // the area - Arrow Rain's falling arrows and Arrow Eruption's rising
        // ones are both the latter.
        // How a move paints the area it affects. The reference client hands
        // "special" to one of three registrars on CAnimationDisplayer, each
        // with its own geometry, and picks the anchor by skill id - so a single
        // "show the special animation" does not reproduce any of them.
        enum EmitterKind
        {
            // No area animation.
            EMIT_NONE,
            // A single animation played once where the move landed.
            EMIT_ONCE,
            // Copies scattered through the box, each travelling and repeating.
            EMIT_FALLING,
            // Copies bursting outward from the box's centre, each played once.
            EMIT_EXPLOSION
        };

        // Where the box the copies fill is placed.
        enum AnchorKind
        {
            // On the caster.
            ANCHOR_CASTER,
            // Where the shot first connected.
            ANCHOR_IMPACT,
            // One emitter per struck monster, on that monster.
            ANCHOR_PER_MOB
        };

        struct AreaEffect
        {
            EmitterKind kind = EMIT_NONE;
            AnchorKind anchor = ANCHOR_CASTER;

            // Shown once for EMIT_ONCE.
            nl::node special;
            // The animations a copy may use; one is picked at random per copy.
            std::vector<nl::node> variants;

            // Copies emitted per tick, the gap between ticks, when the first
            // tick happens and when emitting stops - all in ms. The emitter
            // runs for duration, so it produces count copies every interval
            // until then, not count copies in total.
            int16_t count = 0;
            int16_t interval = 0;
            int16_t start = 0;
            int16_t duration = 0;
            // EMIT_FALLING only: how far a copy travels, and how long it takes.
            // "fall" is a time, not a distance.
            int16_t travelx = 0;
            int16_t travely = 0;
            int16_t falltime = 0;
            // Opacity a copy is drawn at; zero means pick one per copy.
            int16_t alpha = 0;

            // The box copies spawn inside, relative to the anchor, and the
            // amount the caller shifts it up by.
            Rectangle<int16_t> spawnbox;
            int16_t lifttop = 0;
            int16_t liftbottom = 0;

            // Left on the terrain for lifetime ms. A tile node holds a set of
            // animations rather than one, the same way special does: the client
            // lays a row of them across the affected ground and picks between
            // the variants for each, which is what makes a burning stretch of
            // floor read as fire rather than as one repeated sprite.
            std::vector<nl::node> tiles;
            // How far apart along the ground consecutive tiles are laid.
            int16_t tilestep = 0;
            uint16_t lifetime = 0;
        };

        virtual ~SpecialMove() {}

        virtual void apply_useeffects(Char& user) const = 0;
        virtual void apply_actions(Char& user, Attack::Type type) const = 0;
        virtual void apply_stats(const Char& user, Attack& attack) const = 0;
        virtual void apply_hiteffects(const AttackUser& user, Mob& target) const = 0;
        virtual Animation get_bullet(const Char& user, int32_t bulletid) const = 0;

        virtual AreaEffect get_area_effect(const Char&) const
        {
            return {};
        }

        virtual CastKind get_cast_kind() const
        {
            return CAST_INSTANT;
        }
        // How long the wind-up runs, in ms. Only meaningful for CAST_PREPARE.
        virtual uint16_t get_prepare_time() const
        {
            return 0;
        }
        // Delay between the cast and the projectile leaving the caster, in ms.
        virtual uint16_t get_ball_delay() const
        {
            return 0;
        }
        // The wind-up animation, for a move that has one.
        virtual nl::node get_prepare_effect() const
        {
            return {};
        }
        // The animation shown while a keydown move is charging, and the one that
        // closes it out.
        virtual nl::node get_keydown_effect() const
        {
            return {};
        }
        virtual nl::node get_keydown_end_effect() const
        {
            return {};
        }
        // Shown on the caster once the move is over.
        virtual nl::node get_finish_effect() const
        {
            return {};
        }
        // Cooldown in ms, or zero when the move has none.
        virtual int32_t get_cooldown(int32_t) const
        {
            return 0;
        }

        // Whether the server expects this move to report a charge time. That
        // is a property of the skill's own data rather than of how the client
        // chooses to cast it, so the two are kept apart.
        virtual bool reports_keydown() const
        {
            return false;
        }

        virtual bool is_attack() const = 0;
        virtual bool is_skill() const = 0;
        // Whether the move is one the player never uses directly. A regular
        // attack never is; only a skill can be.
        virtual bool is_passive() const
        {
            return false;
        }
        // Whether the server needs to be told where the move was cast. Only a
        // move that leaves something standing on the map does.
        virtual bool needs_position() const
        {
            return false;
        }
        virtual int32_t get_id() const = 0;

        virtual ForbidReason can_use(int32_t level, Weapon::Type weapon,
            const Job& job, uint16_t hp, uint16_t mp, uint16_t bullets) const = 0;
    };
}
