/*
 *  world.h
 *
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004, 2005 - Niko Ritari
 *  Copyright (C) 2003, 2004, 2005, 2006 - Jani Rivinoja
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

#ifndef WORLD_H_INC
#define WORLD_H_INC

#include <vector>
#include <list>
#include <string>
#include <algorithm>
#include "commont.h"
#include "nassert.h"
#include "protocol.h"   // needed for possible definition of SEND_FRAMEOFFSET
#include "utility.h"

typedef std::pair<double, double> Coords;
typedef std::pair<double, Coords> BounceData;

class WallBase {    // base class
public:
    WallBase() { }
    WallBase(int tex_, int alpha_) : tex(tex_), alpha(alpha_) { }
    virtual ~WallBase() { }
    virtual bool intersects_rect(double x1, double y1, double x2, double y2) const = 0;
    virtual bool intersects_circ(double x, double y, double r) const = 0;
    virtual void tryBounce(BounceData* bd, double stx, double sty, double mx, double my, double plyRadius) const = 0;
    int texture() const { return tex; }

private:
    int tex, alpha;
};

class RectWall : public WallBase {  // rectangular wall
public:
    RectWall() { }
    RectWall(double a_, double b_, double c_, double d_, int tex_, int alpha_)
            : WallBase(tex_, alpha_), a(a_), b(b_), c(c_), d(d_) { if (c<a) std::swap(a, c); if (d<b) std::swap(b, d); }

    double x1() const { return a; }
    double y1() const { return b; }
    double x2() const { return c; }
    double y2() const { return d; }

    bool intersects_rect(double x1, double y1, double x2, double y2) const { return x1<=c && x2>=a && y1<=d && y2>=b; } // perfect
    bool intersects_circ(double x, double y, double r) const;   // perfect
    void tryBounce(BounceData* bd, double stx, double sty, double mx, double my, double plyRadius) const;

private:
    double a, b, c, d;  // rectangle coords (a,b)->(c,d)
};

class TriWall : public WallBase {   // triangular wall
public:
    TriWall() { }
    TriWall(double x1, double y1, double x2, double y2, double x3, double y3, int tex_, int alpha_);

    double x1() const { return p1x; }
    double y1() const { return p1y; }
    double x2() const { return p2x; }
    double y2() const { return p2y; }
    double x3() const { return p3x; }
    double y3() const { return p3y; }

    bool intersects_rect(double rx1, double ry1, double rx2, double ry2) const; // perfect
    bool intersects_circ(double x, double y, double r) const;                   // perfect
    void tryBounce(BounceData* bd, double stx, double sty, double mx, double my, double plyRadius) const;

private:
    double p1x, p1y, p2x, p2y, p3x, p3y;
    double boundx1, boundy1, boundx2, boundy2;
};

class CircWall : public WallBase {  // circular wall
public:
    CircWall() { }
    CircWall(double x_, double y_, double ro_, double ri_, double ang1, double ang2, int tex_, int alpha_);

    double X() const { return x; }
    double Y() const { return y; }
    double radius() const { return ro; }
    double radius_in() const { return ri; }
    const double* angles() const { return angle; }
    const Coords& angle_vector_1() const { return va1; }
    const Coords& angle_vector_2() const { return va2; }

    bool intersects_rect(double x1, double y1, double x2, double y2) const; // very much imperfect (uses bounding circle)
    bool intersects_circ(double rcx, double rcy, double rr) const;  // imperfect
    void tryBounce(BounceData* bd, double stx, double sty, double mx, double my, double plyRadius) const;

private:
    double x, y, ro, ri;
    double angle[2];
    Coords va1, va2, midvec;
    double anglecos;
};

class Room {
public:
    ~Room();

    void addWall(WallBase* w) { walls.push_back(w); }
    void addGround(WallBase* w) { ground.push_back(w); }

    bool fall_on_wall(int x1, int y1, int x2, int y2) const;    // this check follows the quality of *Wall::intersects_rect and isn't perfect
    bool fall_on_wall(int x, int y, int r) const;   // this check follows the quality of *Wall::intersects_circ and isn't perfect
    BounceData genGetTimeTillWall(double x, double y, double mx, double my, double radius, double maxFraction) const;

    const std::vector<WallBase*>& readWalls() const { return walls; }
    const std::vector<WallBase*>& readGround() const { return ground; }

private:
    std::vector<WallBase*> walls, ground;   // ground: optional list of textures for ground
};

//entity locale
struct WorldCoords {
    WorldCoords(int px_, int py_, int x_, int y_): px(px_), py(py_), x(x_), y(y_) { }
    WorldCoords() { }
    int px, py; //screen (if px == -1, unused)
    int x, y;   //relative (to screen) X,Y position
};

//team info
struct MapTeam {
    std::vector<WorldCoords> flags; //flag positions
    std::vector<WorldCoords> spawn; //team spawn points
    unsigned int lastspawn;         //last team spawn point used

    MapTeam() : lastspawn(0) { }
};

class Map {
    bool parse_file(LogSet& log, std::istream& in);
    bool parse_line(LogSet& log, const std::string& line, const std::vector<std::pair<std::string, std::vector<std::string> > >& label_lines,
                    int& crx, int& cry, double& scalex, double& scaley, bool label_block = false);

public:
    MapTeam tinfo[2];   //team information for red=0 and blue=1 teams
    std::vector<WorldCoords> wild_flags;
    std::vector< std::vector<Room> > room;  // accessed by [x][y]

    std::string title;  //map title
    std::string author;
    int w, h;   // width height
    NLushort crc;   //map's 16bit CRC

    Map() : w(0), h(0), crc(0) { }

    bool fall_on_wall(int px, int py, int x1, int y1, int x2, int y2) const {
if (px<0 || py<0 || px>=w || py>=h) return false;   //#fix: remove this and track why these are given sometimes
        nAssert(px>=0 && py>=0 && px<w && py<h);
        return room[px][py].fall_on_wall(x1, y1, x2, y2);
    }
    bool fall_on_wall(int px, int py, int x, int y, int r) const {
if (px<0 || py<0 || px>=w || py>=h) return false;   //#fix: remove this and track why these are given sometimes
        nAssert(px>=0 && py>=0 && px<w && py<h);
        return room[px][py].fall_on_wall(x, y, r);
    }
    bool load(LogSet& log, const std::string& mapdir, const std::string& mapname);
};

class MapInfo {
public:
    std::string title, author, file;
    int width, height;
    int votes, sentVotes;
    NLulong last_game;  // last game in the map (frame #)
    bool highlight;     // for the map list in the client

    MapInfo();
    bool load(LogSet& log, const std::string& mapName);
};

class Statistics {
public:
    Statistics();

    void clear(bool preserveTime);

    void set_frags(int n) { total_frags = n; }
    void set_kills(int n) { total_kills = n; }
    void set_deaths(int n) { total_deaths = n; }
    void set_cons_kills(int n) { most_consecutive_kills = n; }
    void set_current_cons_kills(int n) { current_consecutive_kills = n; }
    void set_cons_deaths(int n) { most_consecutive_deaths = n; }
    void set_current_cons_deaths(int n) { current_consecutive_deaths = n; }
    void set_suicides(int n) { total_suicides = n; }
    void set_captures(int n) { total_captures = n; }
    void set_flags_taken(int n) { total_flags_taken = n; }
    void set_flags_dropped(int n) { total_flags_dropped = n; }
    void set_flags_returned(int n) { total_flags_returned = n; }
    void set_carriers_killed(int n) { total_flag_carriers_killed = n; }
    void set_shots(int n) { total_shots = n; }
    void set_hits(int n) { total_hits = n; }
    void set_shots_taken(int n) { total_shots_taken = n; }
    void set_movement(double amount) { total_movement = amount; }
    void set_spawn_time(double time) { last_spawn_time = time; }
    void set_start_time(double time) { starttime = time; }
    void set_lifetime(double time) { total_lifetime = time; }
    void set_flag_carrying_time(double time) { total_flag_carrying_time = time; }
    void set_flag_take_time(double time) { flag_taking_time = time; }
    void set_flag(bool f, bool wild) { nAssert(!wild || f); flag = f; wild_flag = wild; }
    void set_dead(bool d) { dead = d; }

    void spawn(double time) { set_spawn_time(time); nAssert(dead); dead = false; }
    void kill(double time, bool allowAlreadyDead = false);

    void add_frag(int n = 1) { total_frags += n; }
    void add_kill(bool deathbringer);
    void add_death(bool deathbringer, double time);
    void add_suicide(double time);
    void add_capture(double time);
    void add_flag_take(double time, bool wild);
    void add_flag_drop(double time);
    void add_flag_return() { ++total_flags_returned; }
    void add_carrier_kill() { ++total_flag_carriers_killed; }
    void add_shot() { ++total_shots; }
    void add_hit() { ++total_hits; }
    void add_shot_take() { ++total_shots_taken; }
    void add_movement(double amount) { total_movement += amount; }

    void finish_stats(double time);

    void save_speed(double time) { saved_speed = speed(time); }

    int frags() const { return total_frags; }
    int kills() const { return total_kills; }
    int deaths() const { return total_deaths; }
    int cons_kills() const { return most_consecutive_kills; }
    int current_cons_kills() const { return current_consecutive_kills; }
    int cons_deaths() const { return most_consecutive_deaths; }
    int current_cons_deaths() const { return current_consecutive_deaths; }
    int suicides() const { return total_suicides; }
    int captures() const { return total_captures; }
    int flags_taken() const { return total_flags_taken; }
    int flags_dropped() const { return total_flags_dropped; }
    int flags_returned() const { return total_flags_returned; }
    int carriers_killed() const { return total_flag_carriers_killed; }
    int shots() const { return total_shots; }
    int hits() const { return total_hits; }
    double accuracy() const;
    int shots_taken() const { return total_shots_taken; }
    double spawn_time() const { return last_spawn_time; }
    double lifetime(double time) const;         // in seconds
    double average_lifetime(double time) const; // in seconds
    double playtime(double time) const;         // in seconds
    double movement() const;                    // in Outgun units
    double speed(double time) const;            // in Outgun units per second
    double old_speed() const { return saved_speed; }
    double start_time() const { return starttime; }
    double flag_carrying_time(double time) const;
    double flag_take_time() const { return flag_taking_time; }
    bool has_flag() const { return flag; }  // true for both enemy flag and wild flag
    bool has_wild_flag() const { return wild_flag; }

private:
    int total_frags;
    int total_kills;
    int total_deaths;
    int total_deathbringer_kills;
    int total_deathbringer_deaths;
    int most_consecutive_kills;
    int current_consecutive_kills;
    int most_consecutive_deaths;
    int current_consecutive_deaths;
    int total_suicides;
    int total_captures;
    int total_flags_taken;
    int total_flags_dropped;
    int total_flags_returned;
    int total_flag_carriers_killed;
    int total_shots;
    int total_hits;
    int total_shots_taken;
    double last_spawn_time;
    double total_lifetime;
    double total_movement;
    double saved_speed;
    double starttime;
    bool dead;
    bool flag, wild_flag;
    double total_flag_carrying_time;
    double flag_taking_time;
};

class PlayerBase {
protected:
    PlayerBase() { clear(false, 0, "", 0); }

    int team_nr;
    int personal_color;
    Statistics player_stats;

public:
    bool item_deathbringer;
    bool item_shield;
    bool item_power;
    bool item_turbo;
    int visibility;     // alpha

    int roomx, roomy;
    double lx, ly, sx, sy;  // position within room and speed
    ClientControls controls;
    int gundir; // gun direction 0-7 (0 = right 1 = right-down 2 = down ...... 7 = right-up

// get rid of (or move elsewhere)
    bool used;
    int id; // as in pid
    std::string name;
    int ping;
    //int frags;
    bool dead;
    ClientLoginStatus reg_status;
    int score, rank;
    int neg_score;

    virtual ~PlayerBase() { }
    void move(double fraction) { lx += sx*fraction; ly += sy*fraction; }
    void clear(bool enable, int _pid, const std::string& _name, int team_id);

    void set_team(int t) { team_nr = t; }
    void set_color(int c) { personal_color = c; }

    const Statistics& stats() const { return player_stats; }
    Statistics& stats() { return player_stats; }

    bool item_shadow() const { return visibility < 255; }
    int team() const { return team_nr; }
    int color() const { return personal_color; }
    virtual bool under_deathbringer_effect(double curr_time) const =0;
};

bool compare_players(const PlayerBase* a, const PlayerBase* b);

class ServerPlayer : public PlayerBase {
public:
    int health;
    int energy;

    int weapon;
    bool attack;    // if player is holding attack button

    double item_power_time;
    double item_turbo_time;
    double item_shadow_time;

    long item_deathbringer_time;    // explosion of this players deathbringer
    int deathbringer_team;  // valid if own deathbringer has exploded (this is needed for when changing teams)
    double deathbringer_end;    // end of effect of another players deathbringer
    int deathbringer_attacker;  // whose deathbringer it is

    int awaiting_client_readies;
    bool want_map_exit;

    size_t current_map_list_item;

    int mapVote;
    int idleFrames;
    int kickTimer;
    int muted;  // 0 = no, 1 = yes, 2 = silently

    bool want_change_teams;
    double team_change_time;
    bool team_change_pending;

    int cid;    // client id (network identity)
    NLubyte lastClientFrame;    // client set frame identifier of the latest data received
    #ifdef SEND_FRAMEOFFSET
    double frameOffset; // at what time within the frame the client's packet arrived
    #endif
    double waitnametime;
    bool localIP;
    int oldfrags;   // last value informed to clients
    int megabonus;

    bool drop_key;
    bool dropped_flag;
    double next_shoot_time;
    double respawn_time;
    bool respawn_to_base;

    double talk_temp;
    double talk_hotness;

    ServerPlayer() { clear(false, 0, 0, "", 0); }

    bool under_deathbringer_effect(double curr_time) const { return deathbringer_end >= curr_time; }

    void clear(bool enable, int _pid, int _cid, const std::string& _name, int team_id);

    void set_fav_colors(const std::vector<char>& colors) { fav_col = colors; }
    const std::vector<char>& fav_colors() const { return fav_col; }

private:
    std::vector<char> fav_col;
};

class ClientPlayer : public PlayerBase {
public:
    ClientPlayer() { clear(false, 0, "", 0); }

    bool deathbringer_affected;
    double next_smoke_effect_time;
    double next_turbo_effect_time;
    double wall_sound_time;
    double player_sound_time;
    bool onscreen;
    double hitfx;
    int drawptr;
    int drawused;
    int oldx, oldy; // detect room changes
    NLulong posUpdated; // on which frame the player's position has been last received (including the low-res info for minimap purposes)
    int alpha;

    // get rid of these since they are only known for the local player
    double item_power_time;
    double item_turbo_time;
    double item_shadow_time;
    int health;
    int energy;
    int weapon;

    bool under_deathbringer_effect(double curr_time) const { (void)curr_time; return deathbringer_affected; }

    void clear(bool enable, int _pid, const std::string& _name, int team_id);
};

// a rocket-shot
class Rocket {
public:
    int owner;  //owning player-id (-1 == unused)
    int team;
    bool power;

    NLulong vislist;    //notification list (bitfield, bit0=player0, bit1=player1... etc.)
    int px, py;         //screen coords
    double x, y;        //start position or current position
    double sx, sy;      //speed
    int direction;
    NLulong time;       //time of shot or current time

    Rocket() { owner = -1; }
    void move(double fraction) { x += sx*fraction; y += sy*fraction; }
};

class Flag {
public:
    Flag(const WorldCoords& pos_);

    void take(int carr);
    void take(int carr, double time);
    void return_to_base();
    void drop();
    void move(const WorldCoords& new_pos) { pos = new_pos; }

    void set_drop_time(double time) { drop_t = time; }

    bool carried() const { return status == status_carried; }
    bool at_base() const { return status == status_at_base; }

    int carrier() const { return carrier_id; }
    double grab_time() const { return grab_t; }
    double drop_time() const { return drop_t; }

    const WorldCoords& position() const { return pos; }
    const WorldCoords& home_position() const { return home_pos; }

private:
    enum Status { status_at_base, status_carried, status_dropped };

    Status status;
    int carrier_id;
    double grab_t;
    double drop_t;
    WorldCoords home_pos;
    WorldCoords pos;
};

class Team {
public:
    Team();

    void clear_stats();

    void set_score(int n) { points = n; }
    void set_kills(int n) { total_kills = n; }
    void set_deaths(int n) { total_deaths = n; }
    void set_suicides(int n) { total_suicides = n; }
    void set_flags_taken(int n) { total_flags_taken = n; }
    void set_flags_dropped(int n) { total_flags_dropped = n; }
    void set_flags_returned(int n) { total_flags_returned = n; }
    void set_shots(int n) { total_shots = n; }
    void set_hits(int n) { total_hits = n; }
    void set_shots_taken(int n) { total_shots_taken = n; }
    void set_base_score(int n) { start_score = n; }
    void set_movement(double amount) { total_movement = amount; }
    void set_power(double pow) { tournament_power = pow; }

    void add_score(double time, const std::string& player);
    void add_kill() { ++total_kills; }
    void add_death() { ++total_deaths; }
    void add_suicide() { ++total_suicides; ++total_deaths; }
    void add_flag_take() { ++total_flags_taken; }
    void add_flag_drop() { ++total_flags_dropped; }
    void add_flag_return() { ++total_flags_returned; }
    void add_shot() { ++total_shots; }
    void add_hit() { ++total_hits; }
    void add_shot_take() { ++total_shots_taken; }
    void add_movement(double amount) { total_movement += amount; }

    void add_flag(const WorldCoords& pos);
    void remove_flags();

    void steal_flag(int n, int carrier);
    void steal_flag(int n, int carrier, double time);

    void return_all_flags();
    void return_flag(int n);
    void drop_flag(int n, const WorldCoords& pos);
    void move_flag(int n, const WorldCoords& pos);

    void set_flag_drop_time(int n, double time);

    int score() const { return points; }
    int kills() const { return total_kills; }
    int deaths() const { return total_deaths; }
    int suicides() const { return total_suicides; }
    int flags_taken() const { return total_flags_taken; }
    int flags_dropped() const { return total_flags_dropped; }
    int flags_returned() const { return total_flags_returned; }
    int shots() const { return total_shots; }
    int hits() const { return total_hits; }
    int shots_taken() const { return total_shots_taken; }
    double movement() const { return total_movement; }
    double accuracy() const;
    double power() const { return tournament_power; }

    const Flag& flag(int n) const { return team_flags[n]; }
    const std::vector<Flag>& flags() const { return team_flags; }

    const std::vector<std::pair<int, std::string> >& captures() const { return caps; }
    int base_score() const { return start_score; }

private:
    int points;
    int total_kills;
    int total_deaths;
    int total_suicides;
    int total_flags_taken;
    int total_flags_dropped;
    int total_flags_returned;
    int total_shots;
    int total_hits;
    int total_shots_taken;
    double total_movement;
    double tournament_power;
    std::vector<Flag> team_flags;
    std::vector<std::pair<int, std::string> > caps; // time and player name
    int start_score;    // for players who join in the middle of the game
};

class Powerup {
public:
    enum Pup_type {
        pup_shield,
        pup_turbo,
        pup_shadow,
        pup_power,
        pup_weapon,
        pup_health,
        pup_deathbringer,
        pup_last_real = pup_deathbringer,
        pup_unused,
        pup_respawning
    };

    Pup_type kind;  // type of powerup

    double respawn_time;        // time to respawn

    int px; //screen
    int py;
    int x;  //position
    int y;

    Powerup(): kind(pup_unused) { }
};

template<class Type> class PointerContainer {   // doesn't delete the objects!
    Type* ptr;

public:
    PointerContainer() : ptr(0) { }
    PointerContainer(Type* p) : ptr(p) { }

    void setPtr(Type* p) { ptr = p; }

          Type* getPtr()       { return ptr; }
    const Type* getPtr() const { return ptr; }

    operator       Type&()       { nAssert(ptr); return *ptr; }
    operator const Type&() const { nAssert(ptr); return *ptr; }
};

class PhysicalSettings {
public:
    double fric, drag, accel;
    double brake_mul, turn_mul, run_mul, turbo_mul, flag_mul;
    double rocket_speed;
    double friendly_fire, friendly_db;
    enum PlayerCollisions { PC_none, PC_normal, PC_special } player_collisions;

    double max_run_speed;   // max speed without turbo, for turbo effect in client

    PhysicalSettings();
    void calc_max_run_speed();
    void read(char* lebuf, int& count);
    void write(char* lebuf, int& count) const;
};

class PhysicsCallbacksBase {
public:
    struct PlayerHitResult {
        std::pair<bool, bool> deaths;
        double bounceStrength1, bounceStrength2;
        PlayerHitResult(bool dead1, bool dead2, double s1, double s2) : deaths(std::pair<bool, bool>(dead1, dead2)), bounceStrength1(s1), bounceStrength2(s2) { }
    };

    virtual ~PhysicsCallbacksBase() { }
    virtual bool collideToRockets() const =0;   // should player to rocket collisions be checked at all
    virtual bool gatherMovementDistance() const =0; // should addMovementDistance be called with player movements
    virtual bool allowRoomChange() const =0;
    virtual void addMovementDistance(int pid, double dist) =0;  // player pid has moved the distance dist
    virtual void playerScreenChange(int pid) =0;    // player pid has moved to a new room (called max. once per frame per player)
    virtual void rocketHitWall(int rid, bool power, double x, double y, int roomx, int roomy) =0;   // caller doesn't remove the rocket
    virtual bool rocketHitPlayer(int rid, int pid) =0;  // returns true if player dies (to be removed from further simulation)
    virtual void playerHitWall(int pid) =0;
    virtual PlayerHitResult playerHitPlayer(int pid1, int pid2, double speed) =0;
    virtual void rocketOutOfBounds(int rid) =0; // caller doesn't remove the rocket
    virtual bool shouldApplyPhysicsToPlayer(int pid) =0;    // returns true physics should be run to player pid
};

class WorldBase {
    void addRocket(int i, int playernum, int team, int px, int py, int x, int y,
                                                    bool power, int dir, int xdelta, int frameAdvance, PhysicsCallbacksBase& cb);

    static BounceData getTimeTillBounce(const Room& room, const PlayerBase& pl, double plyRadius, double maxFraction);
    static double getTimeTillWall(const Room& room, const Rocket& rock, double maxFraction);
    static double getTimeTillCollision(const PlayerBase& pl, const Rocket& rock, double collRadius);
    static double getTimeTillCollision(const PlayerBase& pl1, const PlayerBase& pl2, double collRadius);
    void limitPlayerSpeed(PlayerBase& pl) const;  // hard limit to somewhat acceptable values; required to call when physically incorrect changes are made
    void applyPlayerAcceleration(int pid);
    void executeBounce(PlayerBase& ply, const Coords& bounceVec, double plyRadius); // needs plyRadius as a shortcut to bounceVec's length
    std::pair<bool, bool> executeBounce(PlayerBase& pl1, PlayerBase& pl2, PhysicsCallbacksBase& callback) const; // returns pair(p1-dead, p2-dead)
    void applyPhysicsToRoom(const Room& room, std::vector<int>& rply, std::vector<int>& rrock, PhysicsCallbacksBase& callback, double plyRadius, double fraction);

    void print_team_stats_row(std::ostream& out, const std::string& header, int amount1, int amount2, const std::string& postfix = "") const;

protected:
    WorldBase(): player(MAX_PLAYERS) { }

public:
    void applyPhysics(PhysicsCallbacksBase& callback, double plyRadius, double fraction);
    void rocketFrameAdvance(int frames, PhysicsCallbacksBase& callback);

    void setMaxPlayers(int num) { maxplayers = num; }

    void remove_team_flags(int t);

    Map map;

    int maxplayers; // actual
    std::vector<PointerContainer<PlayerBase> > player;
    Team teams[2];

    std::vector<Flag> wild_flags;   // both teams can capture these (team ID is 2)

    Rocket rock[MAX_ROCKETS];
    Powerup item[MAX_PICKUPS];

    PhysicalSettings physics;

    virtual ~WorldBase() { }

    void shootRockets(PhysicsCallbacksBase& cb, int playernum, int pow, int dir, NLubyte* rids,
                                        int frameAdvance, int team, bool power, int px, int py, int x, int y);

    void run_server_player_physics(int pid);
    virtual bool load_map(LogSet& log, const std::string& mapdir, const std::string& mapname) { return map.load(log, mapdir, mapname); }
    virtual void returnAllFlags();
    virtual void returnFlag(int team, int flag);
    virtual void dropFlag(int team, int flag, int roomx, int roomy, int lx, int ly);
    virtual void stealFlag(int team, int flag, int carrier);

    void save_stats(const std::string& dir, const std::string& map_name) const;
};

class PowerupSettings {
    int pups_by_percent(int percentage, const Map& map) const;

public:
    int pups_min, pups_max, pups_respawn_time, pup_chance_shield, pup_chance_turbo, pup_chance_shadow,
        pup_chance_power, pup_chance_weapon, pup_chance_megahealth, pup_chance_deathbringer;
    bool pups_min_percentage, pups_max_percentage;
    int pup_add_time, pup_max_time;
    bool pup_deathbringer_switch;
    double pup_deathbringer_time;
    bool pups_drop_at_death;
    int pups_player_max;
    int pup_health_bonus;
    double pup_power_damage;
    int pup_weapon_max;
    bool pup_shield_one_hit;

    void reset();

    Powerup::Pup_type choose_powerup_kind() const;
    int getMinPups(const Map& map) const { return pups_min_percentage ? pups_by_percent(pups_min, map) : pups_min; }
    int getMaxPups(const Map& map) const { return pups_max_percentage ? pups_by_percent(pups_max, map) : pups_max; }
    int getRespawnTime() const { return pups_respawn_time; }
    bool getDeathbringerSwitch() const { return pup_deathbringer_switch; }
    double addTime(double t) const { t += pup_add_time; if (t > pup_max_time) t = pup_max_time; return t; }
};

class WorldSettings {
public:
    enum Team_balance { TB_disabled = 0, TB_balance, TB_balance_and_shuffle };

    double respawn_time, waiting_time_deathbringer, respawn_balancing_time;
    int shadow_minimum; // smallest alpha value allowed; 0 is when even the coordinates are not sent
    int rocket_damage;
    NLulong time_limit;
    NLulong extra_time;
    bool sudden_death;
    int capture_limit;
    int win_score_difference;     // minimum score difference needed to win the game
    double flag_return_delay; // in seconds
    Team_balance balance_teams;

    bool lock_team_flags;
    bool lock_wild_flags;
    bool capture_on_team_flag;
    bool capture_on_wild_flag;

    static const int shadow_minimum_normal;

    void reset();

    double getRespawnTime(int playerTeamSize, int enemyTeamSize) const;
    double getDeathbringerWaitingTime() const { return waiting_time_deathbringer; }
    int getShadowMinimum() const { return shadow_minimum; }
    int getCaptureLimit() const { return capture_limit; }
    int getWinScoreDifference() const { return win_score_difference; }
    NLulong getTimeLimit() const { return time_limit; }
    NLulong getExtraTime() const { return extra_time; }

    Team_balance balanceTeams() const { return balance_teams; }
    bool suddenDeath() const { return sudden_death; }
};

class ServerNetworking;
class Server;   //#fix: get rid of non-networking callbacks?

class ServerWorld : public WorldBase {
    Server* host;
    ServerNetworking* net;
    PowerupSettings pupConfig;
    WorldSettings config;
    LogSet log;

    NLubyte getFreeRocket();    // may give an existing rocket to overwrite if the table is full
    void drop_pickup(const ServerPlayer& player);
    void drop_worst_powerup(ServerPlayer& player);

    void player_steals_flag(int pid, int team, int flag);
    void player_captures_flag(int pid, int team, int flag);

    bool lock_team_flags_in_effect() const;
    bool lock_wild_flags_in_effect() const;
    bool capture_on_team_flags_in_effect() const;
    bool capture_on_wild_flags_in_effect() const;

    bool all_kind_of_flags_exist() const;

public:
    NLulong frame;
    NLulong map_start_time; // frame #
    ServerPlayer player[MAX_PLAYERS];

    ServerWorld(Server* hostp, ServerNetworking* netp, LogSet logset) :
                    host(hostp), net(netp), log(logset), frame(0), map_start_time(0) {
        for (int i = 0; i < MAX_PLAYERS; ++i)
            WorldBase::player[i].setPtr(&player[i]);
    }

    void setConfig(const WorldSettings& ws, const PowerupSettings& ps) { config = ws; pupConfig = ps; }

    const WorldSettings& getConfig() const { return config; }
    const PowerupSettings& getPupConfig() const { return pupConfig; }

    // common (virtual in base) extended functions
    bool load_map(const std::string& mapdir, const std::string& mapname);
    void returnAllFlags();
    void returnFlag(int team, int flag);
    void dropFlag(int team, int flag, int roomx, int roomy, int lx, int ly);
    void stealFlag(int team, int flag, int carrier);
    int getMapTime() const { return frame - map_start_time; }
    bool isTimeLimit() const { return config.getTimeLimit() > 0; }
    int getTimeLeft() const { return config.getTimeLimit() - getMapTime(); }
    int getExtraTimeLeft() const { return config.getTimeLimit() + config.getExtraTime() - getMapTime(); }

    // server specific functions
    void reset();
    void reset_time() { map_start_time = frame; }
    void respawnPlayer(int pid, bool dontInformClients = false);
    void printTimeStatus(LineReceiver& printer);

    void resetPlayer(int target, double time_penalty = 0.); // take the player out of the game; the clients must be informed and this function doesn't do that
    void killPlayer(int target, bool time_penalty); // kill the player in the usual way with score penalties and deathbringer effect; the clients must be informed and this function doesn't do that
    void damagePlayer(int target, int attacker, int damage, DamageType type);
    void removePlayer(int pid);
    void suicide(int pid);
    void respawn_pickup(int p);
    void check_pickup_creation(bool instant);
    void game_touch_pickup(int p, int pk);
    bool check_flag_touch(const Flag& flag, int px, int py, int x, int y);
    void game_player_screen_change(int p);

    bool dropFlagIfAny(int pid, bool purpose = false);
    void shootRockets(int pid, int numshots);
    void deleteRocket(int r, NLshort hitx, NLshort hity, int targ);
    void changeRocketsOwner(int source, int target);
    void swapRocketOwners(int a, int b);

    void simulateFrame();

    void addMovementDistanceCallback(int pid, double dist);
    void playerScreenChangeCallback(int pid);
    void rocketHitWallCallback(int rid);
    bool rocketHitPlayerCallback(int rid, int pid);
    PhysicsCallbacksBase::PlayerHitResult playerHitPlayerCallback(int pid1, int pid2, double speed);
    void rocketOutOfBoundsCallback(int rid);
    bool shouldApplyPhysicsToPlayerCallback(int pid);
};

class ClientWorld : public WorldBase {
public:
    bool skipped;   // frame is invalid -- when frame is skipped in the broadcast
    double frame;

    std::vector<ClientPlayer> player;

    ClientWorld() : skipped(true), player(MAX_PLAYERS) {
        for (int i = 0; i < MAX_PLAYERS; ++i)
            WorldBase::player[i].setPtr(&player[i]);
    }
    // extrapolate : advances from source, a frame per every ctrl listed except the last one which gets subFrameAfter, controls are for player me
    void extrapolate(ClientWorld& source, PhysicsCallbacksBase& physCallbacks, int me,
                     ClientControls* ctrlTab, NLubyte ctrlFirst, NLubyte ctrlLast, double subFrameAfter);

    /*void save_stats(const std::string& dir, const Team* teams,
                const std::vector<ClientPlayer*>& players, const std::string& map_name) const;*/
};

#endif
