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
#include "Player.h"
#include "PlayerStates.h"

#include "../Constants.h"
#include "../Data/WeaponData.h"
#include "../IO/UI.h"
#include "../IO/UITypes/UIStatsInfo.h"
#include "../Net/Packets/GameplayPackets.h"
#include "../Net/Packets/InventoryPackets.h"
#include "../Util/Misc.h"
#include "SkillId.h"

#include "nlnx/nx.hpp"
#include "nlnx/node.hpp"

namespace jrc
{
    const PlayerNullState nullstate;

    const PlayerState* get_state(Char::State state)
    {
        static PlayerStandState standing;
        static PlayerWalkState walking;
        static PlayerFallState falling;
        static PlayerProneState lying;
        static PlayerClimbState climbing;
        static PlayerSitState sitting;
        static PlayerFlyState flying;

        switch (state)
        {
        case Char::STAND:
            return &standing;
        case Char::WALK:
            return &walking;
        case Char::FALL:
            return &falling;
        case Char::PRONE:
            return &lying;
        case Char::LADDER:
        case Char::ROPE:
            return &climbing;
        case Char::SIT:
            return &sitting;
        case Char::SWIM:
            return &flying;
        default:
            return nullptr;
        }
    }

    Player::Player(const CharEntry& entry)
        : Char(entry.cid, entry.look, entry.stats.name), stats(entry.stats)
    {
        attacking  = false;
        underwater = false;

        set_state(STAND);
        set_direction(false);
    }

    Player::Player()
        : Char(0, {}, "") {}

    void Player::respawn(Point<int16_t> pos, bool uw)
    {
        set_position(pos.x(), pos.y());
        underwater = uw;
        keysdown.clear();
        attacking = false;
        ladder = nullptr;
        releasedladder = nullptr;
        phobj.type = PhysicsObject::NORMAL;
        phobj.clear_flags();
        phobj.hspeed = 0.0;
        phobj.vspeed = 0.0;
        phobj.hforce = 0.0;
        phobj.vforce = 0.0;
        phobj.hacc = 0.0;
        phobj.vacc = 0.0;
        nullstate.update_state(*this);
    }

    void Player::send_action(KeyAction::Id action, bool down)
    {
        if (state == DIED)
        {
            return;
        }

        const PlayerState* pst = get_state(state);
        if (pst)
        {
            pst->send_action(*this, action, down);
        }
        keysdown[action] = down;

        if (!down && (action == KeyAction::UP || action == KeyAction::DOWN))
        {
            // Letting go of the climb key ends the jump-off that blocked the
            // ladder, so the next press is free to grab it again.
            releasedladder = nullptr;
        }
    }

    void Player::recalc_stats(bool equipchanged)
    {
        Weapon::Type weapontype = get_weapontype();

        stats.set_weapontype(weapontype);
        stats.init_totalstats();

        if (equipchanged)
        {
            inventory.recalc_stats(weapontype);
        }

        for (auto stat : Equipstat::values)
        {
            int32_t inventory_total = inventory.get_stat(stat);
            stats.add_value(stat, inventory_total);
        }

        auto passive_skills = skillbook.collect_passives();
        for (auto& passive : passive_skills)
        {
            int32_t skill_id = passive.first;
            int32_t skill_level = passive.second;

            passive_buffs.apply_buff(stats, skill_id, skill_level);
        }

        apply_shield_mastery();

        for (const Buff& buff : buffs.values())
        {
            active_buffs.apply_buff(stats, buff.stat, buff.value);
        }

        stats.close_totalstats();

        if (auto statsinfo = UI::get().get_element<UIStatsinfo>())
        {
            statsinfo->update_all_stats();
        }
    }

    bool Player::is_finisher(int32_t skill_id)
    {
        switch (skill_id)
        {
        case SkillId::SWORD_PANIC:
        case SkillId::AXE_PANIC:
        case SkillId::SWORD_COMA:
        case SkillId::AXE_COMA:
            return true;
        default:
            return false;
        }
    }

    void Player::apply_shield_mastery()
    {
        int32_t level = skillbook.get_level(SkillId::SHIELD_MASTERY);
        if (level <= 0)
        {
            return;
        }

        // Nothing to raise with an empty off hand, which the skill says in as
        // many words.
        uint16_t shield_wdef = inventory.get_equipped_stat(
            Equipslot::SHIELD, Equipstat::WDEF
        );
        if (shield_wdef == 0)
        {
            return;
        }

        // The wz stores the shield's defence as a percentage of itself - 105 at
        // level one for the "5% increased" the tooltip quotes, 200 at twenty -
        // so what it adds is the part above the whole.
        std::string strid = string_format::extend_id(SkillId::SHIELD_MASTERY, 7);
        nl::node src = nl::nx::skill[strid.substr(0, 3) + ".img"]["skill"][strid]
            ["level"][level];

        auto percent = static_cast<int32_t>(src["x"]);
        if (percent <= 100)
        {
            return;
        }

        stats.add_value(
            Equipstat::WDEF,
            shield_wdef * (percent - 100) / 100
        );
    }

    void Player::change_equip(int16_t slot)
    {
        if (int32_t itemid = inventory.get_item_id(InventoryType::EQUIPPED, slot))
        {
            look.add_equip(itemid);
        }
        else
        {
            look.remove_equip(Equipslot::by_id(slot));
        }
    }

    void Player::use_item(int32_t itemid)
    {
        InventoryType::Id type = InventoryType::by_item_id(itemid);
        if (int16_t slot = inventory.find_item(type, itemid))
        {
            switch (type)
            {
            case InventoryType::USE:
                UseItemPacket(slot, itemid).dispatch();
                break;
            default:
                return;
            }
        }
    }

    void Player::draw(Layer::Id layer, double viewx, double viewy, float alpha) const
    {
        if (layer == get_layer())
        {
            Char::draw(viewx, viewy, alpha);
        }
    }

    void Player::draw(double viewx, double viewy, float alpha) const
    {
        Char::draw(viewx, viewy, alpha);
    }

    int8_t Player::update(const Physics& physics)
    {
        update_cooldowns();

        if (state == DIED)
        {
            Char::update(physics, 1.0f);
        }
        else
        {
            const PlayerState* pst = get_state(state);
            if (pst)
            {
                pst->update(*this);
                physics.move_object(phobj);

                bool aniend = Char::update(physics, get_stancespeed());
                if (aniend && attacking)
                {
                    attacking = false;
                    nullstate.update_state(*this);
                }
                else
                {
                    pst->update_state(*this);
                }
            }
        }

        uint8_t stancebyte = flip ? state : state + 1;
        Movement newmove(phobj, stancebyte);
        bool needupdate = lastmove.hasmoved(newmove);
        if (needupdate)
        {
            MovePlayerPacket(newmove).dispatch();
            lastmove = newmove;
        }

        return get_layer();
    }

    int8_t Player::get_integer_attackspeed() const
    {
        int32_t weapon_id = look.get_equips().get_weapon();
        if (weapon_id <= 0)
        {
            return 0;
        }

        const WeaponData& weapon = WeaponData::get(weapon_id);

        int8_t base_speed = stats.get_attackspeed();
        int8_t weapon_speed = weapon.get_speed();
        return base_speed + weapon_speed;
    }

    void Player::set_direction(bool flipped)
    {
        if (!attacking)
        {
            Char::set_direction(flipped);
        }
    }

    void Player::set_state(State st)
    {
        if (!attacking)
        {
            Char::set_state(st);

            const PlayerState* pst = get_state(st);
            if (pst)
            {
                pst->initialize(*this);
            }
        }
    }

    bool Player::is_attacking() const
    {
        return attacking;
    }

    bool Player::can_attack() const
    {
        if (attacking || is_dead() || is_climbing() || is_sitting()
            || !look.get_equips().has_weapon())
        {
            return false;
        }

        return can_shoot();
    }

    bool Player::can_shoot() const
    {
        switch (get_weapontype())
        {
        case Weapon::BOW:
        case Weapon::CROSSBOW:
            // Swimming counts as footing for this: the reference client only
            // refuses the shot when the character has no foothold and is not in
            // the water either.
            return phobj.onground || is_underwater();
        default:
            return true;
        }
    }

    SpecialMove::ForbidReason Player::can_use(const SpecialMove& move) const
    {
        if (is_dead())
        {
            return SpecialMove::FBR_OTHER;
        }

        if (move.is_skill() && state == PRONE)
        {
            return SpecialMove::FBR_OTHER;
        }

        if (move.is_attack() && (state == LADDER || state == ROPE))
        {
            return SpecialMove::FBR_OTHER;
        }

        if (has_cooldown(move.get_id()))
        {
            return SpecialMove::FBR_COOLDOWN;
        }

        // A finisher spends combo orbs, so it needs at least one banked. The
        // buff's value counts from one on a fresh cast, meaning a value of one
        // is the combo running with nothing saved up yet.
        if (is_finisher(move.get_id()) && buffs[Buffstat::COMBO].value <= 1)
        {
            return SpecialMove::FBR_OTHER;
        }

        int32_t level       = skillbook.get_level(move.get_id());
        Weapon::Type weapon = get_weapontype();
        const Job& job      = stats.get_job();
        uint16_t hp         = stats.get_stat(Maplestat::HP);
        uint16_t mp         = stats.get_stat(Maplestat::MP);
        uint16_t bullets    = inventory.get_bulletcount();

        return move.can_use(level, weapon, job, hp, mp, bullets);
    }

    Attack Player::prepare_attack(bool skill, bool closerange) const
    {
        Attack::Type attacktype;
        bool degenerate;
        if (state == PRONE)
        {
            degenerate = true;
            attacktype = Attack::CLOSE;
        }
        else
        {
            Weapon::Type weapontype = get_weapontype();
            switch (weapontype)
            {
            case Weapon::BOW:
            case Weapon::CROSSBOW:
                // A monster standing on top of an archer is hit with the bow
                // rather than shot at - the reference client tries the melee
                // swing first and only looses an arrow if that found nothing.
                degenerate = closerange || !inventory.has_projectile();
                attacktype = degenerate ? Attack::CLOSE : Attack::RANGED;
                break;
            case Weapon::CLAW:
            case Weapon::GUN:
                degenerate = !inventory.has_projectile();
                attacktype = degenerate ? Attack::CLOSE : Attack::RANGED;
                break;
            case Weapon::WAND:
            case Weapon::STAFF:
                degenerate = !skill;
                attacktype = degenerate ? Attack::CLOSE : Attack::MAGIC;
                break;
            default:
                attacktype = Attack::CLOSE;
                degenerate = false;
            }
        }

        Attack attack;
        attack.type      = attacktype;
        // Anything fired sweeps a line from the muzzle rather than a rect.
        attack.linear    = attacktype == Attack::RANGED;
        attack.mindamage = stats.get_mindamage();
        attack.maxdamage = stats.get_maxdamage();
        if (degenerate)
        {
            attack.mindamage /= 10;
            attack.maxdamage /= 10;
        }
        attack.critical    = stats.get_critical();
        attack.critdamage  = stats.get_critdamage();
        attack.ignoredef   = stats.get_ignoredef();
        attack.accuracy    = stats.get_total(Equipstat::ACC);
        attack.playerlevel = stats.get_stat(Maplestat::LEVEL);
        attack.range       = stats.get_range();
        // Only a shot carries a projectile. A bow swung at something next to it
        // - or fired from a crouch, which the client turns into a swing - has
        // no arrow to draw, and leaving one on sent one flying anyway.
        attack.bullet      = (attacktype == Attack::RANGED)
            ? inventory.get_bulletid()
            : 0;
        attack.origin      = get_position();
        attack.toleft      = !flip;
        attack.speed       = static_cast<uint8_t>(get_integer_attackspeed());

        return attack;
    }

    void Player::rush(double targetx)
    {
        if (phobj.onground)
        {
            uint16_t delay = get_attackdelay(1);
            phobj.movexuntil(targetx, delay);
            phobj.set_flag(PhysicsObject::TURNATEDGES);
        }
    }

    bool Player::is_hidden() const
    {
        return gm_hidden || has_buff(Buffstat::DARKSIGHT);
    }

    void Player::set_gm_hidden(bool hidden)
    {
        gm_hidden = hidden;
    }

    bool Player::is_invincible() const
    {
        if (state == DIED)
        {
            return true;
        }

        if (is_hidden())
        {
            return true;
        }

        return Char::is_invincible();
    }

    MobAttackResult Player::damage(const MobAttack& attack)
    {
        int32_t damage = stats.calculate_damage(attack.watk, attack.level);
        show_damage(damage);

        bool fromleft = attack.origin.x() > phobj.get_x();

        bool missed = damage <= 0;
        bool immovable = ladder || state == DIED;
        bool knockback = !missed && !immovable;
        if (knockback && randomizer.above(stats.get_stance()))
        {
            // Knockback is an impulse, so it has to land on the speeds directly.
            // Queueing it as a force made it depend on what the character was
            // doing at that exact moment: the physics only consume vforce while
            // the character is on the ground, so a hit taken in mid-air lost its
            // vertical half entirely, and a hit that arrived on the same tick as
            // a jump added to the jump instead of overriding it and threw the
            // character close to twice its normal jump height.
            phobj.hspeed = fromleft ? -1.5 : 1.5;
            phobj.vspeed = -3.5;
            phobj.hforce = 0.0;
            phobj.vforce = 0.0;
        }

        uint8_t direction = fromleft ? 0 : 1;
        return { attack, damage, direction };
    }

    void Player::die()
    {
        if (state == DIED)
        {
            return;
        }

        keysdown.clear();
        attacking = false;
        ladder = nullptr;
        releasedladder = nullptr;

        // Freeze the player immediately so no stale movement or key state leaks
        // through while waiting for the server-driven respawn.
        phobj.type = PhysicsObject::FIXATED;
        phobj.clear_flags();
        phobj.hspeed = 0.0;
        phobj.vspeed = 0.0;
        phobj.hforce = 0.0;
        phobj.vforce = 0.0;
        phobj.hacc = 0.0;
        phobj.vacc = 0.0;

        set_state(DIED);
    }

    void Player::revive()
    {
        if (state != DIED)
        {
            return;
        }

        phobj.type = PhysicsObject::NORMAL;
        phobj.clear_flags();
        nullstate.update_state(*this);
    }

    bool Player::is_dead() const
    {
        return state == DIED;
    }

    void Player::give_buff(Buff buff)
    {
        buffs[buff.stat] = buff;

        // Combo Attack carries its orb count as the buff's value, and the
        // server resends the whole buff on every change, so the orbs follow it
        // rather than being counted here.
        if (buff.stat == Buffstat::COMBO)
        {
            set_combo_orbs(buff.value);
        }
    }

    void Player::cancel_buff(Buffstat::Id stat)
    {
        buffs[stat] = {};

        if (stat == Buffstat::COMBO)
        {
            set_combo_orbs(0);
        }
    }

    bool Player::has_buff(Buffstat::Id stat) const
    {
        return buffs[stat].value > 0;
    }

    int32_t Player::get_buff_skillid(Buffstat::Id stat) const
    {
        return buffs[stat].skillid;
    }

    void Player::change_skill(int32_t skill_id, int32_t skill_level,
        int32_t masterlevel, int64_t expiration) {

        int32_t old_level = skillbook.get_level(skill_id);
        skillbook.set_skill(skill_id, skill_level, masterlevel, expiration);

        if (old_level != skill_level)
        {
            recalc_stats(false);
        }
    }

    void Player::add_cooldown(int32_t skill_id, int32_t cooltime)
    {
        // The server sends a zero to say a cooldown is over, which arrives
        // whether or not the local count has run out yet, so it wins either way.
        if (cooltime <= 0)
        {
            cooldowns.erase(skill_id);
            return;
        }

        // Sent in seconds; kept in milliseconds so it can be counted down a
        // timestep at a time.
        int32_t total = cooltime * 1000;
        cooldowns[skill_id] = { total, total };
    }

    bool Player::has_cooldown(int32_t skill_id) const
    {
        return cooldowns.count(skill_id) > 0;
    }

    Player::Cooldown Player::get_cooldown(int32_t skill_id) const
    {
        auto iter = cooldowns.find(skill_id);
        if (iter == cooldowns.end())
        {
            return {};
        }

        return iter->second;
    }

    void Player::update_cooldowns()
    {
        for (auto iter = cooldowns.begin(); iter != cooldowns.end(); )
        {
            iter->second.remaining -= Constants::TIMESTEP;

            // Only dropped once it has actually run out locally. The server
            // sends its own end, but a skill whose count is up is usable again
            // regardless, and holding it back would cost the player a cast.
            iter = iter->second.remaining <= 0 ? cooldowns.erase(iter) : std::next(iter);
        }
    }

    void Player::change_level(uint16_t level)
    {
        uint16_t oldlevel = get_level();
        if (level > oldlevel)
        {
            show_effect_id(CharEffect::LEVELUP);
        }
        stats.set_stat(Maplestat::LEVEL, level);
    }

    uint16_t Player::get_level() const
    {
        return stats.get_stat(Maplestat::LEVEL);
    }

    int32_t Player::get_skilllevel(int32_t skillid) const
    {
        return skillbook.get_level(skillid);
    }

    void Player::change_job(uint16_t jobid)
    {
        show_effect_id(CharEffect::JOBCHANGE);
        stats.change_job(jobid);
    }

    void Player::set_seat(Optional<const Seat> seat)
    {
        if (seat)
        {
            set_position(seat->getpos());
            set_state(Char::SIT);
        }
    }

    void Player::release_ladder()
    {
        releasedladder = ladder;
        set_ladder(nullptr);
    }

    bool Player::can_climb(const Ladder& candidate) const
    {
        return releasedladder.get() != &candidate;
    }

    void Player::set_ladder(Optional<const Ladder> ldr)
    {
        ladder = ldr;

        if (ladder)
        {
            phobj.set_x(ldr->get_x());
            phobj.set_y(ldr->attach_y(phobj.get_y()));
            phobj.hspeed  = 0.0;
            phobj.vspeed  = 0.0;
            phobj.fhlayer = 7;
            set_state(ldr->is_ladder() ? Char::LADDER : Char::ROPE);
            set_direction(false);
        }
    }

    float Player::get_walkforce() const
    {
        return 0.05f + 0.11f * static_cast<float>(stats.get_total(Equipstat::SPEED)) / 100;
    }

    float Player::get_jumpforce() const
    {
        return 1.0f + 3.5f * static_cast<float>(stats.get_total(Equipstat::JUMP)) / 100;
    }

    float Player::get_climbforce() const
    {
        return static_cast<float>(stats.get_total(Equipstat::SPEED)) / 100;
    }

    float Player::get_flyforce() const
    {
        return 0.25f;
    }

    bool Player::is_underwater() const
    {
        return underwater;
    }

    bool Player::is_key_down(KeyAction::Id action) const
    {
        return keysdown.count(action) ? keysdown.at(action) : false;
    }

    CharStats& Player::get_stats()
    {
        return stats;
    }

    const CharStats& Player::get_stats() const
    {
        return stats;
    }

    Inventory& Player::get_inventory()
    {
        return inventory;
    }

    const Inventory& Player::get_inventory() const
    {
        return inventory;
    }

    Skillbook& Player::get_skills()
    {
        return skillbook;
    }

    Questlog& Player::get_quests()
    {
        return questlog;
    }

    const Questlog& Player::get_quests() const
    {
        return questlog;
    }

    Telerock& Player::get_telerock()
    {
        return telerock;
    }

    Monsterbook& Player::get_monsterbook()
    {
        return monsterbook;
    }

    Optional<const Ladder> Player::get_ladder() const
    {
        return ladder;
    }
}
