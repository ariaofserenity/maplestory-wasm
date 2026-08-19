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
#include "PlayerStates.h"

#include "../Audio/Audio.h"


namespace jrc
{
    namespace
    {
        // The reference client never latches a walking direction from a key
        // event. Every tick it polls both arrow keys and works with the signed
        // sum of the two, so releasing one of a pair of opposed keys hands
        // control straight back to the one still held, and holding both cancels
        // out into no horizontal intent at all.
        int8_t horizontal_input(const Player& player)
        {
            int8_t right = player.is_key_down(KeyAction::RIGHT) ? 1 : 0;
            int8_t left  = player.is_key_down(KeyAction::LEFT)  ? 1 : 0;
            return static_cast<int8_t>(right - left);
        }

        // Point the character along the polled intent. Facing follows the input
        // rather than the key press for the same reason.
        void face_input(Player& player, int8_t inputx)
        {
            if (inputx != 0)
            {
                player.set_direction(inputx > 0);
            }
        }

        // The stance a character on a foothold should be in for the keys that
        // are held right now. Crouching wins over walking, matching what a down
        // press does from stand and from walk, and it has to be re-derived on
        // every landing so that a held key is not lost across a jump or a fall.
        Char::State grounded_state(Player& player)
        {
            if (player.is_key_down(KeyAction::DOWN))
            {
                return Char::PRONE;
            }

            int8_t inputx = horizontal_input(player);
            if (inputx != 0)
            {
                face_input(player, inputx);
                return Char::WALK;
            }

            return Char::STAND;
        }
    }

    // Base class
    void PlayerState::play_jumpsound() const
    {
        Sound(Sound::JUMP).play();
    }

    // Null state
    void PlayerNullState::update_state(Player& player) const
    {
        Char::State state;
        if (player.get_phobj().onground)
        {
            state = grounded_state(player);
        }
        else
        {
            Optional<const Ladder> ladder = player.get_ladder();
            if (ladder)
            {
                state = ladder->is_ladder() ? Char::LADDER : Char::ROPE;
            }
            else
            {
                state = Char::FALL;
            }
        }

        player.get_phobj().clear_flags();

        player.set_state(state);
    }


    // Standing
    void PlayerStandState::initialize(Player& player) const
    {
        player.get_phobj().type = PhysicsObject::NORMAL;
    }

    void PlayerStandState::send_action(Player& player, KeyAction::Id ka, bool down) const
    {
        if (player.is_attacking())
        {
            return;
        }

        if (down)
        {
            switch (ka)
            {
            case KeyAction::LEFT:
            case KeyAction::RIGHT:
                // Facing is settled by the polled input in the walk state, so
                // the press only has to hand the character over to it.
                player.set_state(Char::WALK);
                break;
            case KeyAction::JUMP:
                play_jumpsound();
                player.get_phobj().vforce = -player.get_jumpforce();
                break;
            case KeyAction::DOWN:
                player.set_state(Char::PRONE);
                break;
            default:
                break;
            }
        }
    }

    void PlayerStandState::update(Player& player) const
    {
        if (!player.get_phobj().enablejd)
        {
            player.get_phobj().set_flag(PhysicsObject::CHECKBELOW);
        }
    }

    void PlayerStandState::update_state(Player& player) const
    {
        if (!player.get_phobj().onground)
        {
            player.set_state(Char::FALL);
        }
        else if (horizontal_input(player) != 0)
        {
            // Polled rather than driven by the key event, so that releasing one
            // of two opposed arrow keys hands walking back to the one that is
            // still held instead of leaving the character standing.
            player.set_state(Char::WALK);
        }
    }


    // Walking
    void PlayerWalkState::initialize(Player& player) const
    {
        player.get_phobj().type = PhysicsObject::NORMAL;
    }

    void PlayerWalkState::send_action(Player& player, KeyAction::Id ka, bool down) const
    {
        if (player.is_attacking())
        {
            return;
        }

        if (down)
        {
            switch (ka)
            {
            case KeyAction::JUMP:
                play_jumpsound();
                player.get_phobj().vforce = -player.get_jumpforce();
                break;
            case KeyAction::DOWN:
                player.set_state(Char::PRONE);
                break;
            default:
                break;
            }
        }
    }

    bool PlayerWalkState::haswalkinput(const Player& player) const
    {
        return horizontal_input(player) != 0;
    }

    void PlayerWalkState::update(Player& player) const
    {
        int8_t inputx = horizontal_input(player);
        if (!player.is_attacking() && inputx != 0)
        {
            face_input(player, inputx);
            player.get_phobj().hforce +=
                inputx > 0 ?  player.get_walkforce()
                           : -player.get_walkforce();
        }

        if (!player.get_phobj().enablejd)
        {
            player.get_phobj().set_flag(PhysicsObject::CHECKBELOW);
        }
    }

    void PlayerWalkState::update_state(Player& player) const
    {
        if (player.get_phobj().onground)
        {
            if (!haswalkinput(player) || player.get_phobj().hspeed == 0.0f)
            {
                player.set_state(Char::STAND);
            }
        }
        else
        {
            player.set_state(Char::FALL);
        }
    }

    // Falling
    void PlayerFallState::initialize(Player& player) const
    {
        player.get_phobj().type = PhysicsObject::NORMAL;
    }

    void PlayerFallState::send_action(Player&, KeyAction::Id, bool) const
    {
        // Facing and air control both come from the polled input in update().
    }

    void PlayerFallState::update(Player& player) const
    {
        int8_t inputx = horizontal_input(player);
        face_input(player, inputx);

        auto& hspeed = player.get_phobj().hspeed;
        if (inputx < 0 && hspeed > 0.0)
        {
            hspeed -= 0.025;
            if (hspeed < 0.0)
            {
                hspeed = 0.0;
            }
        }
        else if (inputx > 0 && hspeed < 0.0)
        {
            hspeed += 0.025;
            if (hspeed > 0.0)
            {
                hspeed = 0.0;
            }
        }
    }

    void PlayerFallState::update_state(Player& player) const
    {
        if (player.get_phobj().onground)
        {
            // Landing keeps whatever stance the held keys ask for, so that
            // holding down and jump drops through one platform after another
            // instead of standing up between them.
            player.set_state(grounded_state(player));
        }
        else if (player.is_underwater())
        {
            player.set_state(Char::SWIM);
        }
    }


    // Prone
    void PlayerProneState::send_action(Player& player, KeyAction::Id ka, bool down) const
    {
        if (down)
        {
            switch (ka)
            {
            case KeyAction::JUMP:
                if (player.get_phobj().enablejd && player.is_key_down(KeyAction::DOWN))
                {
                    play_jumpsound();
                    player.get_phobj().y = player.get_phobj().groundbelow;
                    player.set_state(Char::FALL);
                }
                else
                {
                    player.set_state(Char::STAND);
                    player.send_action(ka, down);
                }
                break;
            default:
                break;
            }
        }
        else
        {
            switch (ka)
            {
            case KeyAction::DOWN:
                player.set_state(Char::STAND);
                break;
            default:
                break;
            }
        }
    }

    void PlayerProneState::update(Player& player) const
    {
        if (!player.get_phobj().enablejd)
        {
            player.get_phobj().set_flag(PhysicsObject::CHECKBELOW);
        }
    }

    void PlayerProneState::update_state(Player& player) const
    {
        if (!player.get_phobj().onground)
        {
            // Crouching is a grounded stance. Losing the floor while prone -
            // pressing down in the same tick the character walks off a ledge -
            // otherwise left it stuck in the crouch for the whole fall, since
            // nothing else moves it out of this state.
            player.set_state(Char::FALL);
        }
    }


    // Sitting
    void PlayerSitState::send_action(Player& player, KeyAction::Id ka, bool down) const
    {
        if (down)
        {
            switch (ka)
            {
            case KeyAction::LEFT:
                player.set_direction(false);
                player.set_state(Char::WALK);
                break;
            case KeyAction::RIGHT:
                player.set_direction(true);
                player.set_state(Char::WALK);
                break;
            case KeyAction::JUMP:
                play_jumpsound();
                player.set_state(Char::STAND);
                break;
            case KeyAction::UP:
                player.set_state(Char::SWIM);
                break;
            default:
                break;
            }
        }
    }


    // Flying
    void PlayerFlyState::initialize(Player& player) const
    {
        player.get_phobj().type =
            player.is_underwater() ? PhysicsObject::SWIMMING
                                   : PhysicsObject::FLYING;
    }

    void PlayerFlyState::send_action(Player&, KeyAction::Id, bool) const
    {
        // Facing and thrust both come from the polled input in update().
    }

    void PlayerFlyState::update(Player& player) const
    {
        if (player.is_attacking())
        {
            return;
        }

        int8_t inputx = horizontal_input(player);
        face_input(player, inputx);
        if (inputx != 0)
        {
            player.get_phobj().hforce = inputx > 0 ?  player.get_flyforce()
                                                   : -player.get_flyforce();
        }

        // Vertical intent is polled the same way: down and up held together
        // cancel instead of letting whichever was pressed first win.
        int8_t inputy = (player.is_key_down(KeyAction::DOWN) ? 1 : 0)
                      - (player.is_key_down(KeyAction::UP) ? 1 : 0);
        if (inputy != 0)
        {
            player.get_phobj().vforce = inputy > 0 ?  player.get_flyforce()
                                                   : -player.get_flyforce();
        }
    }

    void PlayerFlyState::update_state(Player& player) const
    {
        if (player.get_phobj().onground && player.is_underwater())
        {
            player.set_state(grounded_state(player));
        }
    }


    // Climbing
    void PlayerClimbState::initialize(Player& player) const
    {
        player.get_phobj().type = PhysicsObject::FIXATED;
    }

    void PlayerClimbState::send_action(Player& player, KeyAction::Id ka, bool down) const
    {
        if (down)
        {
            switch (ka)
            {
            case KeyAction::JUMP:
                if (player.is_key_down(KeyAction::LEFT))
                {
                    play_jumpsound();
                    player.set_direction(false);
                    player.get_phobj().hspeed = -player.get_walkforce() * 8.0;
                    player.get_phobj().vspeed = -player.get_jumpforce() / 1.5;
                    jump_off_ladder(player);
                }
                else if (player.is_key_down(KeyAction::RIGHT))
                {
                    play_jumpsound();
                    player.set_direction(true);
                    player.get_phobj().hspeed = player.get_walkforce() * 8.0;
                    player.get_phobj().vspeed = -player.get_jumpforce() / 1.5;
                    jump_off_ladder(player);
                }
                break;
            default:
                break;
            }
        }
    }

    void PlayerClimbState::update(Player& player) const
    {
        if (player.is_key_down(KeyAction::UP))
        {
            player.get_phobj().vspeed = -player.get_climbforce();
        }
        else if (player.is_key_down(KeyAction::DOWN))
        {
            player.get_phobj().vspeed = player.get_climbforce();
        }
        else
        {
            player.get_phobj().vspeed = 0.0;
        }
    }

    void PlayerClimbState::update_state(Player& player) const
    {
        int16_t y = player.get_phobj().get_y();
        bool downwards = player.is_key_down(KeyAction::DOWN);
        auto ladder = player.get_ladder();
        if (ladder && ladder->felloff(y, downwards))
        {
            cancel_ladder(player);
        }
    }

    void PlayerClimbState::cancel_ladder(Player& player) const
    {
        player.set_state(Char::FALL);
        player.set_ladder(nullptr);
    }

    void PlayerClimbState::jump_off_ladder(Player& player) const
    {
        player.set_state(Char::FALL);
        // Climbing off the end of a ladder puts the character out of its
        // vertical range, but jumping off does not: the climb key is still held
        // and the character is still lined up with the ladder, so the attach
        // check would grab it again on the very next tick. Remember which ladder
        // was left until the climb key is pressed again.
        player.release_ladder();
    }
}
