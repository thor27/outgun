/*
 *  robot.cpp
 *
 *  Copyright (C) 2006, 2008 - Peter Kosyh
 *  Copyright (C) 2006, 2008 - Niko Ritari
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

#include <queue>
#include <vector>

#include "binaryaccess.h"
#include "leetnet/client.h"
#include "nassert.h"
#include "protocol.h"

#include "client.h"

using std::make_pair;
using std::min;
using std::pair;
using std::queue;
using std::vector;

const int SCAN_RADIUS = ROCKET_RADIUS;

const int S_W = plw;
const int S_H = plh;
const int FADEOUT = 50;

inline GunDirection inv_dir(GunDirection dir) throw () { return dir.adjust(4); }
inline int inv_dir(int dir) throw () { return dir ^ 4; }

bool Client::IsBehindWall(double mex, double mey, double dx, double dy) const throw () {
    const double tx = mex + dx;
    const double ty = mey + dy;
    const double dist = sqrt(sqr(dx) + sqr(dy));
    if (dist < PLAYER_RADIUS)
        return false;
    const double sx = dx / dist * 2 * SCAN_RADIUS;
    const double sy = dy / dist * 2 * SCAN_RADIUS;

    const Room& room = fx.map.room[fx.player[me].roomx][fx.player[me].roomy];
    bool at_wall = room.fall_on_wall(mex, mey, SCAN_RADIUS);

    while (1) {
        const bool blocked = room.fall_on_wall(mex, mey, SCAN_RADIUS);
        if (blocked && !at_wall)
            return true;
        if (!blocked)
            at_wall = false;

        mex += sx;
        mey += sy;
        if (fabs(tx - mex) < PLAYER_RADIUS && fabs(ty - mey) < PLAYER_RADIUS)
            break;
        if (mex > S_W || mex < 0)
            break;
        if (mey > S_H || mey < 0)
            break;
    }
    return false;
}

double Client::ScanDir(double mex, double mey, GunDirection dir) const throw () {
    const double deg = dir.toRad();

    const double sx = cos(deg) * 2 * SCAN_RADIUS;
    const double sy = sin(deg) * 2 * SCAN_RADIUS;

    double tx = mex;
    double ty = mey;

    const Room& room = fx.map.room[fx.player[me].roomx][fx.player[me].roomy];
    bool at_wall = room.fall_on_wall(mex, mey, SCAN_RADIUS);
    while (1) {
        const bool blocked = room.fall_on_wall(tx, ty, SCAN_RADIUS);
        if (blocked && !at_wall)
            break;
        if (!blocked && at_wall)
            at_wall = false;

        tx += sx;
        ty += sy;

        if (tx > S_W || tx < 0)
            break;
        if (ty > S_H || ty < 0)
            break;
    }
    return sqrt((tx - mex) * (tx - mex) + (ty - mey) * (ty - mey));
}

int Client::IsAimed(double mex, double mey, int i) const throw () { // return 2 if in hit point, 1 if almost in the gun direction and not behind a wall, 0 if elsewhere
    nAssert(!fx.physics.allowFreeTurning);

    // XXX?

    const double ttx = fx.player[i].lx + averageLag * fx.player[i].sx;
    const double tty = fx.player[i].ly + averageLag * fx.player[i].sy;

    double dx = ttx - mex;
    double dy = tty - mey;

    const double dist = sqrt(dx * dx + dy * dy);

    if (dist <= PLAYER_RADIUS)
        return 2;

    const double tm = dist / fx.physics.rocket_speed;
    dx += tm * fx.player[i].sx;
    dy += tm * fx.player[i].sy;

    const int dir = GetDir(dx, dy).to8way();

    if (myGundir != dir)
        return 0;

    if (IsBehindWall(mex, mey, dx, dy))
        return 0;

    static const int rocketsPerWeaponLevel[9] = { 1, 2, 3, 2, 3, 2, 3, 2, 3 };
    const int w = fx.player[me].weapon;
    const int rockets = w >= 1 && w <= 9 ? rocketsPerWeaponLevel[w - 1] : 1;
    static const double treshold = 1.3 * PLAYER_RADIUS + .7 * WorldBase::shot_deltax * (rockets - 1); // both 1.3 and .7 are semi-arbitrary
    if (dir == 0 || dir == 4) // left or right
        return fabs(dy) < treshold ? 2 : 1;
    if (dir == 2 || dir == 6) // up or down
        return fabs(dx) < treshold ? 2 : 1;
    // diagonal
    return fabs(fabs(dy) - fabs(dx)) <= treshold * sqrt(2) ? 2 : 1;
}

GunDirection Client::GetDir(double dx, double dy) const throw () {
    return GunDirection().fromRad(atan2(dy, dx));
}

int Client::GetDangerousRocket() const throw () {
    static const int thinkAheadFrames = 10;
    static const double safetyRoomMul = 4., framesAfterBounce = .5;

    int nearRocket = -1;
    double firstTime = 1e99;

    const ClientPlayer& player = fx.player[me];

    const Room& room = fx.map.room[player.roomx][player.roomy];

    const double bounceTime = fx.getTimeTillBounce(room, player, PLAYER_RADIUS, thinkAheadFrames).first;

    for (int i = 0; i < MAX_ROCKETS; ++i) {
        const Rocket& rocket = fx.rock[i];

        if (rocket.owner == -1 || rocket.team == player.team() && !fx.physics.friendly_fire || rocket.px != player.roomx || rocket.py != player.roomy)
            continue;

        const double collisionTime = fx.getTimeTillCollision(player, rocket, (PLAYER_RADIUS + ROCKET_RADIUS) * safetyRoomMul);
        if (collisionTime > bounceTime + framesAfterBounce || collisionTime >= firstTime)
            continue;

        const double rocketWallTime = fx.getTimeTillWall(room, rocket, thinkAheadFrames);
        if (collisionTime > rocketWallTime)
            continue;

        firstTime = collisionTime;
        nearRocket = i;
    }

    return nearRocket;
}

int Client::GetEasyEnemy(double mex, double mey) const throw () {
    int target = -1;
    double nearestDist = 1e10;

    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& enemy = fx.player[i];
        const ClientPlayer& player = fx.player[me];
        if (!enemy.used || enemy.team() == player.team() || !enemy.onscreen || enemy.dead)
            continue;

        const double ttx = enemy.lx + averageLag * enemy.sx;
        const double tty = enemy.ly + averageLag * enemy.sy;

        const double dx = ttx - mex;
        const double dy = tty - mey;

        const double playerDist = sqrt(dx * dx + dy * dy);
        const double escapeDist = (playerDist / fx.physics.rocket_speed) * fx.physics.max_run_speed / 2;

        double rocketPlaneDist;
        if (fx.physics.allowFreeTurning)
            rocketPlaneDist = playerDist;
        else {
            const int d = GetDir(dx, dy).to8way();
            if (d == 0 || d == 4)
                rocketPlaneDist = fabs(dy);
            else if (d == 2 || d == 6)
                rocketPlaneDist = fabs(dx);
            else if (d == 1 || d == 5)
                rocketPlaneDist = fabs(dy - dx) / sqrt(2.);
            else
                rocketPlaneDist = fabs(dy + dx) / sqrt(2.);
        }

        if (rocketPlaneDist < 2 * PLAYER_RADIUS && rocketPlaneDist + escapeDist < nearestDist && !IsBehindWall(mex, mey, ttx - mex, tty - mey)) { //was 4
            target = i;
            nearestDist = rocketPlaneDist + escapeDist;
        }
    }
    #ifdef BOTDEBUG
    if (target != -1)
        fprintf(stderr,"Looking on easy. enemy %d\n", target);
    #endif
    return target;
}

int Client::GetDangerousEnemy(double mex, double mey) const throw () {
    int target = -1;
    double nearestDist = 1e10;

    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& enemy = fx.player[i];
        const ClientPlayer& player = fx.player[me];
        if (!enemy.used || enemy.team() == player.team() || !enemy.onscreen || enemy.dead)
            continue;

        const double ttx = enemy.lx + averageLag * enemy.sx;
        const double tty = enemy.ly + averageLag * enemy.sy;

        const double dx = ttx - mex;
        const double dy = tty - mey;

        const double playerDist = sqrt(dx * dx + dy * dy);
        const GunDirection aimTowardsMe = inv_dir(GetDir(dx, dy));

        bool aimed;
        if (fx.physics.allowFreeTurning) {
            static const double tolerance = N_PI_4; // this in both directions -> angles within a 90° range are considered dangerous
            const double diff = positiveFmod(enemy.gundir.toRad() - aimTowardsMe.toRad(), 2 * N_PI);
            aimed = diff < tolerance || diff > 2 * N_PI - tolerance;
        }
        else
            aimed = enemy.gundir.to8way() == aimTowardsMe.to8way();

        if (!aimed && playerDist > 2 * PLAYER_RADIUS)
            continue;

        const double escapeDist = (playerDist / fx.physics.rocket_speed) * fx.physics.max_run_speed / 2;

        double rocketPlaneDist;
        if (fx.physics.allowFreeTurning)
            rocketPlaneDist = playerDist;
        else {
            const int d = aimTowardsMe.to8way();
            if (d == 0 || d == 4)
                rocketPlaneDist = fabs(dy);
            else if (d == 2 || d == 6)
                rocketPlaneDist = fabs(dx);
            else if (d == 1 || d == 5)
                rocketPlaneDist = fabs(dy - dx) / sqrt(2.);
            else
                rocketPlaneDist = fabs(dy + dx) / sqrt(2.);
        }

        if (rocketPlaneDist < 2 * PLAYER_RADIUS && rocketPlaneDist + escapeDist < nearestDist && !IsBehindWall(mex, mey, ttx - mex, tty - mey)) { //was 4
            target = i;
            nearestDist = rocketPlaneDist + escapeDist;
        }
    }
    #ifdef BOTDEBUG
    if (target != -1)
        fprintf(stderr,"Looking on dang. enemy %d\n", target);
    #endif
    return target;
}

int Client::GetNearestEnemy(double mex, double mey) const throw () {
    int target = -1;
    double mdist = 0;

    const ClientPlayer& player = fx.player[me];

    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& enemy = fx.player[i];
        if (!enemy.used || enemy.team() == player.team() || !enemy.onscreen || enemy.dead)
            continue;

        // XXX?
        const double ttx = enemy.lx + averageLag * enemy.sx;
        const double tty = enemy.ly + averageLag * enemy.sy;

        const double dx = ttx - mex;
        const double dy = tty - mey;

        const double dist = sqrt(dx * dx + dy * dy);

        if (dist < mdist || mdist == 0) {
            //if (IsBehindWall(mex, mey, dx, dy)) // XXX ?
            //    continue;
            mdist = dist;
            target = i;
        }
    }
    return target;
}

pair<bool, GunDirection> Client::TryAim(double mex, double mey, int target) const throw () {
    nAssert(fx.physics.allowFreeTurning);

    const double ttx = fx.player[target].lx + averageLag * fx.player[target].sx;
    const double tty = fx.player[target].ly + averageLag * fx.player[target].sy;

    double dx = ttx - mex;
    double dy = tty - mey;

    const double dist = sqrt(dx * dx + dy * dy);

    if (dist < 1)
        return make_pair(true, gunDir);

    const double tm = dist / fx.physics.rocket_speed;
    dx += tm * fx.player[target].sx;
    dy += tm * fx.player[target].sy;

    return make_pair(!IsBehindWall(mex, mey, dx, dy), GetDir(dx, dy));
}

double Client::GetHitTime(double mex, double mey, const GunDirection& dir, int iTarget) const throw () {
    const ClientPlayer& target = fx.player[iTarget];

    if (!target.onscreen || target.dead)
        return 1e100;

    const double ttx = target.lx + averageLag * target.sx;
    const double tty = target.ly + averageLag * target.sy;

    double dx = ttx - mex;
    double dy = tty - mey;

    const double initialDist = sqrt(sqr(dx) + sqr(dy));

    if (initialDist <= PLAYER_RADIUS)
        return 0;

    const double rsx = cos(dir.toRad()) * fx.physics.rocket_speed;
    const double rsy = sin(dir.toRad()) * fx.physics.rocket_speed;

    const double hitTime = initialDist / fx.physics.rocket_speed;
    dx += hitTime * (target.sx - rsx);
    dy += hitTime * (target.sy - rsy);

    return sqrt(sqr(dx) + sqr(dy)) < 3 * PLAYER_RADIUS ? hitTime : 1e100;
}

double Client::GetHitTeammateTime(double mex, double mey, const GunDirection& dir) const throw () {
    double hitTime = 1e100;
    if (fx.physics.friendly_fire == 0)
        return hitTime;
    for (int i = 0; i < maxplayers; ++i)
        if (fx.player[i].used && fx.player[i].team() == fx.player[me].team() && i != me)
            hitTime = min(hitTime, GetHitTime(mex, mey, dir, i));
    return hitTime;
}

pair<bool, GunDirection> Client::NeedShoot(double mex, double mey, const GunDirection& defaultDir) throw () {
    const ClientPlayer& player = fx.player[me];

    vector<int> tryOrder;
    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& pl = fx.player[i];
        if (!pl.used || pl.team() == player.team() || !pl.onscreen || pl.dead)
            continue;

        if (fx.physics.allowFreeTurning) {
            if (i != last_seen)
                tryOrder.push_back(i);
        }
        else {
            if (IsAimed(mex, mey, i) == 2 && GetHitTime(mex, mey, defaultDir, i) <= GetHitTeammateTime(mex, mey, defaultDir))
                return make_pair(true, GunDirection()); // the direction doesn't make any difference with non-free turning
        }
    }

    if (!fx.physics.allowFreeTurning)
        return make_pair(false, GunDirection());

    pair<bool, GunDirection> aimLastSeen = last_seen == -1 ? make_pair(false, defaultDir) : TryAim(mex, mey, last_seen);
    if (aimLastSeen.first) {
        if (GetHitTime(mex, mey, aimLastSeen.second, last_seen) <= GetHitTeammateTime(mex, mey, aimLastSeen.second))
            return aimLastSeen;
        aimLastSeen.first = false;
    }
    random_shuffle(tryOrder.begin(), tryOrder.end());
    for (vector<int>::const_iterator ti = tryOrder.begin(); ti != tryOrder.end(); ++ti) {
        const pair<bool, GunDirection> aim = TryAim(mex, mey, *ti);
        if (aim.first && GetHitTime(mex, mey, aim.second, *ti) <= GetHitTeammateTime(mex, mey, aim.second)) {
            last_seen = *ti;
            return aim;
        }
    }
    return aimLastSeen; // aim at last_seen if no one is actually shootable
}

ClientControls Client::EscapeRocket(double mex, double mey, int mrock) const throw () {
    const Rocket& rocket = fx.rock[mrock];
    const double sdx = rocket.x + averageLag * rocket.sx - mex;
    const double sdy = rocket.y + averageLag * rocket.sy - mey;

    ClientControls ctrl;
    ctrl.setStrafe();
    ctrl.setRun();

    switch (rocket.direction.to8way()) {
        case 0: // r -> d or up
        case 4: // l -> u or d
            if (sdy > 0)
                ctrl.setUp();
            else
                ctrl.setDown();
            break;
        case 2: // d - > l or r
        case 6: // u -> l or r
            if (sdx > 0)
                ctrl.setLeft();
            else
                ctrl.setRight();
            break;
        case 1: // rd -> ru | ld  "\"
        case 5: // lu -> ru | ld
            if (sdy > sdx) {
                ctrl.setUp();
                ctrl.setRight();
            }
            else {
                ctrl.setDown();
                ctrl.setLeft();
            }
            break;
        case 3: // ld -> rd | lu "/"
        case 7: // ur -> lu | rd
            if (sdy > -sdx) {
                ctrl.setUp();
                ctrl.setLeft();
            }
            else {
                ctrl.setDown();
                ctrl.setRight();
            }
            break;
    }
    return ctrl;
}

ClientControls Client::Aim(double mex, double mey, int i) const throw () {
    nAssert(!fx.physics.allowFreeTurning);

    const double ttx = fx.player[i].lx + averageLag * fx.player[i].sx;
    const double tty = fx.player[i].ly + averageLag * fx.player[i].sy;

    const double dx = ttx - mex;
    const double dy = tty - mey;

    const int aimed = IsAimed(mex, mey, i);
    if (aimed == 2)
        return ClientControls();
    else if (aimed == 0)
        return MoveTo(mex, mey, dx, dy);

    // almost aimed
    ClientControls ctrl;
    ctrl.setRun(); //always run!
    ctrl.setStrafe();
    if (myGundir == 0 || myGundir == 2 || myGundir == 4 || myGundir == 6) {
        if (dx > 0)
            ctrl.setRight();
        else if (dx < 0)
            ctrl.setLeft();
        if (dy > 0)
            ctrl.setDown();
        else if (dy < 0)
            ctrl.setUp();
    }
    else if (fabs(dy) > fabs(dx)) {
        if (dy < 0)
            ctrl.setUp();
        else
            ctrl.setDown();
    }
    else {
        if (dx < 0)
            ctrl.setLeft();
        else
            ctrl.setRight();
    }
    return ctrl;
}

int Client::FreeDir(double mex, double mey) const throw () {
    int mdir = 0;
    double mdist = 0;

    for (int i = myGundir - 1; i <= myGundir + 1; ++i) {
        const int d = (i + 8) % 8;
        const double dist = ScanDir(mex, mey, GunDirection().from8way(d));

        if (dist > mdist || mdist == 0 || dist == mdist && i == myGundir) {
            mdist = dist;
            mdir = d;
        }
    }
    if (mdist < 1.5 * PLAYER_RADIUS)
        mdir = inv_dir(mdir);
    return mdir;
}

ClientControls Client::MoveDirNoAggregate(int dir) const throw () {
    double sdx = 0;
    double sdy = 0;

    int n = 0;

    for (int i = 0; i < maxplayers; ++i) {
        if (!fx.player[i].used || fx.player[i].team() != fx.player[me].team() || fx.player[i].dead || !fx.player[i].onscreen || i == me)
            continue;

        const double dx = fx.player[i].lx - fx.player[me].lx + averageLag * (fx.player[i].sx - fx.player[me].sx);
        const double dy = fx.player[i].ly - fx.player[me].ly + averageLag * (fx.player[i].sy - fx.player[me].sy);
        if (fabs(dx) > PLAYER_RADIUS || fabs(dy) > PLAYER_RADIUS)
            continue;
        sdx += dx;
        sdy += dy;
        n++;
    }

    if (!n)
        return MoveDir(dir);

    if (!sdx && !sdy)
        dir = rand() % 8;

    sdx /= n;
    sdy /= n;

    ClientControls ctrl;
    ctrl.setRun();
    ctrl.setStrafe();

    switch (dir) {
        case 0:
        case 4:
            if (sdy > 0)
                ctrl.setUp();
            else
                ctrl.setDown();
            break;
        case 2:
        case 6:
            if (sdx > 0)
                ctrl.setLeft();
            else
                ctrl.setRight();
            break;
        case 1:
            if (sdy < sdx)
                ctrl.setDown();
            else
                ctrl.setRight();
            break;
        case 5:
            if (sdy < sdx)
                ctrl.setLeft();
            else
                ctrl.setUp();
            break;
        case 3:
            if (sdy > -sdx)
                ctrl.setLeft();
            else
                ctrl.setDown();
            break;
        case 7:
            if (sdy > -sdx)
                ctrl.setUp();
            else
                ctrl.setRight();
            break;
    }
    return ctrl;
}

ClientControls Client::MoveDir(int dir) const throw () {
    ClientControls ctrl;
    ctrl.setRun();
    switch(dir) {
        case 0:
            ctrl.setRight();
            break;
        case 1:
            ctrl.setRight();
            ctrl.setDown();
            break;
        case 2:
            ctrl.setDown();
            break;
        case 3:
            ctrl.setDown();
            ctrl.setLeft();
            break;
        case 4:
            ctrl.setLeft();
            break;
        case 5:
            ctrl.setLeft();
            ctrl.setUp();
            break;
        case 6:
            ctrl.setUp();
            break;
        case 7:
            ctrl.setUp();
            ctrl.setRight();
            break;
    }
    return ctrl;
}

ClientControls Client::FreeWalk(double mex, double mey) const throw () {
    return MoveDirNoAggregate(FreeDir(mex, mey));
}

ClientControls Client::MoveToNoAggregate(double mex, double mey, double dx, double dy) const throw () {
    if (IsBehindWall(mex, mey, dx, dy)) { //walking
        const int mdir = FreeDir(mex, mey);
        return MoveDir(mdir);
    }
    else {
        const int mdir = GetDir(dx, dy).to8way();
        return MoveDirNoAggregate(mdir);
    }
}


ClientControls Client::MoveTo(double mex, double mey, double dx, double dy) const throw () {
    int mdir;
    if (IsBehindWall(mex, mey, dx, dy))//walking
        mdir = FreeDir(mex, mey);
    else
        mdir = GetDir(dx, dy).to8way();
    return MoveDir(mdir);
}

ClientControls Client::GetPowerup(double mex, double mey) const throw () {
    for (int i = 0; i < MAX_POWERUPS; ++i) {
        if (fx.item[i].kind == Powerup::pup_unused || fx.item[i].kind == Powerup::pup_respawning ||
            fx.item[i].px != fx.player[me].roomx || fx.item[i].py != fx.player[me].roomy)
                continue;
        return MoveTo(mex, mey, fx.item[i].x - mex, fx.item[i].y - mey);
    }
    return ClientControls();
}

ClientControls Client::GetFlag(double mex, double mey) const throw () {
    const int myTeam = fx.player[me].team();

    if (HaveFlag(me)) {
        for (int type = 0; type < 2; ++type) { // 0 for own flags, 1 for wild
            if (!(type == 0 ? capture_on_team_flags_in_effect : capture_on_wild_flags_in_effect))
                continue;
            const int team = type == 0 ? myTeam : 2;
            const vector<Flag>& flags = type == 0 ? fx.teams[team].flags() : fx.wild_flags;
            for (vector<Flag>::const_iterator fi = flags.begin(); fi != flags.end(); ++fi) {
                if (fi->position().px != fx.player[me].roomx || fi->position().py != fx.player[me].roomy || fi->carried())
                    continue;
                if (type == 0 || IsFlagAtBase(*fi, team)) // try to capture, or return own flag so that capture is possible; can't return wild flags
                    return MoveTo(mex, mey, fi->position().x - mex, fi->position().y - mey);
            }
        }
        return ClientControls(); // can't pick up another flag
    }

    for (int type = 0; type < 3; ++type) { // 0 for own flags, 1 for enemy, 2 for wild
        if (type == 2 ? lock_wild_flags_in_effect : lock_team_flags_in_effect)
            continue;
        const int team = type == 0 ? myTeam : type == 1 ? !myTeam : 2;
        const vector<Flag>& flags = type == 2 ? fx.wild_flags : fx.teams[team].flags();
        for (vector<Flag>::const_iterator fi = flags.begin(); fi != flags.end(); ++fi) {
            if (fi->position().px != fx.player[me].roomx || fi->position().py != fx.player[me].roomy || fi->carried())
                continue;
            if (type != 0 || !IsFlagAtBase(*fi, team)) // try to pick up enemy or wild flag, or return own flag; nothing to do with own flags at base
                return MoveTo(mex, mey, fi->position().x - mex, fi->position().y - mey);
        }
    }

    return ClientControls();
}

int Client::Teams(int x, int y, int& en, int& fr) const throw () {
    int me_nr = -1;
    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& pl = fx.player[i];
        if (!pl.used || pl.roomx != x || pl.roomy != y || pl.dead)
            continue;
        if (pl.team() == fx.player[me].team()) {
            if (i == me)
                me_nr = fr;
            fr++;
        }
    }

    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& pl = fx.player[i];
        if (!pl.used || pl.roomx != x || pl.roomy != y || pl.dead)
            continue;
        if (pl.team() != fx.player[me].team()) {
            if (fx.frame - pl.posUpdated > FADEOUT)
                continue;
            if (fr && fx.frame - pl.posUpdated > 5)
                continue;
            en++;
        }
    }
    return me_nr;
}

bool Client::AmILast() const throw () {
    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& pl = fx.player[i];
        if (!pl.used || !pl.onscreen || pl.dead)
            continue;
        if (pl.team() == fx.player[me].team() && i > me)
            return false; // i am not last one
    }
    return true;
}

ClientControls Client::Escape(double mex, double mey) const throw () {
    if (!HaveFlag(me))
        return ClientControls();

    const int roomx = fx.player[me].roomx;
    const int roomy = fx.player[me].roomy;

    int enemies = 0;
    int friends = 0;
    Teams(roomx, roomy, enemies, friends);

    if (enemies <= friends)
        return ClientControls();
    // looking for friends

    for (int i = 0; i < 4; ++i) {
        if (!fx.map.room[roomx][roomy].pass[i])
            continue;
        int x = roomx, y = roomy;
        next_room(x, y, i);
        Teams(x, y, enemies, friends);
        if (friends + 1 > enemies && friends > 0)
            return MoveToDoor(mex, mey, i);
    }
    return ClientControls();
}

ClientControls Client::FollowFlag(double mex, double mey) const throw () {
    double dx = 0;
    double dy = 0;
    double sx = 0;
    double sy = 0;
    int num = 0;
    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& pl = fx.player[i];
        if (!pl.used || pl.team() != fx.player[me].team() || !pl.onscreen || pl.dead || i == me || !HaveFlag(i))
            continue;

        dx += pl.lx;
        dy += pl.ly;
        sx += pl.sx;
        sy += pl.sy;
        num++;
    }
    if (!num || (!sx && !sy) || IsHome(fx.player[me].roomx, fx.player[me].roomy))
        return ClientControls();
    dx = dx / num - mex;
    dy = dy / num - mey;
    sx = sx / num;
    sy = sy / num;
    double dist = (S_H + S_W) / 4;
    const double tm = dist / fx.physics.max_run_speed;
    dx += sx * tm;
    dy += sy * tm;
    dist = sqrt(dx * dx + dy * dy);
    if (dist < 3 * PLAYER_RADIUS)
        return ClientControls();
    return MoveToNoAggregate(mex, mey, dx, dy);
}

bool Client::scan_door(Room& room, int x, int y, int dx, int dy, int len) const throw () {
    const int nr = len / PLAYER_RADIUS;
    for (int i = 0; i < nr; i++) {
        if (!room.fall_on_wall(x, y, PLAYER_RADIUS))
            return true;
        x += dx;
        y += dy;
    }
    return false;
}

void Client::BuildMap() throw () {
    last_seen = -1;
    myGundir = -1;

    for (int x = 0; x < fx.map.w; ++x)
        for (int y = 0; y < fx.map.h; ++y) {
            Room& room = fx.map.room[x][y];
            room.pass[0] = scan_door(room, PLAYER_RADIUS,   0, PLAYER_RADIUS, 0, S_W - PLAYER_RADIUS);
            room.pass[1] = scan_door(room, PLAYER_RADIUS, S_H, PLAYER_RADIUS, 0, S_W - PLAYER_RADIUS);
            room.pass[2] = scan_door(room,   0, PLAYER_RADIUS, 0, PLAYER_RADIUS, S_H - PLAYER_RADIUS);
            room.pass[3] = scan_door(room, S_W, PLAYER_RADIUS, 0, PLAYER_RADIUS, S_H - PLAYER_RADIUS);
            for (int i = 0; i < Table_Max; i++) {
                room.route[i] = false;
                room.label[i] = -1;
            }
            room.visited_frame = 0;
            #ifdef BOTDEBUG
            fprintf(stderr,"%d %d: %d %d %d %d\n", x, y,
                    room.pass[0], room.pass[1], room.pass[2], room.pass[3]);
            #endif
        }

    for (int i = 0; i < Table_Max; i++) {
        route_x[i] = route_y[i] = -1;
        routing[i] = Route_None;
        routeTableCenter[i].x = -1;
    }
}

void Client::next_room(int& x, int& y, int i) const throw () {
    switch (i) {
        case 0:
            --y;
            if (y < 0)
                y = fx.map.h - 1;
            break;
        case 1:
            ++y;
            if (y >= fx.map.h)
                y = 0;
            break;
        case 2:
            --x;
            if (x < 0)
                x = fx.map.w - 1;
            break;
        case 3:
            ++x;
            if (x >= fx.map.w)
                x = 0;
            break;
    }
}

int Client::route_room(int& x, int& y, RouteTable num) throw () {
    int label = fx.map.room[x][y].label[num];
    if (label == -1) // not labeled
        return 0;

    int routes[4];
    int route_nr = 0;

    for (int i = 0; i < 4 && label != 0; i++) {
        if (!fx.map.room[x][y].pass[i])
            continue;
        int nx = x;
        int ny = y;
        next_room(nx, ny, i);
        if (fx.map.room[nx][ny].label[num] != label - 1)
            continue;
        routes[route_nr++] = i;
    }
    if (route_nr == 0)
        return 0;
    next_room(x, y, routes[rand() % route_nr]);
    fx.map.room[x][y].route[num] = true;
    return route_nr;
}

void Client::BuildRouteTable(int mex, int mey, RouteTable num) throw () {
    return BuildRouteTable(vector<RoomCoords>(1, RoomCoords(mex, mey)), num);
}

void Client::BuildRouteTable(const vector<RoomCoords>& startPoints, RouteTable num) throw () {
    const int w = fx.map.w;
    const int h = fx.map.h;

    if (startPoints.size() == 1) {
        if (routeTableCenter[num] == startPoints[0])
            return;
        routeTableCenter[num] = startPoints[0];
    }
    else
        routeTableCenter[num].x = -1;

    for (int x = 0; x < w; ++x)
        for (int y = 0; y < h; ++y) {
            fx.map.room[x][y].label[num] = -1;
            fx.map.room[x][y].route[num] = false;
        }

    queue<RoomCoords> workQueue;
    for (vector<RoomCoords>::const_iterator ti = startPoints.begin(); ti != startPoints.end(); ++ti) {
        fx.map.room[ti->x][ti->y].label[num] = 0;
        workQueue.push(*ti);
    }
    while (!workQueue.empty()) {
        const RoomCoords& rc = workQueue.front();
        const Room& r = fx.map.room[rc.x][rc.y];
        for (int i = 0; i < 4; ++i) {
            if (!r.pass[i])
                continue;
            int nx = rc.x, ny = rc.y;
            next_room(nx, ny, i);
            if (fx.map.room[nx][ny].label[num] != -1) // already labeled
                continue;
            fx.map.room[nx][ny].label[num] = r.label[num] + 1;
            workQueue.push(RoomCoords(nx, ny));
        }
        workQueue.pop();
    }
    #ifdef BOTDEBUG
    fprintf(stderr,"BuildRoute table from %d %d\n", mex, mey);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x)
            fprintf(stderr,"%02d ", fx.map.room[x][y].label[num]);
        fprintf(stderr,"\n");
    }
    #endif
}

int Client::BuildRoute(int tox, int toy, RouteTable num) throw () {
    #ifdef BOTDEBUG
    static int tox_old = -1, toy_old = -1;
    if (tox != tox_old || toy != toy_old) {
        fprintf(stderr, "Build route %d to %d %d\n", me, tox, toy);
        tox_old = tox;
        toy_old = toy;
    }
    #endif
    nAssert(tox >= 0 && tox < fx.map.w);
    nAssert(toy >= 0 && toy < fx.map.h);
    nAssert(me >= 0 && me < maxplayers);

    const int label = fx.map.room[tox][toy].label[num];
    const int mex = fx.player[me].roomx;
    const int mey = fx.player[me].roomy;

    if (fx.map.room[mex][mey].route[num] && tox == route_x[num] && toy == route_y[num])
        return label;

    for (int x = 0; x < fx.map.w; ++x) // clear route
        for (int y = 0; y < fx.map.h; ++y)
            fx.map.room[x][y].route[num] = false;

    if (label == -1 || fx.map.room[mex][mey].label[num] == -1 ||
        fx.map.room[mex][mey].label[num] > fx.map.room[tox][toy].label[num])
            nAssert(0);

    fx.map.room[tox][toy].route[num] = true;

    int i = 0;

    route_x[num] = tox;
    route_y[num] = toy;

    while (route_room(tox, toy, num))
        ++i;
    #ifdef BOTDEBUG
    fprintf(stderr, "BuildRoute  to %d %d\n", tox, toy);
    for (int y = 0; y < fx.map.h; ++y) {
        for (int x = 0; x < fx.map.w; ++x)
            fprintf(stderr,"%02d ", fx.map.room[x][y].route[num]);
        fprintf(stderr,"\n");
    }
    #endif
    return i;
}

ClientControls Client::Route(double melx, double mely, RouteTable num) const throw () {
    if (routing[num] == Route_None)
        return ClientControls();

    const int mex = fx.player[me].roomx;
    const int mey = fx.player[me].roomy;

    const int label = fx.map.room[mex][mey].label[num];

    if (label == -1 || route_x[num] == mex && route_y[num] == mey)
        return ClientControls();

    int dir = -1;

    for (int i = 0; i < 4; ++i) {
        if (!fx.map.room[mex][mey].pass[i])
            continue;
        int x = mex, y = mey;
        next_room(x, y, i);
        if (fx.map.room[x][y].route[num] && fx.map.room[x][y].label[num] == label + 1) {
            if (HaveFlag(me)) {
                int enemies = 0, friends = 0;
                Teams(x, y, enemies, friends);
                if (enemies > friends + 1)
                    continue;
            }
            dir = i;
            break;
        }
    }

    if (dir == -1)
        return ClientControls(); // no need to go

    #ifdef BOTDEBUG
    fprintf(stderr,"i am @ (%d %d) -> (%d %d)\n", fx.player[me].roomx, fx.player[me].roomy, mex, mey);
    #endif

    return MoveToDoor(melx, mely, dir);
}

ClientControls Client::MoveToDoor(double dmex, double dmey, int dir) const throw () {
    const int mex = int(dmex), mey = int(dmey);
    int fixedBorder, fixedTarget;
    bool xFixed;
    ClientControls ctrl;
    ctrl.setRun();
    switch (dir) {
    /*break;*/ case 0:
            fixedBorder = 0;
            fixedTarget = -PLAYER_RADIUS;
            xFixed = false;
            if (dmey < 0) // if predicted correctly, we're already in the target room (by the time our controls reach the server)
                return ctrl.setUp();
        break; case 1:
            fixedBorder = S_H;
            fixedTarget = S_H + PLAYER_RADIUS;
            xFixed = false;
            if (dmey > S_H)
                return ctrl.setDown();
        break; case 2:
            fixedBorder = 0;
            fixedTarget = -PLAYER_RADIUS;
            xFixed = true;
            if (dmex < 0)
                return ctrl.setLeft();
        break; case 3:
            fixedBorder = S_W;
            fixedTarget = S_W + PLAYER_RADIUS;
            xFixed = true;
            if (dmex > S_W)
                return ctrl.setRight();
        break; default:
            nAssert(0);
    }
    const Room& room = fx.map.room[fx.player[me].roomx][fx.player[me].roomy];
    for (int r = 4; r >= 1; --r) { // try to find a larger hole first, mainly to avoid bumping into walls
        const double holeRadius = r * PLAYER_RADIUS;
        if (xFixed) {
            for (int dist = 0; dist < S_H; dist += PLAYER_RADIUS) {
                if (mey - dist > 0   && !room.fall_on_wall(fixedBorder, mey - dist, holeRadius))
                    return MoveToNoAggregate(mex, mey, fixedTarget - mex, -dist);
                if (mey + dist < S_H && !room.fall_on_wall(fixedBorder, mey + dist, holeRadius))
                    return MoveToNoAggregate(mex, mey, fixedTarget - mex,  dist);
            }
        }
        else
            for (int dist = 0; dist < S_W; dist += PLAYER_RADIUS) {
                if (mex - dist > 0   && !room.fall_on_wall(mex - dist, fixedBorder, holeRadius))
                    return MoveToNoAggregate(mex, mey, -dist, fixedTarget - mey);
                if (mex + dist < S_W && !room.fall_on_wall(mex + dist, fixedBorder, holeRadius))
                    return MoveToNoAggregate(mex, mey,  dist, fixedTarget - mey);
            }
    }
    // not even a player-sized opening found (at the probed places); one should reveal itself when we move about, or if not, the map sucks
    if (xFixed)
        return MoveToNoAggregate(mex, mey, fixedTarget - mex, 0);
    else
        return MoveToNoAggregate(mex, mey, 0, fixedTarget - mey);
}

int Client::GetPlayers(int team) const throw () {
    int npl = 0;
    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& player = fx.player[i];
        if (player.used && player.team() == team)
            ++npl;
    }
    return npl;
}

bool Client::IsDefender() throw () {
    // get flag base
    const int team = fx.player[me].team();
    const vector<WorldCoords>& tflags = fx.map.tinfo[team].flags;

    if (tflags.empty())
        return false;

    const int npl = GetPlayers(team);
    const int defNum = npl / 2 / tflags.size();

    // for all bases
    for (vector<WorldCoords>::const_iterator pi = tflags.begin(); pi != tflags.end(); ++pi) {
        BuildRouteTable(pi->px, pi->py, Table_Def);
        const int m_label = fx.map.room[fx.player[me].roomx][fx.player[me].roomy].label[Table_Def];
        int nearNum = 0;
        for (int i = 0; i < maxplayers; ++i) {
            const ClientPlayer& player = fx.player[i];
            if (!player.used || player.team() != fx.player[me].team() || player.dead || i == me ||
                player.roomx >= fx.map.w || player.roomy >= fx.map.h)
                    continue;
            const int label = fx.map.room[player.roomx][player.roomy].label[Table_Def];
            if (label < m_label || label == m_label && i < me || HaveFlag(i))
                nearNum++;
        }
        if (nearNum < defNum)
            return true;
    }
    return false;
}

bool Client::RouteLogic(RouteTable num) throw () { // NEED rewrite
    const int flag = HaveFlag(me);
    routing[num] = Route_None;

    if (!flag) {
        const bool at_bases = IsFlagsAtBases(fx.player[me].team()); // are own flags safe?

        const bool sef = !lock_team_flags_in_effect; // try to steal enemy flags?
        const bool swf = !lock_wild_flags_in_effect; // try to steal wild flags?

        bool efc = !IsCarriersDef(1 - fx.player[me].team()); // try to defend carriers of enemy flags?
        bool wfc = !IsCarriersDef(2); // try to defend carriers of wild flags?
        bool mfb = sef && IsDefender(); // try to defend own flags at bases? (!sef means the enemy won't try our flags either, so nothing to defend)

        if (at_bases && !efc && !wfc && !mfb) { // all flags are safe and nothing to support
            TargetRoute(sef, sef,   0,
                          0,   0,   0,
                        swf, swf,   0,   0,
                          0,   0,
                          0,   0,   0,
                        num);
            if (routing[num] == Route_None) { // we are in control of all flags -> always defend
                efc = wfc = 1;
                mfb = sef; // still no point in defending the base if the flag can't be taken - resources better spent defending carriers
            }
        }
        if (routing[num] == Route_None) {
            TargetRoute(sef, sef, efc,
                        mfb,   1,   1,
                        swf, swf,   1, wfc,
                          0,   0,
                          0,   0,   0,
                        num);
        }
        if (routing[num] == Route_None) {
            TargetRoute(  0,   0,   0,
                          0,   0,   0,
                          0,   0,   0,   0,
                          1,   0,
                        sef,   0,   0,
                        num);  // ..., or enemy, or enemy base
        }
        if (routing[num] == Route_None || routing[num] == Route_Base) {
            if (routing[num] == Route_Base) {
                int enemies = 0;
                int friends = 0;
                if (Teams(route_x[num], route_y[num], enemies, friends) > 0)
                    friends--;

                if (friends) { // if we are going to base where is already our forces, forget it
                    if (route_x[num] != fx.player[me].roomx || route_y[num] != fx.player[me].roomy || AmILast())
                        TargetFog(num);
                }
            }
            else
                TargetFog(num);
        }
    }
    else { // i am flagman ;)
        const bool ctf = capture_on_team_flags_in_effect;
        const bool cwf = capture_on_wild_flags_in_effect;

        if (GetPlayers(fx.player[me].team()) > 1) {
            TargetRoute(  0,   0,   0,
                        ctf,   0,   0,
                        cwf,   0,   0,   0,
                          0,   0,
                          0,   0,   0,
                        num); // available capture point
        }
        else {
            TargetRoute(  0,   0,   0,
                        ctf,   1,   0,
                        cwf,   0,   0,   0,
                          0,   0,
                          0,   0,   0,
                        num); // available capture point, or dropped own team flag
        }
        if (routing[num] == Route_None) {
            TargetRoute(  0,   0,   0,
                          0,   0,   0,
                          0,   0,   0,   0,
                          0,   0,
                          0, ctf, cwf && flag != 2, // don't expect to capture a wild flag on an empty wild flag base: the missing flag might be the one we're carrying
                        num);  // ok, to capture point, even if unavailable
        }
    }
    #ifdef BOTDEBUG
    //fprintf(stderr,"RouteLogic: %d\n", i);
    #endif
    return routing[num] != Route_None;
}

bool Client::IsMassive() const throw () {
    double dx = 0, dy = 0;
    int n = 0;
    for (int i = 0; i < maxplayers; ++i) {
        if (!fx.player[i].used || fx.player[i].team() != fx.player[me].team() || fx.player[i].dead || !fx.player[i].onscreen)
            continue;

        dx += fabs(fx.player[i].lx - fx.player[me].lx + averageLag * (fx.player[i].sx - fx.player[me].sx));
        dy += fabs(fx.player[i].ly - fx.player[me].ly + averageLag * (fx.player[i].sy - fx.player[me].sy));
        ++n;
    }

    if (n > 1) {
        dx = dx / n;
        dy = dy / n;
    }
    else
        return false;

    const double dist = sqrt(dx * dx + dy * dy);
    return dist <= 2 * PLAYER_RADIUS;
}

int Client::HaveFlag(int n) const throw () {
    const int t = 1 - fx.player[n].team();
    nAssert(t == 0 || t == 1);

    // look for enemy flags in team
    for (vector<Flag>::const_iterator fi = fx.teams[t].flags().begin(); fi != fx.teams[t].flags().end(); ++fi)
        if (fi->carried() && fi->carrier() == n)
            return 1;

    // looking for wild flags
    for (vector<Flag>::const_iterator fi = fx.wild_flags.begin(); fi != fx.wild_flags.end(); ++fi)
        if (fi->carried() && fi->carrier() == n)
            return 2;

    return 0;
}

bool Client::IsFlagAtBase(const Flag& f, int team) const throw () {
    const vector<WorldCoords>& bases = fx.map.tinfo[team].flags;
    for (vector<WorldCoords>::const_iterator bi = bases.begin(); bi != bases.end(); ++bi)
        if (bi->px == f.position().px && bi->py == f.position().py && fabs(bi->x - f.position().x) <= 5. && fabs(bi->y - f.position().y) <= 5.)
            return true;
    return false;
}

int Client::TargetNearestBase(int& m_label, int& x, int& y, int team, RouteTable num) throw () {
    const vector<WorldCoords>& tflags = fx.map.tinfo[team].flags;
    int label = 0;

    for (vector<WorldCoords>::const_iterator pi = tflags.begin(); pi != tflags.end(); ++pi) {
        label = fx.map.room[pi->px][pi->py].label[num];
        if (label == -1)
            continue;
        if (label < m_label || m_label == -1) {
            m_label = label;
            x = pi->px;
            y = pi->py;
            routing[num] = Route_Base;
        }
    }

    return 0;
}

int Client::TargetNearestTeam(int& m_label, int& x, int& y, int team, RouteTable num) throw () {
    // looking for soldiers
    const bool enemy = (fx.player[me].team() != team);

    int label = 0;
    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& pl = fx.player[i];
        if (i == me || !pl.used || pl.team() != team || pl.dead || pl.roomx >= fx.map.w || pl.roomy >= fx.map.h)
            continue;

        label = fx.map.room[pl.roomx][pl.roomy].label[num];
        if (label == -1)
            continue;

        if (enemy) { // if enemy, check fadeout
            if (fx.frame - pl.posUpdated > FADEOUT) // TODO fadeout
                continue; // old data
            if (!pl.onscreen) {
                if (pl.roomx == fx.player[me].roomx && pl.roomy == fx.player[me].roomy)
                    continue; // already here
                if (fx.map.room[pl.roomx][pl.roomy].visited_frame > pl.posUpdated)
                    continue; // was her
            }
        }

        if (label < m_label || m_label == -1) {
            m_label = label;
            x = pl.roomx;
            y = pl.roomy;
            routing[num] = Route_Team;
        }
    }

    return 0;
}

bool Client::IsCarriersDef(int team) throw () {
    const vector<Flag>& flags = (team != 2) ? fx.teams[team].flags() : fx.wild_flags;

    vector<RoomCoords> carrierRooms;

    for (vector<Flag>::const_iterator fi = flags.begin(); fi != flags.end(); ++fi)
        if (fi->carried()) {
            const ClientPlayer& pl = fx.player[fi->carrier()];
            carrierRooms.push_back(RoomCoords(pl.roomx, pl.roomy));
        }

    if (carrierRooms.empty()) // nothing to defend
        return true;

    BuildRouteTable(carrierRooms, Table_Def);

    int teammates = 0, nearer = 0;
    const int myDist = fx.map.room[fx.player[me].roomx][fx.player[me].roomy].label[Table_Def];
    for (int pi = 0; pi < maxplayers; ++pi) {
        const ClientPlayer& pl = fx.player[pi];
        if (!pl.used || pl.team() != fx.player[me].team())
            continue;
        ++teammates;
        const int dist = fx.map.room[pl.roomx][pl.roomy].label[Table_Def];
        if (dist < myDist || dist == myDist && pi < me)
            ++nearer;
    }
    return nearer >= teammates / 2;
}

bool Client::IsHome(int mex, int mey) const throw () {
    const vector<WorldCoords>& tflags = fx.map.tinfo[fx.player[me].team()].flags;
    // our bases
    for (vector<WorldCoords>::const_iterator pi = tflags.begin(); pi != tflags.end(); ++pi)
        if (pi->px == mex && pi->py == mey)
            return true;
    return false;
}

bool Client::IsFlagsAtBases(int team) const throw () {
    const vector<Flag>& flags = (team != 2) ? fx.teams[team].flags() : fx.wild_flags;

    for (vector<Flag>::const_iterator fi = flags.begin(); fi != flags.end(); ++fi) {
        if (fi->carried())
            return false;
        const vector<WorldCoords>& tflags = fx.map.tinfo[team].flags;
        bool at_base = false;
        for (vector<WorldCoords>::const_iterator pi = tflags.begin(); pi != tflags.end(); ++pi)
            if (pi->px == fi->position().px && pi->py == fi->position().py) {
                at_base = true;
                break;
            }
        if (!at_base)
            return false;
    }
    return true;
}

int Client::TargetNearestFlag(int& m_label, int& x, int& y, int team, int state, RouteTable num) throw () {
    // state - 0 - at base, 1 - dropped off base, 2 - carried by friends, 3 - carried by enemy

    const bool wantCarried = state == 2 || state == 3;
    const int carrierTeam = state == 2 ? fx.player[me].team() : !fx.player[me].team();

    const vector<Flag>& flags = (team != 2) ? fx.teams[team].flags() : fx.wild_flags;

    for (vector<Flag>::const_iterator fi = flags.begin(); fi != flags.end(); ++fi) {
        if (fi->carried() != wantCarried)
            continue;

        int nx, ny;
        if (fi->carried()) {
            const ClientPlayer& pl = fx.player[fi->carrier()];
            if (pl.team() != carrierTeam || !pl.used || pl.roomx >= fx.map.w || pl.roomy >= fx.map.h)
                continue;
            if (state == 3 && !pl.onscreen) { // check if the position is current enough
                if (fx.frame - pl.posUpdated > FADEOUT) // TODO fadeout
                    continue;
                if (pl.roomx == fx.player[me].roomx && pl.roomy == fx.player[me].roomy)
                    continue;
                if (fx.map.room[pl.roomx][pl.roomy].visited_frame > pl.posUpdated)
                    continue;
            }
            nx = pl.roomx;
            ny = pl.roomy;
        }
        else {
            if (IsFlagAtBase(*fi, team) != (state == 0))
                continue;
            nx = fi->position().px;
            ny = fi->position().py;
        }

        nAssert(nx < fx.map.w && ny < fx.map.h);
        const int label = fx.map.room[nx][ny].label[num];
        if (label == -1)
            continue;

        if (label < m_label || m_label == -1) {
            m_label = label;
            x = nx;
            y = ny;
            routing[num] = Route_Flag;
        }
    }

    return 0; // build route to nearest target
}

int Client::TargetFog(RouteTable num) throw () {
    int roomx = fx.player[me].roomx;
    int roomy = fx.player[me].roomy;
    double delta = 0;
    int maxi = -1;
    double max_delta = 0;

    for (int i = 0; i < 4; i++) {
        int x = roomx, y = roomy;
        const Room& room = fx.map.room[x][y];
        if (!room.pass[i])
            continue;
        next_room(x, y, i);
        int enemies = 0, friends = 0;
        if (Teams(x, y, enemies, friends) >= 0)
            friends--;
        if (friends && !enemies) // our sector
            continue;
        delta = fabs(fx.frame - fx.map.room[x][y].visited_frame);
        if (delta >= max_delta) {
            max_delta = delta;
            maxi = i;
        }
    }
    if (maxi == -1)
        return 0;

    next_room(roomx, roomy, maxi);
    routing[num] = Route_Fog;
    return BuildRoute(roomx, roomy, num);
}

/* TargetRoute(enemy flag {at base, dropped off base, carried},
 *                my flag {at base, dropped off base, carried},
 *              wild flag {at base, dropped off base, carried by enemy, carried by friend},
 *       {enemy, my} team,
 * {enemy, my, wild} base,
 *                   <route table number>)
 *
 * targets first one of the enabled options that yields the minimal distance among enabled options.
 *
 * flag: a flag with desired status and probably known location
 * team: a living non-me player with probably known location
 * base: a base, regardless of where its flag is
 */
int Client::TargetRoute(int efb, int efd, int efc,
                        int mfb, int mfd, int mfc,
                        int wfb, int wfd, int wfce, int wfcf,
                        int en,  int fr,
                        int eb,  int fb, int wb,
                        RouteTable num) throw () {
    int m_label = -1;
    int x = -1, y = -1;
    const int t = fx.player[me].team();
    const int et = 1 - t;
    int n = 0;

    BuildRouteTable(fx.player[me].roomx, fx.player[me].roomy, num);

    routing[num] = Route_None;

    if (efb)
        n += TargetNearestFlag(m_label, x, y, et, 0, num);

    if (efd)
        n += TargetNearestFlag(m_label, x, y, et, 1, num);

    if (efc)
        n += TargetNearestFlag(m_label, x, y, et, 2, num);

    if (mfb)
        n += TargetNearestFlag(m_label, x, y, t, 0, num);

    if (mfd)
        n += TargetNearestFlag(m_label, x, y, t, 1, num);

    if (mfc)
        n += TargetNearestFlag(m_label, x, y, t, 3, num);

    if (wfb)
        n += TargetNearestFlag(m_label, x, y, 2, 0, num);

    if (wfd)
        n += TargetNearestFlag(m_label, x, y, 2, 1, num);

    if (wfce)
        n += TargetNearestFlag(m_label, x, y, 2, 3, num);

    if (wfcf)
        n += TargetNearestFlag(m_label, x, y, 2, 2, num);

    if (en)
        n += TargetNearestTeam(m_label, x, y, et, num);

    if (fr)
        n += TargetNearestTeam(m_label, x, y, t, num);

    if (eb)
        n += TargetNearestBase(m_label, x, y, et, num);
    if (fb)
        n += TargetNearestBase(m_label, x, y, t, num);
    if (wb)
        n += TargetNearestBase(m_label, x, y, 2, num);

    if (n < 0)
        return -1;

    if (routing[num] == Route_None) // nothing todo
        return 0;

    nAssert(x != -1 && y != -1);

    return BuildRoute(x, y, num);
}

bool Client::IsMission(RouteTable num) const throw () {
    const int to_home = IsHome(route_x[num], route_y[num]);
    // if we are looking for flag or going to our base for something
    if (fx.player[me].roomx == route_x[num] && fx.player[me].roomy == route_y[num])
        return false;
    return HaveFlag(me) || routing[num] == Route_Flag || to_home || !to_home && routing[num] == Route_Base;
}

ClientControls Client::getRobotControls() throw () {
    const double mex = fx.player[me].lx + averageLag * fx.player[me].sx;
    const double mey = fx.player[me].ly + averageLag * fx.player[me].sy;

    fx.map.room[fx.player[me].roomx][fx.player[me].roomy].visited_frame = fx.frame;

    if (last_seen != -1) {
        const ClientPlayer& lsp = fx.player[last_seen];
        if (!lsp.used || lsp.team() == fx.player[me].team() || !lsp.onscreen || lsp.dead) // lost target
            last_seen = -1;
    }

    if (!IsMassive()) {
        const int dangerousRocket = GetDangerousRocket();
        if (dangerousRocket != -1) {
            if (last_seen == -1)
                last_seen = GetDangerousEnemy(mex, mey);
            return EscapeRocket(mex, mey, dangerousRocket);
        }
    }

    if (last_seen == -1) {
        const ClientControls ctrl = GetFlag(mex, mey);
        if (!ctrl.idle()) // if any
            return ctrl;
    }

    RouteLogic(Table_Main);

    if (IsMission(Table_Main)) // get only dangerous enemy
        last_seen = GetDangerousEnemy(mex, mey);
    else if (last_seen != -1) { // already locked on someone
        const int easy = GetEasyEnemy(mex, mey); // more easy???
        if (easy != -1)
            last_seen = easy;
    }
    else // get someone
        last_seen = GetNearestEnemy(mex, mey);

    if (last_seen != -1 && !fx.physics.allowFreeTurning)
        return Aim(mex, mey, last_seen);

    // ok, free tour ;)
    ClientControls ctrl = GetPowerup(mex, mey);
    if (!ctrl.idle())
        return ctrl;

    ctrl = Escape(mex, mey);
    if (!ctrl.idle())
        return ctrl;

    ctrl = Route(mex, mey, Table_Main);
    if (!ctrl.idle())
        return ctrl;

    ctrl = FollowFlag(mex, mey);
    if (!ctrl.idle())
        return ctrl;

    ctrl = FreeWalk(mex, mey);
    if (last_seen == -1)
        ctrl.clearRun();
    return ctrl;
}

ClientControls Client::Robot() throw () {
    const bool hide_map = !map_ready || gameover_plaque != NEXTMAP_NONE || fx.skipped || me < 0 || me >= maxplayers;

    if (hide_map || !fx.player[me].used || fx.player[me].dead || fx.player[me].team() != 0 && fx.player[me].team() != 1 ||
                    fx.player[me].roomx >= fx.map.w || fx.player[me].roomy >= fx.map.h) {
        myGundir = -1;
        if (botPrevFire) {
            BinaryBuffer<16> msg;
            msg.U8(data_fire_off);
            client->send_message(msg);
            botPrevFire = false;
        }
        return ClientControls();
    }

    if (myGundir == -1) // was dead, or something like that
        myGundir = fx.player[me].gundir.to8way();

    const ClientControls ctrl = getRobotControls();

    if (!ctrl.isStrafe()) {
        const int newDirection = ctrl.getDirection();
        if (newDirection != -1)
            myGundir = newDirection;
    }

    const double mex = fx.player[me].lx + averageLag * fx.player[me].sx;
    const double mey = fx.player[me].ly + averageLag * fx.player[me].sy;
    const pair<bool, GunDirection> shootDir = NeedShoot(mex, mey, GunDirection().from8way(myGundir)); // if there's no player to target, aim where we're going
    bool actuallyShoot = shootDir.first;
    if (fx.physics.allowFreeTurning) {
        // adjust gunDir
        static const double turnCeilingPerFrame = N_PI_2;
        static const double displacementMul = .7;
        static const double shootTreshold = N_PI / 8.; // shoot if aim is within 22° of target
        const double targetDiff = positiveFmod(shootDir.second.toRad() - gunDir.toRad() + N_PI, 2 * N_PI) - N_PI;
        double actualDiff = targetDiff;
        if (fabs(actualDiff) > turnCeilingPerFrame)
            actualDiff = turnCeilingPerFrame * (actualDiff > 0 ? +1. : -1.);
        const double modifier = (rand() % 10001 - 5000) / 5000.;
        actualDiff *= 1. + displacementMul * modifier * modifier * modifier; // actually turn by something between actualDiff * (1 +/- displacementMul), weighted so that values in the middle are more likely than extremes
        gunDir.adjust(actualDiff);
        if (fabs(actualDiff - targetDiff) > shootTreshold)
            actuallyShoot = false;
    }
    if (actuallyShoot) {
        if (!botPrevFire) {
            BinaryBuffer<16> msg;
            msg.U8(data_fire_on);
            client->send_message(msg);
            botPrevFire = true;
        }
    }
    else if (botPrevFire) {
        BinaryBuffer<16> msg;
        msg.U8(data_fire_off);
        client->send_message(msg);
        botPrevFire = false;
    }

    return ctrl;
}
