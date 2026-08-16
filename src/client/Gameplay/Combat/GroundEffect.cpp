#include "GroundEffect.h"

#include "../../Constants.h"

namespace jrc
{
    GroundEffect::GroundEffect(Animation a, Point<int16_t> p,
        uint16_t lifetime, bool f)
        : animation(a), position(p), flip(f)
    {
        remaining = (lifetime > 0) ? lifetime : Constants::TIMESTEP;
        this->lifetime = remaining;
    }

    float GroundEffect::opacity() const
    {
        // Burn out over the closing slice, and ease in briefly so a strip of
        // tiles does not snap on all at once.
        constexpr int32_t FADE_IN_MS = 120;
        constexpr int32_t FADE_OUT_MS = 500;

        int32_t elapsed = lifetime - remaining;
        if (elapsed < FADE_IN_MS)
            return static_cast<float>(elapsed) / FADE_IN_MS;

        if (remaining < FADE_OUT_MS)
            return static_cast<float>(remaining) / FADE_OUT_MS;

        return 1.0f;
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
