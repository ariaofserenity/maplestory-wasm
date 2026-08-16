#include "GroundEffect.h"

#include "../../Constants.h"
#include "../../Util/Randomizer.h"

#include "SpecialMove.h"

#include <algorithm>

namespace jrc
{
    namespace
    {
        const Randomizer randomizer;
    }

    GroundEffect::GroundEffect(Animation a, Point<int16_t> p,
        uint16_t hold, bool f)
        : animation(a), position(p), flip(f)
    {
        // The hold is only the middle of the effect's life: it fades up to full
        // strength first and away again afterwards, and is only dropped once
        // that last fade is over.
        fadein = (randomizer.next_int<int32_t>(TILE_FADE_IN_STEPS) + 1)
            * TILE_FADE_IN_STEP;
        peak = static_cast<float>(
            randomizer.next_int<int32_t>(TILE_ALPHA_MIN, TILE_ALPHA_MAX + 1)
        ) / TILE_ALPHA_MAX;

        lifetime = std::max<int32_t>(hold, Constants::TIMESTEP)
            + TILE_FADE_OUT_MS;
        remaining = lifetime;
    }

    float GroundEffect::opacity() const
    {
        // Fading up from nothing over a stretch rolled for this tile alone is
        // what makes a row of them light one after another rather than all at
        // once - each patch of fire catches as its own arrow reaches it.
        int32_t elapsed = lifetime - remaining;
        if (elapsed < fadein)
            return peak * elapsed / fadein;

        if (remaining < TILE_FADE_OUT_MS)
            return peak * remaining / TILE_FADE_OUT_MS;

        return peak;
    }

    void GroundEffect::draw(double viewx, double viewy, float alpha) const
    {
        // The view offsets are added, matching MovingObject::get_absolute.
        Point<int16_t> onscreen{
            static_cast<int16_t>(position.x() + viewx),
            static_cast<int16_t>(position.y() + viewy)
        };
        animation.draw({ onscreen, flip, opacity() }, alpha);
    }

    bool GroundEffect::update()
    {
        // The animation loops for as long as the effect lasts; its own
        // completion is not the end of the effect.
        animation.update();

        remaining -= Constants::TIMESTEP;
        return remaining <= 0;
    }
}
