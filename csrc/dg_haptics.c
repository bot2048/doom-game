// SPDX-License-Identifier: GPL-2.0-or-later
//
// Vibration in place of sound. KeyOS gives apps no audio output, so the
// sounds that matter most to the player are turned into haptic patterns.
// s_sound.c calls DG_SoundHook from S_StartSound (a Passport port patch);
// src/haptics.rs maps each DG_HAPTIC_* kind to a KeyOS pattern.
//
// Setting the sound effects volume to 0 in the menu turns this off.

#include <stddef.h>

#include "doomstat.h"
#include "p_local.h"
#include "sounds.h"

// Keep in sync with Haptic in src/haptics.rs.
enum
{
    DG_HAPTIC_FIRE_LIGHT = 1,
    DG_HAPTIC_FIRE_HEAVY,
    DG_HAPTIC_FIRE_RAPID,
    DG_HAPTIC_SAW,
    DG_HAPTIC_PAIN,
    DG_HAPTIC_DEATH,
    DG_HAPTIC_PICKUP,
    DG_HAPTIC_PICKUP_WEAPON,
    DG_HAPTIC_POWERUP,
    DG_HAPTIC_MENU,
};

extern int sfxVolume;

// Implemented in src/haptics.rs.
extern void dg_haptic(int kind);

// True for sounds made by the player, or by a missile the player just fired
// (missiles start at the shooter's position, and their sound plays before
// P_SpawnPlayerMissile sets their target).
static int from_player(const mobj_t *origin)
{
    const mobj_t *player = players[consoleplayer].mo;

    if (origin == NULL || player == NULL)
        return 0;
    return origin == player || (origin->x == player->x && origin->y == player->y);
}

static int haptic_for(const mobj_t *origin, int sfx_id)
{
    if (origin == NULL)
    {
        // Pickups and menus play without an origin.
        switch (sfx_id)
        {
            case sfx_itemup: return DG_HAPTIC_PICKUP;
            case sfx_wpnup: return DG_HAPTIC_PICKUP_WEAPON;
            case sfx_getpow: return DG_HAPTIC_POWERUP;
            case sfx_swtchn:
            case sfx_pistol: return DG_HAPTIC_MENU;
            default: return 0;
        }
    }
    if (!from_player(origin))
        return 0;
    switch (sfx_id)
    {
        case sfx_pistol:
        case sfx_punch: return DG_HAPTIC_FIRE_LIGHT;
        case sfx_shotgn:
        case sfx_dshtgn:
        case sfx_rlaunc:
        case sfx_bfg: return DG_HAPTIC_FIRE_HEAVY;
        case sfx_plasma: return DG_HAPTIC_FIRE_RAPID;
        case sfx_sawup:
        case sfx_sawful:
        case sfx_sawhit: return DG_HAPTIC_SAW;
        case sfx_plpain: return DG_HAPTIC_PAIN;
        case sfx_pldeth:
        case sfx_pdiehi: return DG_HAPTIC_DEATH;
        default: return 0;
    }
}

void DG_SoundHook(void *origin, int sfx_id)
{
    int kind;

    // No buzzing while the title screen plays its attract-mode demos.
    if (sfxVolume == 0 || demoplayback)
        return;
    kind = haptic_for((const mobj_t *) origin, sfx_id);
    if (kind != 0)
        dg_haptic(kind);
}
