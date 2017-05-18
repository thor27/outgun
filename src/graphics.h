/*
 *  graphics.h
 *
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004, 2005, 2006 - Niko Ritari
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

#ifndef GRAPHICS_H_INC
#define GRAPHICS_H_INC

#include <string>
#include <list>
#include <vector>

#include "incalleg.h"
#include "mutex.h"
#include "utility.h"
#include "world.h"

// ---- client screen layout ----

class Map;
class MapInfo;
class Room;
class RectWall;
class TriWall;
class CircWall;
class ClientPlayer;
class Sounds;
class GraphicsEffect;
class Team;
class Flag;
class Powerup;
class Message;
class WorldCoords;
class Rocket;

class ScreenMode {
public:
    int width, height;

    ScreenMode(int w, int h) : width(w), height(h) { }
    bool operator==(const ScreenMode& o) const { return width == o.width && height == o.height; }
    bool operator<(const ScreenMode& o) const { return width < o.width || (width == o.width && height < o.height); }
};

class Bitmap {
    BITMAP* ptr;

public:
    Bitmap() : ptr(0) { }
    Bitmap(BITMAP* ptr_) : ptr(ptr_) { }
    ~Bitmap() { free(); }

    Bitmap(const Bitmap& o) : ptr(0) { nAssert(!o.ptr); }
    void operator=(const Bitmap& o) { nAssert(!ptr); nAssert(!o.ptr); }

    void free() { if (ptr) { destroy_bitmap(ptr); ptr = 0; } }
    const Bitmap& operator=(BITMAP* ptr_) { nAssert(!ptr); ptr = ptr_; return *this; }

    operator BITMAP*() const { return ptr; }
    BITMAP* operator->() const { return ptr; }
};

class Graphics {
public:
    Graphics(LogSet logs);
    ~Graphics();

    bool depthAvailable(int depth) const;
    std::vector<ScreenMode> getResolutions(int depth, bool forceTryIfNothing = true) const; // returns a sorted list of unique resolutions
    bool init(int width, int height, int depth, bool windowed, bool flipping);
    void make_layout();
    void videoMemoryCorrupted();    // call this when that happens with page flipping; predraw also needs to be called

    void startDraw();   // call startDraw before any drawing operations are done and endDraw when done, before drawScreen
    void endDraw();
    void startPlayfieldDraw();  // between calls to startPlayfieldDraw and endPlayfieldDraw, anything is only drawn to within the playfield area
    void endPlayfieldDraw();

    void draw_screen(bool acquireWithFlipping);
    bool save_screenshot(const std::string& filename) const;

    void clear();

    void reset_playground_colors();
    void random_playground_colors();

    void predraw(const Room& room, int texRoomX, int texRoomY, const std::vector< std::pair<int, const WorldCoords*> >& flags,
                 const std::vector< std::pair<int, const WorldCoords*> >& spawns, bool grid = false);

    void draw_background();
    void draw_empty_background(bool map_ready);

    void predraw_room_ground(const Room& room, int texOffsetBaseX, int texOffsetBaseY);
    void predraw_room_walls(const Room& room, int texOffsetBaseX, int texOffsetBaseY);

    void draw_flag(int team, int x, int y);
    void draw_flagpos_mark(int team, int flag_x, int flag_y);
    void draw_mini_flag(int team, const Flag& flag, const Map& map);
    void draw_minimap_background();
    void update_minimap_background(const Map& map);
    void draw_minimap_player(const Map& map, const ClientPlayer& player);
    void draw_minimap_me(const Map& map, const ClientPlayer& player, double time);
    void draw_minimap_room(const Map& map, int rx, int ry, float visibility);

    void draw_player(int x, int y, int team, int pli, int gundir, double hitfx, bool power, int alpha, double time);
    void draw_player_name(const std::string& name, int x, int y, int team, bool highlight = false);
    void draw_player_dead(const ClientPlayer& player);

    void draw_rocket(const Rocket& rocket, bool shadow, double time);
    void draw_gun_explosion(int x, int y, int rad, int team);
    void draw_deathbringer_smoke(int x, int y, double time, double alpha);
    void draw_deathbringer(int x, int y, int team, double time);

    void draw_player_health(int value);
    void draw_player_energy(int value);

    void draw_deathbringer_affected(int x, int y, int team, int alpha);
    void draw_deathbringer_carrier_effect(int x, int y, int alpha);
    void draw_shield(int x, int y, int r, int alpha = 255, int team = -1, int direction = 0);

    void draw_virou_sorvete(int x, int y);

    void draw_waiting_map_message(const std::string& caption, const std::string& map);
    void draw_loading_map_message(const std::string& text);
    void draw_scores(const std::string& text, int col, int score1, int score2);
    void print_chat_messages(std::list<Message>::const_iterator begin, const std::list<Message>::const_iterator& end,
                             const std::string& talkbuffer);

    void draw_scoreboard(const std::vector<ClientPlayer*>& players, const Team* teams, int maxplayers, bool pings = false, bool underlineMasterAuthenticated = false, bool underlineServerAuthenticated = false);

    void team_statistics(const Team* teams);
    void draw_statistics(const std::vector<ClientPlayer*>& players, int page, int time, int maxplayers, int max_world_rank = 0);
    void debug_panel(const std::vector<ClientPlayer>& players, int me, int bpsin, int bpsout,
                     const std::vector<std::vector<std::pair<int, int> > >& sticks, const std::vector<int>& buttons);

    void map_list(const std::vector<MapInfo>& maps, int current = -1,
                  int own_vote = -1, const std::string& edit_vote = "");

    void map_list_prev() { --map_list_start; }
    void map_list_next() { ++map_list_start; }
    void map_list_prev_page() { map_list_start -= map_list_size; }
    void map_list_next_page() { map_list_start += map_list_size; }
    void map_list_begin() { map_list_start = 0; }
    void map_list_end() { map_list_start = INT_MAX; }

    void team_captures_prev() { --team_captures_start; }
    void team_captures_next() { ++team_captures_start; }

    void draw_player_power(double val);
    void draw_player_turbo(double val);
    void draw_player_shadow(double val);
    void draw_player_weapon(int level);
    void map_time(int seconds);
    void draw_fps(double fps);
    void draw_change_team_message(double time);
    void draw_change_map_message(double time, bool delayed = false);

    // power-ups
    void draw_pup(const Powerup& pup, double time);
    void draw_pup_shield(int x, int y);
    void draw_pup_turbo(int x, int y);
    void draw_pup_shadow(int x, int y, double time);
    void draw_pup_power(int x, int y, double time);
    void draw_pup_weapon(int x, int y, double time);
    void draw_pup_health(int x, int y, double time);
    void draw_pup_deathbringer(int x, int y);

    // client side effects
    void draw_effects(int room_x, int room_y, double time);
    void draw_turbofx(int room_x, int room_y, double time);

    void clear_fx();

    void create_wallexplo(int x, int y, int px, int py, int team);
    void create_powerwallexplo(int x, int y, int px, int py, int team);
    void create_smoke(int x, int y, int px, int py);
    void create_deathcarrier(int x, int y, int px, int py, int alpha);
    void create_turbofx(int x, int y, int px, int py, int col1, int col2, int gundir, int alpha);
    void create_deathbringer(int team, double start_time, int x, int y, int px, int py);
    void create_gunexplo(int x, int y, int px, int py, int team);

    bool save_map_picture(const std::string& filename, const Map& map);

    void search_themes(LineReceiver& dst_theme, LineReceiver& dst_bg) const;
    void select_theme(const std::string& name, const std::string& bg_dir, bool use_theme_bg);

    void search_fonts(LineReceiver& dst_font) const;
    void select_font(const std::string& file);

    void set_antialiasing(bool enable) { antialiasing = enable; }

    void set_min_transp(bool enable) { min_transp = enable; }

    int player_color(int index) const { nAssert(index >= 0 && index < 16); return col[index]; }

    // How many lines fit on the chat area and screen.
    int chat_lines() const;
    int chat_max_lines() const;

    BITMAP* drawbuffer() { return drawbuf; }

    // public only for Mappic
    void setColors();

    void set_stats_alpha(int alpha) { stats_alpha = alpha; }

private:
    void unload_bitmaps();

    bool reset_video_mode(int width, int height, int depth, bool windowed, int pages = 1);

    void setPlaygroundColors();

    void make_background(BITMAP* buffer);

    void update_minimap_background(BITMAP* buffer, const Map& map, bool save_map_pic = false);

    // texture should really be const BITMAP* but Allegero function needs BITMAP* for some reason
    void draw_room_ground(BITMAP* buffer, const Room& room, double x, double y, int texOffsetBaseX, int texOffsetBaseY, double scale, int color, bool texture);
    void draw_room_walls(BITMAP* buffer, const Room& room, double x, double y, int texOffsetBaseX, int texOffsetBaseY, double scale, int color, bool texture);

    static void draw_wall(BITMAP* buffer, WallBase* wall, double x, double y, int texOffsetBaseX, int texOffsetBaseY, double scale, int color, BITMAP* tex);
    static void draw_rect_wall(BITMAP* buffer, const RectWall& wall, double x0, double y0, int texOffsetBaseX, int texOffsetBaseY, double scale, int color, BITMAP* texture);
    static void draw_tri_wall (BITMAP* buffer, const TriWall & wall, double x0, double y0, int texOffsetBaseX, int texOffsetBaseY, double scale, int color, BITMAP* texture);
    static void draw_circ_wall(BITMAP* buffer, const CircWall& wall, double x0, double y0, int texOffsetBaseX, int texOffsetBaseY, double scale, int color, BITMAP* texture);

    std::pair<int, int> calculate_minimap_coordinates(const Map& map, const ClientPlayer& player) const;

    void draw_bar(int x, int y, const std::string& caption, int value, int c100, int c200, int c300);
    void draw_powerup_time(int line, const std::string& caption, double val, int c);

    void draw_player_statistics(const FONT* stfont, const ClientPlayer& player, int x, int y, int page, int time);

    void draw_scoreboard_name(const FONT* sbfont, const std::string& name, int x, int y, int pcol, bool underline);
    void draw_scoreboard_points(const FONT* sbfont, int points, int x, int y, int team);

    void print_chat_message(Message_type type, const std::string& message, int x, int y, bool highlight = false);
    void print_chat_input(const std::string& message, int x, int y);

    void print_text(const std::string& text, int x, int y, int textcol, int bgcol);
    void print_text_centre(const std::string& text, int x, int y, int textcol, int bgcol);

    void print_text_border(const std::string& text, int x, int y, int textcol, int bordercol, int bgcol);
    void print_text_border_centre(const std::string& text, int x, int y, int textcol, int bordercol, int bgcol);

    void print_text_border_check_bg(const std::string& text, int x, int y, int textcol, int bordercol, int bgcol);
    void print_text_border_centre_check_bg(const std::string& text, int x, int y, int textcol, int bordercol, int bgcol);

    void print_text_border(const std::string& text, int x, int y, int textcol, int bordercol, int bgcol, bool centring);

    void scrollbar(int x, int y, int height, int bar_y, int bar_h, int col1, int col2);

    void make_db_effect();

    void load_pictures();
    void load_background();

    void load_floor_textures(const std::string& filename);
    void load_wall_textures(const std::string& filename);
    BITMAP* get_floor_texture(int texid);
    BITMAP* get_wall_texture(int texid);

    void load_player_sprites(const std::string& path);
    void load_shield_sprites(const std::string& path);
    void load_dead_sprites(const std::string& path);
    void load_rocket_sprites(const std::string& path);
    void load_flag_sprites(const std::string& path);
    void load_pup_sprites(const std::string& path);

    BITMAP* scale_sprite(const std::string& filename, int x, int y) const;
    BITMAP* scale_alpha_sprite(const std::string& filename, int x, int y) const;
    static void set_alpha_channel(BITMAP* bitmap, BITMAP* alpha);
    static void rotate_trans_sprite(BITMAP* bmp, BITMAP* sprite, int x, int y, fixed angle, int alpha); // x,y are destination coords of the sprite center
    static void rotate_alpha_sprite(BITMAP* bmp, BITMAP* sprite, int x, int y, fixed angle);    // x,y are destination coords of the sprite center
    static int colorTo32(int color) { return makecol32(getr(color), getg(color), getb(color)); }
    static void overlayColor(BITMAP* bmp, BITMAP* alpha, int color);    // alpha must be an 8-bit bitmap; give the color in same format as bmp
    static void combine_sprite(BITMAP* sprite, BITMAP* common, BITMAP* team, BITMAP* personal, int tcol, int pcol); // give the colors in same format as sprite

    void unload_pictures();
    void unload_floor_textures();
    void unload_wall_textures();
    void unload_player_sprites();
    void unload_shield_sprites();
    void unload_dead_sprites();
    void unload_rocket_sprites();
    void unload_flag_sprites();
    void unload_pup_sprites();

    void load_font(const std::string& file);

    int scale(double value) const;

    // drawing screens
    Bitmap vidpage1;
    Bitmap vidpage2;
    Bitmap backbuf;
    bool page_flipping;
    bool min_transp;

    BITMAP* drawbuf;    // main draw buffer (points to vidpage# or backbuf at a given time)
    Bitmap background;  // draw buffer for floor, walls and minimap
    Bitmap minibg;      // minimap draw buffer
    Bitmap minibg_fog;  // minimap with fog in every room

    Bitmap roombg;      // room background sub-bitmap

    int plx, ply;       // playground position on the screen
    int mmx, mmy;       // minimap position

    int scoreboard_x1, scoreboard_x2;  // scoreboard position
    int scoreboard_y1, scoreboard_y2;

    int minimap_w, minimap_h;
    int minimap_place_w, minimap_place_h;
    int minimap_start_x, minimap_start_y;
    int indicators_y;
    int health_x, energy_x, pups_x, pups_val_x, weapon_x, time_x, time_y;

    bool show_chat_messages;
    bool show_scoreboard;
    bool show_minimap;

    static const int flagpos_radius = 30;
    double scr_mul; // screen size multiplier

    Bitmap bg_texture;

    std::vector<Bitmap> floor_texture;
    std::vector<Bitmap> wall_texture;

    Bitmap player_sprite_power;
    std::vector<Bitmap> pup_sprite;

    // Team specific sprites
    std::vector<Bitmap> player_sprite[2];
    Bitmap shield_sprite[2];
    Bitmap dead_sprite[2];
    Bitmap rocket_sprite[2];
    Bitmap power_rocket_sprite[2];
    Bitmap flag_sprite[3];  // red, blue and wild flag

    Bitmap ice_cream;

    Bitmap db_effect;       // the darkening of the ground around the player

    FONT* default_font;
    FONT* border_font;

    int map_list_size;
    int map_list_start;

    int team_captures_start;

    std::list<GraphicsEffect> cfx;

    std::string theme_path;
    std::string bg_path;

    bool antialiasing;

    int stats_alpha;

    //colors
    enum {
        //player's colors
        COLGREEN,
        COLYELLOW,
        COLWHITE,
        COLMAG,
        COLCYAN,
        COLORA,
        COLLRED,        // light red
        COLLBLUE,       // light blue
        //MORE player colors
        COL9,
        COL10,
        COL11,
        COL12,
        COL13,
        COL14,
        COL15,
        COL16,

        //team colors
        COLRED,         //team 1 (color 0)
        COLBLUE,        //team 2 (color 1)

        //base colors
        COLBRED,            //team 1 (color 0)
        COLBBLUE,       //team 2 (color 1)

        //other
        COLFOGOFWAR,
        COLMENUWHITE,
        COLMENUBLACK,
        COLMENUGRAY,
        COLGROUND,
        COLWALL,
        COLBLACK,
        COLDARKGRAY,
        COLSHADOW,
        COLDARKORA,
        COLINFO,
        COLENER3,
        COLGROUND_DEF,
        COLWALL_DEF,
        COLDARKGREEN,
        NUM_OF_COL
    };

    int teamcol[3];
    int teamlcol[2];    // light colours for statusbar
    int teamdcol[2];    // dark colours for player name

    int col[NUM_OF_COL];
    int groundCol[3], wallCol[3];

    static const int fogOfWarMaxAlpha = 0x38;

    mutable LogSet log;
};

#endif // GRAPHICS_H_INC
