#pragma once

#include "../../plugins/BlackFlame/src/CorpseColourRules.h"

#include <cmath>

namespace CorpseColoursTests
{
    struct Colour
    {
        float red, green, blue, alpha;
        bool operator==(const Colour&) const = default;
    };

    struct Particles
    {
        Colour colorKey1, colorKey2, colorKey3;
    };

    struct Membrane
    {
        Colour fillColor, rimColor;
    };

    inline bool Grey(const Colour& a_colour)
    {
        return a_colour.red == a_colour.green && a_colour.green == a_colour.blue;
    }

    template <class Check>
    void Run(Check&& a_check)
    {
        namespace C = BlackFlame::CorpseColours;
        const Particles live{ { 1.0f, 0.25f, 0.0f, 0.3f }, { 0.5f, 0.1f, 0.0f, 0.7f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        // The record can be patched after load; the engine factory reads its live values. No texture copy or palette
        // exists in this case. A neighbouring NPC SRE already uses the shader's shared particle property.
        Particles shared = live;
        Particles own{};
        bool shareSeen = true;
        int calls = 0;
        auto factory = [&](bool a_share) {
            ++calls;
            shareSeen = a_share;
            if (a_share) {
                return &shared;
            }
            own = live;
            return &own;
        };
        auto* marked = C::MakeParticles(true, true, factory);
        a_check(calls == 1 && !shareSeen && marked == &own && Grey(own.colorKey1) && Grey(own.colorKey2) &&
                    Grey(own.colorKey3) && shared.colorKey1 == live.colorKey1 && shared.colorKey2 == live.colorKey2,
            "corpse colours: marked SRE without texture copies gets private grey keys; the NPC's shared keys stay orange");
        a_check(own.colorKey1.alpha == live.colorKey1.alpha && own.colorKey2.alpha == live.colorKey2.alpha &&
                    own.colorKey3.alpha == live.colorKey3.alpha && std::abs(own.colorKey1.red - 0.3914f) < 0.00001f,
            "corpse colours: live RGB uses Rec. 709 luminance and preserves every key's alpha");
        (void)C::MakeParticles(true, false, factory);
        a_check(calls == 2 && !shareSeen && Grey(own.colorKey1),
            "corpse colours: an already private factory request still greys the keys");
        auto* npc = C::MakeParticles(false, true, factory);
        a_check(calls == 3 && shareSeen && npc == &shared && shared.colorKey1 == live.colorKey1,
            "corpse colours: unmarked SRE forwards share=true and keeps its colours");
        (void)C::MakeParticles(false, false, factory);
        a_check(!shareSeen && own.colorKey1 == live.colorKey1,
            "corpse colours: unmarked private SRE is left alone too");
        a_check(C::MakeParticles(true, true, [](bool) -> Particles* { return nullptr; }) == nullptr,
            "corpse colours: a failed factory is not dereferenced");

        const Membrane source{ live.colorKey1, live.colorKey2 };
        Membrane player = source, other = source;
        a_check(C::GreyMembrane(true, &player) && Grey(player.fillColor) && Grey(player.rimColor) &&
                    player.fillColor.alpha == source.fillColor.alpha && player.rimColor.alpha == source.rimColor.alpha &&
                    !C::GreyMembrane(false, &other) && other.fillColor == source.fillColor && other.rimColor == source.rimColor,
            "corpse colours: only the marked membrane's fill and rim RGB change, with alpha preserved");
        player = source;  // Update / Pop refill the instance from the still-orange record
        a_check(C::GreyMembrane(true, &player) && Grey(player.fillColor) && Grey(player.rimColor),
            "corpse colours: greying after a membrane refill does not depend on the first attach or the current buff");
        a_check(!C::GreyMembrane(true, static_cast<Membrane*>(nullptr)),
            "corpse colours: a particle-only SRE has no membrane to grey");
        player.rimColor = { -1.0f, -0.25f, 0.0f, 0.7f };
        (void)C::GreyMembrane(true, &player);
        a_check(Grey(player.rimColor) && player.rimColor.red < 0.0f && player.rimColor.alpha == 0.7f,
            "corpse colours: subtractive rim RGB stays signed rather than clamped to black");
    }
}
