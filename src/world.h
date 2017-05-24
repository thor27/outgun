/*
 *  world.h
 *
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004, 2005, 2008 - Niko Ritari
 *  Copyright (C) 2003, 2004, 2005, 2006, 2008 - Jani Rivinoja
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

#include "incalleg.h"

#include <vector>
#include <list>
#include <string>
#include <algorithm>

#include "commont.h"
#include "nassert.h"
#include "utility.h"

class BinaryReader;
class BinaryWriter;

typedef std::pair<double, double> Coords;
typedef std::pair<double, Coords> BounceData;

class WallBase {    // base class
public:
    WallBase() throw () { }
    WallBase(int tex_, int alpha_) throw () : tex(tex_), alpha(alpha_) { }
    virtual ~WallBase() throw () { }
    virtual bool intersects_rect(double x1, double y1, double x2, double y2) const throw () = 0;
    virtual bool intersects_circ(double x, double y, double r) const throw () = 0;
    virtual void tryBounce(BounceData* bd, double stx, double sty, double mx, double my, double plyRadius) const throw () = 0;
    int texture() const throw () { return tex; }

private:
    int tex, alpha;
};

class RectWall : public WallBase {  // rectangular wall
public:
    RectWall() throw () { }
    RectWall(double a_, double b_, double c_, double d_, int tex_, int alpha_) throw ()
            : WallBase(tex_, alpha_), a(a_), b(b_), c(c_), d(d_) { if (c<a) std::swap(a, c); if (d<b) std::swap(b, d); }

    double x1() const throw () { return a; }
    double y1() const throw () { return b; }
    double x2() const throw () { return c; }
    double y2() const throw () { return d; }

    bool intersects_rect(double x1, double y1, double x2, double y2) const throw () { return x1<=c && x2>=a && y1<=d && y2>=b; } // perfect
    bool intersects_circ(double x, double y, double r) const throw ();   // perfect
    void tryBounce(BounceData* bd, double stx, double sty, double mx, double my, double plyRadius) const throw ();

private:
    double a, b, c, d;  // rectangle coords (a,b)->(c,d)
};

class TriWall : public WallBase {   // triangular wall
public:
    TriWall() throw () { }
    TriWall(double x1, double y1, double x2, double y2, double x3, double y3, int tex_, int alpha_) throw ();

    double x1() const throw () { return p1x; }
    double y1() const throw () { return p1y; }
    double x2() const throw () { return p2x; }
    double y2() const throw () { return p2y; }
    double x3() const throw () { return p3x; }
    double y3() const throw () { return p3y; }

    bool intersects_rect(double rx1, double ry1, double rx2, double ry2) const throw (); // perfect
    bool intersects_circ(double x, double y, double r) const throw ();                   // perfect
    void tryBounce(BounceData* bd, double stx, double sty, double mx, double my, double plyRadius) const throw ();

private:
    double p1x, p1y, p2x, p2y, p3x, p3y;
    double boundx1, boundy1, boundx2, boundy2;
};

class CircWall : public WallBase {  // circular wall
public:
    CircWall() throw () { }
    CircWall(double x_, double y_, double ro_, double ri_, double ang1, double ang2, int tex_, int alpha_) throw ();

    double X() const throw () { return x; }
    double Y() const throw () { return y; }
    double radius() const throw () { return ro; }
    double radius_in() const throw () { return ri; }
    const double* angles() const throw () { return angle; }
    const Coords& angle_vector_1() const throw () { return va1; }
    const Coords& angle_vector_2() const throw () { return va2; }

    bool intersects_rect(double x1, double y1, double x2, double y2) const throw (); // very much imperfect (uses bounding circle)
    bool intersects_circ(double rcx, double rcy, double rr) const throw ();  // imperfect
    void tryBounce(BounceData* bd, double stx, double sty, double mx, double my, double plyRadius) const throw ();

private:
    double x, y, ro, ri;
    double angle[2];
    Coords va1, va2, midvec;
    double anglecos;
};

enum RouteTable {
    Table_Main = 0,
    Table_Def = 1,
    Table_Max = 2
};

class Room {
public:
    // for bots:
    bool pass[4];
    int label[Table_Max];
    bool route[Table_Max];
    double visited_frame;

    Room() throw () { }
    Room(const Room& room) throw ();
    ~Room() throw ();

    void addWall(WallBase* w) throw () { walls.push_back(w); }
    void addGround(WallBase* w) throw () { ground.push_back(w); }

    bool fall_on_wall(double x1, double y1, double x2, double y2) const throw ();    // this check follows the quality of *Wall::intersects_rect and isn't perfect
    bool fall_on_wall(double x, double y, double r) const throw ();   // this check follows the quality of *Wall::intersects_circ and isn't perfect
    BounceData genGetTimeTillWall(double x, double y, double mx, double my, double radius, double maxFraction) const throw ();

    const std::vector<WallBase*>& readWalls() const throw () { return walls; }
    const std::vector<WallBase*>& readGround() const throw () { return ground; }

    Room& operator=(const Room& op) throw ();

private:
    std::vector<WallBase*> walls, ground;   // ground: optional list of textures for ground
};

//entity locale
struct WorldCoords {
    WorldCoords(int px_, int py_, double x_, double y_) throw () : px(px_), py(py_), x(x_), y(y_) { }
    WorldCoords() throw () : px(-1), py(-1) { }

    bool unknown() const throw () { return px == -1 && py == -1; }

    bool operator==(const WorldCoords& op) const throw () { return px == op.px && py == op.py && x == op.x && y == op.y; }
    bool operator!=(const WorldCoords& op) const throw () { return !(*this == op); }

    int px, py; // room
    double x, y; // coords within the room
};

struct WorldRect {
    WorldRect(int px_, int py_, double x1_, double y1_, double x2_, double y2_) throw () : px(px_), py(py_), x1(x1_), y1(y1_), x2(x2_), y2(y2_) { }
    WorldRect() throw () { }

    int px, py; // room
    double x1, y1, x2, y2;
};

//team info
struct MapTeam {
    std::vector<WorldCoords> flags; //flag positions
    std::vector<WorldCoords> spawn; //team spawn points
    std::vector<WorldRect> respawn; //team respawn areas
    unsigned int lastspawn;         //last team spawn point used

    MapTeam() throw () : lastspawn(0) { }
};

class Map {
    bool parse_line(LogSet& log, const std::string& line, const std::vector<std::pair<std::string, std::vector<std::string> > >& label_lines,
                    int& crx, int& cry, double& scalex, double& scaley, bool label_block = false) throw ();

public:
    MapTeam tinfo[2];   //team information for red=0 and blue=1 teams
    std::vector<WorldCoords> wild_flags;
    std::vector< std::vector<Room> > room;  // accessed by [x][y]

    std::string title;  //map title
    std::string author;
    int w, h;   // width height
    uint16_t crc;   //map's 16bit CRC

    Map() throw () : w(0), h(0), crc(0) { }

    bool fall_on_wall(int px, int py, double x1, double y1, double x2, double y2) const throw () {
        //if (px<0 || py<0 || px>=w || py>=h) return false;   //#fix: remove this and track why these are given sometimes
        nAssert(px>=0 && py>=0 && px<w && py<h);
        return room[px][py].fall_on_wall(x1, y1, x2, y2);
    }
    bool fall_on_wall(int px, int py, double x, double y, double r) const throw () {
        //if (px<0 || py<0 || px>=w || py>=h) return false;   //#fix: remove this and track why these are given sometimes
        nAssert(px>=0 && py>=0 && px<w && py<h);
        return room[px][py].fall_on_wall(x, y, r);
    }
    bool load(LogSet& log, const std::string& mapdir, const std::string& mapname, std::string* buffer = 0) throw ();
    bool parse_file(LogSet& log, std::istream& in) throw ();
};

class MapInfo {
public:
    std::string title, author, file;
    int width, height;
    bool random;
    float over_edge;
    int votes, sentVotes;
    uint32_t last_game;  // last game in the map (frame #)
    bool highlight;     // for the map list in the client

    MapInfo() throw ();
    bool load(LogSet& log, const std::string& mapName) throw ();
    void update(const Map& map) throw ();
    bool operator<(const MapInfo& o) const throw () { return cmp_case_ins(title, o.title); }
};

class Statistics {
public:
    Statistics() throw ();

    void clear(bool preserveTime) throw ();

    void set_frags(int n) throw () { total_frags = n; }
    void set_kills(int n) throw () { total_kills = n; }
    void set_deaths(int n) throw () { total_deaths = n; }
    void set_cons_kills(int n) throw () { most_consecutive_kills = n; }
    void set_current_cons_kills(int n) throw () { current_consecutive_kills = n; }
    void set_cons_deaths(int n) throw () { most_consecutive_deaths = n; }
    void set_current_cons_deaths(int n) throw () { current_consecutive_deaths = n; }
    void set_suicides(int n) throw () { total_suicides = n; }
    void set_captures(int n) throw () { total_captures = n; }
    void set_flags_taken(int n) throw () { total_flags_taken = n; }
    void set_flags_dropped(int n) throw () { total_flags_dropped = n; }
    void set_flags_returned(int n) throw () { total_flags_returned = n; }
    void set_carriers_killed(int n) throw () { total_flag_carriers_killed = n; }
    void set_shots(int n) throw () { total_shots = n; }
    void set_hits(int n) throw () { total_hits = n; }
    void set_shots_taken(int n) throw () { total_shots_taken = n; }
    void set_movement(double amount) throw () { total_movement = amount; }
    void set_spawn_time(double time) throw () { last_spawn_time = time; }
    void set_start_time(double time) throw () { starttime = time; }
    void set_lifetime(double time) throw () { total_lifetime = time; }
    void set_flag_carrying_time(double time) throw () { total_flag_carrying_time = time; }
    void set_flag_take_time(double time) throw () { flag_taking_time = time; }
    void set_flag(bool f, bool wild) throw () { nAssert(!wild || f); flag = f; wild_flag = wild; }
    void set_dead(bool d) throw () { dead = d; }

    void spawn(double time) throw () { set_spawn_time(time); nAssert(dead); dead = false; }
    void kill(double time, bool allowAlreadyDead = false) throw ();

    void add_frag(int n = 1) throw () { total_frags += n; }
    void add_kill(bool deathbringer) throw ();
    void add_death(bool deathbringer, double time) throw ();
    void add_suicide(double time) throw ();
    void add_capture(double time) throw ();
    void add_flag_take(double time, bool wild) throw ();
    void add_flag_drop(double time) throw ();
    void add_flag_return() throw () { ++total_flags_returned; }
    void add_carrier_kill() throw () { ++total_flag_carriers_killed; }
    void add_shot() throw () { ++total_shots; }
    void add_hit() throw () { ++total_hits; }
    void add_shot_take() throw () { ++total_shots_taken; }
    void add_movement(double amount) throw () { total_movement += amount; }

    void finish_stats(double time) throw ();

    void save_speed(double time) throw () { saved_speed = speed(time); }

    int frags() const throw () { return total_frags; }
    int kills() const throw () { return total_kills; }
    int deaths() const throw () { return total_deaths; }
    int cons_kills() const throw () { return most_consecutive_kills; }
    int current_cons_kills() const throw () { return current_consecutive_kills; }
    int cons_deaths() const throw () { return most_consecutive_deaths; }
    int current_cons_deaths() const throw () { return current_consecutive_deaths; }
    int suicides() const throw () { return total_suicides; }
    int captures() const throw () { return total_captures; }
    int flags_taken() const throw () { return total_flags_taken; }
    int flags_dropped() const throw () { return total_flags_dropped; }
    int flags_returned() const throw () { return total_flags_returned; }
    int carriers_killed() const throw () { return total_flag_carriers_killed; }
    int shots() const throw () { return total_shots; }
    int hits() const throw () { return total_hits; }
    double accuracy() const throw ();
    int shots_taken() const throw () { return total_shots_taken; }
    double spawn_time() const throw () { return last_spawn_time; }
    double lifetime(double time) const throw ();         // in seconds
    double average_lifetime(double time) const throw (); // in seconds
    double playtime(double time) const throw ();         // in seconds
    double movement() const throw ();                    // in Outgun units
    double speed(double time) const throw ();            // in Outgun units per second
    double old_speed() const throw () { return saved_speed; }
    double start_time() const throw () { return starttime; }
    double flag_carrying_time(double time) const throw ();
    double flag_take_time() const throw () { return flag_taking_time; }
    bool has_flag() const throw () { return flag; }  // true for both enemy flag and wild flag
    bool has_wild_flag() const throw () { return wild_flag; }

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

class GunDirection {
    double data;

    GunDirection(double data_) throw () : data(data_) { }

    void normalize() throw () { data = positiveFmod(data, 8.); }

public:
    GunDirection() throw () : data(-1) { }

    GunDirection& adjust(double change) throw () { data += change; normalize(); return *this; }

    GunDirection& from8way(int dir) throw () { data = dir; return *this; }
    GunDirection& fromControls(const ClientControls& c) throw () { data = c.getDirection(); return *this; }
    GunDirection& updateFromControls(const ClientControls& c) throw () { const int d = c.getDirection(); if (d != -1) data = d; return *this; }
    GunDirection& fromRad(double r) throw () { data = r / N_PI_4; normalize(); return *this; }

    GunDirection& fromNetworkShortForm(uint8_t data_) throw () { data = data_ & 7; return *this; }
    GunDirection& fromNetworkLongForm(uint16_t data_) throw () { data = data_ / 256.; return *this; } // only 11 bits used

    uint8_t toNetworkShortForm() const throw () { return to8way(); }
    uint16_t toNetworkLongForm() const throw () { nAssert(data >= 0 && data <= 8); return iround(data * 256.) % (256 * 8); } // only 11 bits used

    int to8way() const throw () { nAssert(data >= 0 && data <= 8); return iround(data) % 8; }
    #ifndef DEDICATED_SERVER_ONLY
    fixed toFixed() const throw () { nAssert(data >= 0 && data <= 8); return ftofix(data * 32.); }
    #endif
    double toRad() const throw () { nAssert(data >= 0 && data <= 8); return data * N_PI_4; }

    bool operator!() const throw () { return data < 0; }
};

class PlayerBase {
protected:
    PlayerBase() throw () { clear(false, 0, "", 0); }

    int team_nr;
    int personal_color;
    Statistics player_stats;

public:
    static const int invalid_color = MAX_PLAYERS / 2;

    bool item_deathbringer;
    int item_shield;    // how many hits the shield can still take, 0 = no shield
    bool item_power;
    bool item_turbo;
    int visibility;     // alpha

    int roomx, roomy;
    double lx, ly, sx, sy;  // position within room and speed
    ClientControls controls;
    AccelerationMode accelerationMode;

    GunDirection gundir;

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

    virtual ~PlayerBase() throw () { }
    void move(double fraction) throw () { lx += sx * fraction; ly += sy * fraction; }
    void clear(bool enable, int _pid, const std::string& _name, int team_id) throw ();

    void set_team(int t) throw () { team_nr = t; }
    void set_color(int c) throw () { personal_color = c; }

    const Statistics& stats() const throw () { return player_stats; }
    Statistics& stats() throw () { return player_stats; }

    bool item_shadow() const throw () { return visibility < 255; }
    int team() const throw () { return team_nr; }
    int color() const throw () { return personal_color; }
    virtual bool under_deathbringer_effect(double curr_time) const throw () = 0;
};

bool compare_players(const PlayerBase* a, const PlayerBase* b) throw ();

class ServerPlayer : public PlayerBase {
public:
    double health;
    double energy;

    int weapon;
    bool attack;     // if player is holding the attack button
    bool attackOnce; // if player has pressed (maybe also released) the attack button after the last frame
    GunDirection attackGunDir; // the gun direction when player last pressed the attack button

    double item_power_time;
    double item_turbo_time;
    double item_shadow_time;

    double deathbringer_end;    // end of effect of another players deathbringer
    int deathbringer_attacker;  // whose deathbringer it is

    int awaiting_client_readies;
    bool want_map_exit;

    size_t current_map_list_item;
    int nextMinimapPlayer, minimapPlayersPerFrame;

    int mapVote;
    int idleFrames;
    int kickTimer;
    int muted;  // 0 = no, 1 = yes, 2 = silently

    bool want_change_teams;
    double team_change_time;

    int cid;    // client id (network identity)
    uint8_t lastClientFrame;    // client set frame identifier of the latest data received
    double frameOffset; // at what time within the frame the client's packet arrived
    double waitnametime;
    bool localIP;
    int oldfrags;   // last value informed to clients
    int megabonus;

    int protocolExtensionsLevel;
    bool toldAboutExtensionAdvantage;

    bool drop_key;
    bool dropped_flag;
    uint32_t next_shoot_frame, start_take_damage_frame;
    int frames_to_respawn, extra_frames_to_respawn;
    bool respawn_to_base;

    double talk_temp;
    double talk_hotness;

    bool record_position;

    unsigned uniqueId;

    ServerPlayer() throw () { clear(false, 0, 0, "", 0, 0); }
    ~ServerPlayer() throw () { }

    bool under_deathbringer_effect(double curr_time) const throw () { return deathbringer_end >= curr_time; }

    void clear(bool enable, int _pid, int _cid, const std::string& _name, int team_id, unsigned uniqueId_) throw ();

    void set_fav_colors(const std::vector<char>& colors) throw () { fav_col = colors; }
    const std::vector<char>& fav_colors() const throw () { return fav_col; }

    void set_bot() throw () { bot = true; }
    bool is_bot() const throw () { return bot; }

private:
    std::vector<char> fav_col;
    bool bot;
};

class ClientPlayer : public PlayerBase {
public:
    ClientPlayer() throw () { clear(false, 0, "", 0); }

    bool deathbringer_affected;
    double next_smoke_effect_time;
    double next_turbo_effect_time;
    double wall_sound_time;
    double player_sound_time;
    bool onscreen;
    double hitfx;
    int oldx, oldy; // detect room changes
    double posUpdated; // on which frame the player's position has been last received (including the low-res info for minimap purposes)
    int alpha;

    // get rid of these since they are only known for the local player
    double item_power_time;
    double item_turbo_time;
    double item_shadow_time;
    int health;
    int energy;
    int weapon;

    bool under_deathbringer_effect(double curr_time) const throw () { (void)curr_time; return deathbringer_affected; }

    void clear(bool enable, int _pid, const std::string& _name, int team_id) throw ();
};

// a rocket-shot
class Rocket {
public:
    int owner;  //owning player-id (-1 == unused)
    int team;
    bool power;

    uint32_t vislist;    //notification list (bitfield, bit0=player0, bit1=player1... etc.)
    int px, py;         //screen coords
    double x, y;        //start position or current position
    double sx, sy;      //speed
    GunDirection direction;
    uint32_t time;       //time of shot or current time

    Rocket() throw () { owner = -1; }
    void move(double fraction) throw () { x += sx*fraction; y += sy*fraction; }
};

class Flag {
public:
    Flag(const WorldCoords& pos_) throw ();

    void take(int carr) throw ();
    void take(int carr, double time) throw ();
    void return_to_base() throw ();
    void drop() throw ();
    void move(const WorldCoords& new_pos) throw () { pos = new_pos; }

    void set_drop_time(double time) throw () { drop_t = time; }
    void set_return_time(double time) throw () { return_t = time; }

    void reset_carrying_time() throw () { ctime = 0; }
    void add_carrying_time(int team) throw ();

    bool carried() const throw () { return status == status_carried; }
    bool at_base() const throw () { return status == status_at_base; }

    int carrier() const throw () { return carrier_id; }
    double grab_time() const throw () { return grab_t; }
    double drop_time() const throw () { return drop_t; }
    double return_time() const throw () { return return_t; }

    const WorldCoords& position() const throw () { return pos; }
    //const WorldCoords& home_position() const throw () { return home_pos; }

    int carrying_team() const throw () { return cteam; }
    int carrying_time() const throw () { return ctime; }

private:
    enum Status { status_at_base, status_carried, status_dropped };

    Status status;
    int carrier_id;
    double grab_t;
    double drop_t;
    double return_t;    // for client only
    WorldCoords home_pos;
    WorldCoords pos;
    // wild flag carrying info for server
    int cteam;          // team that last time carried the flag, -1 if not carried yet
    int ctime;          // in frames
};

class Team {
public:
    Team() throw ();

    void clear_stats() throw ();

    void set_score(int n) throw () { points = n; }
    void set_kills(int n) throw () { total_kills = n; }
    void set_deaths(int n) throw () { total_deaths = n; }
    void set_suicides(int n) throw () { total_suicides = n; }
    void set_flags_taken(int n) throw () { total_flags_taken = n; }
    void set_flags_dropped(int n) throw () { total_flags_dropped = n; }
    void set_flags_returned(int n) throw () { total_flags_returned = n; }
    void set_shots(int n) throw () { total_shots = n; }
    void set_hits(int n) throw () { total_hits = n; }
    void set_shots_taken(int n) throw () { total_shots_taken = n; }
    void set_base_score(int n) throw () { start_score = n; }
    void set_movement(double amount) throw () { total_movement = amount; }
    void set_power(double pow) throw () { tournament_power = pow; }

    void add_point() throw () { ++points; }
    void add_score(double time, const std::string& player) throw ();
    void add_kill() throw () { ++total_kills; }
    void add_death() throw () { ++total_deaths; }
    void add_suicide() throw () { ++total_suicides; ++total_deaths; }
    void add_flag_take() throw () { ++total_flags_taken; }
    void add_flag_drop() throw () { ++total_flags_dropped; }
    void add_flag_return() throw () { ++total_flags_returned; }
    void add_shot() throw () { ++total_shots; }
    void add_hit() throw () { ++total_hits; }
    void add_shot_take() throw () { ++total_shots_taken; }
    void add_movement(double amount) throw () { total_movement += amount; }

    void add_flag(const WorldCoords& pos) throw ();
    void remove_flags() throw ();

    void steal_flag(int n, int carrier) throw ();
    void steal_flag(int n, int carrier, double time) throw ();

    void return_all_flags() throw ();
    void return_flag(int n) throw ();
    void drop_flag(int n, const WorldCoords& pos) throw ();
    void move_flag(int n, const WorldCoords& pos) throw ();

    void set_flag_drop_time(int n, double time) throw ();
    void set_flag_return_time(int n, double time) throw ();

    int score() const throw () { return points; }
    int kills() const throw () { return total_kills; }
    int deaths() const throw () { return total_deaths; }
    int suicides() const throw () { return total_suicides; }
    int flags_taken() const throw () { return total_flags_taken; }
    int flags_dropped() const throw () { return total_flags_dropped; }
    int flags_returned() const throw () { return total_flags_returned; }
    int shots() const throw () { return total_shots; }
    int hits() const throw () { return total_hits; }
    int shots_taken() const throw () { return total_shots_taken; }
    double movement() const throw () { return total_movement; }
    double accuracy() const throw ();
    double power() const throw () { return tournament_power; }

    const Flag& flag(int n) const throw () { return team_flags[n]; }
    const std::vector<Flag>& flags() const throw () { return team_flags; }

    const std::vector<std::pair<int, std::string> >& captures() const throw () { return caps; }
    int base_score() const throw () { return start_score; }

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

    Pup_type kind;

    double respawn_time;

    int px; //screen
    int py;
    int x;  //position
    int y;

    Powerup() throw () : kind(pup_unused) { }
};

class DeathbringerExplosion {
    double frame0;
    WorldCoords pos;
    int ownerPid;
    int ownerTeam; // team != world.player[owner].team() exactly if owner changed team after the explosion

public:
    DeathbringerExplosion(double explosionFrame, const PlayerBase& owner) throw ()
            : frame0(explosionFrame), pos(owner.roomx, owner.roomy, owner.lx, owner.ly), ownerPid(owner.id), ownerTeam(owner.team()), playersOutsideMask(~0u) { }
    DeathbringerExplosion(double explosionFrame, const WorldCoords& position, int team) throw ()
            : frame0(explosionFrame), pos(position), ownerPid(-1), ownerTeam(team), playersOutsideMask(~0u) { }

    void pidChange(int newPid) throw () { nAssert(newPid >= 0); ownerPid = newPid; }

    bool expired(double frame) const throw () { return frame > frame0 + 18.; } // so that radius(frame)² > plw² + plh²
    const WorldCoords& position() const throw () { return pos; }
    double radius(double frame) const throw ();
    int team() const throw () { return ownerTeam; }
    int player() const throw () { nAssert(ownerPid != -1); return ownerPid; } // can only be used if initialized with the player

    uint32_t playersOutsideMask; // bit set for every player that was on the previous frame in the same room but outside the db ring, kind of waiting to be hit (only those can be hit on this frame); additionally, every player when the deathbringer is new (even if they happen to be in another room, it doesn't matter in the calculations)
};

template<class Type> class PointerAsReference {   // doesn't delete the objects!
    Type* ptr;

public:
    PointerAsReference() throw () : ptr(0) { }
    PointerAsReference(Type* p) throw () : ptr(p) { }

    void setPtr(Type* p) throw () { ptr = p; }

          Type* getPtr()       throw () { return ptr; }
    const Type* getPtr() const throw () { return ptr; }

    operator       Type&()       throw () { nAssert(ptr); return *ptr; }
    operator const Type&() const throw () { nAssert(ptr); return *ptr; }
};

class PhysicalSettings {
public:
    double fric, drag, accel;
    double brake_mul, turn_mul, run_mul, turbo_mul, flag_mul;
    double rocket_speed;
    double friendly_fire, friendly_db;
    enum PlayerCollisions { PC_none, PC_normal, PC_special } player_collisions;
    bool allowFreeTurning;

    double max_run_speed;   // max speed without turbo, for turbo effect in client

    PhysicalSettings() throw ();
    void calc_max_run_speed() throw ();
    void read(BinaryReader& reader) throw ();
    void write(BinaryWriter& writer) const throw ();
};

class PhysicsCallbacksBase {
public:
    struct PlayerHitResult {
        std::pair<bool, bool> deaths;
        double bounceStrength1, bounceStrength2;
        PlayerHitResult(bool dead1, bool dead2, double s1, double s2) throw () : deaths(std::pair<bool, bool>(dead1, dead2)), bounceStrength1(s1), bounceStrength2(s2) { }
    };

    virtual ~PhysicsCallbacksBase() throw () { }
    virtual bool collideToRockets() const throw () = 0; // should player to rocket collisions be checked at all
    virtual bool collidesToRockets(int pid) const throw () = 0; // should player to rocket collisions be checked for player pid (if collideToRockets())
    virtual bool collidesToPlayers(int pid) const throw () = 0; // should player to player collisions be checked for player pid (with other players who collideToPlayers)
    virtual bool gatherMovementDistance() const throw () = 0; // should addMovementDistance be called with player movements
    virtual bool allowRoomChange() const throw () = 0;
    virtual void addMovementDistance(int pid, double dist) throw () = 0; // player pid has moved the distance dist
    virtual void playerScreenChange(int pid) throw () = 0; // player pid has moved to a new room (called max. once per frame per player)
    virtual void rocketHitWall(int rid, bool power, double x, double y, int roomx, int roomy) throw () = 0; // caller doesn't remove the rocket
    virtual bool rocketHitPlayer(int rid, int pid) throw () = 0; // returns true if player dies (to be removed from further simulation)
    virtual void playerHitWall(int pid) throw () = 0;
    virtual PlayerHitResult playerHitPlayer(int pid1, int pid2, double speed) throw () = 0;
    virtual void rocketOutOfBounds(int rid) throw () = 0; // caller doesn't remove the rocket
    virtual bool shouldApplyPhysicsToPlayer(int pid) throw () = 0; // returns true if physics should be run to player pid
};

class WorldBase {
public:
    static const int shot_deltax = PLAYER_RADIUS + ROCKET_RADIUS - 2;

private:
    void addRocket(int i, int playernum, int team, int px, int py, int x, int y,
                   bool power, GunDirection dir, int xdelta, int frameAdvance, PhysicsCallbacksBase& cb) throw ();

public: //#fix: needed by bots; make accessible through some more sophisticated methods?
    static BounceData getTimeTillBounce(const Room& room, const PlayerBase& pl, double plyRadius, double maxFraction) throw ();
    static double getTimeTillWall(const Room& room, const Rocket& rock, double maxFraction) throw ();
    static double getTimeTillCollision(const PlayerBase& pl, const Rocket& rock, double collRadius) throw ();
private:
    static double getTimeTillCollision(const PlayerBase& pl1, const PlayerBase& pl2, double collRadius) throw ();
    void limitPlayerSpeed(PlayerBase& pl) const throw ();  // hard limit to somewhat acceptable values; required to call when physically incorrect changes are made
    void applyPlayerAcceleration(int pid) throw ();
    void executeBounce(PlayerBase& ply, const Coords& bounceVec, double plyRadius) throw (); // needs plyRadius as a shortcut to bounceVec's length
    std::pair<bool, bool> executeBounce(PlayerBase& pl1, PlayerBase& pl2, PhysicsCallbacksBase& callback) const throw (); // returns pair(p1-dead, p2-dead)
    void applyPhysicsToRoom(const Room& room, std::vector<int>& rply, std::vector<int>& rrock, PhysicsCallbacksBase& callback, double plyRadius, double fraction) throw ();
    void applyPhysicsToPlayerInIsolation(PlayerBase& pl, double plyRadius, double fraction) throw ();

    void print_team_stats_row(std::ostream& out, const std::string& header, int amount1, int amount2, const std::string& postfix = "") const throw ();

protected:
    WorldBase() throw () : player(MAX_PLAYERS) { }

public:
    virtual void reset() throw ();

    void applyPhysics(PhysicsCallbacksBase& callback, double plyRadius, double fraction) throw ();
    void rocketFrameAdvance(int frames, PhysicsCallbacksBase& callback) throw ();

    void setMaxPlayers(int num) throw () { maxplayers = num; }

    void remove_team_flags(int t) throw ();
    void add_random_flag(int t) throw ();

    virtual double get_frame() const throw () = 0;

    Map map;

    int maxplayers; // actual
    std::vector<PointerAsReference<PlayerBase> > player;
    Team teams[2];

    std::vector<Flag> wild_flags;   // both teams can capture these (team ID is 2)

    Rocket rock[MAX_ROCKETS];
    Powerup item[MAX_POWERUPS];

    PhysicalSettings physics;

    virtual ~WorldBase() throw () { }

    void shootRockets(PhysicsCallbacksBase& cb, int playernum, int pow, GunDirection dir, const uint8_t* rids,
                      int frameAdvance, int team, bool power, int px, int py, int x, int y) throw ();

    void run_server_player_physics(int pid) throw ();
    virtual bool load_map(LogSet& log, const std::string& mapdir, const std::string& mapname, std::string* buffer = 0) throw () { return map.load(log, mapdir, mapname, buffer); }
    virtual void returnAllFlags() throw ();
    virtual void returnFlag(int team, int flag) throw ();
    virtual void dropFlag(int team, int flag, int roomx, int roomy, double lx, double ly) throw ();
    virtual void stealFlag(int team, int flag, int carrier) throw ();

    void save_stats(const std::string& dir, const std::string& map_name) const throw ();

    void addDeathbringerExplosion(const DeathbringerExplosion& db) throw () { dbExplosions.push_back(db); }
    void cleanOldDeathbringerExplosions() throw ();
    const std::list<DeathbringerExplosion>& deathbringerExplosions() const throw () { return dbExplosions; }

protected:
    std::list<DeathbringerExplosion> dbExplosions;
};

class ConstFlagIterator {
    const WorldBase& w;
    unsigned iTeam, iFlag;
    const std::vector<Flag>* flags;

    void setFlags() throw () { flags = iTeam == 2 ? &w.wild_flags : &w.teams[iTeam].flags(); }
    void findValid() throw ();

protected:
    bool valid() const throw () { return iTeam < 3; }
    void next() throw () { ++iFlag; findValid(); }
    const Flag& flag() const throw () { nAssert(iTeam < 3 && iFlag < flags->size()); return (*flags)[iFlag]; }

public:
    ConstFlagIterator(const WorldBase& world) throw () : w(world), iTeam(0), iFlag(0) { setFlags(); findValid(); }
    virtual ~ConstFlagIterator() throw () { }

    bool operator!() const throw () { return !valid(); }
    operator bool() const throw () { return valid(); }
    virtual ConstFlagIterator& operator++() throw () { next(); return *this; }

    int team() const throw () { return iTeam; }
    const Flag& operator*() const throw () { return flag(); }
    const Flag* operator->() const throw () { return &flag(); }
};

class PowerupSettings {
    int pups_by_percent(int percentage, const Map& map) const throw ();

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
    int pup_shield_hits;
    int deathbringer_health_limit, deathbringer_energy_limit;
    double deathbringer_health_degradation, deathbringer_energy_degradation;

    bool start_shield;
    int start_turbo;
    int start_shadow;
    int start_power;
    int start_weapon;
    bool start_deathbringer;

    void reset() throw ();

    Powerup::Pup_type choose_powerup_kind() const throw ();
    int getMinPups(const Map& map) const throw () { return pups_min_percentage ? pups_by_percent(pups_min, map) : pups_min; }
    int getMaxPups(const Map& map) const throw () { return pups_max_percentage ? pups_by_percent(pups_max, map) : pups_max; }
    int getRespawnTime() const throw () { return pups_respawn_time; }
    bool getDeathbringerSwitch() const throw () { return pup_deathbringer_switch; }
    double addTime(double t) const throw () { t += pup_add_time; if (t > pup_max_time) t = pup_max_time; return t; }
};

class WorldSettings {
public:
    enum Team_balance { TB_disabled = 0, TB_balance, TB_balance_and_shuffle };

    double respawn_time, extra_respawn_time_alone, waiting_time_deathbringer, respawn_balancing_time;
    bool respawn_on_capture;
    int shadow_minimum; // smallest alpha value allowed; 0 is when even the coordinates are not sent
    int rocket_damage;
    int start_health, start_energy;
    int min_health_for_run_penalty;
    int health_max, energy_max;
    double health_regeneration_0_to_100, energy_regeneration_0_to_100,
           health_regeneration_100_to_200, energy_regeneration_100_to_200,
           health_regeneration_200_to_max, energy_regeneration_200_to_max;
    double run_health_degradation, run_energy_degradation;
    double shooting_energy_base, shooting_energy_per_extra_rocket;
    double hit_stun_time;
    double shoot_interval, shoot_interval_with_energy;
    double spawn_safe_time;
    uint32_t time_limit;
    uint32_t extra_time;
    bool sudden_death;
    int capture_limit;
    int win_score_difference;     // minimum score difference needed to win the game
    double flag_return_delay; // in seconds
    Team_balance balance_teams;

    bool random_wild_flag;

    bool lock_team_flags;
    bool lock_wild_flags;
    bool capture_on_team_flag;
    bool capture_on_wild_flag;

    int see_rockets_distance;

    int carrying_score_time;

    static const int shadow_minimum_normal;

    void reset() throw ();

    int get_hit_stun_time_frames() const throw () { return iround(hit_stun_time * 10.); }
    int get_shoot_interval_frames() const throw () { return iround(shoot_interval * 10.); }
    int get_shoot_interval_with_energy_frames() const throw () { return iround(shoot_interval_with_energy * 10.); }
    int get_spawn_safe_time_frames() const throw () { return iround(spawn_safe_time * 10.); }

    std::pair<double, double> getRespawnTime(int playerTeamSize, int enemyTeamSize) const throw ();
    double getDeathbringerWaitingTime() const throw () { return waiting_time_deathbringer; }
    int getShadowMinimum() const throw () { return shadow_minimum; }
    int getCaptureLimit() const throw () { return capture_limit; }
    int getWinScoreDifference() const throw () { return win_score_difference; }
    uint32_t getTimeLimit() const throw () { return time_limit; }
    uint32_t getExtraTime() const throw () { return extra_time; }

    Team_balance balanceTeams() const throw () { return balance_teams; }
    bool suddenDeath() const throw () { return sudden_death; }
};

class ServerNetworking;
class Server;   //#fix: get rid of non-networking callbacks?

class ServerWorld : public WorldBase {
    Server* host;
    ServerNetworking* net;
    PowerupSettings pupConfig;
    WorldSettings config;
    LogSet log;

    uint8_t getFreeRocket() throw ();    // may give an existing rocket to overwrite if the table is full
    bool doesPlayerSeeRocket(ServerPlayer& pl, int roomx, int roomy) const throw ();
    void drop_powerup(const ServerPlayer& player) throw ();
    void drop_worst_powerup(ServerPlayer& player) throw ();

    void regenerateHealthOrEnergy(ServerPlayer& pl) throw ();
    void degradeHealthOrEnergyForRunning(ServerPlayer& pl) throw ();

    void player_steals_flag(int pid, int team, int flag) throw ();
    void player_captures_flag(int pid, int team, int flag) throw ();
    void team_gets_carrying_point(int team, bool forTournament) throw ();

    bool all_kind_of_flags_exist() const throw ();

public:
    uint32_t frame;
    uint32_t map_start_time; // frame #
    ServerPlayer player[MAX_PLAYERS];

    ServerWorld(Server* hostp, ServerNetworking* netp, LogSet logset) throw () :
        host(hostp), net(netp), log(logset), frame(0), map_start_time(0)
    {
        for (int i = 0; i < MAX_PLAYERS; ++i)
            WorldBase::player[i].setPtr(&player[i]);
    }
    ~ServerWorld() throw () { }

    void setConfig(const WorldSettings& ws, const PowerupSettings& ps) throw () { config = ws; pupConfig = ps; }

    const WorldSettings& getConfig() const throw () { return config; }
    const PowerupSettings& getPupConfig() const throw () { return pupConfig; }

    // common (virtual in base) extended functions
    void reset() throw ();
    void generate_map(const std::string& mapdir, const std::string& file_name, int width, int height, float over_edge, const std::string& title, const std::string& author) throw ();
    bool load_map(const std::string& mapdir, const std::string& mapname, std::string* buffer) throw ();
    void returnAllFlags() throw ();
    void returnFlag(int team, int flag) throw ();
    void dropFlag(int team, int flag, int roomx, int roomy, double lx, double ly) throw ();
    void stealFlag(int team, int flag, int carrier) throw ();
    int getMapTime() const throw () { return frame - map_start_time; }
    bool isTimeLimit() const throw () { return config.getTimeLimit() > 0; }
    int getTimeLeft() const throw () { return config.getTimeLimit() - getMapTime(); }
    int getExtraTimeLeft() const throw () { return config.getTimeLimit() + config.getExtraTime() - getMapTime(); }
    double get_frame() const throw () { return frame; }

    // server specific functions
    void start_game() throw ();
    void reset_time() throw () { map_start_time = frame; }
    void respawnPlayer(int pid, bool dontInformClients = false) throw ();
    void printTimeStatus(LineReceiver& printer) throw ();

    void resetPlayer(int target, double time_penalty = 0.) throw (); // take the player out of the game; the clients must be informed and this function doesn't do that
    void killPlayer(int target, bool time_penalty) throw (); // kill the player in the usual way with score penalties and deathbringer effect; the clients must be informed and this function doesn't do that
    void damagePlayer(int target, int attacker, int damage, DamageType type) throw ();
    void removePlayer(int pid) throw ();
    void suicide(int pid) throw ();
    void respawn_powerup(int p) throw ();
    void check_powerup_creation(bool instant) throw ();
    void game_touch_powerup(int p, int pk) throw ();
    bool check_flag_touch(const Flag& flag, int px, int py, double x, double y) throw ();
    void game_player_screen_change(int p) throw ();

    bool dropFlagIfAny(int pid, bool purpose = false) throw ();
    void shootRockets(int pid, int numshots) throw ();
    void deleteRocket(int r, int16_t hitx, int16_t hity, int targ) throw ();
    void changeEmbeddedPids(int source, int target) throw ();
    void swapEmbeddedPids(int a, int b) throw ();

    void simulateFrame() throw ();

    void addMovementDistanceCallback(int pid, double dist) throw ();
    void playerScreenChangeCallback(int pid) throw ();
    void rocketHitWallCallback(int rid) throw ();
    bool rocketHitPlayerCallback(int rid, int pid) throw ();
    PhysicsCallbacksBase::PlayerHitResult playerHitPlayerCallback(int pid1, int pid2, double speed) throw ();
    void rocketOutOfBoundsCallback(int rid) throw ();
    bool shouldApplyPhysicsToPlayerCallback(int pid) throw ();

    bool lock_team_flags_in_effect() const throw ();
    bool lock_wild_flags_in_effect() const throw ();
    bool capture_on_team_flags_in_effect() const throw ();
    bool capture_on_wild_flags_in_effect() const throw ();
};

class ClientWorld : public WorldBase {
public:
    bool skipped;   // frame is invalid -- when frame is skipped in the broadcast
    double frame;

    std::vector<ClientPlayer> player;

    ClientWorld() throw () : skipped(true), player(MAX_PLAYERS) {
        for (int i = 0; i < MAX_PLAYERS; ++i)
            WorldBase::player[i].setPtr(&player[i]);
    }
    ~ClientWorld() throw () { }
    double get_frame() const throw () { return frame; }
    // extrapolate : advances from source, a frame per every ctrl listed except the last one which gets subFrameAfter, controls are for player me
    void extrapolate(ClientWorld& source, PhysicsCallbacksBase& physCallbacks, int me,
                     ClientControls* ctrlTab, uint8_t ctrlFirst, uint8_t ctrlLast, double subFrameAfter) throw ();

    /*void save_stats(const std::string& dir, const Team* teams,
                const std::vector<ClientPlayer*>& players, const std::string& map_name) const throw ();*/
};

#endif
