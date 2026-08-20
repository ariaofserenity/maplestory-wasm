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
#include "CharStats.h"

#include "StatCaps.h"

#include <algorithm>
#include <cmath>

namespace jrc
{
    CharStats::CharStats(const StatsEntry& s)
        : name(s.name), petids(s.petids),
        exp(s.exp), mapid(s.mapid), portal(s.portal),
        rank(s.rank), jobrank(s.jobrank), basestats(s.stats) {

        job = basestats[Maplestat::JOB];
        init_totalstats();
    }

    CharStats::CharStats() {}

    void CharStats::init_totalstats()
    {
        totalstats.clear();
        buffdeltas.clear();
        percentages.clear();

        totalstats[Equipstat::HP] = get_stat(Maplestat::MAXHP);
        totalstats[Equipstat::MP] = get_stat(Maplestat::MAXMP);
        totalstats[Equipstat::STR] = get_stat(Maplestat::STR);
        totalstats[Equipstat::DEX] = get_stat(Maplestat::DEX);
        totalstats[Equipstat::INT] = get_stat(Maplestat::INT);
        totalstats[Equipstat::LUK] = get_stat(Maplestat::LUK);
        totalstats[Equipstat::SPEED] = 100;
        totalstats[Equipstat::JUMP] = 100;

        maxdamage = 0;
        mindamage = 0;
        honor = 0;
        attackspeed = 0;
        projectilerange = 400;
        // Zero until a mastery skill says otherwise; the weapon's own floor
        // takes over from there, so an unmastered attack still has a bound.
        mastery = 0.0f;
        critical = 0.05f;
        // Only Critical Shot and Sharp Eyes state what a critical is worth, so
        // a character with neither keeps the flat half-again the client has
        // always dealt rather than losing criticals outright.
        critdamage = 1.5f;
        damagepercent = 0.0f;
        bossdmg = 0.0f;
        ignoredef = 0.0f;
        stance = 0.0f;
        resiststatus = 0.0f;
        // A fraction of incoming damage rather than a discount off it, so the
        // neutral value is the whole of it.
        reducedamage = 1.0f;
    }

    void CharStats::close_totalstats()
    {
        totalstats[Equipstat::ACC] += calculateaccuracy();

        for (auto iter : percentages)
        {
            Equipstat::Id stat = iter.first;
            int32_t total = totalstats[stat];
            total += static_cast<int32_t>(total * iter.second);
            set_total(stat, total);
        }

        int32_t primary = get_primary_stat();
        int32_t secondary = get_secondary_stat();
        int32_t attack = get_total(Equipstat::WATK);
        float multiplier = damagepercent + static_cast<float>(attack) / 100;
        maxdamage = static_cast<int32_t>((primary + secondary) * multiplier);
        // The floor is a straight share of the ceiling. Weighting the primary
        // stat by mastery and leaving the secondary one alone, as this used to,
        // made the real floor drift below what the mastery skill promises -
        // enough for a maxed Bow Mastery's 60% to land nearer 54%.
        mindamage = static_cast<int32_t>(
            std::round(maxdamage * mastery_ratio())
        );
    }

    float CharStats::mastery_ratio() const
    {
        // Every weapon deals at least a fixed share of its maximum even with no
        // mastery skill at all, and how large that share is depends on what is
        // being swung. Nothing exceeds 95% however much mastery is stacked.
        constexpr float MASTERY_CAP = 0.95f;
        return std::min(std::max(mastery, unmastered_floor()), MASTERY_CAP);
    }

    float CharStats::unmastered_floor() const
    {
        switch (weapontype)
        {
        case Weapon::WAND:
        case Weapon::STAFF:
            return 0.25f;
        case Weapon::BOW:
        case Weapon::CROSSBOW:
        case Weapon::CLAW:
        case Weapon::GUN:
            return 0.15f;
        default:
            return 0.20f;
        }
    }

    int32_t CharStats::calculateaccuracy() const
    {
        int32_t totaldex = get_total(Equipstat::DEX);
        int32_t totalluk = get_total(Equipstat::LUK);
        return static_cast<int32_t>(totaldex * 0.8f + totalluk * 0.5f);
    }

    int32_t CharStats::get_primary_stat() const
    {
        Equipstat::Id primary = job.get_primary(weapontype);
        return static_cast<int32_t>(get_multiplier() * get_total(primary));
    }

    int32_t CharStats::get_secondary_stat() const
    {
        // Cosmic treats claw users and thief daggers as a mixed secondary stat.
        if (job.get_id() / 100 == 4 && (weapontype == Weapon::CLAW || weapontype == Weapon::DAGGER))
        {
            return get_total(Equipstat::DEX) + get_total(Equipstat::STR);
        }

        Equipstat::Id secondary = job.get_secondary(weapontype);
        return get_total(secondary);
    }

    float CharStats::get_multiplier() const
    {
        switch (weapontype)
        {
        case Weapon::SWORD_1H:
            return 4.0f;
        case Weapon::AXE_1H:
        case Weapon::MACE_1H:
        case Weapon::WAND:
        case Weapon::STAFF:
            return 4.4f;
        case Weapon::DAGGER:
        case Weapon::CROSSBOW:
        case Weapon::CLAW:
        case Weapon::GUN:
            return 3.6f;
        case Weapon::SWORD_2H:
            return 4.6f;
        case Weapon::AXE_2H:
        case Weapon::MACE_2H:
        case Weapon::KNUCKLE:
            return 4.8f;
        case Weapon::SPEAR:
        case Weapon::POLEARM:
            return 5.0f;
        case Weapon::BOW:
            return 3.4f;
        default:
            return 0.0f;
        }
    }

    void CharStats::set_stat(Maplestat::Id stat, uint16_t value)
    {
        basestats[stat] = value;
    }

    void CharStats::set_total(Equipstat::Id stat, int32_t value)
    {
        auto iter = EQSTAT_CAPS.find(stat);
        if (iter != EQSTAT_CAPS.end())
        {
            int32_t cap_value = iter->second;

            if (value > cap_value)
                value = cap_value;
        }

        totalstats[stat] = value;
    }

    void CharStats::add_buff(Equipstat::Id stat, int32_t value)
    {
        int32_t current = get_total(stat);
        set_total(stat, current + value);
        buffdeltas[stat] += value;
    }

    void CharStats::add_value(Equipstat::Id stat, int32_t value)
    {
        int32_t current = get_total(stat);
        set_total(stat, current + value);
    }

    void CharStats::add_percent(Equipstat::Id stat, float percent)
    {
        percentages[stat] += percent;
    }

    void CharStats::set_weapontype(Weapon::Type w)
    {
        weapontype = w;
    }

    void CharStats::set_exp(int64_t e)
    {
        exp = e;
    }

    void CharStats::set_mapid(int32_t id)
    {
        mapid = id;
    }

    void CharStats::set_portal(uint8_t p)
    {
        portal = p;
    }

    void CharStats::raise_mastery(float m)
    {
        if (m > mastery)
        {
            mastery = m;
        }
    }

    void CharStats::add_critical(float c)
    {
        critical += c;
    }

    void CharStats::set_critdamage(float c)
    {
        critdamage = c;
    }

    void CharStats::add_critdamage(float c)
    {
        critdamage += c;
    }

    void CharStats::set_damagepercent(float d)
    {
        damagepercent = d;
    }

    void CharStats::set_reducedamage(float r)
    {
        reducedamage = r;
    }

    void CharStats::change_job(uint16_t id)
    {
        basestats[Maplestat::JOB] = id;
        job.change_job(id);
    }

    int32_t CharStats::calculate_damage(int32_t mobatk, int32_t moblevel) const
    {
        // Weapon defence buys two separate reductions, both off a quarter of
        // it: a flat subtraction, and a percentage that grows as its square
        // root, so the first points of defence are worth far more than the
        // last. Whichever of the two leaves the character better off wins.
        double quarter = get_total(Equipstat::WDEF) * 0.25;
        auto flatdef = static_cast<int32_t>(quarter + 0.5);
        auto pctdef = static_cast<int32_t>(std::sqrt(quarter));

        // Being under-levelled eats into both, four points of the flat
        // reduction and two of the percentage per level of the gap. Out-
        // levelling a monster is worth nothing extra.
        if (level_gap_against(moblevel) > 0)
        {
            int32_t gap = level_gap_against(moblevel);
            flatdef -= std::min(gap * 4, flatdef);
            pctdef -= std::min(gap * 2, pctdef);
        }

        double damage = std::min(
            mobatk * (100 - pctdef) / 100.0,
            static_cast<double>(mobatk - flatdef)
        );

        // Achilles and friends scale what is left; the stat holds the fraction
        // that still lands, so an untouched character keeps all of it.
        damage *= reducedamage;

        // A hit always registers, however well defended.
        return std::max(1, static_cast<int32_t>(damage));
    }

    int32_t CharStats::level_gap_against(int32_t moblevel) const
    {
        return moblevel - static_cast<int32_t>(get_stat(Maplestat::LEVEL));
    }

    bool CharStats::is_damage_buffed() const
    {
        return get_buffdelta(Equipstat::WATK) > 0
            || get_buffdelta(Equipstat::MAGIC) > 0;
    }

    uint16_t CharStats::get_stat(Maplestat::Id stat) const
    {
        return basestats[stat];
    }

    int32_t CharStats::get_total(Equipstat::Id stat) const
    {
        return totalstats[stat];
    }

    int32_t CharStats::get_buffdelta(Equipstat::Id stat) const
    {
        return buffdeltas[stat];
    }

    Rectangle<int16_t> CharStats::get_range() const
    {
        return Rectangle<int16_t>(-projectilerange, -5, -50, 50);
    }

    int32_t CharStats::get_mapid() const
    {
        return mapid;
    }

    uint8_t CharStats::get_portal() const
    {
        return portal;
    }

    int64_t CharStats::get_exp() const
    {
        return exp;
    }

    const std::string& CharStats::get_name() const
    {
        return name;
    }

    const std::string& CharStats::get_jobname() const
    {
        return job.get_name();
    }

    Weapon::Type CharStats::get_weapontype() const
    {
        return weapontype;
    }

    float CharStats::get_mastery() const
    {
        return mastery;
    }

    float CharStats::get_critical() const
    {
        return critical;
    }

    float CharStats::get_critdamage() const
    {
        return critdamage;
    }

    float CharStats::get_reducedamage() const
    {
        return reducedamage;
    }

    float CharStats::get_bossdmg() const
    {
        return bossdmg;
    }

    float CharStats::get_ignoredef() const
    {
        return ignoredef;
    }

    void CharStats::set_stance(float s)
    {
        stance = s;
    }

    float CharStats::get_stance() const
    {
        return stance;
    }

    float CharStats::get_resistance() const
    {
        return resiststatus;
    }

    int32_t CharStats::get_maxdamage() const
    {
        return maxdamage;
    }

    int32_t CharStats::get_mindamage() const
    {
        return mindamage;
    }

    uint16_t CharStats::get_honor() const
    {
        return honor;
    }

    void CharStats::set_attackspeed(int8_t as)
    {
        attackspeed = as;
    }

    int8_t CharStats::get_attackspeed() const
    {
        return attackspeed;
    }

    const Job& CharStats::get_job() const
    {
        return job;
    }
}
