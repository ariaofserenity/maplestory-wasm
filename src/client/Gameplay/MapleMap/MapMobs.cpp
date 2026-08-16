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
#include "MapMobs.h"

#include "Mob.h"

#include <algorithm>
#include <iostream>
#include <map>


namespace jrc
{
    namespace
    {
        // How close a monster has to be to a puppet before it walks to it. The
        // server uses the same reach to decide which monsters it has to nudge
        // the client about, so the two agree on which are under its pull.
        constexpr int32_t PUPPET_AGGRO_RANGE = 421;
    }

    void MapMobs::draw(Layer::Id layer, double viewx, double viewy, float alpha) const
    {
        mobs.draw(layer, viewx, viewy, alpha);
    }

    void MapMobs::update(const Physics& physics)
    {
        for ( ; !spawns.empty(); spawns.pop())
        {
            const MobSpawn& spawn = spawns.front();

            if (Optional<Mob> mob = mobs.get(spawn.get_oid()))
            {
                int8_t mode = spawn.get_mode();
                if (mode > 0)
                {
                    mob->set_control(mode);
                }
                mob->makeactive();
            }
            else
            {
                mobs.add(
                    spawn.instantiate()
                );
            }
        }

        // A puppet pulls in whatever is close enough to notice it. Only the
        // mobs this client drives can be told where to go; everybody else's are
        // moved by whoever controls them.
        for (auto& entry : mobs)
        {
            Optional<Mob> mob = entry.second.get();
            if (!mob)
                continue;

            if (haspuppet
                && (puppet - mob->get_position()).length() <= PUPPET_AGGRO_RANGE)
            {
                mob->aggro_to(puppet);
            }
            else
            {
                mob->clear_aggro();
            }
        }

        mobs.update(physics);
    }

    void MapMobs::set_puppet(bool active, Point<int16_t> position)
    {
        haspuppet = active;
        puppet = position;
    }

    bool MapMobs::has_mob_in_reach(Rectangle<int16_t> range,
        Point<int16_t> origin, bool toleft) const {

        // Every rect in the game files is written facing left and is mirrored
        // when the attacker faces right, exactly as send_attack does it.
        Rectangle<int16_t> box = toleft
            ? Rectangle<int16_t>{
                static_cast<int16_t>(origin.x() + range.l()),
                static_cast<int16_t>(origin.x() + range.r()),
                static_cast<int16_t>(origin.y() + range.t()),
                static_cast<int16_t>(origin.y() + range.b()) }
            : Rectangle<int16_t>{
                static_cast<int16_t>(origin.x() - range.r()),
                static_cast<int16_t>(origin.x() - range.l()),
                static_cast<int16_t>(origin.y() + range.t()),
                static_cast<int16_t>(origin.y() + range.b()) };

        return find_touching(box) != 0;
    }

    int32_t MapMobs::find_touching(Rectangle<int16_t> box) const
    {
        for (auto& entry : mobs)
        {
            Optional<const Mob> mob = entry.second.get();
            if (mob && mob->is_alive() && mob->is_in_range(box))
            {
                return entry.first;
            }
        }
        return 0;
    }

    void MapMobs::spawn(MobSpawn&& spawn)
    {
        spawns.emplace(
            std::move(spawn)
        );
    }

    void MapMobs::remove(int32_t oid, int8_t animation)
    {
        if (Optional<Mob> mob = mobs.get(oid))
        {
            mob->kill(animation);
        }
    }

    void MapMobs::clear()
    {
        mobs.clear();
    }

    void MapMobs::set_control(int32_t oid, bool control)
    {
        int8_t mode = control ? 1 : 0;
        if (Optional<Mob> mob = mobs.get(oid))
        {
            mob->set_control(mode);
        }
    }

    void MapMobs::send_mobhp(int32_t oid, int8_t percent, uint16_t playerlevel)
    {
        if (Optional<Mob> mob = mobs.get(oid))
        {
            mob->show_hp(percent, playerlevel);
        }
    }

    void MapMobs::send_movement(int32_t oid, Point<int16_t> start, std::vector<Movement>&& movements)
    {
        if (Optional<Mob> mob = mobs.get(oid))
        {
            mob->send_movement(
                start,
                std::move(movements)
            );
        }
    }

    AttackResult MapMobs::send_attack(const Attack& attack)
    {
        Point<int16_t> origin    = attack.origin;
        Rectangle<int16_t> range = attack.range;
        // A fired attack does not use the rect at all: the reference client
        // builds a line at the muzzle running out to the reach and widens it
        // by the skill's own vertical adjustment, which is zero for most of
        // them. The rect a shoot skill carries describes the area its burst
        // covers where it lands, not what the shot can reach on the way.
        if (attack.linear && attack.reach > 0)
        {
            const int16_t muzzley = static_cast<int16_t>(
                origin.y() - Attack::MUZZLE_HEIGHT
            );

            if (!attack.rectsearch)
            {
                return shoot_wedge(attack, { origin.x(), muzzley });
            }

            int16_t top = static_cast<int16_t>(muzzley - attack.vadjust);
            int16_t bottom = static_cast<int16_t>(muzzley + 1 + attack.vadjust);

            range = attack.toleft
                ? Rectangle<int16_t>{
                    static_cast<int16_t>(origin.x() - attack.reach),
                    origin.x(), top, bottom }
                : Rectangle<int16_t>{
                    origin.x(),
                    static_cast<int16_t>(origin.x() + attack.reach),
                    top, bottom };

            return collect_targets(attack, range, origin);
        }

        int16_t l = range.l();
        int16_t r = range.r();
        if (attack.reach > 0)
        {
            l = static_cast<int16_t>(-attack.reach);
            r = 0;
        }

        // Every rect in the game files is written facing left, and adjust_rect
        // negates and swaps its x edges when the character faces right - the
        // mirror is on the right branch, not the left. It makes no difference
        // to the skill rects, which are symmetric about x, but an afterimage's
        // is not: its trail runs from -w to 0, so mirroring it the wrong way
        // put the whole box behind the character.
        if (attack.toleft)
        {
            range = {
                static_cast<int16_t>(origin.x() + l),
                static_cast<int16_t>(origin.x() + r),
                static_cast<int16_t>(origin.y() + range.t()),
                static_cast<int16_t>(origin.y() + range.b())
            };
        }
        else
        {
            range = {
                static_cast<int16_t>(origin.x() - r),
                static_cast<int16_t>(origin.x() - l),
                static_cast<int16_t>(origin.y() + range.t()),
                static_cast<int16_t>(origin.y() + range.b())
            };
        }

        return collect_targets(attack, range, origin);
    }

    AttackResult MapMobs::shoot_wedge(const Attack& attack, Point<int16_t> muzzle)
    {
        AttackResult result = attack;

        // The reference client walks outward from the muzzle in 20 px slices,
        // each one a band reaching a quarter of the distance travelled above
        // and below the muzzle, and takes the first monster any of them
        // touches. A shot therefore forgives more height the further it goes,
        // and hits one monster however many stand behind it.
        constexpr int16_t SLICE = 20;
        const int16_t step = attack.toleft
            ? static_cast<int16_t>(-SLICE)
            : SLICE;

        int32_t first = 0;
        Point<int16_t> at;
        for (int16_t travelled = 0; travelled < attack.reach && !first;
             travelled += SLICE)
        {
            int16_t x = static_cast<int16_t>(
                muzzle.x() + (attack.toleft ? -travelled : travelled)
            );
            int16_t far = static_cast<int16_t>(x + step);
            int16_t spread = static_cast<int16_t>(travelled / SHOOT_WEDGE_SLOPE);

            Rectangle<int16_t> slice{
                std::min(x, far), std::max(x, far),
                static_cast<int16_t>(muzzle.y() - spread),
                static_cast<int16_t>(muzzle.y() + spread + 1)
            };

            for (int32_t oid : find_closest(slice, muzzle, 1))
            {
                first = oid;
                break;
            }
        }

        if (!first)
            return result;

        Optional<Mob> hit = mobs.get(first);
        if (!hit)
            return result;

        at = hit->get_body_position();
        result.damagelines.emplace_back(first, hit->calculate_damage(attack));
        result.mobcount++;

        // Splash skills then take whatever else stands inside the level's rect
        // around the monster the shot found.
        if (attack.splash && attack.mobcount > 1 && !attack.range.empty())
        {
            Rectangle<int16_t> around{
                static_cast<int16_t>(at.x() + attack.range.l()),
                static_cast<int16_t>(at.x() + attack.range.r()),
                static_cast<int16_t>(at.y() + attack.range.t()),
                static_cast<int16_t>(at.y() + attack.range.b())
            };

            for (int32_t oid : find_closest(around, at, attack.mobcount))
            {
                if (oid == first)
                    continue;

                if (Optional<Mob> mob = mobs.get(oid))
                {
                    result.damagelines.emplace_back(oid, mob->calculate_damage(attack));
                    result.mobcount++;
                }

                if (result.mobcount >= attack.mobcount)
                    break;
            }
        }

        result.first_oid = result.damagelines.front().first;
        result.last_oid = result.damagelines.back().first;
        return result;
    }

    AttackResult MapMobs::collect_targets(const Attack& attack,
        Rectangle<int16_t> range, Point<int16_t> origin)
    {
        AttackResult result = attack;
        // find_closest returns targets sorted by distance; appending in that
        // order keeps the sweep running from the attacker outwards, and it is
        // also what decides a piercing shot's place in the queue.
        size_t pierced = 0;
        for (int32_t target : find_closest(range, origin, attack.mobcount))
        {
            if (Optional<Mob> mob = mobs.get(target))
            {
                std::vector<std::pair<int32_t, bool>> lines =
                    mob->calculate_damage(attack);

                const double rate = pierce_damage_rate(attack.skill, pierced);
                if (rate != 1.0)
                {
                    for (auto& line : lines)
                    {
                        line.first = static_cast<int32_t>(line.first * rate);
                    }
                }

                result.damagelines.emplace_back(target, std::move(lines));
                result.mobcount++;
                pierced++;
            }
        }

        // Derived from the final list rather than tracked inside the loop, so
        // last_oid stays valid when fewer mobs are in range than the skill
        // can hit (Rush reads it to pick its destination).
        if (!result.damagelines.empty())
        {
            result.first_oid = result.damagelines.front().first;
            result.last_oid  = result.damagelines.back().first;
        }
        return result;
    }

    void MapMobs::apply_damage(int32_t oid, int32_t damage, bool toleft,
        const AttackUser& user, const SpecialMove& move) {

        if (Optional<Mob> mob = mobs.get(oid))
        {
            mob->apply_damage(damage, toleft);

            // maybe move this into the method above too?
            move.apply_hiteffects(user, *mob);
        }
    }

    std::vector<int32_t> MapMobs::find_closest(Rectangle<int16_t> range,
        Point<int16_t> origin, uint8_t mobcount) const {

        std::multimap<uint16_t, int32_t> distances;
        for (const auto& mmo : mobs)
        {
            const Mob* mob = static_cast<const Mob*>(mmo.second.get());
            if (mob && mob->is_alive() && mob->is_in_range(range))
            {
                int32_t oid = mob->get_oid();
                uint16_t distance = mob->get_body_position().distance(origin);
                distances.emplace(distance, oid);
            }
        }

        std::vector<int32_t> targets;
        for (auto& iter : distances)
        {
            if (targets.size() >= mobcount)
            {
                break;
            }

            targets.push_back(iter.second);
        }
        return targets;
    }

    bool MapMobs::contains(int32_t oid) const
    {
        return mobs.contains(oid);
    }

    int32_t MapMobs::find_colliding(const MovingObject& moveobj) const
    {
        Range<int16_t> horizontal{ moveobj.get_last_x(), moveobj.get_x() };
        Range<int16_t> vertical{ moveobj.get_last_y(), moveobj.get_y() };
        Rectangle<int16_t> player_rect{
            horizontal.smaller(),
            horizontal.greater(),
            vertical.smaller() - 50,
            vertical.greater()
        };

        auto iter = std::find_if(mobs.begin(), mobs.end(), [&player_rect](auto& mmo){
            Optional<Mob> mob = mmo.second.get();
            return mob && mob->is_alive() && mob->is_in_range(player_rect);
        });

        if (iter == mobs.end())
        {
            return 0;
        }

        return iter->second->get_oid();
    }

    MobAttack MapMobs::create_attack(int32_t oid) const
    {
        if (Optional<const Mob> mob = mobs.get(oid))
        {
            return mob->create_touch_attack();
        }
        else
        {
            return {};
        }
    }

    Point<int16_t> MapMobs::get_mob_position(int32_t oid) const
    {
        if (auto mob = mobs.get(oid))
        {
            return mob->get_position();
        }
        else
        {
            return {};
        }
    }

    Point<int16_t> MapMobs::get_mob_body_position(int32_t oid) const
    {
        if (Optional<const Mob> mob = mobs.get(oid))
        {
            return mob->get_body_position();
        }
        else
        {
            return {};
        }
    }

    Point<int16_t> MapMobs::get_mob_head_position(int32_t oid) const
    {
        if (Optional<const Mob> mob = mobs.get(oid))
        {
            return mob->get_head_position();
        }
        else
        {
            return {};
        }
    }
}
