#pragma once
#include "MapObjects.h"
#include "Summon.h"

#include <cstdint>
#include <vector>

namespace jrc
{
    class Combat;
    class MapMobs;
    class Player;

    // The summons standing on the current map, everyone's and not just the
    // player's.
    //
    // A summon is driven by the client that owns it: that client walks it after
    // its owner, picks what it swings at and works out what it did, then tells
    // the server. Everyone else only ever plays back what the server repeats.
    // That split is why the attack code below is gated on ownership - running
    // it for someone else's hawk would have two clients claiming the same kill.
    class MapSummons
    {
    public:
        void draw(Layer::Id layer, double viewx, double viewy, float alpha) const;
        // Advance every summon, and drive the player's own.
        void update(const Physics& physics, const Player& player, MapMobs& mobs,
            Combat& combat);

        // Put a summon on the map, replacing an earlier one from the same skill
        // - a second cast of Puppet moves the puppet rather than adding one.
        void spawn(int32_t oid, int32_t owner, int32_t skillid, int32_t skilllevel,
            Summon::MovementType movementtype, bool attacks,
            Point<int16_t> position, bool flip);
        void remove(int32_t owner, int32_t oid, bool animated);
        void clear();

        // Play back a summon's movement, swing or flinch as the server reports
        // it. All three are no-ops for a summon this client is driving itself.
        void send_movement(int32_t owner, int32_t oid, Point<int16_t> position, bool flip);
        void show_attack(int32_t owner, int32_t oid);
        void show_hit(int32_t owner, int32_t oid);

    private:
        // Look for something for one of the player's summons to hit, and swing
        // at it if there is.
        void try_attack(Summon& summon, const Player& player, MapMobs& mobs,
            Combat& combat);

        MapObjects summons;
        // Time since the player's summons last reported where they are, in ms.
        // Their positions are streamed rather than sent per step, the same way
        // the player's own are.
        int32_t movementtimer = 0;
    };
}
