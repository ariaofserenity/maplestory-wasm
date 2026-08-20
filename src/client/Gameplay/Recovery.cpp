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
#include "Recovery.h"

#include "../Constants.h"
#include "../Character/SkillId.h"
#include "../Net/Packets/PlayerPackets.h"
#include "../Util/Misc.h"

#include "nlnx/node.hpp"
#include "nlnx/nx.hpp"

namespace jrc
{
    namespace
    {
        // A tick comes due every ten seconds and hands out a flat amount scaled
        // by the field's recovery rate: ten hp and three mp at the ordinary
        // rate of one.
        constexpr int32_t TICK = 10000;
        constexpr double HP_PER_TICK = 10.0;
        constexpr double MP_PER_TICK = 3.0;
        // A chair is worth half again as much. The flag rides along on the
        // packet, since the server's ceiling on a plausible heal allows for it.
        constexpr double SIT_RATE = 1.5;
        constexpr int8_t FLAG_STANDING = 0;
        constexpr int8_t FLAG_SITTING = 2;

        // The wz node holding one level of a skill, laid out the way
        // PassiveBuffs reads them.
        nl::node skill_level(int32_t skill_id, int32_t level)
        {
            std::string strid = skill_id < 10000000
                ? string_format::extend_id(skill_id, 7)
                : std::to_string(skill_id);

            return nl::nx::skill[strid.substr(0, 3) + ".img"]["skill"][strid]
                ["level"][level];
        }
    }

    void Recovery::update(Player& player, float maprate)
    {
        const CharStats& stats = player.get_stats();

        // The dead recover nothing, and neither does anyone still on the
        // loading screen with no stats to speak of.
        if (stats.get_stat(Maplestat::HP) == 0 || stats.get_total(Equipstat::HP) <= 0)
        {
            reset();
            return;
        }

        bool sitting = player.is_sitting();
        double rate = maprate * (sitting ? SIT_RATE : 1.0);

        int32_t hp = 0;
        int32_t mp = 0;

        // Hp waits on the character holding still. Climbing counts only for a
        // swordman who has taken Endure, which is the whole of that skill.
        int32_t interval = hp_interval(player);
        bool hp_missing = stats.get_stat(Maplestat::HP) < stats.get_total(Equipstat::HP);
        if (interval > 0 && hp_missing)
        {
            hp_elapsed += Constants::TIMESTEP;
            if (hp_elapsed >= interval)
            {
                hp_elapsed = 0;
                hp = hp_amount(player, rate);
            }
        }
        else
        {
            hp_elapsed = 0;
        }

        // Mp is not gated on standing still - it comes back while walking too,
        // and TryRecovery checks nothing but whether there is room for it.
        if (stats.get_stat(Maplestat::MP) < stats.get_total(Equipstat::MP))
        {
            mp_elapsed += Constants::TIMESTEP;
            if (mp_elapsed >= TICK)
            {
                mp_elapsed = 0;
                mp = static_cast<int32_t>(rate * MP_PER_TICK);
            }
        }
        else
        {
            mp_elapsed = 0;
        }

        if (hp <= 0 && mp <= 0)
        {
            return;
        }

        HealOverTimePacket(
            static_cast<int16_t>(hp),
            static_cast<int16_t>(mp),
            sitting ? FLAG_SITTING : FLAG_STANDING
        ).dispatch();

        // The server answers with a stat update, so the number over the
        // character's head is all this applies itself.
        if (hp > 0)
        {
            player.show_recovery(hp);
        }
    }

    void Recovery::reset()
    {
        hp_elapsed = 0;
        mp_elapsed = 0;
    }

    int32_t Recovery::hp_interval(const Player& player) const
    {
        if (player.is_climbing())
        {
            // Endure is what puts hp recovery on a rope or a ladder at all.
            // Past that its levels buy nothing here: TryRecovery runs a climb
            // through the same ten-second accumulator as standing, against the
            // same threshold, and reads no skill level to shorten it. The wz
            // quotes a per-level wait - 31 seconds down to 10 - that the client
            // never applies, and honouring it instead left a tick so far apart
            // the skill read as dead.
            return player.get_skilllevel(SkillId::IMPROVED_LADDER_RECOVERY) > 0
                ? TICK
                : 0;
        }

        // Sitting is a stance of its own rather than one of the standing ones,
        // and is the case a chair's larger rate is for.
        if (player.is_standing() || player.is_sitting())
        {
            return TICK;
        }

        return 0;
    }

    int32_t Recovery::hp_amount(const Player& player, double rate) const
    {
        int32_t amount = static_cast<int32_t>(rate * HP_PER_TICK);

        // Improved HP Recovery reads as adding to this tick - the wz stores
        // exactly the "+3 per level" its tooltip quotes, against a base of ten.
        // Where the two meet is reconstructed from the skill's own data: the
        // TryRecovery we decompiled reads no skill level at all, so this is the
        // one part of the port not copied from it.
        int32_t level = player.get_skilllevel(SkillId::IMPROVED_HP_RECOVERY);
        if (level > 0)
        {
            amount += static_cast<int32_t>(
                skill_level(SkillId::IMPROVED_HP_RECOVERY, level)["hp"]
            );
        }

        return amount;
    }
}
