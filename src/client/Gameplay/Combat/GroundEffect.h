#pragma once
#include "../../Graphics/Animation.h"
#include "../../Template/Point.h"

#include <cstdint>

namespace jrc
{
    // An animation that lingers where a skill landed: the fire Inferno leaves
    // burning, a poison cloud, the scorch marks of a meteor.
    //
    // Unlike MovingEffect this does not travel, and unlike a character effect it
    // is anchored to the map and outlives by seconds the attack that created it,
    // so it cannot live in the caster's own effect layer.
    class GroundEffect
    {
    public:
        GroundEffect(Animation animation, Point<int16_t> position,
            uint16_t lifetime, bool flip);

        void draw(double viewx, double viewy, float alpha) const;
        // Advance one tick. Returns true once the effect has expired.
        bool update();

    private:
        // Opacity ramp so the effect dies away instead of vanishing.
        float opacity() const;

        Animation animation;
        Point<int16_t> position;
        int32_t lifetime;
        int32_t remaining;
        bool flip;
    };
}
