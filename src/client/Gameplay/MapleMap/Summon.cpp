#include "Summon.h"

#include "../../Constants.h"
#include "../../Data/SkillData.h"
#include "../../Util/Randomizer.h"

#include "nlnx/nx.hpp"

#include <algorithm>
#include <cstdlib>

namespace jrc
{
    namespace
    {
        // A following summon does not chase its owner. It picks a point spread
        // this far around them, flies there, and picks another once it arrives.
        // Written as the full width of the spread, the way the client rolls it.
        constexpr int32_t FLY_SPREAD_X = 120;
        constexpr int32_t FLY_SPREAD_Y = 60;
        // How close counts as having arrived, and the same slack the steering
        // uses before it bothers pushing along an axis at all.
        constexpr int16_t FLY_ARRIVE_SLACK = 10;
        // How far the owner may drift from the point the summon is flying to
        // before that point is abandoned and a fresh one picked. Without this a
        // summon would finish a stale journey before noticing its owner left.
        constexpr int16_t FLY_OWNER_SLACK = 120;

        // How hard a summon pushes itself along, and how quickly it sheds that
        // speed again. The reference client hands its flyer a direction and
        // lets the shared vector controller decide the rest, which is not the
        // same machinery as this client's physics, so both figures are the ones
        // this client already flies its other followers under.
        constexpr double FLY_FORCE = 0.2;
        constexpr double FLY_FRICTION = 0.05;

        // How far the owner may get from the summon before it gives up flying
        // and simply appears next to them. The reference client carries a box
        // around the summon for this and teleports the moment its owner leaves
        // it - a bird has no way to path down through a platform its owner just
        // dropped off, so without this it is stranded on top of one. The box
        // itself is set outside the routines that were recovered, so the
        // distance is the one this client already recalls its flying pets at.
        constexpr int32_t FLY_RECALL_DISTANCE = 250;

        // The shortest gap between two of a summon's attacks, in ms. The
        // reference client states this per skill and gives every summon that is
        // not an octopus this same figure - which is the whole reason a hawk
        // pecks at a monster every few seconds rather than continuously.
        constexpr int32_t ATTACK_DELAY_DEFAULT = 3000;
        constexpr int32_t ATTACK_DELAY_OCTOPUS = 1500;

        const Randomizer randomizer;

        int32_t attack_delay_for(int32_t skillid)
        {
            switch (skillid)
            {
            case 5211002:  // Gaviota
            case 5220002:  // Wrath of the Octopi
                return ATTACK_DELAY_OCTOPUS;
            default:
                return ATTACK_DELAY_DEFAULT;
            }
        }
    }

    std::string Summon::nameof(Stance stance)
    {
        switch (stance)
        {
        case SUMMONED:
            return "summoned";
        case FLY:
            return "fly";
        case ATTACK:
            return "attack1";
        case HIT:
            return "hit";
        case DIE:
            return "die";
        default:
            return "stand";
        }
    }

    Summon::Summon(int32_t o, int32_t ow, int32_t sid, int32_t slvl,
        MovementType mt, bool att, Point<int16_t> position, bool fl)
        : MapObject(o, position), owner(ow), skillid(sid), skilllevel(slvl),
          movementtype(mt), attacks(att), flip(fl) {

        nl::node src = SkillData::get(skillid).get_summon();
        for (int32_t i = 0; i < NUM_STANCES; i++)
        {
            Stance which = static_cast<Stance>(i);
            if (nl::node sub = src[nameof(which)])
            {
                animations.emplace(which, sub);
            }
        }
        attackinfo = src["attack1"]["info"];

        stance = has_stance(SUMMONED) ? SUMMONED : resting_stance();
        reported = position;
        hasflytarget = false;
        dying = false;
        expired = false;
        attackcooldown = 0;
        stancetimer = 0;

        // Nothing about a summon is decided by the terrain: a puppet is planted
        // where the server put it, and a follower flies over the map rather
        // than along it. Both therefore integrate their own motion and are kept
        // clear of the physics that would stand them on the nearest platform.
        phobj.type = PhysicsObject::FIXATED;
    }

    bool Summon::has_stance(Stance which) const
    {
        return animations.count(which) > 0;
    }

    Summon::Stance Summon::resting_stance() const
    {
        // A flying summon hovers rather than stands, and a summon with neither
        // animation simply keeps whatever it has.
        if (movementtype == CIRCLE_FOLLOW && has_stance(FLY))
            return FLY;
        if (has_stance(STAND))
            return STAND;
        return has_stance(FLY) ? FLY : SUMMONED;
    }

    void Summon::set_stance(Stance newstance)
    {
        if (!has_stance(newstance))
            newstance = resting_stance();

        if (stance == newstance)
            return;

        stance = newstance;

        auto iter = animations.find(stance);
        if (iter != animations.end())
            iter->second.reset();
    }

    void Summon::draw(double viewx, double viewy, float alpha) const
    {
        if (expired)
            return;

        Point<int16_t> absp = phobj.get_absolute(viewx, viewy, alpha);

        auto iter = animations.find(stance);
        if (iter != animations.end())
        {
            iter->second.draw(DrawArgument(absp, flip), alpha);
        }
    }

    int8_t Summon::update(const Physics& physics)
    {
        physics.move_object(phobj);

        auto iter = animations.find(stance);
        bool ended = (iter != animations.end()) && iter->second.update();

        if (attackcooldown > 0)
            attackcooldown -= Constants::TIMESTEP;

        if (dying)
        {
            // A death animation that has run out, or a summon with none at all,
            // is done being drawn.
            if (ended || iter == animations.end())
                expired = true;
            return phobj.fhlayer;
        }

        if (stancetimer > 0)
        {
            stancetimer -= Constants::TIMESTEP;
            if (stancetimer <= 0)
                set_stance(resting_stance());
        }
        else if (ended && (stance == SUMMONED || stance == ATTACK || stance == HIT))
        {
            // These three each play once and hand back to the resting stance.
            set_stance(resting_stance());
        }

        return phobj.fhlayer;
    }

    void Summon::set_movement(Point<int16_t> position, bool fl)
    {
        set_position(position);
        flip = fl;
    }

    void Summon::follow(Point<int16_t> ownerposition)
    {
        if (movementtype == STATIONARY || dying)
            return;

        Point<int16_t> at = get_position();

        // Out of reach: appear beside the owner and start over. This is the
        // client's own answer to a summon that cannot get where it needs to be,
        // and it is what carries one across a gap or down off a platform.
        if ((ownerposition - at).length() > FLY_RECALL_DISTANCE)
        {
            set_position(ownerposition);
            phobj.hspeed = 0.0;
            phobj.vspeed = 0.0;
            hasflytarget = false;
            // The reported position is deliberately left alone, so the next
            // update sends the whole jump and everyone else's screen moves the
            // summon too rather than leaving it where it was stranded.
            // The client replays the arrival animation on a recall, so the
            // summon reads as having flown back rather than blinked.
            set_stance(has_stance(SUMMONED) ? SUMMONED : resting_stance());
            return;
        }

        if (hasflytarget)
        {
            int32_t gapx = flytarget.x() - at.x();
            int32_t gapy = flytarget.y() - at.y();

            // Push along an axis only once the gap is worth closing, so the
            // summon coasts to a stop instead of hunting around the point.
            double hforce = 0.0;
            if (std::abs(gapx) >= FLY_ARRIVE_SLACK)
            {
                hforce = (gapx > 0) ? FLY_FORCE : -FLY_FORCE;
                flip = gapx > 0;
            }

            // The reference client only ever pushes a flyer upwards and lets it
            // sink under its own weight. Nothing here weighs anything, so the
            // descent has to be driven as well.
            double vforce = 0.0;
            if (std::abs(gapy) >= FLY_ARRIVE_SLACK)
            {
                vforce = (gapy > 0) ? FLY_FORCE : -FLY_FORCE;
            }

            // A summon flies over the terrain rather than through it, so its
            // motion is worked out here rather than handed to the physics that
            // would stand it on the nearest platform and leave it there.
            phobj.hspeed += hforce - FLY_FRICTION * phobj.hspeed;
            phobj.vspeed += vforce - FLY_FRICTION * phobj.vspeed;

            const bool arrived = std::abs(gapx) <= FLY_ARRIVE_SLACK
                && std::abs(gapy) <= FLY_ARRIVE_SLACK;
            // The owner walking away from where the summon was headed strands
            // it finishing a journey to nowhere, so that point is dropped too.
            const bool stale =
                std::abs(ownerposition.x() - flytarget.x()) > FLY_OWNER_SLACK
                || std::abs(ownerposition.y() - flytarget.y()) > FLY_OWNER_SLACK;

            if (arrived || stale)
                hasflytarget = false;
        }

        if (!hasflytarget)
        {
            flytarget = {
                static_cast<int16_t>(ownerposition.x()
                    + randomizer.next_int<int32_t>(FLY_SPREAD_X) - FLY_SPREAD_X / 2),
                static_cast<int16_t>(ownerposition.y()
                    + randomizer.next_int<int32_t>(FLY_SPREAD_Y) - FLY_SPREAD_Y / 2)
            };
            hasflytarget = true;
        }

        if (stance != ATTACK && stance != HIT && stance != SUMMONED)
            set_stance(has_stance(FLY) ? FLY : resting_stance());
    }

    void Summon::face(bool toleft)
    {
        flip = !toleft;
    }

    void Summon::show_attack()
    {
        if (dying)
            return;

        // The swing runs for exactly as long as its animation does; the stance
        // hands back on its own when the last frame is up.
        set_stance(ATTACK);
        stancetimer = 0;
    }

    void Summon::show_hit()
    {
        if (dying || !has_stance(HIT))
            return;

        set_stance(HIT);
    }

    void Summon::kill(bool animated)
    {
        dying = true;
        if (animated && has_stance(DIE))
        {
            stance = DIE;
            animations[stance].reset();
        }
        else
        {
            expired = true;
        }
    }

    bool Summon::is_expired() const
    {
        return expired;
    }

    int32_t Summon::get_owner() const
    {
        return owner;
    }

    int32_t Summon::get_skillid() const
    {
        return skillid;
    }

    int32_t Summon::get_skilllevel() const
    {
        return skilllevel;
    }

    bool Summon::getflip() const
    {
        return flip;
    }

    bool Summon::can_attack() const
    {
        // Two things hold a summon back: the gap the client imposes between one
        // attack and the next, and the swing it is already in the middle of.
        return attacks && !dying && attackinfo
            && attackcooldown <= 0 && stance != ATTACK;
    }

    Rectangle<int16_t> Summon::get_attack_range() const
    {
        // The box is written around the summon, so it is simply carried to
        // wherever the summon is standing.
        nl::node range = attackinfo["range"];
        Rectangle<int16_t> box = range;

        Point<int16_t> at = get_position();
        return {
            static_cast<int16_t>(at.x() + box.l()),
            static_cast<int16_t>(at.x() + box.r()),
            static_cast<int16_t>(at.y() + box.t()),
            static_cast<int16_t>(at.y() + box.b())
        };
    }

    uint8_t Summon::get_mobcount() const
    {
        return static_cast<uint8_t>(
            attackinfo["mobCount"].get_integer(1)
        );
    }

    uint16_t Summon::get_attack_delay() const
    {
        return static_cast<uint16_t>(
            attackinfo["attackAfter"].get_integer(0)
        );
    }

    Point<int16_t> Summon::get_reported_position() const
    {
        return reported;
    }

    void Summon::mark_reported()
    {
        reported = get_position();
    }

    uint8_t Summon::get_stance_byte() const
    {
        return flip ? 0 : 1;
    }

    void Summon::start_attack_cooldown()
    {
        attackcooldown = attack_delay_for(skillid);
    }
}
