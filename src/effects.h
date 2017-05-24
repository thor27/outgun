/*
 *  effects.h
 *
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2004 - Jani Rivinoja
 *  Copyright (C) 2008 - Niko Ritari
 *
 *  This file is part of Outgun.
 *
 *  Outgun is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Outgun is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Outgun; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef EFFECTS_H_INC
#define EFFECTS_H_INC

#include "world.h"

// client side effects

enum FX_TYPE {
    FX_GUN_EXPLOSION,
    FX_TURBO,
    FX_WALL_EXPLOSION,
    FX_POWER_WALL_EXPLOSION,
    FX_DEATHCARRIER_SMOKE
};

struct GraphicsEffect {
    FX_TYPE type;       // type of fx
    WorldCoords pos;
    double time;        // start time

    int team;
    float alpha;  // [0, 1]
    // for turbo effect
    int col1, col2;
    GunDirection gundir;

    GraphicsEffect(FX_TYPE type_, const WorldCoords& pos_, double time_, int team_, float alpha_ = 0, int col1_ = 0, int col2_ = 0, GunDirection gundir_ = GunDirection()) throw ()
        : type(type_), pos(pos_), time(time_), team(team_), alpha(alpha_), col1(col1_), col2(col2_), gundir(gundir_) { }
};

#endif // EFFECTS_H_INC
