#include "MapSummons.h"

#include "MapMobs.h"

#include "../../Character/Player.h"
#include "../Combat/Combat.h"
#include "../../Constants.h"
#include "../../Data/SkillData.h"
#include "../../Net/Packets/SummonPackets.h"

#include <algorithm>
#include <vector>

namespace jrc
{
    namespace
    {
        // How often a summon this client drives tells the server where it is.
        // Streaming every step would put a packet on the wire every tick for
        // something the server only needs an approximate position for.
        constexpr int32_t MOVEMENT_INTERVAL = 200;

        // What share of the caster's own maximum a summon deals per point of
        // the attack its level data states. The server holds a client to this
        // exact ceiling, so the same two constants have to be used here or a
        // legitimate hit reads to it as an edited packet.
        constexpr double SUMMON_DAMAGE_MOD_LOW = 0.077;
        constexpr double SUMMON_DAMAGE_MOD_HIGH = 0.054;
        // The maximum above which the smaller of the two applies.
        constexpr int32_t SUMMON_DAMAGE_MOD_CUTOFF = 438;
        // The equivalent rate for a summon that deals magic damage.
        constexpr double SUMMON_MAGIC_DAMAGE_MOD = 0.05;

        // The bounds one of a summon's hits falls between.
        std::pair<double, double> summon_damage(const Player& player,
            int32_t skillid, int32_t skilllevel) {

            const SkillData::Stats& stats =
                SkillData::get(skillid).get_stats(skilllevel);

            // A summon without a weapon attack of its own is a magic one and
            // spends its caster's magic instead. Both scale off the caster's
            // own maximum; only the rate differs.
            const bool magic = stats.watk == 0;
            const int32_t power = magic ? stats.matk : stats.watk;
            const int32_t base = player.get_stats().get_maxdamage();

            const double mod = magic
                ? SUMMON_MAGIC_DAMAGE_MOD
                : ((base >= SUMMON_DAMAGE_MOD_CUTOFF)
                    ? SUMMON_DAMAGE_MOD_HIGH
                    : SUMMON_DAMAGE_MOD_LOW);

            const double max = base * mod * power;
            return { max * player.get_stats().mastery_ratio(), max };
        }
    }

    void MapSummons::draw(Layer::Id layer, double viewx, double viewy, float alpha) const
    {
        summons.draw(layer, viewx, viewy, alpha);
    }

    void MapSummons::update(const Physics& physics, const Player& player,
        MapMobs& mobs, Combat& combat) {

        // Steering first: it only sets the forces the summon flies under, so it
        // has to happen before physics moves anything or every summon would be
        // acting on the forces it was given a tick ago.
        for (auto& entry : summons)
        {
            Summon& summon = static_cast<Summon&>(*entry.second);
            if (summon.get_owner() == player.get_oid())
                summon.follow(player.get_position());
        }

        summons.update(physics);

        movementtimer += Constants::TIMESTEP;
        const bool report = movementtimer >= MOVEMENT_INTERVAL;
        if (report)
            movementtimer = 0;

        std::vector<int32_t> gone;
        for (auto& entry : summons)
        {
            Summon& summon = static_cast<Summon&>(*entry.second);

            if (summon.is_expired())
            {
                gone.push_back(entry.first);
                continue;
            }

            // Only the summon's owner decides what it hits and tells the server
            // where it went.
            if (summon.get_owner() != player.get_oid())
                continue;

            // The whole stretch walked since the last report is sent as one
            // step, so a summon that drifted between two reports is not left
            // behind on everyone else's screen.
            Point<int16_t> from = summon.get_reported_position();
            Point<int16_t> to = summon.get_position();
            if (report && from != to)
            {
                Movement movement(
                    to.x(), to.y(), from.x(), from.y(),
                    summon.get_stance_byte(),
                    static_cast<int16_t>(MOVEMENT_INTERVAL)
                );
                MoveSummonPacket(summon.get_oid(), from, movement).dispatch();
                summon.mark_reported();
            }

            try_attack(summon, player, mobs, combat);
        }

        for (int32_t oid : gone)
        {
            summons.remove(oid);
        }
    }

    void MapSummons::try_attack(Summon& summon, const Player& player,
        MapMobs& mobs, Combat& combat) {

        if (!summon.can_attack())
            return;

        // The summon's box is already in map coordinates, so the attack is
        // handed the box outright rather than one written around a caster.
        Rectangle<int16_t> box = summon.get_attack_range();

        Attack attack;
        attack.type = Attack::CLOSE;
        attack.damagetype = Attack::DMG_WEAPON;
        attack.origin = summon.get_position();
        attack.range = {
            static_cast<int16_t>(box.l() - attack.origin.x()),
            static_cast<int16_t>(box.r() - attack.origin.x()),
            static_cast<int16_t>(box.t() - attack.origin.y()),
            static_cast<int16_t>(box.b() - attack.origin.y())
        };
        attack.mobcount = summon.get_mobcount();
        attack.hitcount = 1;
        attack.toleft = summon.getflip();
        attack.playerlevel = static_cast<int16_t>(player.get_level());
        attack.accuracy = player.get_stats().get_total(Equipstat::ACC);
        attack.skill = summon.get_skillid();

        auto damage = summon_damage(
            player, summon.get_skillid(), summon.get_skilllevel()
        );
        attack.mindamage = damage.first;
        attack.maxdamage = damage.second;

        AttackResult result = mobs.send_attack(attack);
        if (result.damagelines.empty())
        {
            // Nothing in reach. The summon keeps hovering and tries again on
            // the next tick rather than swinging at nothing.
            return;
        }

        // The client picks the side its swing catches most on and turns the
        // summon that way before it swings. Every summon box in the game files
        // is symmetric about the summon, so the two sides always catch the same
        // monsters and only the turn is left of that - a hawk still has to face
        // what it is pecking at.
        if (mobs.contains(result.first_oid))
        {
            attack.toleft =
                mobs.get_mob_position(result.first_oid).x() < attack.origin.x();
            summon.face(attack.toleft);
            result.toleft = attack.toleft;
        }

        summon.show_attack();
        summon.start_attack_cooldown();

        std::vector<SummonAttackPacket::Target> targets;
        targets.reserve(result.damagelines.size());
        for (auto& line : result.damagelines)
        {
            int32_t total = 0;
            for (auto& single : line.second)
            {
                total += single.first;
            }
            targets.emplace_back(line.first, total);
        }

        // The damage itself is handed to the combat component, which is where
        // damage numbers live and where the delay before a hit lands is kept.
        combat.apply_summon_damage(
            result,
            AttackUser{
                summon.get_skilllevel(), player.get_level(), false,
                !attack.toleft
            },
            summon.get_attack_delay()
        );

        SummonAttackPacket(
            summon.get_oid(), summon.get_position(), attack.toleft, targets
        ).dispatch();
    }

    void MapSummons::spawn(int32_t oid, int32_t owner, int32_t skillid,
        int32_t skilllevel, Summon::MovementType movementtype, bool attacks,
        Point<int16_t> position, bool flip) {

        // One summon per skill per owner: casting Puppet again picks the puppet
        // up and puts it down somewhere else rather than leaving two standing.
        std::vector<int32_t> replaced;
        for (auto& entry : summons)
        {
            Summon& existing = static_cast<Summon&>(*entry.second);
            if (existing.get_owner() == owner && existing.get_skillid() == skillid)
            {
                replaced.push_back(entry.first);
            }
        }
        for (int32_t old : replaced)
        {
            summons.remove(old);
        }

        summons.add(std::make_unique<Summon>(
            oid, owner, skillid, skilllevel, movementtype, attacks, position, flip
        ));
    }

    void MapSummons::remove(int32_t, int32_t oid, bool animated)
    {
        if (Optional<Summon> summon = summons.get(oid))
        {
            summon->kill(animated);
        }
    }

    void MapSummons::clear()
    {
        summons.clear();
    }

    void MapSummons::send_movement(int32_t, int32_t oid, Point<int16_t> position, bool flip)
    {
        if (Optional<Summon> summon = summons.get(oid))
        {
            summon->set_movement(position, flip);
        }
    }

    void MapSummons::show_attack(int32_t, int32_t oid)
    {
        if (Optional<Summon> summon = summons.get(oid))
        {
            summon->show_attack();
        }
    }

    void MapSummons::show_hit(int32_t, int32_t oid)
    {
        if (Optional<Summon> summon = summons.get(oid))
        {
            summon->show_hit();
        }
    }
}
