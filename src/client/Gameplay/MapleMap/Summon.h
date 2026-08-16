#pragma once
#include "MapObject.h"

#include "../Combat/Attack.h"

#include "../../Graphics/Animation.h"
#include "../../Graphics/Geometry.h"
#include "../../Template/Rectangle.h"

#include "nlnx/node.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace jrc
{
    // A creature a skill puts on the map on its caster's behalf: a puppet
    // standing where it was planted, a hawk or a phoenix circling overhead.
    //
    // Everything a summon looks like and everything it does comes out of the
    // skill's own summon node - the stances it can hold, the box it looks for
    // monsters in, how long its swing takes to land. Nothing here is keyed on
    // a skill id, so a summon the game files describe works whether or not
    // anybody thought to list it.
    class Summon : public MapObject
    {
    public:
        // How a summon carries itself. The server states which of the three a
        // summon uses when it spawns it.
        enum MovementType : uint8_t
        {
            // Stays exactly where it was put. Puppets do this.
            STATIONARY = 0,
            // Walks after its owner along the ground.
            FOLLOW = 1,
            // Flies after its owner, ignoring the terrain. The bird summons do
            // this, which is why they cross gaps their owner has to jump.
            CIRCLE_FOLLOW = 3
        };

        Summon(int32_t oid, int32_t owner, int32_t skillid, int32_t skilllevel,
            MovementType movementtype, bool attacks, Point<int16_t> position,
            bool flip);

        void draw(double viewx, double viewy, float alpha) const override;
        int8_t update(const Physics& physics) override;

        // Move the summon to where its owner's client says it went.
        void set_movement(Point<int16_t> position, bool flip);
        // Steer after the owner for one tick. This only sets the forces the
        // summon flies under; the move itself happens when physics runs.
        void follow(Point<int16_t> ownerposition);
        // Begin the swing everyone else in the map is being told about.
        void show_attack();
        // Turn the summon to face one way or the other.
        void face(bool toleft);
        // Play the hit animation, which is all a summon taking damage does -
        // it has no health of its own that this client tracks.
        void show_hit();
        // Start the death animation. The summon is dropped once it finishes,
        // or at once when it has no animation to play.
        void kill(bool animated);

        // Whether the summon soaks attacks on its owner's behalf. Only one kind
        // of summon does, and the game files say which: it is the one given a
        // hit animation, because it is the only one that ever gets hit.
        bool is_decoy() const;
        // Whether it is due to take another hit. A decoy is briefly untouchable
        // after each one, or a monster standing on it would empty it in a tick.
        bool can_be_hit() const;
        // Take a hit. The summon dies once its health is gone.
        void take_damage(int32_t damage);
        // The box a monster has to be standing in to be hitting it.
        Rectangle<int16_t> get_body_rect() const;

        // Whether the summon has finished dying and can be dropped.
        bool is_expired() const;
        int32_t get_owner() const;
        int32_t get_skillid() const;
        int32_t get_skilllevel() const;
        bool getflip() const;

        // Whether this summon is due to swing, given how long it has been since
        // it last did. A summon that cannot attack is never due.
        bool can_attack() const;
        // The box it looks for monsters in, in map coordinates.
        Rectangle<int16_t> get_attack_range() const;
        // How many monsters one swing may catch.
        uint8_t get_mobcount() const;
        // How long after the swing starts its damage lands, in ms.
        uint16_t get_attack_delay() const;
        // Note that a swing has just been thrown, so the next one waits.
        void start_attack_cooldown();

        // Where this summon was the last time its owner told the server, and a
        // note that it has just been told again. Positions are streamed on a
        // timer rather than per step, so the two have to be tracked apart from
        // the live position.
        Point<int16_t> get_reported_position() const;
        void mark_reported();
        // The stance byte a summon travels as: the low bit says which way it
        // faces, the same convention every other map object uses.
        uint8_t get_stance_byte() const;

    private:
        // The stances a summon can hold. Which of them a given summon actually
        // has depends on what the game files give it: a puppet has no fly and
        // no attack, and the code falls back rather than drawing nothing.
        enum Stance
        {
            SUMMONED,
            STAND,
            FLY,
            ATTACK,
            HIT,
            DIE,
            NUM_STANCES
        };

        static std::string nameof(Stance stance);

        // Switch to a stance, falling back to one the summon actually has.
        void set_stance(Stance newstance);
        // Whether an animation exists for this stance.
        bool has_stance(Stance stance) const;
        // The stance a summon settles into when it has nothing else to do.
        Stance resting_stance() const;

        std::unordered_map<Stance, Animation> animations;
        // The attack1/info node, which describes the swing.
        nl::node attackinfo;

        int32_t owner;
        int32_t skillid;
        int32_t skilllevel;
        MovementType movementtype;
        // Whether the server said this summon may attack at all.
        bool attacks;

        Stance stance;
        bool flip;
        bool dying;
        bool expired;

        // The position last sent to the server, so a summon that has drifted
        // since then can be told apart from one standing still.
        Point<int16_t> reported;

        // Where the summon is currently flying to, and whether it has one. A
        // following summon does not home in on its owner: it picks a point near
        // them, flies to it, and picks another. That is what gives a hawk its
        // wandering circle rather than a rigid tether.
        Point<int16_t> flytarget;
        bool hasflytarget;

        // What the summon can soak before it goes, what it started with, and
        // how long it has left to stand there at all. All from the level data.
        int32_t hp;
        int32_t maxhp;
        int32_t lifetime;
        // Shown over a decoy for as long as it is standing, so its owner can
        // see how much of it is left rather than guessing.
        MobHpBar hpbar;
        // Time left before it may be hit again, in ms.
        int32_t hitperiod;

        // Time left before this summon may swing again, in ms.
        int32_t attackcooldown;
        // Time left before the current one-shot stance gives way, in ms.
        int32_t stancetimer;
    };
}
