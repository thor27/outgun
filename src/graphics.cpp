/*
 *  graphics.cpp
 *
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004, 2005, 2006, 2008 - Niko Ritari
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

#include <fstream>
#include <iomanip>
#include <sstream>

#include <cmath>

#include "antialias.h"
#include "commont.h"
#include "effects.h"
#include "language.h"
#include "platform.h"
#include "sounds.h"
#include "timer.h"

#include "graphics.h"

static const bool TEST_FALL_ON_WALL = false;

static const bool DISABLE_ROOM_CACHE = false;

// these shouldn't be changed - rather change allegro.cfg
static const int  WINMODE = GFX_AUTODETECT_WINDOWED;
static const int FULLMODE = GFX_AUTODETECT;

static const bool SWITCH_PAUSE_CLIENT = false;

static const int GUNPOINT_RADIUS = 28;

using std::ifstream;
using std::istringstream;
using std::left;
using std::list;
using std::max;
using std::min;
using std::ostringstream;
using std::pair;
using std::right;
using std::setfill;
using std::setprecision;
using std::setw;
using std::sort;
using std::string;
using std::vector;

#ifdef WITH_PNG
const string Graphics::save_extension = ".png";
#else
const string Graphics::save_extension = ".pcx";
#endif

Graphics::Graphics(LogSet logs) throw () :
    roomLayout          (*this),
    background          (*this),
    show_chat_messages  (true),
    show_scoreboard     (true),
    show_minimap        (true),
    default_font        (font),
    border_font         (0),
    map_list_size       (20),
    map_list_start      (0),
    team_captures_start (0),
    antialiasing        (true),
    colour              (logs),
    mapNeedsRedraw      (true),
    log                 (logs)
{
    file_extensions.push_back(".pcx");
    #ifdef WITH_PNG
    file_extensions.push_back(".png");
    #endif
}

Graphics::~Graphics() throw () {
    unload_bitmaps();
}

bool Graphics::init(int width, int height, int depth, bool windowed, bool flipping) throw () {
    unload_bitmaps();

    if (windowed)
        flipping = false;

    int backgroundPages = 8, minBackgroundPages = flipping ? 1 : backgroundPages;
    for (; backgroundPages >= minBackgroundPages; --backgroundPages)
        if (reset_video_mode(width, height, depth, windowed, flipping ? 2 + backgroundPages : 1))
            break;
    if (backgroundPages < minBackgroundPages)
        return false;

    page_flipping = flipping;

    if (page_flipping) {
        vidpage1   = create_video_bitmap(SCREEN_W, SCREEN_H);
        vidpage2   = create_video_bitmap(SCREEN_W, SCREEN_H);
        if (!vidpage1 || !vidpage2 || !background.allocate(true, backgroundPages)) {
            log("Not enough video memory. Can't use page flipping.");
            // free those that _were_ allocated
            vidpage1.free();
            vidpage2.free();
            background.free();
            return false;
        }
        drawbuf = vidpage1;
    }
    else {
        backbuf = create_bitmap(SCREEN_W, SCREEN_H);
        nAssert(backbuf);
        const bool result = background.allocate(false, backgroundPages);
        nAssert(result);
        drawbuf = backbuf;
    }

    setColors();

    make_layout();

    load_generic_pictures();
    load_background();
    needReloadPlayfieldPictures = true;
    mapNeedsRedraw = true;

    return true;
}

void Graphics::make_layout() throw () {
    if (!show_minimap && !show_scoreboard)
        scr_mul = static_cast<double>(SCREEN_W) / plw;
    else
        scr_mul = static_cast<double>(SCREEN_W) / 640;

    // Room background
    const int bottombar_h = 3 * (text_height(font) + 2) + 5;
    if (SCREEN_H < scr_mul * plh + bottombar_h + text_height(font))          // the window is too low for playground and one line for messages
        scr_mul = static_cast<double>(SCREEN_H - bottombar_h - text_height(font)) / plh;
    if (!show_minimap && !show_scoreboard)
        playfield_x = (SCREEN_W - scale(plw)) / 2;
    else
        playfield_x = 0;
    playfield_y = SCREEN_H - scale(plh) - bottombar_h;
    playfield_w = static_cast<int>(ceil(scr_mul * plw));
    playfield_h = static_cast<int>(ceil(scr_mul * plh));

    // Minimap
    minimap_w = minimap_h = 0; // to be determined when the map is drawn
    minimap_place_w = SCREEN_W - playfield_w - 4; // 4 for left and right margin
    minimap_place_x = SCREEN_W - minimap_place_w - 2;
    if (minimap_place_x > text_length(font, "M") * 80) {  // check if minimap fits to the right of chat messages
        minimap_place_y = 4;
        minimap_place_h = scale(100) + playfield_y - 4;
    }
    else {
        minimap_place_y = playfield_y;
        minimap_place_h = scale(100);
    }
    const int extra_space = SCREEN_H - 450 - (minimap_place_y + minimap_place_h);   // 450 = scoreboard max height + FPS line
    if (extra_space > 0)
        minimap_place_h += extra_space;
    minibg.free();
    minibg = create_bitmap(minimap_place_w, minimap_place_h);
    nAssert(minibg);
    minibg_fog.free();
    minibg_fog = create_bitmap(minimap_place_w, minimap_place_h);   // if the creation fails, it just won't be used

    // Scoreboard
    scoreboard_x1 = playfield_x + playfield_w;
    scoreboard_x2 = SCREEN_W - 1;
    scoreboard_y1 = minimap_place_y + minimap_place_h;
    scoreboard_y2 = SCREEN_H - 1;

    // Bottom bar
    indicators_y = playfield_y + playfield_h + 5;
    const int left_margin = min(10, scale(10));
    const int max_w = scoreboard_x1 - 5 - left_margin;
    const int num_len = text_length(font, "0000");
    const int health_w = max(text_length(font, _("Health")) + num_len, scale(100));
    const int energy_w = max(text_length(font, _("Energy")) + num_len, scale(100));
    const int pups_w = max(max(text_length(font, _("Power")), text_length(font, _("Turbo"))), text_length(font, _("Shadow"))) + num_len;
    const int weapon_w = text_length(font, _("Weapon $1", "0"));
    const int time_w = text_length(font, "000:00");
    int min_w = health_w + energy_w + pups_w + weapon_w + time_w;
    int space;
    if (min_w + 4 * 5 > max_w) {
        min_w -= time_w;
        space = (max_w - min_w) / 3;
        time_y = indicators_y + text_height(font) + 2;
    }
    else {
        space = (max_w - min_w) / 4;
        time_y = indicators_y;
    }
    health_x = left_margin;
    energy_x = health_x + health_w + space;
    pups_x = energy_x + energy_w + space;
    pups_val_x = pups_x + pups_w;   // align right
    weapon_x = pups_x + pups_w + space;
    if (time_y == indicators_y)
        time_x = weapon_x + weapon_w + space + time_w;  // align right
    else
        time_x = weapon_x + time_w;

    needReloadPlayfieldPictures = true;

    roomGraphicsChanged();
    mapNeedsRedraw = true;
}

void Graphics::reload_playfield_pictures() throw () {
    needReloadPlayfieldPictures = false;
    unload_playfield_pictures();
    load_playfield_pictures();
    roomGraphicsChanged();
}

void Graphics::toggle_full_playfield() throw () {
    show_minimap = show_scoreboard = !show_minimap;
    make_layout();
}

void Graphics::videoMemoryCorrupted() throw () {
    roomGraphicsChanged();
}

void Graphics::unload_bitmaps() throw () {
    vidpage1  .free();
    vidpage2  .free();
    backbuf   .free();
    background.free();
    minibg    .free();
    minibg_fog.free();
    db_effect .free();
    unload_pictures();
}

void Graphics::startDraw() throw () {
    acquire_bitmap(drawbuf);
}

void Graphics::endDraw() throw () {
    release_bitmap(drawbuf);
}

void Graphics::startPlayfieldDraw() throw () {
    set_clip_rect(drawbuf, roomLayout.x0(), roomLayout.y0(), roomLayout.xMax(), roomLayout.yMax());
}

void Graphics::endPlayfieldDraw() throw () {
    set_clip_rect(drawbuf, 0, 0, drawbuf->w - 1, drawbuf->h - 1);
}

void Graphics::draw_screen(bool acquireWithFlipping) throw () {
    if (page_flipping) {
        if (acquireWithFlipping)
            acquire_screen();
        show_video_bitmap(drawbuf);
        if (acquireWithFlipping)
            release_screen();

        if (drawbuf == vidpage1)
            drawbuf = vidpage2;
        else
            drawbuf = vidpage1;
    }
    else {
        acquire_screen();
        blit(drawbuf, screen, 0, 0, 0, 0, SCREEN_W, SCREEN_H);
        release_screen();
    }
}

void Graphics::setColors() throw () {
    colour.init(colour_file);

    reset_playground_colors();

    // player colours
    int i = 0;
    col[i++] = colour[Colour::player01];
    col[i++] = colour[Colour::player02];
    col[i++] = colour[Colour::player03];
    col[i++] = colour[Colour::player04];
    col[i++] = colour[Colour::player05];
    col[i++] = colour[Colour::player06];
    col[i++] = colour[Colour::player07];
    col[i++] = colour[Colour::player08];
    col[i++] = colour[Colour::player09];
    col[i++] = colour[Colour::player10];
    col[i++] = colour[Colour::player11];
    col[i++] = colour[Colour::player12];
    col[i++] = colour[Colour::player13];
    col[i++] = colour[Colour::player14];
    col[i++] = colour[Colour::player15];
    col[i++] = colour[Colour::player16];
    col[i++] = colour[Colour::player_unknown];
    nAssert(i == 17 && MAX_PLAYERS == 32);

    // team colours for players, flags, etc.
    teamcol[0] = colour[Colour::team_red_basic];
    teamcol[1] = colour[Colour::team_blue_basic];
    teamflashcol[0] = colour[Colour::team_red_flash];
    teamflashcol[1] = colour[Colour::team_blue_flash];

    // wild flag colour
    teamcol[2] = colour[Colour::wild_flag];
    teamflashcol[2] = colour[Colour::wild_flag_flash];

    // light colours for text
    teamlcol[0] = colour[Colour::team_red_light];
    teamlcol[1] = colour[Colour::team_blue_light];

    // dark colours for bases on map and team text background
    teamdcol[0] = colour[Colour::team_red_dark];
    teamdcol[1] = colour[Colour::team_blue_dark];

    roomGraphicsChanged();
    mapNeedsRedraw = true;
}

void Graphics::reset_playground_colors() throw () {
    groundCol = colour[Colour::ground];
    wallCol   = colour[Colour::wall];
    roomGraphicsChanged();
}

void Graphics::random_playground_colors() throw () {
    groundCol = Colour(rand() % 256, rand() % 256, rand() % 256);
    wallCol   = Colour(rand() % 256, rand() % 256, rand() % 256);
    roomGraphicsChanged();
}

int Graphics::chat_lines() const throw () {
    return max(1, roomLayout.y0() / (text_height(font) + 3));
}

int Graphics::chat_max_lines() const throw () {
    return max(1, (playfield_y + scale(plh)) / (text_height(font) + 3));
}

bool Graphics::depthAvailable(int depth) const throw () {
    return !getResolutions(depth, false).empty();   //#opt: no need to go through all modes or create a vector
}

vector<ScreenMode> Graphics::getResolutions(int depth, bool forceTryIfNothing) const throw () {  // returns a sorted list of unique resolutions
    vector<ScreenMode> mvec;

    #ifdef ALLEGRO_WINDOWS
    GFX_MODE_LIST* const modes = get_gfx_mode_list(GFX_DIRECTX);
    #else
    #ifdef GFX_XWINDOWS_FULLSCREEN
    GFX_MODE_LIST* const modes = get_gfx_mode_list(GFX_XWINDOWS_FULLSCREEN);
    #else
    GFX_MODE_LIST* const modes = 0;
    #endif
    #endif
    if (modes) {
        const int depth2 = (depth == 16) ? 15 : depth;    // 15 and 16 bit modes are considered equal
        for (int i = 0; i < modes->num_modes; i++) {
            const GFX_MODE& mode = modes->mode[i];
            if (mode.width >= 320 && mode.height >= 200 && (mode.bpp == depth || mode.bpp == depth2))
                mvec.push_back(ScreenMode(mode.width, mode.height));
        }
        destroy_gfx_mode_list(modes);
    }
    if (mvec.empty())
        log("No usable %d-bit fullscreen modes autodetected.", depth);

    ifstream resFile((wheregamedir + "config" + directory_separator + "gfxmodes.txt").c_str());
    for (;;) {
        string line;
        if (!getline_skip_comments(resFile, line))
            break;
        istringstream ss(line);
        int width, height, bits;
        char nullc;
        ss >> width >> height >> bits;
        const bool ok = ss;
        ss >> nullc;
        if (!ok || ss) {
            log.error(_("Syntax error in $1, line '$2'.", "gfxmodes.txt", line));
            break;
        }
        if (width < 320 || height < 200 || (bits != 16 && bits != 24 && bits != 32)) {
            log.error(_("Unusable mode in gfxmodes.txt: $1×$2×$3 (should be at least 320×200 with bits 16, 24 or 32).",
                            itoa(width), itoa(height), itoa(bits)));
            break;
        }
        if (bits == depth)
            mvec.push_back(ScreenMode(width, height));
    }

    if (mvec.empty() && forceTryIfNothing)
        mvec.push_back(ScreenMode(640, 480));   // just try something

    sort(mvec.begin(), mvec.end());
    mvec.erase(std::unique(mvec.begin(), mvec.end()), mvec.end());
    return mvec;
}

bool Graphics::reset_video_mode(int width, int height, int depth, bool windowed, int pages) throw () {
    log("Setting video mode: %d×%d×%d %s", width, height, depth, windowed ? "windowed" : "fullscreen");

    int virtual_w = 0, virtual_h = 0;
    #ifdef ALLEGRO_VRAM_SINGLE_SURFACE
    if (pages > 1) {
        virtual_w = width;
        virtual_h = height * pages;
    }
    #else
    (void)pages;
    #endif

    if (set_gfx_mode_if_new(depth, windowed ? WINMODE : FULLMODE, width, height, virtual_w, virtual_h)) {
        log("Error: '%s'", allegro_error);
        if (depth == 16) {  // try equivalent 15-bit mode too
            if (set_gfx_mode_if_new(15, windowed ? WINMODE : FULLMODE, width, height, virtual_w, virtual_h)) {
                log("Error with equivalent 15-bit mode: '%s'", allegro_error);
                return false;
            }
        }
        else
            return false;
    }

    if (!SWITCH_PAUSE_CLIENT) {
        if (set_display_switch_mode(SWITCH_BACKAMNESIA) == -1) {
            if (set_display_switch_mode(SWITCH_BACKGROUND) == -1) {
                log("Client cannot run in the background!");
                return false;
            }
            else
                log("Switch_background set ok.");
        }
        else
            log("Switch_backamnesia set ok.");
        GlobalDisplaySwitchHook::install();
    }

    #ifdef ALLEGRO_WINDOWS
    log("Testing. If Outgun hangs here, restarting Windows should help. To avoid the problem, don't run Outgun with certain programs that use overlays (e.g. TV software).");
    acquire_screen();
    release_screen();
    log("Hang test complete, no problems.");
    #endif

    return true;
}

void Graphics::setRoomLayout(const Map& map, double visible_rooms, bool repeatMap) throw () {
    if (roomLayout.set(map.w, map.h, visible_rooms, repeatMap))
        needReloadPlayfieldPictures = true;
}

void Graphics::draw_background(bool map_ready) throw () {
    background.draw_background(drawbuf, map_ready && show_minimap, false);
}

void Graphics::draw_background(const Map& map, const VisibilityMap& roomVis, const WorldCoords& topLeft, bool continuousTextures, bool mapInfoMode) throw () {
    if (needReloadPlayfieldPictures)
        reload_playfield_pictures();
    roomLayout.setTopLeft(topLeft);
    background.draw_background(drawbuf, show_minimap, true);
    background.draw_playfield_background(drawbuf, map, roomVis, continuousTextures, mapInfoMode);
}

void Graphics::drawRoomBackground(BITMAP* roombg, const Map& map, int roomx, int roomy, int texRoomX, int texRoomY, bool mapInfoMode) throw () {
    const Room& room = map.room[roomx][roomy];

    // the room at top left is textured like it's the room at coordinates (texRoomX,texRoomY)
    // this means moving the texture offsetting origin to the top left of room (0,0)
    const int texOffsetBaseX = - texRoomX * roombg->w;
    const int texOffsetBaseY = - texRoomY * roombg->h;

    if (antialiasing) {
        // we want to fill roombg, but there's the chance that its size is not exactly 4:3
        // we want to fill both directions to the fullest and clip away the excess in one direction (if any), taking the same amount from both sides
        const double fillingScaleX = (roombg->w - .02) / plw; // -.02 because to be safe with the limits we clip .01 pixels from all sides
        const double fillingScaleY = (roombg->h - .02) / plh;
        const double scale = max(fillingScaleX, fillingScaleY);
        const double x0 = (roombg->w - scale * plw) / 2., y0 = (roombg->h - scale * plh) / 2.;

        SceneAntialiaser scene;
        vector<TextureData> textures;

        // add ground/floor
        scene.setScaling(x0, y0, scale);
        scene.addRectangle(0, 0, plw, plh, 0);
        for (vector<WallBase*>::const_iterator wi = room.readGround().begin(); wi != room.readGround().end(); ++wi)
            scene.addWall(*wi, (*wi)->texture());

        TextureData backupTexture;
        TextureData td;
        if (floor_texture.front())
            backupTexture.setTexture(floor_texture.front(), texOffsetBaseX, texOffsetBaseY);
        else
            backupTexture.setSolid(groundCol);
        for (vector<Bitmap>::const_iterator ti = floor_texture.begin(); ti != floor_texture.end(); ++ti) {
            if (*ti) { td.setTexture(*ti, texOffsetBaseX, texOffsetBaseY); textures.push_back(td); }
            else textures.push_back(backupTexture);
        }

        // add respawn areas as overlays
        if (mapInfoMode) {
            for (int team = 0; team < 2; ++team) {
                for (vector<WorldRect>::const_iterator ri = map.tinfo[team].respawn.begin(); ri != map.tinfo[team].respawn.end(); ++ri)
                    if (ri->px == roomx && ri->py == roomy)
                        scene.addRectangle(ri->x1 - PLAYER_RADIUS, ri->y1 - PLAYER_RADIUS,
                                           ri->x2 + PLAYER_RADIUS, ri->y2 + PLAYER_RADIUS, textures.size(), true);
                td.setSolid(teamcol[team], 120);
                textures.push_back(td);
            }
        }

        // add flag markers as overlays
        const double fr = flagpos_radius;
        for (int team = 0; team < 3; ++team) {
            const vector<WorldCoords>& tflags = (team == 2 ? map.wild_flags : map.tinfo[team].flags);
            for (vector<WorldCoords>::const_iterator fi = tflags.begin(); fi != tflags.end(); ++fi) {
                if (fi->px != roomx || fi->py != roomy)
                    continue;
                scene.addRectangle(fi->x - fr, fi->y - fr, fi->x + fr, fi->y + fr, textures.size(), true);
                td.setFlagmarker(teamcol[team],
                                 x0 + fi->x * scale,
                                 y0 + fi->y * scale,
                                 flagpos_radius * scale);
                textures.push_back(td);
            }
        }

        // add room boundaries
        scene.setScaling(0, 0, 1);
        {
            const double x0 = 0, x1 = roombg->w, y0 = 0, y1 = roombg->h;
            const double bw = .5; // boundary width in pixels
            scene.addRectangle(x0     , y0     , x1     , x0 + bw, textures.size());
            scene.addRectangle(x0     , y1 - bw, x1     , y1     , textures.size());
            scene.addRectangle(x0     , y0     , x0 + bw, y1     , textures.size());
            scene.addRectangle(x1 - bw, y0     , x1     , y1     , textures.size());
        }
        td.setSolid(colour[Colour::room_border]);
        textures.push_back(td);

        // add walls
        scene.setScaling(x0, y0, scale);
        for (vector<WallBase*>::const_iterator wi = room.readWalls().begin(); wi != room.readWalls().end(); ++wi)
            scene.addWall(*wi, (*wi)->texture() + textures.size());

        if (wall_texture.front())
            backupTexture.setTexture(wall_texture.front(), texOffsetBaseX, texOffsetBaseY);
        else
            backupTexture.setSolid(wallCol);
        for (vector<Bitmap>::const_iterator ti = wall_texture.begin(); ti != wall_texture.end(); ++ti) {
            if (*ti) { td.setTexture(*ti, texOffsetBaseX, texOffsetBaseY); textures.push_back(td); }
            else textures.push_back(backupTexture);
        }

        // clip
        scene.setScaling(0, 0, 1);
        scene.setClipping(.01, .01, roombg->w - .01, roombg->h - .01);
        scene.clipAll();

        // draw
        Texturizer tex(roombg, 0, 0, textures);
        scene.render(tex);
        tex.finalize();
    }
    else {
        const double fillingScaleX = double(roombg->w) / plw;
        const double fillingScaleY = double(roombg->h) / plh;
        const double scale = max(fillingScaleX, fillingScaleY);

        // draw ground/floor
        if (floor_texture.front())
            drawing_mode(DRAW_MODE_COPY_PATTERN, floor_texture.front(), texOffsetBaseX, texOffsetBaseY);
        rectfill(roombg, 0, 0, roombg->w - 1, roombg->h - 1, groundCol);
        solid_mode();
        draw_room_ground(roombg, room, 0, 0, texOffsetBaseX, texOffsetBaseY, scale);

        // draw respawn areas
        if (mapInfoMode) {
            set_trans_mode(120);
            for (int team = 0; team < 2; ++team)
                for (vector<WorldRect>::const_iterator ri = map.tinfo[team].respawn.begin(); ri != map.tinfo[team].respawn.end(); ++ri)
                    if (ri->px == roomx && ri->py == roomy) {
                        rectfill(roombg,
                                 pf_scale(ri->x1 - PLAYER_RADIUS),
                                 pf_scale(ri->y1 - PLAYER_RADIUS),
                                 pf_scale(ri->x2 + PLAYER_RADIUS),
                                 pf_scale(ri->y2 + PLAYER_RADIUS), teamcol[team]);
                    }
            solid_mode();
        }
        // draw flag position marks
        for (int team = 0; team < 3; ++team) {
            const vector<WorldCoords>& tflags = (team == 2 ? map.wild_flags : map.tinfo[team].flags);
            for (vector<WorldCoords>::const_iterator fi = tflags.begin(); fi != tflags.end(); ++fi)
                if (fi->px == roomx && fi->py == roomy)
                    draw_flagpos_mark(roombg, team, pf_scale(fi->x), pf_scale(fi->y));
        }

        // draw room boundaries
        hline(roombg, 0, 0, roombg->w - 1, colour[Colour::room_border]);
        vline(roombg, 0, 0, roombg->h - 1, colour[Colour::room_border]);

        // draw walls
        draw_room_walls(roombg, room, 0, 0, texOffsetBaseX, texOffsetBaseY, scale);
    }

    if (mapInfoMode) {
        for (int team = 0; team < 2; ++team)
            for (vector<WorldCoords>::const_iterator si = map.tinfo[team].spawn.begin(); si != map.tinfo[team].spawn.end(); ++si)
                if (si->px == roomx && si->py == roomy)
                    circlefill(roombg, pf_scale(si->x), pf_scale(si->y), pf_scale(PLAYER_RADIUS), teamcol[team]);
        for (int y = 1; y < 12; ++y)
            hline(roombg, 0, pf_scale(plh * y / 12.), roombg->w - 1, y == 6 ? colour[Colour::map_info_grid_main] : colour[Colour::map_info_grid]);
        for (int x = 1; x < 16; ++x)
            vline(roombg, pf_scale(plw * x / 16.), 0, roombg->h - 1, x == 8 ? colour[Colour::map_info_grid_main] : colour[Colour::map_info_grid]);
        hline(roombg, 0, 0, roombg->w - 1, colour[Colour::map_info_grid_room]);
        vline(roombg, 0, 0, roombg->h - 1, colour[Colour::map_info_grid_room]);
    }
    if (TEST_FALL_ON_WALL)
        for (int y = 0; y < plh; y += 2)
            for (int x = 0; x < plw; x += 2)
                putpixel(roombg, pf_scale(x), pf_scale(y), room.fall_on_wall(x, y, 15) ? makecol(255, 0, 0) : makecol(255, 255, 255));
}

void Graphics::draw_flagpos_mark(BITMAP* roombg, int team, int flag_x, int flag_y) throw () {
    const int radius = pf_scale(flagpos_radius);
    const int step = 300 / radius;
    for (int y = flag_y - radius; y < flag_y + radius; y++)
        for (int x = flag_x - radius; x < flag_x + radius; x++) {
            const int dx = flag_x - x;
            const int dy = flag_y - y;
            const double dist = sqrt(static_cast<double>(dx * dx + dy * dy));
            const int alpha = static_cast<int>(step * (radius - dist));
            if (alpha <= 0)
                continue;
            set_trans_mode(min(alpha, 255));
            putpixel(roombg, x, y, teamcol[team]);
        }
    solid_mode();
}

void Graphics::draw_room_ground(BITMAP* buffer, const Room& room, int x, int y, int texOffsetBaseX, int texOffsetBaseY, double scale) throw () {
    for (vector<WallBase*>::const_iterator wi = room.readGround().begin(); wi != room.readGround().end(); ++wi)
        draw_wall(buffer, *wi, x, y, texOffsetBaseX, texOffsetBaseY, scale, groundCol, get_floor_texture((*wi)->texture()));
}

void Graphics::draw_room_walls(BITMAP* buffer, const Room& room, int x, int y, int texOffsetBaseX, int texOffsetBaseY, double scale) throw () {
    for (vector<WallBase*>::const_iterator wi = room.readWalls().begin(); wi != room.readWalls().end(); ++wi)
        draw_wall(buffer, *wi, x, y, texOffsetBaseX, texOffsetBaseY, scale, wallCol, get_wall_texture((*wi)->texture()));
}

void Graphics::draw_wall(BITMAP* buffer, WallBase* wall, double x, double y, int texOffsetBaseX, int texOffsetBaseY, double scale, int color, BITMAP* tex) throw () {
    if      (RectWall* rwp = dynamic_cast<RectWall*>(wall))
        draw_rect_wall(buffer, *rwp, x, y, texOffsetBaseX, texOffsetBaseY, scale, color, tex);
    else if (TriWall * twp = dynamic_cast<TriWall *>(wall))
        draw_tri_wall (buffer, *twp, x, y, texOffsetBaseX, texOffsetBaseY, scale, color, tex);
    else if (CircWall* cwp = dynamic_cast<CircWall*>(wall))
        draw_circ_wall(buffer, *cwp, x, y, texOffsetBaseX, texOffsetBaseY, scale, color, tex);
    else
        nAssert(0);
}

void Graphics::draw_rect_wall(BITMAP* buffer, const RectWall& wall, double x0, double y0, int texOffsetBaseX, int texOffsetBaseY, double scale, int color, BITMAP* texture) throw () {
    if (texture)
        drawing_mode(DRAW_MODE_COPY_PATTERN, texture, texOffsetBaseX, texOffsetBaseY);
    rectfill(buffer, iround(x0 + scale * wall.x1()    ), iround(y0 + scale * wall.y1()    ),
                     iround(x0 + scale * wall.x2() - 1), iround(y0 + scale * wall.y2() - 1), color);
    if (texture)
        solid_mode();
}

void Graphics::draw_tri_wall(BITMAP* buffer, const TriWall& wall, double x0, double y0, int texOffsetBaseX, int texOffsetBaseY, double scale, int color, BITMAP* texture) throw () {
    if (texture)
        drawing_mode(DRAW_MODE_COPY_PATTERN, texture, texOffsetBaseX, texOffsetBaseY);
    triangle(buffer,
        iround(x0 + scale * wall.x1()), iround(y0 + scale * wall.y1()),
        iround(x0 + scale * wall.x2()), iround(y0 + scale * wall.y2()),
        iround(x0 + scale * wall.x3()), iround(y0 + scale * wall.y3()), color);
    if (texture)
        solid_mode();
}

void Graphics::draw_circ_wall(BITMAP* buffer, const CircWall& wall, double x0, double y0, int texOffsetBaseX, int texOffsetBaseY, double scale, int color, BITMAP* texture) throw () {
    const double x = wall.X();
    const double y = wall.Y();
    const double ro = wall.radius();
    const double ri = wall.radius_in();
    const double* const angle = wall.angles();
    if (ri == 0 && angle[0] == angle[1]) {  // simple filled circle
        if (texture)
            drawing_mode(DRAW_MODE_COPY_PATTERN, texture, texOffsetBaseX, texOffsetBaseY);
        circlefill(buffer, iround(x0 + scale * x), iround(y0 + scale * y), iround(scale * ro), color);
        if (texture)
            solid_mode();
        return;
    }
    // ring or sector
    Bitmap cbuff = create_bitmap(iround(2 * scale * ro) + 1, iround(2 * scale * ro) + 1);
    nAssert(cbuff);
    const int transparent = bitmap_mask_color(cbuff);
    clear_to_color(cbuff, transparent);
    if (texture)
        drawing_mode(DRAW_MODE_COPY_PATTERN, texture, iround(scale * (ro - x)) + texOffsetBaseX, iround(scale * (ro - y)) + texOffsetBaseY);
    circlefill(cbuff, iround(scale * ro), iround(scale * ro), iround(scale * ro), color);
    if (texture)
        solid_mode();
    if (ri > 0)                     // ring
        circlefill(cbuff, iround(scale * ro), iround(scale * ro), iround(scale * ri - 1), transparent);
    if (angle[0] != angle[1]) {     // sector
        const double vx[] = { wall.angle_vector_1().first, wall.angle_vector_2().first };
        const double vy[] = { wall.angle_vector_1().second, wall.angle_vector_2().second };
        // remove unnecessary   2 1
        // quarters             3 4
        double ang1 = angle[0];
        double ang2 = angle[1];
        if (ang1 >= 90 && (ang1 <= ang2 || ang2 == 0))  // quarter 1
            rectfill(cbuff, iround(scale * ro), 0, iround(scale * 2 * ro), iround(scale * ro), transparent);
        rotate_angle(ang1, 90);
        rotate_angle(ang2, 90);
        if (ang1 >= 90 && (ang1 <= ang2 || ang2 == 0))  // quarter 2
            rectfill(cbuff, 0, 0, iround(scale * ro), iround(scale * ro), transparent);
        rotate_angle(ang1, 90);
        rotate_angle(ang2, 90);
        if (ang1 >= 90 && (ang1 <= ang2 || ang2 == 0))  // quarter 3
            rectfill(cbuff, 0, iround(scale * ro), iround(scale * ro), iround(scale * 2 * ro), transparent);
        rotate_angle(ang1, 90);
        rotate_angle(ang2, 90);
        if (ang1 >= 90 && (ang1 <= ang2 || ang2 == 0))  // quarter 4
            rectfill(cbuff, iround(scale * ro), iround(scale * ro), iround(scale * 2 * ro), iround(scale * 2 * ro), transparent);
        // remove the rest unnecessary sectors of the circle
        const double k = 1.5;
        double diff = angle[1] - angle[0];
        if (diff < 0)
            diff += 360;
        if (vx[0] * vx[1] > 0 && vy[0] * vy[1] > 0 && diff > 90) {  // remove a sector (<90°) between the angles
            triangle(cbuff, iround(scale * ro), iround(scale * ro),
                    iround(scale * (ro + k * vx[0] * ro)), iround(scale * (ro + k * (-vy[0]) * ro)),
                    iround(scale * (ro + k * vx[1] * ro)), iround(scale * (ro + k * (-vy[1]) * ro)), transparent);
        }
        else {                                          // remove sectors between the angles and n·90°
            for (int i = 0; i < 2; i++) {
                int tx, ty;
                if (angle[i] < 90) {
                    tx = 0 + i;
                    ty = 1 - i;
                }
                else if (angle[i] > 270) {
                    tx = -1 + i;
                    ty = 0 + i;
                }
                else if (angle[i] > 180 && angle[i] < 270) {
                    tx = 0 - i;
                    ty = -1 + i;
                }
                else if (angle[i] > 90 && angle[i] < 180) {
                    tx = 1 - i;
                    ty = 0 - i;
                }
                else {
                    tx = 0;
                    ty = 0;
                }
                if (tx != 0 || ty != 0)
                    triangle(cbuff, iround(scale * ro), iround(scale * ro),
                        iround(scale * (ro + k * vx[i] * ro)), iround(scale * (ro + k * (-vy[i]) * ro)),
                        iround(scale * (ro + k * tx * ro)), iround(scale * (ro + k * (-ty) * ro)), transparent);
            }
        }
        // draw back removed lines at n·90°
        if (texture)
            drawing_mode(DRAW_MODE_COPY_PATTERN, texture, iround(scale * (ro - x)) + texOffsetBaseX, iround(scale * (ro - y)) + texOffsetBaseY);
        for (int i = 0; i < 2; i++) {
            if (angle[i] == 0)
                vline(cbuff, iround(scale * ro), iround(scale * (ro - ri)), 0, color);
            else if (angle[i] == 90)
                hline(cbuff, iround(scale * (ro + ri)), iround(scale * ro), iround(scale * 2 * ro), color);
            else if (angle[i] == 180)
                vline(cbuff, iround(scale * ro), iround(scale * (ro + ri)), iround(scale * 2 * ro), color);
            else if (angle[i] == 270)
                hline(cbuff, iround(scale * (ro - ri)), iround(scale * ro), 0, color);
        }
    }
    masked_blit(cbuff, buffer, 0, 0, iround(x0 + scale * (x - ro)), iround(y0 + scale * (y - ro)), cbuff->w, cbuff->h);
    solid_mode();
}

void Graphics::draw_flag(int team, const WorldCoords& pos, bool flash, int alpha, bool emphasize, double timeUnknown) throw () {
    set_trans_mode(alpha);

    ScaledCoordSet sc(pos, this);
    while (sc.next()) {
        const int x = sc.x();
              int y = sc.y();

        if (emphasize && !flash) {
            const int a = pf_scale(20);
            const int d[6] = { -a,3*a, a,3*a, 0,2*a };
            for (int i = 0; i < 4; ++i) {
                const int rev = i & 1, mul = 1 - (i & 2);
                triangle(drawbuf, x + mul * d[0 + rev], y + mul * d[1 - rev], x + mul * d[2 + rev], y + mul * d[3 - rev], x + mul * d[4 + rev], y + mul * d[5 - rev], teamcol[team]);
            }
        }

        const Bitmap& sprite = flash ? flag_flash_sprite[team] : flag_sprite[team];
        if (sprite) {
            const int sx = x - sprite->w / 2, sy = y - sprite->h / 2;
            if (alpha < 255)
                draw_trans_sprite(drawbuf, sprite, sx, sy);
            else
                draw_sprite(drawbuf, sprite, sx, sy);
        }
        else {
            //draw flagpole
            rectfill(drawbuf,
                     x - pf_scale(3),
                     y - pf_scale(20),
                     x + pf_scale(3),
                     y + pf_scale(20),
                     colour[Colour::flag_pole]
                     );
            //draw the flag itself
            rectfill(drawbuf,
                     x,
                     y - pf_scale(18),
                     x + pf_scale(20),
                     y,
                     flash ? teamflashcol[team] : teamcol[team]
                     );
        }
        if (timeUnknown >= 3.)
            print_text_border_centre(itoa(static_cast<int>(timeUnknown)), x, y, teamcol[team], colour[Colour::timer_border], -1);
    }

    solid_mode();
}

// Minimap functions

void Graphics::draw_mini_flag(int team, const Flag& flag, const Map& map, bool flash, bool old) throw () {
    if (!show_minimap)
        return;
    const double px = (flag.position().px * plw + flag.position().x) / (plw * map.w);
    const double py = (flag.position().py * plh + flag.position().y) / (plh * map.h);
    const int pix = minimap_x + static_cast<int>(px * minimap_w);
    const int piy = minimap_y + static_cast<int>(py * minimap_h);
    const int scl = minimap_place_w;
    void (*rectfn)(BITMAP*, int, int, int, int, int) = old ? rect : rectfill;
    rectfn(drawbuf, pix, piy - scl / 32, pix + scl / 160 - 1, piy, colour[Colour::flag_pole]);
    rectfn(drawbuf, pix + 1, piy - scl / 32, pix + scl / 32, piy - scl / 80, flash ? teamflashcol[team] : teamcol[team]);
}

void Graphics::draw_minimap_player(const Map& map, const ClientPlayer& player) throw () {
    if (!show_minimap)
        return;
    const pair<int, int> coords = calculate_minimap_coordinates(map, player);
    const int a = scale(1);
    const int b = a / 2;
    rectfill(drawbuf, coords.first - a, coords.second - a, coords.first + a, coords.second + a, teamcol[player.team()]);
    if (a > 0) // else, only one pixel is shown; let it be the team color
        rectfill(drawbuf, coords.first - b, coords.second - b, coords.first + b, coords.second + b, col[player.color()]);
}

void Graphics::draw_minimap_me(const Map& map, const ClientPlayer& player, double time) throw () {
    if (!show_minimap)
        return;
    const pair<int, int> coords = calculate_minimap_coordinates(map, player);
    if (static_cast<int>(fmod(time * 15, 3)) > 0) {
        circlefill(drawbuf, coords.first, coords.second, scale(2), colour[Colour::map_player_me_1]);
        circlefill(drawbuf, coords.first, coords.second, scale(1), teamlcol[player.team()]);
    }
    else
        circlefill(drawbuf, coords.first, coords.second, scale(2), colour[Colour::map_player_me_2]);
}

pair<int, int> Graphics::calculate_minimap_coordinates(const Map& map, const ClientPlayer& player) const throw () {
    const double px = (player.roomx * plw + player.lx) / static_cast<double>(plw * map.w);
    const double py = (player.roomy * plh + player.ly) / static_cast<double>(plh * map.h);
    const int x = static_cast<int>(px * minimap_w) + minimap_x;
    const int y = static_cast<int>(py * minimap_h) + minimap_y;
    return pair<int, int>(x, y);
}

void Graphics::draw_minimap_room(const Map& map, int rx, int ry, float visibility) throw () {
    if (!show_minimap || visibility >= .99)
        return;
    const int x1 =  rx      * minimap_w / map.w;
    const int y1 =  ry      * minimap_h / map.h;
    const int x2 = (rx + 1) * minimap_w / map.w - 1;
    const int y2 = (ry + 1) * minimap_h / map.h - 1;
    if ((visibility <= 0.01 || min_transp) && minibg_fog)
        blit(minibg_fog, drawbuf, x1, y1, minimap_x + x1, minimap_y + y1, x2 - x1 + 1, y2 - y1 + 1);
    else {
        const int alpha = static_cast<int>(fogOfWarMaxAlpha * (1. - visibility));
        if (alpha > 0) {
            set_trans_mode(alpha);
            rectfill(drawbuf, minimap_x + x1, minimap_y + y1, minimap_x + x2, minimap_y + y2, colour[Colour::map_fog]);
            solid_mode();
        }
    }
}

void Graphics::highlight_minimap_rooms() throw () {
    if (!show_minimap)
        return;

    const WorldCoords& topLeft = roomLayout.topLeftCoords();
    const int map_w = roomLayout.mapWidth();
    const int map_h = roomLayout.mapHeight();
    const double size_x = roomLayout.visibleRoomsX();
    const double size_y = roomLayout.visibleRoomsY();

    const int x0 = minimap_x, y0 = minimap_y;
    const int xm = x0 + minimap_w - 1, ym = y0 + minimap_h - 1;

    double xPos = positiveFmod(topLeft.px + topLeft.x / plw, map_w);
    double yPos = positiveFmod(topLeft.py + topLeft.y / plh, map_h);
    const int x1 = size_x > map_w ? x0 : x0 + static_cast<int>(xPos / map_w * minimap_w);
    const int y1 = size_y > map_h ? y0 : y0 + static_cast<int>(yPos / map_h * minimap_h);
    xPos = positiveFmod(xPos + size_x, map_w);
    yPos = positiveFmod(yPos + size_y, map_h);
    const int x2 = size_x > map_w ? xm : x0 + positiveModulo(static_cast<int>(xPos / map_w * minimap_w) - 1, minimap_w);
    const int y2 = size_y > map_h ? ym : y0 + positiveModulo(static_cast<int>(yPos / map_h * minimap_h) - 1, minimap_h);

    const int col = colour[Colour::map_room_highlight];
    // horizontal borders
    if (size_y <= map_h) {
        if (x2 <= x1) {
            line(drawbuf, x0, y1, x2, y1, col);
            line(drawbuf, x0, y2, x2, y2, col);
            line(drawbuf, x1, y1, xm, y1, col);
            line(drawbuf, x1, y2, xm, y2, col);
        }
        else {
            line(drawbuf, x1, y1, x2, y1, col);
            line(drawbuf, x1, y2, x2, y2, col);
        }
    }
    // vertical borders
    if (size_x <= map_w) {
        if (y2 <= y1) {
            line(drawbuf, x1, y0, x1, y2, col);
            line(drawbuf, x2, y0, x2, y2, col);
            line(drawbuf, x1, y1, x1, ym, col);
            line(drawbuf, x2, y1, x2, ym, col);
        }
        else {
            line(drawbuf, x1, y1, x1, y2, col);
            line(drawbuf, x2, y1, x2, y2, col);
        }
    }
}

void Graphics::update_minimap_background(const Map& map) throw () {
    mapNeedsRedraw = false;
    update_minimap_background(minibg, map);
    if (minibg_fog) {
        blit(minibg, minibg_fog, 0, 0, 0, 0, minibg->w, minibg->h);
        set_trans_mode(fogOfWarMaxAlpha);
        rectfill(minibg_fog, 0, 0, minibg_fog->w - 1, minibg_fog->h - 1, colour[Colour::map_fog]);
        solid_mode();
    }
}

class MinimapHelper {   // small helper solely for Graphics::update_minimap_background
public:
    MinimapHelper(BITMAP* buffer_, double x0_, double y0_, double plw_, double plh_, double scale_) throw ()
        : buffer(buffer_), x0(x0_), y0(y0_), plw(plw_), plh(plh_), scale(scale_) { }

    void paintByFlags(int rx, int ry, const vector<const WorldCoords*>* teamFlags, int* teamColor, int backgroundColor, int colorToPaint) throw ();
    pair<int, int> flagCoords(const WorldCoords& coords) const throw ();

private:
    BITMAP* buffer;
    double x0, y0, plw, plh, scale;
};

// Paint within room (rx,ry) every pixel already of the given colorToPaint with a team or background color according to which color flag or neither is nearest to it.
void MinimapHelper::paintByFlags(int rx, int ry, const vector<const WorldCoords*>* teamFlags, int* teamColor, int backgroundColor, int colorToPaint) throw () {
    const int xmin = static_cast<int>(x0 + plw * scale *  rx         );
    const int xmax = static_cast<int>(x0 + plw * scale * (rx + 1) - 1);
    const int ymin = static_cast<int>(y0 + plh * scale *  ry         );
    const int ymax = static_cast<int>(y0 + plh * scale * (ry + 1) - 1);

    for (int y = ymin; y <= ymax; ++y) {
        const double roomy = (y + 1 - ymin) / scale;
        for (int x = xmin; x <= xmax; ++x) {
            if (getpixel(buffer, x, y) != colorToPaint)
                continue;
            const double roomx = (x + 1 - xmin) / scale;
            double dist2[2] = { INT_MAX, INT_MAX };
            for (int t = 0; t < 2; ++t)
                for (vector<const WorldCoords*>::const_iterator fi = teamFlags[t].begin(); fi != teamFlags[t].end(); ++fi)
                    dist2[t] = min(dist2[t], sqr((*fi)->y - roomy) + sqr((*fi)->x - roomx));
            const double diff = dist2[0] - dist2[1];
            if (diff < -2)
                putpixel(buffer, x, y, teamColor[0]);
            else if (diff > 2)
                putpixel(buffer, x, y, teamColor[1]);
            else
                putpixel(buffer, x, y, backgroundColor);  // don't paint about equally distant pixels
        }
    }
}

pair<int, int> MinimapHelper::flagCoords(const WorldCoords& coords) const throw () {
    // the coords are not rounded because the max coord is already (minimap_w - small safety)
    const int x = static_cast<int>(x0 + (coords.px * plw + coords.x) * scale);
    const int y = static_cast<int>(y0 + (coords.py * plh + coords.y) * scale);
    return pair<int, int>(x, y);
}

void Graphics::update_minimap_background(BITMAP* buffer, const Map& map, bool save_map_pic) throw () {
    if (map.w == 0 || map.h == 0)
        return;

    // Calculate new minimap size.
    if (map.w * 4 * minimap_place_h > map.h * 3 * minimap_place_w) {
        minimap_w = minimap_place_w;
        minimap_h = static_cast<int>(static_cast<double>(minimap_w * map.h * 3) / (map.w * 4.)) + 1;    // add 1 because w should be the relatively smaller one for safety (because it's used in determining 'scale' below)
    }
    else {
        minimap_h = minimap_place_h;
        minimap_w = static_cast<int>(static_cast<double>(minimap_h * map.w * 4) / (map.h * 3.));    // truncate to make sure w is the relatively smaller one
    }

    minimap_x = minimap_place_x + (minimap_place_w - minimap_w) / 2;
    minimap_y = minimap_place_y + (minimap_place_h - minimap_h) / 2;

    const double startx = .005;   // .005 is a safety to make sure we stay within the bitmap even with small calculation errors
    const double starty = .005 * map.h / map.w;   // in proportion, because scale (below) is calculated from x axis, and affects y in a different amount of pixels

    const double maxx = plw * map.w;
    const double maxy = plh * map.h;
    const double scale = (minimap_w - .01) / maxx;  // -.01 is a safety to make sure we stay within the bitmap even with small calculation errors

    SceneAntialiaser scene;

    // add background (noticeable in case maxy < minimap_h)
    scene.setScaling(0, 0, 1.);
    scene.addRectangle(.005, .005, minimap_w - .005, minimap_h - .005, 0);

    scene.setScaling(startx, starty, scale);

    // add floors
    scene.addRectangle(0, 0, maxx, maxy, colour[Colour::map_ground]);

    // add room boundaries
    const double halfPixw = .49999 / scale;
    // vertical boundaries
    scene.addRectangle(0, 0, halfPixw, maxy, 2);    // first boundary on the left is only a 'half' one
    for (int i = 1; i < map.w; i++)
        scene.addRectangle(plw * i - halfPixw, 0, plw * i + halfPixw, maxy, 2);
    scene.addRectangle(maxx - halfPixw, 0, maxx, maxy, 2);  // last boundary on the right is only a 'half' one
    // the same for horizontal boundaries
    scene.addRectangle(0, 0, maxx, halfPixw, 2);
    for (int i = 1; i < map.h; i++)
        scene.addRectangle(0, plh * i - halfPixw, maxx, plh * i + halfPixw, 2);
    scene.addRectangle(0, maxy - halfPixw, maxx, maxy, 2);

    // add walls
    for (int y = 0; y < map.h; y++) {
        const double by = starty + y * plh * scale;
        for (int x = 0; x < map.w; x++) {
            const double bx = startx + x * plw * scale;
            scene.setScaling(bx, by, scale);
            scene.setClipping(0, 0, plw, plh);
            const Room& room = map.room[x][y];
            for (vector<WallBase*>::const_iterator wi = room.readWalls().begin(); wi != room.readWalls().end(); ++wi)
                scene.addWallClipped(*wi, 1);
        }
    }

    // draw
    const int room_border_col = save_map_pic ? colour[Colour::map_pic_room_border] : colour[Colour::map_room_border];
    vector<TextureData> colors;
    TextureData td;
    td.setSolid(colour[Colour::map_ground]); colors.push_back(td);   // ground
    td.setSolid(colour[Colour::map_wall]);   colors.push_back(td);   // walls
    td.setSolid(room_border_col);            colors.push_back(td);   // room boundaries
    Texturizer tex(buffer, 0, 0, colors);
    scene.render(tex);
    tex.finalize();

    // colorize bases
    MinimapHelper helper(buffer, startx, starty, plw, plh, scale);
    for (int ry = 0; ry < map.h; ++ry)
        for (int rx = 0; rx < map.w; ++rx) {
            // collect the flags that are in this room, simultaneously checking for flags that are on a wall pixel (not really but in the picture)
            bool onWall = false;
            vector<const WorldCoords*> teamFlags[2];    // one vector for each team
            for (int t = 0; t < 2; ++t)
                for (vector<WorldCoords>::const_iterator fi = map.tinfo[t].flags.begin(); fi != map.tinfo[t].flags.end(); ++fi) {
                    const WorldCoords& flag = *fi;
                    if (flag.px == rx && flag.py == ry) {
                        teamFlags[t].push_back(&flag);
                        const pair<int, int> coords = helper.flagCoords(flag);
                        if (getpixel(buffer, coords.first, coords.second) != colour[Colour::map_ground])
                            onWall = true;
                    }
                }
            if (teamFlags[0].empty() && teamFlags[1].empty())
                continue;
            if (onWall) {
                // The normal area-connected-to-the-flag paint algorithm can't be used for any flags in this case.
                // Instead, paint every floor pixel in the room with the nearest flag's color.
                helper.paintByFlags(rx, ry, teamFlags, teamdcol, colour[Colour::map_ground], colour[Colour::map_ground]);
                continue;
            }
            // Spread paint from each flag pos to all reachable space in the room (by floodfill).
            // When flags of both teams would fill the same space, paint each pixel with the nearest flag's color.
            bool bothFlags = (!teamFlags[0].empty() && !teamFlags[1].empty());
            for (int t = 0; t < 2; ++t)
                for (vector<const WorldCoords*>::const_iterator fi = teamFlags[t].begin(); fi != teamFlags[t].end(); ++fi) {
                    const WorldCoords& flag = **fi;
                    const pair<int, int> coords = helper.flagCoords(flag);
                    if (getpixel(buffer, coords.first, coords.second) != colour[Colour::map_ground]) // this only happens when the flag has already been fully taken care of
                        continue;
                    if (!bothFlags) {
                        floodfill(buffer, coords.first, coords.second, teamdcol[t]);
                        continue;
                    }
                    const int tempColor = colour[Colour::map_temporary];
                    floodfill(buffer, coords.first, coords.second, tempColor);
                    // check all opposing team's flags for being in the same area
                    const int ot = 1 - t;
                    vector<const WorldCoords*> problemFlags[2];
                    for (vector<const WorldCoords*>::const_iterator fj = teamFlags[ot].begin(); fj != teamFlags[ot].end(); ++fj) {
                        const pair<int, int> coords = helper.flagCoords(**fj);
                        if (getpixel(buffer, coords.first, coords.second) == tempColor)
                            problemFlags[ot].push_back(*fj);
                    }
                    if (!problemFlags[ot].empty()) { // only in this case the more complex repainting needs to be done
                        for (vector<const WorldCoords*>::const_iterator fj = fi; fj != teamFlags[t].end(); ++fj) {
                            const pair<int, int> coords = helper.flagCoords(**fj);
                            if (getpixel(buffer, coords.first, coords.second) == tempColor)
                                problemFlags[t].push_back(*fj);
                        }
                        helper.paintByFlags(rx, ry, problemFlags, teamdcol, colour[Colour::map_ground], tempColor);
                    }
                    else    // just repaint the questionable area with the final color
                        floodfill(buffer, coords.first, coords.second, teamdcol[t]);
                }
        }

    //draw circles (or flags) to flag positions
    const double room_w = plw * scale;
    for (int t = 0; t <= 2; ++t) {
        const vector<WorldCoords>& flags = (t == 2 ? map.wild_flags : map.tinfo[t].flags);
        for (vector<WorldCoords>::const_iterator fi = flags.begin(); fi != flags.end(); ++fi) {
            const pair<int, int> coords = helper.flagCoords(*fi);
            const int px = coords.first, py = coords.second;
            if (save_map_pic) {
                //draw the flagpole
                rectfill(buffer, px, py - static_cast<int>(room_w / 12), px + static_cast<int>(room_w / 60) - 1, py, colour[Colour::flag_pole]);
                //draw the flag
                rectfill(buffer, px + static_cast<int>(room_w / 60), py - static_cast<int>(room_w / 12),
                                 px + static_cast<int>(room_w / 12), py - static_cast<int>(room_w / 30), teamcol[t]);
            }
            else
                circle(buffer, px, py, minimap_place_w / 50, teamcol[t]);
        }
    }
}

void Graphics::draw_neighbor_marker(bool flag, const WorldCoords& pos, int team, bool old) throw () {
    static const int marginDist = 15;
    static const int flagSizeMax = 8, flagSizeMin = 5, playerRadMax = 10, playerRadMin = 5;

    nAssert(flag || !old);

    const double xDelta = roomLayout.distanceFromScreenX(pos.px, pos.x),
                 yDelta = roomLayout.distanceFromScreenY(pos.py, pos.y);

    vector<int> x, y;
    double dist;
    if (xDelta) {
        if (yDelta)
            return;
        if (xDelta < 0)
            x.push_back(roomLayout.x0() + pf_scale(marginDist));
        else
            x.push_back(roomLayout.xMax() - pf_scale(marginDist));
        dist = fabs(xDelta);
        y = roomLayout.scale_y(pos);
    }
    else {
        if (!yDelta)
            return;
        if (yDelta < 0)
            y.push_back(roomLayout.y0() + pf_scale(marginDist));
        else
            y.push_back(roomLayout.yMax() - pf_scale(marginDist));
        dist = fabs(yDelta);
        x = roomLayout.scale_x(pos);
    }
    if (dist >= 1.)
        return;
    CartesianProductIterator coords(x.size(), y.size());
    if (flag) {
        const int flagSize = pf_scale(flagSizeMax - (flagSizeMax - flagSizeMin) * dist);
        while (coords.next()) {
            const int x0 = x[coords.i1()] - flagSize / 2, y0 = y[coords.i2()] - flagSize / 2;
            if (old)
                rect    (drawbuf, x0, y0, x0 + flagSize, y0 + flagSize, teamcol[team]);
            else
                rectfill(drawbuf, x0, y0, x0 + flagSize, y0 + flagSize, teamcol[team]);
        }
    }
    else {
        const double playerRad = playerRadMax - iround((playerRadMax - playerRadMin) * dist);
        while (coords.next())
            circle(drawbuf, x[coords.i1()], y[coords.i2()], pf_scale(playerRad), teamcol[team]);
    }
}

//draws a basic player object
void Graphics::draw_player(const WorldCoords& pos, int team, int colorId, GunDirection gundir, double hitfx, bool item_power, int alpha, double time) throw () {
    nAssert(colorId >= 0 && colorId <= MAX_PLAYERS / 2);

    if (alpha <= 0)
        return;

    int pc1 = teamcol[team];
    int pc2 = col[colorId];
    // blink player when hit
    const double deltafx = hitfx - time;
    if (deltafx > 0) {
        const int add = static_cast<int>(deltafx * 600.0);
        const int r = colour[Colour::player_hit].red() + add;
        const int g = colour[Colour::player_hit].green() + add;
        const int b = colour[Colour::player_hit].blue() + add;
        pc1 = pc2 = makecolBounded(r, g, b);
    }
    else if (item_power && static_cast<int>(fmod(time * 10, 2))) {
        pc1 = colour[Colour::player_power_team];
        pc2 = colour[Colour::player_power_personal];
    }

    BITMAP* sprite;
    if (!gundir || colorId == MAX_PLAYERS / 2)
        sprite = 0;
    else if (item_power && player_sprite_power && static_cast<int>(fmod(time * 10, 2)))
        sprite = player_sprite_power;
    else {
        nAssert(team == 0 || team == 1);
        sprite = player_sprite[team][colorId];
    }

    ScaledCoordSet sc(pos, this);
    while (sc.next()) {
        const int x = sc.x(), y = sc.y();

        if (sprite) {
            if (alpha < 255)
                rotate_trans_sprite(drawbuf, sprite, x, y, gundir.toFixed(), alpha);
            else
                rotate_sprite(drawbuf, sprite, x - sprite->w / 2, y - sprite->h / 2, gundir.toFixed());
        }
        else {
            set_trans_mode(alpha);

            const int player_radius = pf_scale(PLAYER_RADIUS);

            // outer color: team color
            circlefill(drawbuf, x, y, player_radius, pc1);
            // inner color: self color
            circlefill(drawbuf, x, y, player_radius * 2 / 3, pc2);

            if (!!gundir) {
                // gun direction
                const double gdx = cos(gundir.toRad()), gdy = sin(gundir.toRad());
                const int xg0 = pf_scale(gdx * PLAYER_RADIUS), xg1 = pf_scale(gdx * GUNPOINT_RADIUS);
                const int yg0 = pf_scale(gdy * PLAYER_RADIUS), yg1 = pf_scale(gdy * GUNPOINT_RADIUS);
                // draw gun in 3 lines: 1 centered, 1 a pixel left and inwards, 1 a pixel right and inwards
                const int xda = static_cast<int>(( gdy - gdx) * 1.3), yda = static_cast<int>((-gdx - gdy) * 1.3);
                const int xdb = static_cast<int>((-gdy - gdx) * 1.3), ydb = static_cast<int>(( gdx - gdy) * 1.3);
                line(drawbuf, x + xg0      , y + yg0      , x + xg1      , y + yg1      , pc1);
                line(drawbuf, x + xg0 + xda, y + yg0 + yda, x + xg1 + xda, y + yg1 + yda, pc1);
                line(drawbuf, x + xg0 + xdb, y + yg0 + ydb, x + xg1 + xdb, y + yg1 + ydb, pc1);
            }

            solid_mode();
        }
    }
}

void Graphics::draw_me_highlight(const WorldCoords& pos, double size) throw () {
    ScaledCoordSet sc(pos, this);
    while (sc.next())
        if (sc.x() >= roomLayout.x0() && sc.x() <= roomLayout.xMax() && sc.y() >= roomLayout.y0() && sc.y() <= roomLayout.yMax())
            circle(drawbuf, sc.x(), sc.y(), scale((8 * size + 1) * PLAYER_RADIUS), colour[Colour::spawn_highlight]);
}

void Graphics::draw_aim(const Room& room, const WorldCoords& pos, GunDirection gundir, int team) throw () {
    const double minDist = pf_scale(GUNPOINT_RADIUS);
    const double gdx = cos(gundir.toRad()), gdy = sin(gundir.toRad());
    const int maxDist = pf_scale(min<double>(room.genGetTimeTillWall(pos.x, pos.y, gdx, gdy, ROCKET_RADIUS * .1, plw + plh).first, plw + plh)); // cap at plw+plh, which is somewhere outside the screen, to avoid drawing a *very* long line
    if (maxDist < minDist)
        return;

    RoomPosSet r(pos.px, pos.py, this);
    while (r.next()) {
        const int sx1 = r.x(), sy1 = r.y();
        TemporaryClipRect clipRestorer(drawbuf, sx1, sy1, sx1 + pf_scale(plw) - 1, sy1 + pf_scale(plh) - 1, true);
        const int x0 = sx1 + pf_scale(pos.x), y0 = sy1 + pf_scale(pos.y);
        line(drawbuf,
             x0 + static_cast<int>(gdx * minDist), y0 + static_cast<int>(gdy * minDist),
             x0 + static_cast<int>(gdx * maxDist), y0 + static_cast<int>(gdy * maxDist),
             colour[team == 0 ? Colour::aim_line_redteam : Colour::aim_line_blueteam]);
        circlefill(drawbuf,
                   x0 + static_cast<int>(gdx * maxDist),
                   y0 + static_cast<int>(gdy * maxDist),
                   pf_scale(ROCKET_RADIUS * .5),
                   colour[team == 0 ? Colour::aim_dot_redteam : Colour::aim_dot_blueteam]);
    }
}

void Graphics::set_alpha_channel(BITMAP* bitmap, BITMAP* alpha) throw () {
    set_write_alpha_blender();
    drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
    if (alpha)
        draw_trans_sprite(bitmap, alpha, 0, 0);
    else    // maximum alpha level
        rectfill(bitmap, 0, 0, bitmap->w - 1, bitmap->h - 1, 255);
    solid_mode();
}

void Graphics::rotate_trans_sprite(BITMAP* bmp, BITMAP* sprite, int x, int y, fixed angle, int alpha) throw () { // x,y are destination coords of the sprite center
    nAssert(sprite);
    nAssert(sprite->w == sprite->h);    // if otherwise, would have to use max(sprite->w, sprite->h) below, and use more complex coords in rotate
    // make room so that rotating won't clip the corners off
    const int size = sprite->h + sprite->h / 2;
    Bitmap buffer = create_bitmap(size, size);
    nAssert(buffer);
    clear_to_color(buffer, bitmap_mask_color(buffer));
    rotate_sprite(buffer, sprite, sprite->h / 4, sprite->h / 4, angle);
    set_trans_mode(alpha);
    draw_trans_sprite(bmp, buffer, x - buffer->w / 2, y - buffer->h / 2);
    solid_mode();
}

void Graphics::rotate_alpha_sprite(BITMAP* bmp, BITMAP* sprite, int x, int y, fixed angle) throw () {    // x,y are destination coords of the sprite center
    nAssert(bitmap_color_depth(sprite) == 32);
    nAssert(sprite->w == sprite->h);    // if otherwise, would have to use max(sprite->w, sprite->h) below, and use more complex coords in rotate
    // make room so that rotating won't clip the corners off
    const int size = sprite->h + sprite->h / 2;
    Bitmap buffer = create_bitmap_ex(32, size, size);
    nAssert(buffer);
    clear_to_color(buffer, bitmap_mask_color(buffer));
    rotate_sprite(buffer, sprite, sprite->h / 4, sprite->h / 4, angle);
    drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
    set_alpha_blender();
    draw_trans_sprite(bmp, buffer, x - buffer->w / 2, y - buffer->h / 2);
    solid_mode();
}

void Graphics::draw_player_dead(const ClientPlayer& player, double respawn_delay) throw () {
    BITMAP* sprite = dead_sprite[player.team()];
    ScaledCoordSet sc(player.roomx, player.roomy, player.lx, player.ly, this);
    while (sc.next()) {
        const int x = sc.x(), y = sc.y();
        if (sprite) {
            GunDirection gundir = player.gundir;
            if (!gundir)
                gundir.from8way(0);
            rotate_alpha_sprite(drawbuf, sprite, x, y, gundir.toFixed());
        }
        else {
            const int c = colour[player.team() == 0 ? Colour::blood_redteam : Colour::blood_blueteam];
            set_trans_mode(90);
            const int plrScale = pf_scale(PLAYER_RADIUS * 10);
            ellipsefill(drawbuf, x - plrScale / 25, y + plrScale / 20, plrScale / 9, plrScale / 10, c);
            ellipsefill(drawbuf, x                , y - plrScale / 30, plrScale / 8, plrScale / 10, c);
            ellipsefill(drawbuf, x + plrScale / 50, y + plrScale / 40, plrScale / 8, plrScale / 10, c);
            solid_mode();
        }
        if (respawn_delay)
            print_text_border_centre(itoa(static_cast<int>(respawn_delay)), x, y, colour[Colour::name_highlight], colour[Colour::timer_border], -1);
    }
}

void Graphics::draw_virou_sorvete(const WorldCoords& pos, double respawn_delay) throw () {
    ScaledCoordSet sc(pos, this);
    while (sc.next()) {
        const int x = sc.x(), y = sc.y();
        if (ice_cream)
            draw_sprite(drawbuf, ice_cream, x - ice_cream->w / 2, y - ice_cream->h / 2);
        else {
            ellipsefill(drawbuf, x              , y               , pf_scale(6), pf_scale(15), colour[Colour::ice_cream_crisp]);
            circlefill (drawbuf, x - pf_scale(8), y - pf_scale(10), pf_scale(8),               colour[Colour::ice_cream_ball_1]);
            circlefill (drawbuf, x + pf_scale(8), y - pf_scale(10), pf_scale(8),               colour[Colour::ice_cream_ball_2]);
            circlefill (drawbuf, x              , y - pf_scale(20), pf_scale(8),               colour[Colour::ice_cream_ball_3]);
            if (pf_scale(100) >= 50) { // when the text gets too large relative to other things, don't draw (exact criterion arbitrary)
                textout_centre_ex(drawbuf, font, _("VIROU"   ).c_str(), x, y - pf_scale(38) - 10, colour[Colour::ice_cream_text], -1);
                textout_centre_ex(drawbuf, font, _("SORVETE!").c_str(), x, y - pf_scale(38)     , colour[Colour::ice_cream_text], -1);
            }
        }
        if (respawn_delay)
            print_text_border_centre(itoa(static_cast<int>(respawn_delay)), x, y, colour[Colour::name_highlight], colour[Colour::timer_border], -1);
    }
}

void Graphics::draw_gun_explosion(const WorldCoords& pos, int rad, int team) throw () {
    const int r = getr(teamcol[team]);
    const int g = getg(teamcol[team]);
    const int b = getb(teamcol[team]);
    const int c = makecol(rand() % (256 - r / 2) + r / 2,
                          rand() % (256 - g / 2) + g / 2,
                          rand() % (256 - b / 2) + b / 2);
    ScaledCoordSet sc(pos, this);
    while (sc.next())
        circle(drawbuf, sc.x(), sc.y(), pf_scale(rad), c);
}

void Graphics::draw_deathbringer_smoke(const WorldCoords& pos, double time, double alpha) throw () {
    const int effAlpha = static_cast<int>((120 - time * 200.0) * alpha);
    if (effAlpha <= 0 || (min_transp && effAlpha <= 10))
        return;
    int c;
    const double drad = 3.0 + 9.0 * (0.6 - time);
    int rad = pf_scale(drad);
    if (min_transp) {
        const int rgb = 120 - effAlpha;
        c = makecol(rgb, rgb, rgb);
        rad /= 2;
    }
    else {
        c = colour[Colour::deathbringer_smoke];
        set_trans_mode(effAlpha);
    }
    const int subdist = pf_scale(96.0 - drad * 8.0);
    ScaledCoordSet sc(pos, this);
    while (sc.next())
        circlefill(drawbuf, sc.x(), sc.y() - subdist, rad, c);
    solid_mode();
}

void Graphics::draw_deathbringer(const DeathbringerExplosion& db, double frame) throw () {
    const int radius = pf_scale(db.radius(frame));
    const WorldCoords& pos = db.position();
    RoomPosSet r(pos.px, pos.py, this);
    while (r.next()) {
        const int sx1 = r.x(), sy1 = r.y();
        TemporaryClipRect clipRestorer(drawbuf, sx1, sy1, sx1 + pf_scale(plw) - 1, sy1 + pf_scale(plh) - 1, true);
        const int lx = pf_scale(pos.x);
        const int ly = pf_scale(pos.y);
        const int maxxd = max(lx, pf_scale(plw) - lx);
        const int maxyd = max(ly, pf_scale(plh) - ly);
        if (maxxd * maxxd + maxyd * maxyd < radius * radius)
            continue;
        const int x = r.x() + pf_scale(pos.x);
        const int y = r.y() + pf_scale(pos.y);
        int rad = radius;
        //brightening ring
        for (int e = 0; e < pf_scale(30); e++, rad++) {
            const int mul = 14 + static_cast<int>(8 * e / scr_mul);
            const int r = mul * getr(teamcol[db.team()]) / 255;
            const int g = mul * getg(teamcol[db.team()]) / 255;
            const int b = mul * getb(teamcol[db.team()]) / 255;
            const int co = makecol(r, g, b);
            circle(drawbuf, x, y, rad, co);
        }
        //darkening ring
        for (int e = 0; e < pf_scale(10); e++, rad++) {
            const int mul = 255 - static_cast<int>(14 * e / scr_mul);
            const int r = mul * getr(teamcol[db.team()]) / 255;
            const int g = mul * getg(teamcol[db.team()]) / 255;
            const int b = mul * getb(teamcol[db.team()]) / 255;
            const int co = makecol(r, g, b);
            circle(drawbuf, x    , y    , rad, co);
            circle(drawbuf, x + 1, y    , rad, co);
            circle(drawbuf, x    , y + 1, rad, co);
        }
    }
}

void Graphics::draw_deathbringer_affected(const WorldCoords& pos, int team, int alpha) throw () {
    set_trans_mode(alpha / 2);

    ScaledCoordSet sc(pos, this);
    while (sc.next()) {
        const int x = sc.x(), y = sc.y();
        for (int i = 0; i < 5; i++)
            circlefill(drawbuf, x + pf_scale(rand() % 40 - 20), y + pf_scale(rand() % 40 - 20), pf_scale(15), teamcol[team]);
        for (int i = 0; i < 5; i++)
            circlefill(drawbuf, x + pf_scale(rand() % 40 - 20), y + pf_scale(rand() % 40 - 20), pf_scale(15), colour[Colour::deathbringer_affected]);
    }

    solid_mode();
}

void Graphics::draw_deathbringer_carrier_effect(const WorldCoords& pos, int alpha) throw () {
    if (min_transp)
        return;
    BITMAP* buffer;
    if (alpha == 255)
        buffer = db_effect;
    else {
        buffer = create_bitmap_ex(32, db_effect->w, db_effect->h);
        nAssert(buffer);
        clear_to_color(buffer, colour[Colour::deathbringer_carrier_circle]);
        // recalculate alpha channel multiplying in the current alpha
        set_write_alpha_blender();
        drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
        for (int y = 0; y < buffer->h; y++)
            for (int x = 0; x < buffer->w; x++) {
                const int a = geta(getpixel(db_effect, x, y));
                const int new_alpha = a * alpha / 255;
                putpixel(buffer, x, y, new_alpha);
            }
        solid_mode();
    }
    drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
    set_alpha_blender();

    ScaledCoordSet sc(pos, this);
    while (sc.next())
        draw_trans_sprite(drawbuf, buffer, sc.x() - db_effect->w / 2, sc.y() - db_effect->h / 2);

    solid_mode();
    if (buffer != db_effect)
        destroy_bitmap(buffer);
}

void Graphics::draw_shield(const WorldCoords& pos, int r, bool live, int alpha, int team, GunDirection direction) throw () {
    r = pf_scale(r);
    BITMAP* const sprite = team == 0 || team == 1 ? static_cast<BITMAP*>(shield_sprite[team]) : 0;

    static int rx[3] = { 0, 0, 0 }, ry[3] = { 0, 0, 0 }; // being static results in all shields looking the same when !live, but that's not really serious
    if (!sprite && live) {
        const int v[] = { pf_scale(2) + 1, pf_scale(4) + 1, pf_scale(8) + 1 };
        for (int i = 0; i < 3; ++i) {
            rx[i] = rand() % v[i];
            ry[i] = rand() % v[i];
        }
    }

    ScaledCoordSet sc(pos, this);
    while (sc.next()) {
        const int x = sc.x(), y = sc.y();

        if (sprite) {
            if (alpha < 255)
                rotate_trans_sprite(drawbuf, sprite, x, y, direction.toFixed(), alpha);
            else
                rotate_sprite(drawbuf, sprite, x - sprite->w / 2, y - sprite->h / 2, direction.toFixed());
            continue;
        }
        else {
            set_trans_mode(alpha);
            if (team == 0 || team == 1)
                for (int i = 0; i < 3; i++) {
                    const int red = getr(teamcol[team]) + rand() % (256 - getr(teamcol[team]));
                    const int green = getg(teamcol[team]) + rand() % (256 - getg(teamcol[team]));
                    const int blue = getb(teamcol[team]) + rand() % (256 - getb(teamcol[team]));
                    ellipse(drawbuf, x, y, r + rx[i], r + ry[i], makecol(red, green, blue));
                }
            else    // powerup lying on the ground
                for (int i = 0; i < 3; i++)
                    ellipse(drawbuf, x, y, r + rx[i], r + ry[i], makecol(rand() % 256, rand() % 256, rand() % 256));
            solid_mode();
        }
    }
}

void Graphics::draw_player_name(const string& name, const WorldCoords& pos, int team, bool highlight) throw () {
    const int c = highlight ? colour[Colour::name_highlight] : colour[Colour::name];
    ScaledCoordSet sc(pos, this);
    while (sc.next())
        print_text_border_centre(name,
                                 sc.x(),
                                 sc.y() - pf_scale(3 * PLAYER_RADIUS / 2) - text_height(font),
                                 c, teamdcol[team], -1);
}

void Graphics::draw_rocket(const Rocket& rocket, bool shadow, double time) throw () {
    BITMAP* const sprite = (rocket.power ? power_rocket_sprite[rocket.team] : rocket_sprite[rocket.team]);
    ScaledCoordSet sc(rocket.px, rocket.py, rocket.x, rocket.y, this);
    while (sc.next()) {
        const int x = sc.x(), y = sc.y();
        if (sprite)
            rotate_sprite(drawbuf, sprite, x - sprite->w / 2, y - sprite->h / 2, rocket.direction.toFixed());
        else if (rocket.power) {
            //draw rocket shadow
            if (shadow)
                ellipsefill(drawbuf, x, y + pf_scale(POWER_ROCKET_RADIUS + 8), pf_scale(POWER_ROCKET_RADIUS), pf_scale(3), colour[Colour::rocket_shadow]);
            //draw the rocket
            if (static_cast<int>(fmod(time * 30, 2)))
                circlefill(drawbuf, x, y, pf_scale(POWER_ROCKET_RADIUS), colour[Colour::power_rocket]);
            else
                circlefill(drawbuf, x, y, pf_scale(POWER_ROCKET_RADIUS), teamlcol[rocket.team]);
        }
        else {
            //draw rocket shadow
            if (shadow)
                ellipsefill(drawbuf, x, y + pf_scale(ROCKET_RADIUS + 8), pf_scale(ROCKET_RADIUS), pf_scale(2), colour[Colour::rocket_shadow]);
            //draw the rocket
            circlefill(drawbuf, x, y, pf_scale(ROCKET_RADIUS), teamcol[rocket.team]);
        }
    }
}

void Graphics::draw_pup(const Powerup& pup, double time, bool live) throw () {
    nAssert(pup.kind >= 0 && pup.kind < static_cast<int>(pup_sprite.size()));
    BITMAP* const sprite = pup_sprite[pup.kind];
    const WorldCoords pos(pup.px, pup.py, pup.x, pup.y);
    if (sprite) {
        ScaledCoordSet sc(pos, this);
        while (sc.next())
            draw_sprite(drawbuf, sprite, sc.x() - sprite->w / 2, sc.y() - sprite->h / 2);
    }
    else
        switch (pup.kind) {
        /*break;*/ case Powerup::pup_shield:       draw_pup_shield      (pos, live);
            break; case Powerup::pup_turbo:        draw_pup_turbo       (pos, live);
            break; case Powerup::pup_shadow:       draw_pup_shadow      (pos, time);
            break; case Powerup::pup_power:        draw_pup_power       (pos, time);
            break; case Powerup::pup_weapon:       draw_pup_weapon      (pos, time);
            break; case Powerup::pup_health:       draw_pup_health      (pos, time);
            break; case Powerup::pup_deathbringer: draw_pup_deathbringer(pos, time, live);
            break; default: nAssert(0);
        }
}

void Graphics::draw_pup_shield(const WorldCoords& pos, bool live) throw () {
    draw_shield(pos, 14, live);
    ScaledCoordSet sc(pos, this);
    while (sc.next())
        circlefill(drawbuf, sc.x(), sc.y(), pf_scale(12), colour[Colour::pup_shield]);
}

void Graphics::draw_pup_turbo(const WorldCoords& pos, bool live) throw () {
    const int r = pf_scale(12);
    static int rx[3] = { 0, 0, 0 }, ry[3] = { 0, 0, 0 }; // being static results in all turbos looking the same when !live, but that's not really serious
    if (live) {
        rx[0] = rand() % 6 - 3; rx[1] = rand() % 8 - 4; rx[2] = rand() % 12 - 6;
        ry[0] = rand() % 6 - 3; ry[1] = rand() % 8 - 4; ry[2] = rand() % 12 - 6;
    }
    ScaledCoordSet sc(pos, this);
    while (sc.next()) {
        const int x = sc.x(), y = sc.y();
        circlefill(drawbuf, x + pf_scale(rx[0]), y + pf_scale(ry[0]), r, colour[Colour::pup_turbo_1]);
        circlefill(drawbuf, x + pf_scale(rx[1]), y + pf_scale(ry[1]), r, colour[Colour::pup_turbo_2]);
        circlefill(drawbuf, x + pf_scale(rx[2]), y + pf_scale(ry[2]), r, colour[Colour::pup_turbo_3]);
    }
}

void Graphics::draw_pup_shadow(const WorldCoords& pos, double time) throw () {
    int alpha = static_cast<int>(fmod(time * 600.0, 400));
    if (alpha > 200)
        alpha = 400 - alpha;
    set_trans_mode(55 + alpha);
    ScaledCoordSet sc(pos, this);
    while (sc.next())
        circlefill(drawbuf, sc.x(), sc.y(), pf_scale(12), colour[Colour::pup_shadow]);
    solid_mode();
}

void Graphics::draw_pup_power(const WorldCoords& pos, double time) throw () {
    ScaledCoordSet sc(pos, this);
    while (sc.next()) {
        if (static_cast<int>(fmod(time * 30, 2)))
            circlefill(drawbuf, sc.x(), sc.y(), pf_scale(13), colour[Colour::pup_power_1]);
        else
            circlefill(drawbuf, sc.x(), sc.y(), pf_scale(11), colour[Colour::pup_power_2]);
    }
}

void Graphics::draw_pup_weapon(const WorldCoords& pos, double time) throw () {
    for (int b = 0; b < 4; b++) {
        // deg: 0..360
        double deg = static_cast<int>(fmod(time * 1000, 1000)) / 1000.;
        deg *= 2 * N_PI;    // 360°
        deg += N_PI_2 * b;  // 90°

        // position
        const int dx = pf_scale(10 * cos(deg));
        const int dy = pf_scale(10 * sin(deg));

        int c;
        switch (b) {
        /*break;*/ case 0: c = colour[Colour::pup_weapon_1];
            break; case 1: c = colour[Colour::pup_weapon_2];
            break; case 2: c = colour[Colour::pup_weapon_3];
            break; case 3: c = colour[Colour::pup_weapon_4];
            break; default: nAssert(0);
        }
        ScaledCoordSet sc(pos, this);
        while (sc.next())
            circlefill(drawbuf, sc.x() + dx, sc.y() + dy, pf_scale(4), c);
    }
}

void Graphics::draw_pup_health(const WorldCoords& pos, double time) throw () {
    int varia = static_cast<int>(fmod(time * 15, 10));
    if (varia > 5)
        varia = 10 - varia;
    const int itemsize = pf_scale(11 + varia);
    const int crossize = pf_scale(8 + varia);
    const int crosslar = pf_scale(3);
    const int border = max(1, pf_scale(2));
    ScaledCoordSet sc(pos, this);
    while (sc.next()) {
        const int x = sc.x(), y = sc.y();
        // health box black border
        rectfill(drawbuf, x - itemsize - border, y - itemsize - border, x + itemsize + border, y + itemsize + border, colour[Colour::pup_health_border]);
        // health box
        rectfill(drawbuf, x - itemsize, y - itemsize, x + itemsize, y + itemsize, colour[Colour::pup_health_bg]);
        // red cross
        rectfill(drawbuf, x - crossize, y - crosslar, x + crossize, y + crosslar, colour[Colour::pup_health_cross]);
        rectfill(drawbuf, x - crosslar, y - crossize, x + crosslar, y + crossize, colour[Colour::pup_health_cross]);
    }
}

void Graphics::draw_pup_deathbringer(const WorldCoords& pos, double time, bool live) throw () {
    ScaledCoordSet sc(pos, this);
    while (sc.next())
        circlefill(drawbuf, sc.x(), sc.y(), pf_scale(12), colour[Colour::pup_deathbringer]);
    if (live)
        create_deathcarrier(pos, 255, time, true);
}

void Graphics::draw_waiting_map_message(const string& caption, const string& map) throw () {
    print_text_border_centre_check_bg(caption, playfield_x + scale(plw) / 2, playfield_y + scale(plh / 2 + 20), colour[Colour::map_loading_1], colour[Colour::text_border], -1);
    print_text_border_centre_check_bg(map, playfield_x + scale(plw) / 2, playfield_y + scale(plh / 2 + 50), colour[Colour::map_loading_2], colour[Colour::text_border], -1);
}

void Graphics::draw_loading_map_message(const string& text) throw () {
    print_text_border_centre_check_bg(text, playfield_x + scale(plw / 2), playfield_y + scale(plh / 2 + 70), colour[Colour::map_loading_1], colour[Colour::text_border], -1);
}

void Graphics::draw_scores(const string& text, int team, int score1, int score2) throw () {
    const int c = team == 0 || team == 1 ? teamlcol[team] : colour[Colour::game_draw];
    print_text_border_centre_check_bg(text, playfield_x + scale(plw / 2), playfield_y + scale(plh / 2 - 40), c, colour[Colour::text_border], -1);
    print_text_border_centre_check_bg(_("SCORE $1 - $2", itoa(score1), itoa(score2)), playfield_x + scale(plw / 2), playfield_y + scale(plh / 2 - 20), c, colour[Colour::text_border], -1);
}

void Graphics::draw_scoreboard(const vector<ClientPlayer*>& players, const Team* teams, int maxplayers, bool pings, bool underlineMasterAuthenticated, bool underlineServerAuthenticated) throw () {
    if (!show_scoreboard) {     // show only the team scores in full screen mode
        int c;
        if (teams[0].score() > teams[1].score())
            c = colour[Colour::team_red_light];
        else if (teams[0].score() < teams[1].score())
            c = colour[Colour::team_blue_light];
        else
            c = colour[Colour::game_draw];
        ostringstream ost;
        ost << teams[0].score() << " - " << teams[1].score();
        print_text_border_check_bg(ost.str(), time_x - text_length(font, ost.str()), time_y + text_height(font), c, colour[Colour::text_border], -1);
        return;
    }

    const int place_width = scoreboard_x2 - scoreboard_x1 + 1;
    const int y_space = scoreboard_y2 - scoreboard_y1 + 1;
    const int lines = 2 + maxplayers + 1;   // caption for each team, players, FPS  //#fixme: could the room for FPS be reserved someplace else...
    const int characters = 20;

    const FONT* sbfont;
    if (font != default_font && text_height(font) > text_height(default_font) && (lines * text_height(font) > y_space || characters * text_length(font, "M") > place_width))
        sbfont = default_font;
    else
        sbfont = font;

    // calculate the position in the given space
    const int char_w = text_length(sbfont, "M");
    const int width = characters * char_w;
    const int sby = scoreboard_y1;
    int sbx;
    if (place_width > width)
        sbx = scoreboard_x1 + (place_width - width) / 2;
    else
        sbx = scoreboard_x1;    // just let it go off the screen if it doesn't fit; it's not critical

    const int nSpacers = 3;  // top, between teams, before FPS
    int spacerHeight = 3;   // consider 3 a minimal spacing (we're lucky that with SCREEN_H = 480, 32 players at line_h = 8, spacerHeight = 3 fills ALL lines!)
    // find optimal line height
    int line_h = (y_space - nSpacers * spacerHeight) / lines;
    if (line_h > text_height(sbfont)) {
        spacerHeight = text_height(sbfont) + 2;  // prefer this over extra line height
        line_h = (y_space - nSpacers * spacerHeight) / lines;
        if (line_h > 2 * text_height(sbfont))
            line_h = 2 * text_height(sbfont);
    }
    // expand spacing if possible
    const int extraSpace = y_space - nSpacers * spacerHeight - line_h * lines;
    spacerHeight += extraSpace / nSpacers;
    if (spacerHeight > 15)
        spacerHeight = 15;

    const int teamy[2] = { sby + spacerHeight, sby + spacerHeight + (1 + maxplayers / 2) * line_h + spacerHeight };

    const int caption_width = characters;

    // background
    const int teambg [2] = { colour[Colour::scoreboard_bg_redteam], colour[Colour::scoreboard_bg_blueteam] };
    const int linecol[2] = { colour[Colour::scoreboard_line_redteam], colour[Colour::scoreboard_line_blueteam] };
    int hpadding = 6;
    if (sbx - scoreboard_x1 < hpadding)
        hpadding = max(1, sbx - scoreboard_x1 - 2);
    const int vpadding = max((line_h - text_height(sbfont)) / 2, 1);
    const int border_x1 = sbx - hpadding;
    const int border_x2 = sbx + width + hpadding;
    for (int i = 0; i < 2; ++i) {
        rectfill(drawbuf, border_x1 + 1, teamy[i] + line_h - vpadding + 1, border_x2 - 1, teamy[i] + (maxplayers / 2 + 1) * line_h - vpadding - 1, teambg[i]);
        rectfill(drawbuf, border_x1 + 1, teamy[i] - vpadding + 1, border_x2 - 1, teamy[i] + line_h - vpadding - 1, teamdcol[i]);
    }
    for (int i = 0; i < 2; ++i) {
        hline(drawbuf, border_x1 + 1, teamy[i] - vpadding, border_x2 - 1, linecol[i]);
        for (int j = 1; j <= maxplayers / 2 + 1; ++j)
            hline(drawbuf, border_x1 + 1, teamy[i] + j * line_h - vpadding, border_x2 - 1, linecol[i]);
    }
    for (int i = 0; i < 2; ++i) {
        vline(drawbuf, border_x1, teamy[i] - vpadding, teamy[i] + (maxplayers / 2 + 1) * line_h - vpadding, linecol[i]);
        vline(drawbuf, sbx + (caption_width - 3) * char_w - 5, teamy[i] + line_h - vpadding, teamy[i] + (maxplayers / 2 + 1) * line_h - vpadding, linecol[i]);
        vline(drawbuf, border_x2, teamy[i] - vpadding, teamy[i] + (maxplayers / 2 + 1) * line_h - vpadding, linecol[i]);
    }

    // captions
    const int textcol[2] = { colour[Colour::scoreboard_caption_redteam], colour[Colour::scoreboard_caption_blueteam] };
    const string teamName[2] = { _("Red Team"), _("Blue Team") };
    for (int team = 0; team < 2; ++team) {
        ostringstream os;
        os << teamName[team];
        if (pings)
            os << setw(caption_width - teamName[team].length()) << _("pings");
        else
            os << setw(caption_width - teamName[team].length()) << _("$1 capt", itoa_w(teams[team].score(), 3));
        textout_ex(drawbuf, sbfont, os.str().c_str(), sbx, teamy[team], textcol[team], -1);
    }

    int line[2] = { 1, 1 };
    for (vector<ClientPlayer*>::const_iterator pi = players.begin(); pi != players.end(); ++pi) {
        const ClientPlayer& player = **pi;
        const int pcol = col[player.color()];
        const int r = 40 + getr(pcol) * 215 / 255;
        const int g = 40 + getg(pcol) * 215 / 255;
        const int b = 40 + getb(pcol) * 215 / 255;
        const int textcol = makecol(r, g, b);
        const int x = sbx;
        const int y = teamy[player.team()] + line[player.team()] * line_h;
        const bool underline = (underlineMasterAuthenticated && player.reg_status.masterAuth()) || (underlineServerAuthenticated && player.reg_status.localAuth());
        draw_scoreboard_name(sbfont, player.name.substr(0, 15), x + 8, y, textcol, underline);
        draw_scoreboard_points(sbfont, pings ? player.ping : player.stats().frags(), x + 20 * char_w, y, player.team());
        if (player.stats().has_flag()) {
            // pole height 7, flag width 4 and height 4 units
            const int flag = text_length(sbfont, "M") / 2;
            const int pole = 7 * flag / 4;
            const int starty = y + (text_height(sbfont) - pole) / 2;
            vline(drawbuf, x, starty, starty + pole, colour[Colour::flag_pole]);
            rectfill(drawbuf, x + 1, starty, x + 1 + flag, starty + flag, player.stats().has_wild_flag() ? teamcol[2] : teamcol[1 - player.team()]);
        }
        line[player.team()]++;
    }
}

void Graphics::draw_scoreboard_name(const FONT* sbfont, const string& name, int x, int y, int pcol, bool underline) throw () {
    if (underline)
        hline(drawbuf, x, y + text_height(font), x + text_length(font, name) - 2, pcol);
    textout_ex(drawbuf, sbfont, name.c_str(), x, y, pcol, -1);
}

void Graphics::draw_scoreboard_points(const FONT* sbfont, int points, int x, int y, int team) throw () {
    textprintf_right_ex(drawbuf, sbfont, x, y, teamlcol[team], -1, "%d", points);
}

void Graphics::team_statistics(const Team* teams) throw () {
    const FONT* stfont = font;
    const int total_captures = static_cast<int>(teams[0].captures().size() + teams[1].captures().size());
    int line_height = text_height(stfont) + 4;
    if (20 * line_height > SCREEN_H) {
        line_height = SCREEN_H / 20;
        if (line_height < text_height(stfont) && text_height(default_font) < text_height(stfont)) {
            stfont = default_font;
            line_height = min(line_height, text_height(stfont) + 4);
        }
    }
    const int w = 43 * text_length(stfont, "M") + 6;
    const int h = min<int>(SCREEN_H, (20 + total_captures) * line_height);
    const int mx = SCREEN_W / 2;
    const int my = SCREEN_H / 2;
    const int x1 = mx - w / 2;
    const int y1 = my - h / 2;
    const int x2 = mx + w / 2;
    const int y2 = my + h / 2;

    if (stats_alpha > 0) {
        set_trans_mode(stats_alpha);
        rectfill(drawbuf, x1, y1, x2, y2, colour[Colour::stats_bg]);
        // caption backgrounds
        int line = 1;
        rectfill(drawbuf, x1, y1 + line * line_height - 4, x2, y1 + (line + 1) * line_height, colour[Colour::stats_caption_bg]);
        line += 2;
        rectfill(drawbuf, x1, y1 + line * line_height - 4, mx - 1, y1 + (line + 1) * line_height, teamdcol[0]);
        rectfill(drawbuf, mx, y1 + line * line_height - 4, x2, y1 + (line + 1) * line_height, teamdcol[1]);
        solid_mode();
    }

    const int c = colour[Colour::stats_text];
    int line = 1;
    textout_centre_ex(drawbuf, stfont, _("Team stats").c_str(), mx               , y1 + line * line_height, c, -1);
    line += 2;
    textout_centre_ex(drawbuf, stfont, _("Red Team"  ).c_str(), (3 * x1 + x2) / 4, y1 + line * line_height, c, -1);
    textout_centre_ex(drawbuf, stfont, _("Blue Team" ).c_str(), (x1 + 3 * x2) / 4, y1 + line * line_height, c, -1);

    const int start_line = line + 2;
    line = start_line;
    textout_centre_ex(drawbuf, stfont, _("Captures"      ).c_str(), mx, y1 + line++ * line_height, c, -1);
    textout_centre_ex(drawbuf, stfont, _("Kills"         ).c_str(), mx, y1 + line++ * line_height, c, -1);
    textout_centre_ex(drawbuf, stfont, _("Deaths"        ).c_str(), mx, y1 + line++ * line_height, c, -1);
    textout_centre_ex(drawbuf, stfont, _("Suicides"      ).c_str(), mx, y1 + line++ * line_height, c, -1);
    textout_centre_ex(drawbuf, stfont, _("Flags taken"   ).c_str(), mx, y1 + line++ * line_height, c, -1);
    textout_centre_ex(drawbuf, stfont, _("Flags dropped" ).c_str(), mx, y1 + line++ * line_height, c, -1);
    textout_centre_ex(drawbuf, stfont, _("Flags returned").c_str(), mx, y1 + line++ * line_height, c, -1);
    textout_centre_ex(drawbuf, stfont, _("Shots"         ).c_str(), mx, y1 + line++ * line_height, c, -1);
    textout_centre_ex(drawbuf, stfont, _("Hit accuracy"  ).c_str(), mx, y1 + line++ * line_height, c, -1);
    textout_centre_ex(drawbuf, stfont, _("Shots taken"   ).c_str(), mx, y1 + line++ * line_height, c, -1);
    textout_centre_ex(drawbuf, stfont, _("Movement"      ).c_str(), mx, y1 + line++ * line_height, c, -1);
    //textout_centre_ex(drawbuf, stfont, _("Team power"    ).c_str(), mx, y1 + line++ * line_height, c, -1);

    for (int t = 0; t < 2; t++) {
        const Team& team = teams[t];
        line = start_line;
        const int x = (t == 0 ? 4 * x1 + x2 : x1 + 4 * x2) / 5;
        textprintf_centre_ex(drawbuf, stfont, x, y1 + line++ * line_height, teamlcol[t], -1, "%d", team.score());
        textprintf_centre_ex(drawbuf, stfont, x, y1 + line++ * line_height, teamlcol[t], -1, "%d", team.kills());
        textprintf_centre_ex(drawbuf, stfont, x, y1 + line++ * line_height, teamlcol[t], -1, "%d", team.deaths());
        textprintf_centre_ex(drawbuf, stfont, x, y1 + line++ * line_height, teamlcol[t], -1, "%d", team.suicides());
        textprintf_centre_ex(drawbuf, stfont, x, y1 + line++ * line_height, teamlcol[t], -1, "%d", team.flags_taken());
        textprintf_centre_ex(drawbuf, stfont, x, y1 + line++ * line_height, teamlcol[t], -1, "%d", team.flags_dropped());
        textprintf_centre_ex(drawbuf, stfont, x, y1 + line++ * line_height, teamlcol[t], -1, "%d", team.flags_returned());
        textprintf_centre_ex(drawbuf, stfont, x, y1 + line++ * line_height, teamlcol[t], -1, "%d", team.shots());
        textout_centre_ex(drawbuf, stfont, _("$1%", fcvt(100. * team.accuracy(), 0)), x, y1 + line++ * line_height, teamlcol[t], -1);
        textprintf_centre_ex(drawbuf, stfont, x, y1 + line++ * line_height, teamlcol[t], -1, "%d", team.shots_taken());
        textout_centre_ex(drawbuf, stfont, fcvt(team.movement() / (2 * PLAYER_RADIUS), 0), x, y1 + line++ * line_height, teamlcol[t], -1);
        //textout_centre_ex(drawbuf, stfont, fcvt(team.power(), 2), x, y1 + line++ * line_height, teamlcol[t], -1);
    }

    line++;
    const int team_captures_start_y = y1 + line * line_height;
    const int team_captures_size = (y1 + h - team_captures_start_y) / line_height - 1;

    if (team_captures_start >= total_captures - team_captures_size)
        team_captures_start = total_captures - team_captures_size;
    if (team_captures_start < 0)
        team_captures_start = 0;

    int red_score = teams[0].base_score(), blue_score = teams[1].base_score();
    int pos = 0;

    for (vector<pair<int, string> >::const_iterator red = teams[0].captures().begin(), blue = teams[1].captures().begin(); ; pos++) {
        const bool skip = (pos < team_captures_start || pos >= team_captures_start + team_captures_size);
        ostringstream message;
        int color = 0;
        if (red != teams[0].captures().end() && (blue == teams[1].captures().end() || red->first <= blue->first)) {
            ++red_score;
            if (!skip) {
                color = teamlcol[0];
                message << setw(3) << red->first / 60 << ':' << setw(2) << setfill('0') << red->first % 60 << setfill(' ');
                message << setw(3) << red_score << " - " << left << setw(3) << blue_score << right;
                message << red->second;
            }
            ++red;
        }
        else if (blue != teams[1].captures().end() && (red == teams[0].captures().end() || blue->first <= red->first)) {
            ++blue_score;
            if (!skip) {
                color = teamlcol[1];
                message << setw(3) << blue->first / 60 << ':' << setw(2) << setfill('0') << blue->first % 60 << setfill(' ');
                message << setw(3) << red_score << " - " << left << setw(3) << blue_score << right;
                message << blue->second;
            }
            ++blue;
        }
        else
            break;
        if (!skip)
            textout_ex(drawbuf, stfont, message.str().c_str(), x1 + 30, y1 + line++ * line_height, color, -1);
    }
    // draw scrollbar if there are more captures than visible on the screen
    if (team_captures_size < total_captures) {
        const int x = x2 - 30;
        const int y = team_captures_start_y;
        const int height = team_captures_size * line_height;
        const int bar_y = static_cast<int>(static_cast<double>(height * team_captures_start) / total_captures + 0.5);
        const int bar_h = static_cast<int>(static_cast<double>(height * team_captures_size ) / total_captures + 0.5);
        scrollbar(x, y, height, bar_y, bar_h, colour[Colour::scrollbar], colour[Colour::scrollbar_bg]);
    }
}

void Graphics::draw_statistics(const vector<ClientPlayer*>& players, int page, int time, int maxplayers, int max_world_rank) throw () {
    // line usage: 1 blank, red team (2 captions, 1 blank, 1 for every player), 1 blank, blue team, 1 page num
    const int num_lines = maxplayers + 3 + 2 * 3;
    const FONT* stfont;
    // stats screen works only with monospace font
    if (text_length(font, "i") != text_length(font, "M") || 67 * text_length(font, "M") > SCREEN_W ||
                                                            num_lines * text_height(font) > SCREEN_H)
        stfont = default_font;
    else
        stfont = font;
    const int line_h = min(text_height(stfont) + 4, SCREEN_H / num_lines);   // Preferred line height is 12 with default font.
    const int h = num_lines * line_h;
    const int w = 67 * text_length(stfont, "M") + 4;
    const int x_margin = 22;
    const int mx = SCREEN_W / 2;
    const int my = SCREEN_H / 2;
    const int x1 = mx - w / 2;
    const int y1 = my - h / 2;
    const int x2 = mx + w / 2;
    const int y2 = my + h / 2;
    const int x_left = x1 + x_margin;
    const int team1y = y1 + line_h, team2y = team1y + (3 + maxplayers / 2 + 1) * line_h, pageNumY = team2y + (3 + maxplayers / 2) * line_h;

    if (stats_alpha > 0) {
        set_trans_mode(stats_alpha);
        rectfill(drawbuf, x1, y1, x2, y2, colour[Colour::stats_bg]);
        // caption backgrounds
        rectfill(drawbuf, x1, team1y - 4, x2, team1y + 2 * line_h - 1, teamdcol[0]);
        rectfill(drawbuf, x1, team2y - 4, x2, team2y + 2 * line_h - 1, teamdcol[1]);
        solid_mode();
    }

    // space for (w - 2 * margin) / char_w == 62 characters per line; 4 + 1 + 16 + 1 used for login and name with spacing; 40 characters for stats
    string caption1, caption2;
    switch (page) {
        // max length:              |........................................|
    /*break;*/ case 0: caption1 = _("     Frags    total/in-row/most");
                       caption2 = _("Ping     Capt Kills      Deaths Suicides");
                       //           |0000  000 00 000/00/00  000/00/00   00  |
        break; case 1: caption1 = _("         Flags          Carriers  Carry");
                       caption2 = _("taken dropped returned   killed    time");
                       //           |  00      00      00       00     00:00 |
        break; case 2: caption1 = _("   Accuracy");
                       caption2 = _("Shots   |  Taken  Movement     Speed");
                       //           |00000  100% 0000  000000 u  00.00 u/s   |
        break; case 3: caption1 = _("          Average        Tournament");
                       caption2 = _("Playtime lifetime    rank  power  score");
                       //           |00000 min   00:00    0000  00.00 -00000 |
        break; default: nAssert(0);
    }
    int red_count = 0, blue_count = 0;
    for (vector<ClientPlayer*>::const_iterator pi = players.begin(); pi != players.end(); ++pi)
        if ((*pi)->team() == 0)
            ++red_count;
        else
            ++blue_count;
    const string red_name  = _("Red Team" ) + " (" + itoa(red_count ) + ')';
    const string blue_name = _("Blue Team") + " (" + itoa(blue_count) + ')';
    const int x_capt = x_left + 22 * text_length(stfont, "M");
    const int c = colour[Colour::stats_text];
    textout_ex(drawbuf, stfont, red_name.c_str() , x_left, team1y + line_h / 2, c, -1);
    textout_ex(drawbuf, stfont, caption1.c_str() , x_capt, team1y             , c, -1);
    textout_ex(drawbuf, stfont, caption2.c_str() , x_capt, team1y + line_h    , c, -1);
    textout_ex(drawbuf, stfont, blue_name.c_str(), x_left, team2y + line_h / 2, c, -1);
    textout_ex(drawbuf, stfont, caption1.c_str() , x_capt, team2y             , c, -1);
    textout_ex(drawbuf, stfont, caption2.c_str() , x_capt, team2y + line_h    , c, -1);

    int i = 0;
    int teamLineY[2] = { team1y + 3 * line_h, team2y + 3 * line_h };
    for (vector<ClientPlayer*>::const_iterator pi = players.begin(); pi != players.end(); ++pi, ++i) {
        const ClientPlayer& player = **pi;
        draw_player_statistics(stfont, player, x_left, teamLineY[player.team()], page, time);
        teamLineY[player.team()] += line_h;
    }

    if (page == 3 && max_world_rank > 0)
        textout_ex(drawbuf, stfont, _("$1 players in the tournament.", itoa(max_world_rank)).c_str(), x_left, pageNumY, colour[Colour::stats_highlight], -1);

    ostringstream page_num;
    page_num << page + 1 << '/' << 4;
    textout_right_ex(drawbuf, stfont, page_num.str().c_str(), x2 - x_margin, pageNumY, colour[Colour::stats_highlight], -1);
}

void Graphics::draw_player_statistics(const FONT* stfont, const ClientPlayer& player, int x, int y, int page, int time) throw () {
    ostringstream stats;
    stats << player.reg_status.strFlags() << ' ';
    stats << left << setw(17) << player.name << right;
    const Statistics& st = player.stats();
    switch (page) {
    /*break;*/ case 0:
            //       Frags    total/in-row/most
            //  Ping     Capt Kills      Deaths Suicides
            // |0000  000 00 000/00/00  000/00/00   00  |
            stats << setw(4) << player.ping
                  << setw(5) << st.frags()
                  << setw(3) << st.captures()
                  << setw(4) << st.kills()  << '/' << setw(2) << st.current_cons_kills()  << '/' << setw(2) << st.cons_kills()
                  << setw(5) << st.deaths() << '/' << setw(2) << st.current_cons_deaths() << '/' << setw(2) << st.cons_deaths()
                  << setw(5) << st.suicides();
        break; case 1:
            //           Flags          Carriers  Carry
            //  taken dropped returned   killed    time
            // |  00      00      00       00     00:00 |
            stats << setw(4) << st.flags_taken()
                  << setw(8) << st.flags_dropped()
                  << setw(8) << st.flags_returned()
                  << setw(9) << st.carriers_killed()
                  << setw(7) << static_cast<int>(st.flag_carrying_time(time)) / 60 << ':'
                  << setw(2) << setfill('0') << static_cast<int>(st.flag_carrying_time(time)) % 60 << setfill(' ');
        break; case 2:
            //     Accuracy
            //  Shots   |  Taken  Movement     Speed
            // |00000  100% 0000  000000 u  00.00 u/s   |
            stats << setw(5) << st.shots()
                  << setw(6) << _("$1%", fcvt(100. * st.accuracy(), 0))
                  << setw(5) << st.shots_taken()
                  << setw(8) << static_cast<int>(st.movement()) / (2 * PLAYER_RADIUS) << " u"
                  << setw(7) << fcvt(st.old_speed(), 2) << " u/s";
        break; case 3:
            //            Average        Tournament
            //  Playtime lifetime    rank  power  score
            // |00000 min   00:00    0000  00.00 -00000 |
            stats << setw(5);
            const int playtime = static_cast<int>(st.playtime(time));
            if (playtime > 2 * 60 * 60)
                stats << playtime / 60 / 60 << " h  ";
            else
                stats << playtime / 60 << " min";
            stats << setw(5) << static_cast<int>(st.average_lifetime(time)) / 60 << ':'
                  << setw(2) << setfill('0') << static_cast<int>(st.average_lifetime(time)) % 60 << setfill(' ');
            if (player.reg_status.masterAuth()) {
                stats << setw(8) << player.rank
                      << setw(7) << setprecision(2) << std::fixed << (player.score + 1.0) / (player.neg_score + 1.0)
                      << setw(7) << player.score - player.neg_score;
            }
    }
    textout_ex(drawbuf, stfont, stats.str().c_str(), x, y, teamlcol[player.team()], -1);
}

void Graphics::debug_panel(const vector<ClientPlayer>& players, int me, int bpsin, int bpsout,
                           const vector<vector<pair<int, int> > >& sticks, const vector<int>& buttons) throw () {
    clear_to_color(drawbuf, colour[Colour::screen_background]);

    int line = 1;
    const int line_h = 10;
    const int margin = 8;
    for (vector<ClientPlayer>::const_iterator player = players.begin(); player != players.end(); ++player, ++line) {
        const int c = (me == line - 1) ? makecol(0xFF, 0xFF, 0x00) : makecol(0xFF, 0xFF, 0xFF);
        textprintf_ex(drawbuf, font, margin, line * line_h, c, -1, "p. %2i u=%i ons=%i sxy=(%i, %i) HR: p=(%.1f, %.1f) s=(%.1f, %.1f)",
            line, player->used, player->onscreen, player->roomx, player->roomy,
            player->lx, player->ly, player->sx, player->sy);
    }

    line++;
    ostringstream axis_info;
    axis_info << _("Joystick axes") << ':';
    for (vector<vector<pair<int, int> > >::const_iterator stick = sticks.begin(); stick != sticks.end(); ++stick) {
        axis_info << " [";
        for (vector<pair<int, int> >::const_iterator axis = stick->begin(); axis != stick->end(); ++axis)
            axis_info << '(' << axis->first << ' ' << axis->second << ')';
        axis_info << ']';
    }
    textout_ex(drawbuf, font, axis_info.str().c_str(), margin, line++ * line_h, colour[Colour::normal_message_unknown], -1);

    line++;
    ostringstream button_info;
    button_info << _("Joystick buttons") << ':';
    for (vector<int>::const_iterator but = buttons.begin(); but != buttons.end(); ++but)
        button_info << ' ' << *but;
    textout_ex(drawbuf, font, button_info.str().c_str(), margin, line++ * line_h, colour[Colour::normal_message_unknown], -1);

    line++;
    const int bpstraffic = bpsin + bpsout;
    textout_ex(drawbuf, font, _("Traffic: $1 B/s"      , itoa_w(bpstraffic, 4)              ).c_str(), margin, line++ * line_h, colour[Colour::normal_message_unknown], -1);
    textout_ex(drawbuf, font, _("in $1 B/s, out $2 B/s", itoa_w(bpsin, 4), itoa_w(bpsout, 4)).c_str(), margin, line++ * line_h, colour[Colour::normal_message_unknown], -1);
}

void Graphics::draw_fps(double fps) throw () {
    const string text = _("FPS:$1", itoa_w((int)fps, 3));
    print_text_border_check_bg(text, SCREEN_W - 2 - text_length(font, text), SCREEN_H - text_height(font) - 2, colour[Colour::fps], colour[Colour::text_border], -1);
}

void Graphics::map_list(const vector< pair<const MapInfo*, int> >& maps, MapListSortKey sortedBy, int current, int own_vote, const string& edit_vote) throw () {
    const FONT* mlfont;
    if (text_length(font, "i") != text_length(font, "M"))   // map list works only with monospace font
        mlfont = default_font;
    else
        mlfont = font;
    const int line_height = text_height(mlfont) + 4;
    const int w = min(SCREEN_W, 67 * text_length(mlfont, "M") + 4);
    const int extra_space = 10 * line_height;
    int h = map_list_size * line_height + extra_space;
    if (h > SCREEN_H) {
        h = SCREEN_H;
        map_list_size = (h - extra_space) / line_height;
    }
    const int mx = SCREEN_W / 2;
    const int my = SCREEN_H / 2;
    const int x1 = max(0, mx - w / 2);
    const int y1 = my - h / 2;
    const int x2 = min(SCREEN_W, mx + w / 2);
    const int y2 = my + h / 2;
    const int x_left = x1 + 30;

    if (stats_alpha > 0) {
        set_trans_mode(stats_alpha);
        rectfill(drawbuf, x1, y1, x2, y2, colour[Colour::stats_bg]);
        // caption background
        rectfill(drawbuf, x1, y1 + line_height - 4, x2, y1 + 2 * line_height, colour[Colour::stats_caption_bg]);
        solid_mode();
    }

    textout_centre_ex(drawbuf, mlfont, _("Server map list").c_str(), mx, y1 + line_height, colour[Colour::stats_text], -1);

    ostringstream caption;
    caption << _(" Nr Vote Title                Size  Author");
    //           |000 00 * <--------20--------> 00×00 <------------27----------->|
    textout_ex(drawbuf, mlfont, caption.str().c_str(), x_left, y1 + 3 * line_height, colour[Colour::stats_text], -1);

    if (map_list_start >= static_cast<int>(maps.size()) - map_list_size)
        map_list_start = maps.size() - map_list_size;
    if (map_list_start < 0)
        map_list_start = 0;

    for (int i = map_list_start; i < static_cast<int>(maps.size()) && i < map_list_start + map_list_size; ++i) {
        const MapInfo& map = *maps[i].first;
        const int mapNumber = maps[i].second;
        ostringstream mapline;
        mapline << setw(3) << mapNumber + 1 << ' ' << setw(2);
        if (map.votes > 0)
            mapline << map.votes;
        else
            mapline << '-';
        if (own_vote == mapNumber)
            mapline << " *";
        else
            mapline << "  ";
        const string title = map.random ? _("<Random>") : map.title.substr(0, 20);
        mapline << ' ' << setw(20) << left << title << right << ' ';
        mapline << setw(2) << map.width << '×' << setw(2) << left << map.height << right << ' ';
        mapline << map.author.substr(0, 27);
        const int y = y1 + 5 * line_height + line_height * (i - map_list_start);
        int c;
        if (mapNumber == current)
            c = colour[Colour::stats_selected];
        else if (map.highlight)
            c = colour[Colour::stats_highlight];
        else
            c = colour[Colour::stats_text];
        textout_ex(drawbuf, mlfont, mapline.str().c_str(), x_left, y, c, -1);
    }
    // draw scrollbar if there are more maps than visible on the screen
    if (map_list_size < static_cast<int>(maps.size())) {
        const int x = x2 - 30;
        const int y = y1 + 5 * line_height - 4;
        const int height = map_list_size * line_height;
        const int bar_y = static_cast<int>(static_cast<double>(height * map_list_start) / maps.size() + 0.5);
        const int bar_h = static_cast<int>(static_cast<double>(height * map_list_size ) / maps.size() + 0.5);
        scrollbar(x, y, height, bar_y, bar_h, colour[Colour::scrollbar], colour[Colour::scrollbar_bg]);
    }
    int y = y1 + (5 + map_list_size + 1) * line_height;
    string sortOrderString;
    switch (sortedBy) {
    /*break;*/ case MLSK_Number:   sortOrderString = _("Map number");
        break; case MLSK_Votes:    sortOrderString = _("Votes");
        break; case MLSK_Title:    sortOrderString = _("Title");
        break; case MLSK_Size:     sortOrderString = _("Size");
        break; case MLSK_Author:   sortOrderString = _("Author");
        break; case MLSK_Favorite: sortOrderString = _("Favorites");
        break; default: nAssert(0);
    }
    sortOrderString = _("Sort order (space to cycle): $1", sortOrderString);
    textout_ex(drawbuf, mlfont, sortOrderString.c_str(), x_left, y, colour[Colour::stats_text], -1);
    y += 2 * line_height;
    ostringstream vote;
    vote << _("Vote map number") << ": " << edit_vote << '_';
    textout_ex(drawbuf, mlfont, vote.str().c_str(), x_left, y, colour[Colour::stats_highlight], -1);
}

void Graphics::draw_player_power(double val) throw () {
    draw_powerup_time(0, _("Power"), val, colour[Colour::power]);
}

void Graphics::draw_player_turbo(double val) throw () {
    draw_powerup_time(1, _("Turbo"), val, colour[Colour::turbo]);
}

void Graphics::draw_player_shadow(double val) throw () {
    draw_powerup_time(2, _("Shadow"), val, colour[Colour::shadow]);
}

void Graphics::draw_powerup_time(int line, const string& caption, double val, int c) throw () {
    const int y = indicators_y + line * (text_height(font) + 2);
    print_text_border_check_bg(caption, pups_x, y, c, colour[Colour::text_border], -1);
    const string value = itoa_w(iround(val), 3);
    print_text_border_check_bg(value, pups_val_x - text_length(font, value), y, c, colour[Colour::text_border], -1);
}

void Graphics::draw_player_weapon(int level) throw () {
    print_text_border_check_bg(_("Weapon $1", itoa(level)), weapon_x, indicators_y, colour[Colour::weapon], colour[Colour::text_border], -1);
}

void Graphics::draw_map_time(int seconds, bool extra_time) throw () {
    ostringstream ost;
    if (extra_time)
        ost << _("ET") << ' ';
    ost << seconds / 60 << ':' << setw(2) << setfill('0') << seconds % 60;
    print_text_border_check_bg(ost.str(), time_x - text_length(font, ost.str()), time_y, colour[Colour::map_time], colour[Colour::text_border], -1);
}

void Graphics::draw_change_team_message(double time) throw () {
    int c;
    if (static_cast<int>(fmod(time * 2.0, 2)))
        c = colour[Colour::change_message_1];
    else
        c = colour[Colour::change_message_2];
    textout_centre_ex(drawbuf, font, _("CHANGE").c_str(), playfield_x + scale(plw - 6 * 8 + 10), playfield_y + scale(plh - 18), c, -1);
    textout_centre_ex(drawbuf, font, _("TEAMS" ).c_str(), playfield_x + scale(plw - 6 * 8 + 10), playfield_y + scale(plh -  9), c, -1);
}

void Graphics::draw_change_map_message(double time, bool delayed) throw () {
    const int x = playfield_x + scale(plw - 64 - 6 * 8), y1 = playfield_y + scale(plh - 18), y2 = playfield_y + scale(plh - 9);
    const string text1 = _("EXIT"), text2 = _("MAP");
    if (delayed) {
        const int tc = colour[Colour::change_message_delayed];
        const int bc = colour[Colour::text_border];
        print_text_border_centre(text1, x, y1, tc, bc, -1);
        print_text_border_centre(text2, x, y2, tc, bc, -1);
    }
    else {
        int c;
        if (static_cast<int>(fmod(time * 2.0, 2)))
            c = colour[Colour::change_message_1];
        else
            c = colour[Colour::change_message_2];
        print_text_centre(text1, x, y1, c, -1);
        print_text_centre(text2, x, y2, c, -1);
    }
}

void Graphics::draw_player_health(int value) throw () {
    draw_bar(health_x, _("Health"), value, colour[Colour::health_100], colour[Colour::health_200], colour[Colour::health_300]);
}

void Graphics::draw_player_energy(int value) throw () {
    draw_bar(energy_x, _("Energy"), value, colour[Colour::energy_100], colour[Colour::energy_200], colour[Colour::energy_300]);
}

void Graphics::draw_bar(int x, const string& caption, int value, int c100, int c200, int c300) throw () {
    const int y = indicators_y;
    print_text_border_check_bg(caption, x, y, colour[Colour::bar_text], colour[Colour::text_border], -1);
    const string val_str = itoa_w(value, 3);
    const int width = scale(100);
    const int bar_y1 = y + 3 * text_height(font) / 2;
    const int bar_y2 = SCREEN_H - 5;

    const int val_x = max(x + text_length(font, caption) + text_length(font, " "), x + width - text_length(font, val_str));
    print_text_border_check_bg(val_str, val_x, y, colour[Colour::bar_text], colour[Colour::text_border], -1);

    rectfill(drawbuf, x, bar_y1, x + width, bar_y2, colour[Colour::bar_0]);
    if (value == 0)
        return;

    int targ = min(value, 100) * width / 100;
    rectfill    (drawbuf, x, bar_y1, x + targ, bar_y2, c100);

    targ = min(value - 100, 100) * width / 100;
    if (targ > 0)
        rectfill(drawbuf, x, bar_y1, x + targ, bar_y2, c200);

    targ = min(value - 200, 100) * width / 100;
    if (targ > 0)
        rectfill(drawbuf, x, bar_y1, x + targ, bar_y2, c300);
}

void Graphics::draw_replay_info(float rate, unsigned position, unsigned length, bool stopped) throw () {
    int x = health_x;
    int y = indicators_y;

    const int height = text_height(font) + 2;
    const int width = 4 * height / 7;
    const int gap = 2;
    // pause ||   slowmotion |>   play >   rewind >>   stop []
    if (stopped) {
        rectfill(drawbuf, x, y, x + 5 * width / 3 + gap - 1, y + height - 1, colour[Colour::replay_symbol]);
        x += 2 * (width + gap);
    }
    else if (rate == 1) {
        triangle(drawbuf, x, y, x, y + height, x + 2 * width - 1, y + height / 2, colour[Colour::replay_symbol]);
        x += 2 * (width + gap);
    }
    else
        for (int i = 0; i < 2; ++i) {
            if (rate == 0 || i == 0 && rate < 1)
                rectfill(drawbuf, x, y, x + 2 * width / 3 - 1, y + height - 1, colour[Colour::replay_symbol]);
            else
                triangle(drawbuf, x, y, x, y + height, x + width - 1, y + height / 2, colour[Colour::replay_symbol]);
            x += width + gap;
        }

    if (!stopped && rate != 0) {
        const string text = rate >= 1 ? itoa(int(rate)) : "1/" + itoa(int(1 / rate));
        print_text_border_check_bg(text, x, y, colour[Colour::replay_text], colour[Colour::replay_text_border], -1);
    }

    x = (health_x + playfield_x + playfield_w) / 2;
    ostringstream time;
    time << setprecision(0) << std::fixed << position / 10 / 60 << ':';
    time << setw(2) << setfill('0') << setprecision(0) << std::fixed << position / 10 % 60;
    if (length > 0) {
        time << " / ";
        time << setprecision(0) << std::fixed << length / 10 / 60 << ':';
        time << setw(2) << setfill('0') << setprecision(0) << std::fixed << length / 10 % 60;
    }
    print_text_border_centre_check_bg(time.str(), x, y, colour[Colour::replay_text], colour[Colour::replay_text_border], -1);

    if (length > 0) {
        y += 3 * max(height, text_height(font)) / 2;
        const int x1 = health_x;
        const int x2 = playfield_x + playfield_w;
        const int pos_x = x1 + position * (x2 - x1) / length;
        const int y1 = y;
        const int y2 = y1 + scale(5);
        rectfill(drawbuf, x1, y1, pos_x, y2, colour[Colour::replay_bar]);
        rectfill(drawbuf, pos_x + 1, y1, x2, y2, colour[Colour::replay_bar_bg]);
    }
}

void Graphics::print_chat_messages(list<Message>::const_iterator msg, const list<Message>::const_iterator& end, const string& talkbuffer, int cursor_pos) throw () {
    if (!show_chat_messages)
        return;
    const int line_height = text_height(font) + 3;
    const int margin = 3;
    int line = 0;
    for (; msg != end; ++msg, ++line) {
        if (msg->text().empty())
            continue;
        print_chat_message(msg->type(), msg->text(), msg->team(), margin, margin + line * line_height, msg->highlighted());
    }
    if (!talkbuffer.empty()) {
        ostringstream message;
        if (talkbuffer[0] == '/')
            message << _("Command");
        else if (talkbuffer[0] == '.')
            message << _("Say team");
        else
            message << _("Say");
        message << ": ";
        const int message_prefix_length = message.str().length();
        cursor_pos += message_prefix_length;
        if (talkbuffer[0] == '.') {
            message << talkbuffer.substr(1);
            cursor_pos--;
        }
        else
            message << talkbuffer;
        const vector<string> lines = split_to_lines(message.str(), 79, 0, true);
        int characters = 0;
        for (vector<string>::const_iterator li = lines.begin(); li != lines.end(); ++li, ++line) {
            int cursor;
            if (cursor_pos < static_cast<int>(characters + li->length()) || characters + li->length() == message.str().length())
                cursor = cursor_pos - characters;
            else
                cursor = -1;
            print_chat_input(*li, margin, margin + line * line_height, cursor);
            characters += li->length();
        }
    }
}

void Graphics::print_chat_message(Message_type type, const string& message, int team, int x, int y, bool highlight) throw () {
    int c;
    switch (type) {
        /*break;*/ case msg_warning: c = colour[Colour::message_warning];
            break; case msg_team:    c = colour[team == 0 ? Colour::team_message_redteam : team == 1 ? Colour::team_message_blueteam : Colour::team_message_unknown];
            break; case msg_info:    c = colour[Colour::message_info];
            break; case msg_header:  c = colour[Colour::message_header];
            break; case msg_server:  c = colour[Colour::message_server];
            break; case msg_normal: default: c = colour[team == 0 ? Colour::normal_message_redteam : team == 1 ? Colour::normal_message_blueteam : Colour::normal_message_unknown];

    }
    if (highlight && type != msg_team)
        c = colour[Colour::message_highlight];
    // Check if the border is needed.
    if (!bg_texture && y + text_height(font) < playfield_y)
        textout_ex(drawbuf, font, message.c_str(), x, y, c, -1);
    else
        print_text_border(message, x, y, c, colour[Colour::text_border], -1);
}

void Graphics::print_chat_input(const string& message, int x, int y, int cursor) throw () {
    print_text_border(message, x, y, colour[Colour::message_input], colour[Colour::text_border], -1);
    if (cursor >= 0 && static_cast<int>(fmod(get_time() * 2., 2)))
        vline(drawbuf, x + text_length(font, message.substr(0, cursor)), y, y + text_height(font), colour[Colour::message_input]);
}

void Graphics::print_text(const string& text, int x, int y, int textcol, int bgcol) throw () {
    textout_ex(drawbuf, font, text.c_str(), x, y, textcol, bgcol);
}

void Graphics::print_text_centre(const string& text, int x, int y, int textcol, int bgcol) throw () {
    textout_centre_ex(drawbuf, font, text.c_str(), x, y, textcol, bgcol);
}

void Graphics::print_text_border(const string& text, int x, int y, int textcol, int bordercol, int bgcol) throw () {
    print_text_border(text, x, y, textcol, bordercol, bgcol, false);
}

void Graphics::print_text_border_centre(const string& text, int x, int y, int textcol, int bordercol, int bgcol) throw () {
    print_text_border(text, x, y, textcol, bordercol, bgcol, true);
}

void Graphics::print_text_border_check_bg(const string& text, int x, int y, int textcol, int bordercol, int bgcol) throw () {
    if (bg_texture)
        print_text_border(text, x, y, textcol, bordercol, bgcol, false);
    else
        textout_ex(drawbuf, font, text.c_str(), x, y, textcol, bgcol);
}

void Graphics::print_text_border_centre_check_bg(const string& text, int x, int y, int textcol, int bordercol, int bgcol) throw () {
    if (bg_texture)
        print_text_border(text, x, y, textcol, bordercol, bgcol, true);
    else
        textout_centre_ex(drawbuf, font, text.c_str(), x, y, textcol, bgcol);
}

void Graphics::print_text_border(const string& text, int x, int y, int textcol, int bordercol, int bgcol, bool centring) throw () {
    void (*print)(BITMAP*, const FONT*, const char*, int, int, int, int);
    if (centring)
        print = textout_centre_ex;
    else
        print = textout_ex;
    if (bgcol != -1)
        print(drawbuf, font, text.c_str(), x, y, textcol, bgcol);
    // nice border
    if (border_font) {
        int dx = x - (centring ? text_length(font, text) / 2 : 0);
        for (string::const_iterator s = text.begin(); s != text.end(); ++s) {
            const string character = string() + *s;
            textout_ex(drawbuf, border_font, character.c_str(), dx - 1, y - 1, bordercol, -1);
            dx += text_length(font, character);
        }
    }
    else
        for (int i = -1; i <= 1; i++)
            for (int j = -1; j <= 1; j++) {
                if (i == 0 && j == 0)
                    continue;
                print(drawbuf, font, text.c_str(), x + i, y + j, bordercol, -1);
            }
    // text itself
    print(drawbuf, font, text.c_str(), x, y, textcol, -1);
}

void Graphics::scrollbar(int x, int y, int height, int bar_y, int bar_h, int col1, int col2) throw () {
    const int width = 10;
    if (height > 0) {
        rectfill(drawbuf, x, y, x + width - 1, y + height - 1, col2);
        if (bar_h > 0)
            rectfill(drawbuf, x, y + bar_y, x + width - 1, y + bar_y + bar_h - 1, col1);
    }
}

bool Graphics::save_screenshot(const string& filename) const throw () {
    PALETTE pal;
    get_palette(pal);
    BITMAP* current_screen;
    if (page_flipping)
        current_screen = drawbuf == vidpage1 ? vidpage2 : vidpage1;
    else
        current_screen = drawbuf;

    const bool needAdjustDepth = save_extension == ".png" && bitmap_color_depth(current_screen) == 32; // we don't want to save the alpha channel

    if (!page_flipping && !needAdjustDepth)
        return !save_bitmap(filename.c_str(), current_screen, pal);
    else {
        Bitmap temp = needAdjustDepth ? create_bitmap_ex(24, current_screen->w, current_screen->h)
                                      : create_bitmap(current_screen->w, current_screen->h);
        nAssert(temp);
        blit(current_screen, temp, 0, 0, 0, 0, current_screen->w, current_screen->h);
        return !save_bitmap(filename.c_str(), temp, pal);
    }
}

// client side effects

//clear clientside fx's
void Graphics::clear_fx() throw () {
    cfx.clear();
}

//create rocket explosion fx
void Graphics::create_wallexplo(const WorldCoords& pos, int team, double time) throw () {
    cfx.push_back(GraphicsEffect(FX_WALL_EXPLOSION, pos, time, team));
}

//create power rocket explosion fx
void Graphics::create_powerwallexplo(const WorldCoords& pos, int team, double time) throw () {
    cfx.push_back(GraphicsEffect(FX_POWER_WALL_EXPLOSION, pos, time, team));
}

//create deathbringer carrier trail fx
void Graphics::create_deathcarrier(WorldCoords pos, int alpha, double time, bool for_item) throw () {
    if (for_item) {
        pos.x += rand() % 30 - 15;
        pos.y += rand() % 30 - 5;
    }
    else {
        pos.x += rand() % 40 - 20;
        pos.y += rand() % 40;
    }
    cfx.push_back(GraphicsEffect(FX_DEATHCARRIER_SMOKE, pos, time, -1 /* no team */, alpha / 255., colour[Colour::deathbringer_smoke]));
}

void Graphics::create_turbofx(const WorldCoords& pos, int col1, int col2, GunDirection gundir, int alpha, double time) throw () {
    cfx.push_back(GraphicsEffect(FX_TURBO, pos, time, -1 /* team not used */, alpha / 255., col1, col2, gundir));
}

//create explosion fx
void Graphics::create_gunexplo(const WorldCoords& pos, int team, double time) throw () {
    cfx.push_back(GraphicsEffect(FX_GUN_EXPLOSION, pos, time, team));
}

void Graphics::draw_effects(double time) throw () {
    for (list<GraphicsEffect>::iterator fx = cfx.begin(); fx != cfx.end(); ) {
        if (!roomLayout.on_screen(fx->pos.px, fx->pos.py)) {
            ++fx;
            continue;
        }
        const double delta = time - fx->time;
        switch (fx->type) {
        /*break;*/ case FX_GUN_EXPLOSION:
                if (delta > 0.4)
                    fx = cfx.erase(fx);
                else {
                    for (int e = 0; e < 3; e++) {
                        const int rad = 4 + e + static_cast<int>(delta * 40);
                        draw_gun_explosion(fx->pos, rad, fx->team);
                    }
                    ++fx;
                }
            break; case FX_TURBO:      // turbo effect, draw another time in another function
                ++fx;
            break; case FX_WALL_EXPLOSION:
                if (delta > 0.2)
                    fx = cfx.erase(fx);
                else {
                    for (int e = 0; e < 2; e++) {
                        const int rad = 4 + e + static_cast<int>(delta * 40);
                        draw_gun_explosion(fx->pos, rad, fx->team);
                    }
                    ++fx;
                }
            break; case FX_POWER_WALL_EXPLOSION:
                if (delta > 0.2)
                    fx = cfx.erase(fx);
                else {
                    for (int e = 0; e < 3; e++) {
                        const int rad = 4 + e + static_cast<int>(delta * 60);
                        draw_gun_explosion(fx->pos, rad, fx->team);
                    }
                    ++fx;
                }
            break; case FX_DEATHCARRIER_SMOKE:
                if (delta > 0.6)
                    fx = cfx.erase(fx);
                else {
                    draw_deathbringer_smoke(fx->pos, delta, fx->alpha);
                    ++fx;
                }
            break; default:
                nAssert(0);
        }
    }
}

void Graphics::draw_turbofx(double time) throw () {
    if (min_transp)
        return;
    for (list<GraphicsEffect>::iterator fx = cfx.begin(); fx != cfx.end(); ) {
        if (!roomLayout.on_screen(fx->pos.px, fx->pos.py) || fx->type != FX_TURBO) {
            ++fx;
            continue;
        }
        const double delta = time - fx->time;
        if (delta > 0.3)
            fx = cfx.erase(fx);
        else {
            const int alpha = static_cast<int>(fx->alpha * (90 - delta * 300));
            draw_player(fx->pos, fx->col1, fx->col2, fx->gundir, time, false, alpha, time);
            ++fx;
        }
    }
}

bool Graphics::save_map_picture(const string& filename, const Map& map) throw () {
    const int old_minimap_p_w = minimap_place_w;
    const int old_minimap_p_h = minimap_place_h;
    minimap_place_w = map.w * 60;
    minimap_place_h = map.h * 45;
    Bitmap buffer = create_bitmap(minimap_place_w, minimap_place_h);
    nAssert(buffer);
    update_minimap_background(buffer, map, true);
    PALETTE pal;
    get_palette(pal);
    minimap_place_w = old_minimap_p_w;
    minimap_place_h = old_minimap_p_h;
    mapNeedsRedraw = true;
    return !save_bitmap(filename.c_str(), buffer, pal);
}

void Graphics::make_db_effect() throw () {
    db_effect.free();
    const int size = max(1, 2 * pf_scale(2 * PLAYER_RADIUS));
    db_effect = create_bitmap_ex(32, size, size);
    nAssert(db_effect);

    clear_to_color(db_effect, colour[Colour::deathbringer_smoke]);

    set_write_alpha_blender();
    drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);

    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            const int dx = size / 2 - x;
            const int dy = size / 2 - y;
            const double dist = sqrt(dx * dx + dy * dy);
            const int max_alpha = 230;
            int alpha = static_cast<int>(max_alpha - dist * 2 * max_alpha / size);
            alpha = max(0, alpha);
            putpixel(db_effect, x, y, alpha);
        }
    solid_mode();
}

// Theme functions

void Graphics::search_themes(LineReceiver& dst_theme, LineReceiver& dst_bg, LineReceiver& dst_colours) const throw () {
    dst_theme(_("<no theme>"));
    dst_bg(_("<no background>"));
    dst_colours(string() + '<' + _("default") + '>');

    vector<string> themes;
    FileFinder* themeDirs = platMakeFileFinder(wheregamedir + "graphics", "", true);
    while (themeDirs->hasNext())
        themes.push_back(themeDirs->next());
    delete themeDirs;
    sort(themes.begin(), themes.end(), cmp_case_ins);
    for (vector<string>::const_iterator ti = themes.begin(); ti != themes.end(); ++ti) {
        const string dir = wheregamedir + "graphics" + directory_separator + *ti + directory_separator;
        // Put only themes with images on the graphics theme list
        for (vector<string>::const_iterator ei = file_extensions.begin(); ei != file_extensions.end(); ++ei) {
            FileFinder* finder = platMakeFileFinder(dir, *ei, false);
            const bool has_images = finder->hasNext();
            delete finder;
            if (has_images) {
                dst_theme(*ti);
                break;
            }
        }
        // Check if the theme has a background image
        const string bg = dir + "background";
        for (vector<string>::const_iterator ei = file_extensions.begin(); ei != file_extensions.end(); ++ei)
            if (platIsFile(bg + *ei)) {
                dst_bg(*ti);
                break;
            }
        // Check if the theme has custom colours
        const string col = dir + "colours.txt";
        if (platIsFile(col))
            dst_colours(*ti);
    }
}

void Graphics::select_theme(const string& theme_dir, const string& bg_dir, bool use_theme_bg, const string& colour_dir, bool use_theme_colours) throw () {
    unload_pictures();
    if (theme_dir == _("<no theme>"))
        theme_path.clear();
    else {
        theme_path = wheregamedir + "graphics" + directory_separator + theme_dir + directory_separator;
        load_generic_pictures();
        log("Loaded graphics theme '%s'.", theme_dir.c_str());
    }

    bg_path.clear();
    if (use_theme_bg) {
        bg_path = theme_path;
        load_background();
    }
    if (!bg_texture && bg_dir != _("<no background>")) {
        bg_path = wheregamedir + "graphics" + directory_separator + bg_dir + directory_separator;
        load_background();
    }

    bool colours_found = false;
    // Try theme colours
    if (use_theme_colours && theme_dir != _("<no theme>")) {
        colour_file = wheregamedir + "graphics" + directory_separator + theme_dir + directory_separator + "colours.txt";
        colours_found = platIsFile(colour_file);
    }
    // Try colours from different theme
    if (!colours_found && colour_dir != string() + '<' + _("default") + '>') {
        colour_file = wheregamedir + "graphics" + directory_separator + colour_dir + directory_separator + "colours.txt";
        colours_found = platIsFile(colour_file);
    }
    // Use default colours
    if (!colours_found)
        colour_file.clear();

    setColors();

    needReloadPlayfieldPictures = true;
    roomGraphicsChanged();
    mapNeedsRedraw = true;
}

void Graphics::load_generic_pictures() throw () {
    floor_texture.resize(8);
    wall_texture.resize(8);
    if (theme_path.empty())
        return;
    load_floor_textures(theme_path);
    load_wall_textures (theme_path);
}

void Graphics::load_playfield_pictures() throw () {
    for (int t = 0; t < 2; t++)
        player_sprite[t].resize(MAX_PLAYERS / 2);
    pup_sprite.resize(Powerup::pup_last_real + 1);
    db_effect.free();
    make_db_effect();
    if (theme_path.empty())
        return;
    load_player_sprites(theme_path);
    load_shield_sprites(theme_path);
    load_dead_sprites  (theme_path);
    load_rocket_sprites(theme_path);
    load_flag_sprites  (theme_path);
    load_pup_sprites   (theme_path);
}

BITMAP* Graphics::load_bitmap(const string& file) const throw () {
    for (vector<string>::const_iterator ei = file_extensions.begin(); ei != file_extensions.end(); ++ei) {
        BITMAP* buf = ::load_bitmap((file + *ei).c_str(), 0);
        if (buf)
            return buf;
    }
    return 0;
}

void Graphics::load_background() throw () {
    if (!bg_path.empty())
        bg_texture = load_bitmap(bg_path + "background");
}

void Graphics::load_floor_textures(const string& path) throw () {
    int t = 0;
    floor_texture[t++] = load_bitmap(path + "floor_normal1");
    floor_texture[t++] = load_bitmap(path + "floor_normal2");
    floor_texture[t++] = load_bitmap(path + "floor_normal3");
    floor_texture[t++] = load_bitmap(path + "floor_red");
    floor_texture[t++] = load_bitmap(path + "floor_blue");
    floor_texture[t++] = load_bitmap(path + "floor_ice");
    floor_texture[t++] = load_bitmap(path + "floor_sand");
    floor_texture[t++] = load_bitmap(path + "floor_mud");
    // Check that width and height are powers of 2.
    for (int i = 0; i < 8; i++) {
        Bitmap& texture = floor_texture[i];
        if (texture && ((texture->w & texture->w - 1) || (texture->h & texture->h - 1))) {
            log.error(_("Width and height of textures must be powers of 2; floor texture $1 is $2×$3.", itoa(i), itoa(texture->w), itoa(texture->h)));
            texture.free();
        }
    }
}

void Graphics::load_wall_textures(const string& path) throw () {
    int t = 0;
    wall_texture[t++] = load_bitmap(path + "wall_normal1");
    wall_texture[t++] = load_bitmap(path + "wall_normal2");
    wall_texture[t++] = load_bitmap(path + "wall_normal3");
    wall_texture[t++] = load_bitmap(path + "wall_red");
    wall_texture[t++] = load_bitmap(path + "wall_blue");
    wall_texture[t++] = load_bitmap(path + "wall_metal");
    wall_texture[t++] = load_bitmap(path + "wall_wood");
    wall_texture[t++] = load_bitmap(path + "wall_rubber");
    // Check that width and height are powers of 2.
    for (int i = 0; i < 8; i++) {
        Bitmap& texture = wall_texture[i];
        if (texture && ((texture->w & texture->w - 1) || (texture->h & texture->h - 1))) {
            log.error(_("Width and height of textures must be powers of 2; wall texture $1 is $2×$3.", itoa(i), itoa(texture->w), itoa(texture->h)));
            texture.free();
        }
    }
}

BITMAP* Graphics::get_floor_texture(int texid) throw () {
    if (floor_texture[texid])
        return floor_texture[texid];
    else
        return floor_texture.front();
}

BITMAP* Graphics::get_wall_texture(int texid) throw () {
    if (wall_texture[texid])
        return wall_texture[texid];
    else
        return wall_texture.front();
}

void Graphics::load_player_sprites(const string& path) throw () {
    const int size = max(1, pf_scale(player_max_size_in_world));
    const Bitmap common   = scale_sprite      (path + "player"         , size, size);
    const Bitmap team     = scale_alpha_sprite(path + "player_team"    , size, size);
    const Bitmap personal = scale_alpha_sprite(path + "player_personal", size, size);
    if (common && team && personal) {
        // Make player sprites by combining player image with team and personal colours.
        for (int t = 0; t < 2; t++)
            for (int p = 0; p < MAX_PLAYERS / 2; p++) {
                player_sprite[t][p] = create_bitmap(size, size);
                nAssert(player_sprite[t][p]);
                combine_sprite(player_sprite[t][p], common, team, personal, teamcol[t], col[p]);
            }
        // Make a sprite for player with power.
        player_sprite_power = create_bitmap(size, size);
        nAssert(player_sprite_power);
        combine_sprite(player_sprite_power, common, team, personal, colour[Colour::player_power_team], colour[Colour::player_power_personal]);
    }
}

void Graphics::overlayColor(BITMAP* bmp, BITMAP* alpha, int color) throw () {    // alpha must be an 8-bit bitmap; give the color in same format as bmp
    if (!alpha)
        return;
    nAssert(bmp);
    nAssert(alpha->w == bmp->w && alpha->h == bmp->h);
    drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
    for (int y = 0; y < bmp->h; y++)
        for (int x = 0; x < bmp->h; x++) {
            const int a = _getpixel(alpha, x, y);
            if (a) {
                set_trans_blender(0, 0, 0, a);
                putpixel(bmp, x, y, color);
            }
        }
    solid_mode();
}

void Graphics::combine_sprite(BITMAP* sprite, BITMAP* common, BITMAP* team, BITMAP* personal, int tcol, int pcol) throw () { // give the colors in same format as sprite
    nAssert(sprite && common);
    blit(common, sprite, 0, 0, 0, 0, common->w, common->h);
    overlayColor(sprite, personal, pcol);
    overlayColor(sprite, team, tcol);
}

void Graphics::load_shield_sprites(const string& path) throw () {
    const int size = max(1, pf_scale(player_max_size_in_world));
    Bitmap picture = scale_sprite(path + "player_shield", size, size);
    if (!picture)
        return;
    Bitmap team  = scale_alpha_sprite(path + "player_shield_team", size, size);
    for (int t = 0; t < 2; t++) {
        shield_sprite[t] = create_bitmap(size, size);
        nAssert(shield_sprite[t]);
        combine_sprite(shield_sprite[t], picture, team, 0, teamcol[t], 0);
    }
}

void Graphics::load_dead_sprites(const string& path) throw () {
    const int size = max(1, pf_scale(player_max_size_in_world));
    ice_cream = scale_sprite(path + "ice_cream", size, size);
    Bitmap picture = scale_sprite(path + "dead", size, size);
    if (!picture)
        return;
    Bitmap alpha = scale_alpha_sprite(path + "dead_alpha", size, size);
    Bitmap team  = scale_alpha_sprite(path + "dead_team" , size, size);
    for (int t = 0; t < 2; t++) {
        dead_sprite[t] = create_bitmap_ex(32, size, size);
        nAssert(dead_sprite[t]);
        combine_sprite(dead_sprite[t], picture, team, 0, colorTo32(teamcol[t]), 0);
        set_alpha_channel(dead_sprite[t], alpha);
    }
}

// Make rocket sprites by combining rocket image with team colour.
void Graphics::load_rocket_sprites(const string& path) throw () {
    const int size = max(1, pf_scale(rocket_max_size_in_world));
    Bitmap normal = scale_sprite(path + "rocket", size, size);
    if (normal) {
        Bitmap team = scale_alpha_sprite(path + "rocket_team", size, size);
        for (int t = 0; t < 2; t++) {
            rocket_sprite[t] = create_bitmap(size, size);
            nAssert(rocket_sprite[t]);
            combine_sprite(rocket_sprite[t], normal, team, 0, teamcol[t], 0);
        }
        normal.free();
    }
    Bitmap power = scale_sprite(path + "rocket_pow", size, size);
    if (power) {
        Bitmap team = scale_alpha_sprite(path + "rocket_pow_team", size, size);
        for (int t = 0; t < 2; t++) {
            power_rocket_sprite[t] = create_bitmap(size, size);
            nAssert(power_rocket_sprite[t]);
            combine_sprite(power_rocket_sprite[t], power, team, 0, teamcol[t], 0);
        }
    }
}

// Make flag sprites by combining flag image with team colour.
void Graphics::load_flag_sprites(const string& path) throw () {
    const int size = max(1, pf_scale(flag_max_size_in_world));
    Bitmap flag = scale_sprite(path + "flag", size, size);
    if (flag) {
        Bitmap team = scale_alpha_sprite(path + "flag_team", size, size);
        for (int t = 0; t < 3; t++) {
            flag_sprite[t] = create_bitmap(size, size);
            flag_flash_sprite[t] = create_bitmap(size, size);
            nAssert(flag_sprite[t] && flag_flash_sprite[t]);
            combine_sprite(flag_sprite[t], flag, team, 0, teamcol[t], 0);
            combine_sprite(flag_flash_sprite[t], flag, team, 0, teamflashcol[t], 0);
        }
    }
}

void Graphics::load_pup_sprites(const string& path) throw () {
    const int size = max(1, pf_scale(item_max_size_in_world));
    pup_sprite[Powerup::pup_shield      ] = scale_sprite(path + "shield"      , size, size);
    pup_sprite[Powerup::pup_turbo       ] = scale_sprite(path + "turbo"       , size, size);
    pup_sprite[Powerup::pup_shadow      ] = scale_sprite(path + "shadow"      , size, size);
    pup_sprite[Powerup::pup_power       ] = scale_sprite(path + "power"       , size, size);
    pup_sprite[Powerup::pup_weapon      ] = scale_sprite(path + "weapon"      , size, size);
    pup_sprite[Powerup::pup_health      ] = scale_sprite(path + "health"      , size, size);
    pup_sprite[Powerup::pup_deathbringer] = scale_sprite(path + "deathbringer", size, size);
}

BITMAP* Graphics::scale_sprite(const string& filename, int x, int y) const throw () {
    Bitmap temp = load_bitmap(filename);
    if (!temp)
        return 0;
    BITMAP* target = create_bitmap(x, y);
    nAssert(target);
    stretch_blit(temp, target, 0, 0, temp->w, temp->h, 0, 0, target->w, target->h);
    return target;
}

BITMAP* Graphics::scale_alpha_sprite(const string& filename, int x, int y) const throw () {
    set_color_conversion(COLORCONV_NONE);
    Bitmap temp = load_bitmap(filename);
    set_color_conversion(COLORCONV_TOTAL);
    if (!temp)
        return 0;
    if (bitmap_color_depth(temp) != 8) {
        log.error(_("Alpha bitmaps must be 8-bit grayscale images; $1 is $2-bit.", filename, itoa(bitmap_color_depth(temp))));
        return 0;
    }
    BITMAP* target = create_bitmap_ex(8, max(1, x), max(1, y));
    nAssert(target);
    stretch_blit(temp, target, 0, 0, temp->w, temp->h, 0, 0, target->w, target->h);
    return target;
}

void Graphics::unload_generic_pictures() throw () {
    unload_floor_textures();
    unload_wall_textures();
}

void Graphics::unload_playfield_pictures() throw () {
    unload_player_sprites();
    unload_shield_sprites();
    unload_dead_sprites();
    unload_rocket_sprites();
    unload_flag_sprites();
    unload_pup_sprites();
}

void Graphics::unload_pictures() throw () {
    unload_generic_pictures();
    unload_playfield_pictures();
    bg_texture.free();
}

void Graphics::unload_floor_textures() throw () {
    for (vector<Bitmap>::iterator pl = floor_texture.begin(); pl != floor_texture.end(); ++pl)
        pl->free();
}

void Graphics::unload_wall_textures() throw () {
    for (vector<Bitmap>::iterator pl = wall_texture.begin(); pl != wall_texture.end(); ++pl)
        pl->free();
}

void Graphics::unload_player_sprites() throw () {
    for (int t = 0; t < 2; t++)
        for (vector<Bitmap>::iterator pl = player_sprite[t].begin(); pl != player_sprite[t].end(); ++pl)
            pl->free();
    player_sprite_power.free();
}

void Graphics::unload_shield_sprites() throw () {
    for (int i = 0; i < 2; i++)
        shield_sprite[i].free();
}

void Graphics::unload_dead_sprites() throw () {
    for (int i = 0; i < 2; i++)
        dead_sprite[i].free();
    ice_cream.free();
}

void Graphics::unload_rocket_sprites() throw () {
    for (int i = 0; i < 2; i++) {
        rocket_sprite[i].free();
        power_rocket_sprite[i].free();
    }
}

void Graphics::unload_flag_sprites() throw () {
    for (int i = 0; i < 3; i++) {
        flag_sprite[i].free();
        flag_flash_sprite[i].free();
    }
}

void Graphics::unload_pup_sprites() throw () {
    for (vector<Bitmap>::iterator pup = pup_sprite.begin(); pup != pup_sprite.end(); ++pup)
        pup->free();
}

// Font functions

void Graphics::search_fonts(LineReceiver& dst_font) const throw () {
    dst_font(string() + '<' + _("default") + '>');

    vector<string> fonts;
    FileFinder* font_files = platMakeFileFinder(wheregamedir + "fonts", ".dat", false);
    while (font_files->hasNext())
        fonts.push_back(FileName(font_files->next()).getBaseName());
    delete font_files;
    sort(fonts.begin(), fonts.end(), cmp_case_ins);
    for (vector<string>::const_iterator fi = fonts.begin(); fi != fonts.end(); ++fi)
        dst_font(*fi);
}

void Graphics::select_font(const string& file) throw () {
    if (file == string() + '<' + _("default") + '>') {
        font = default_font;
        border_font = 0;
    }
    else
        load_font(file);
    make_layout();
}

void Graphics::load_font(const string& file) throw () {
    const string filename = wheregamedir + "fonts" + directory_separator + file + ".dat";

    static DATAFILE* font_data = 0;
    if (font_data)
        unload_datafile_object(font_data);
    font_data = load_datafile_object(filename.c_str(), "font");
    if (font_data)
        font = static_cast<FONT*>(font_data->dat);
    else
        font = default_font;

    static DATAFILE* border_data = 0;
    if (border_data)
        unload_datafile_object(border_data);
    border_data = load_datafile_object(filename.c_str(), "border");
    if (border_data)
        border_font = static_cast<FONT*>(border_data->dat);
    else
        border_font = 0;

    log("Loaded font '%s'.", file.c_str());
}

int Graphics::scale(double value) const throw () {
    return iround(scr_mul * value);
}

TemporaryClipRect::TemporaryClipRect(BITMAP* bitmap, int x1_, int y1_, int x2_, int y2_, bool respectOld) throw () : b(bitmap) {
    get_clip_rect(b, &x1, &y1, &x2, &y2);
    if (respectOld) {
        x1_ = max(x1_, x1);
        y1_ = max(y1_, y1);
        x2_ = min(x2_, x2);
        y2_ = min(y2_, y2);
    }
    set_clip_rect(b, x1_, y1_, x2_, y2_);
}

TemporaryClipRect::~TemporaryClipRect() throw () {
    set_clip_rect(b, x1, y1, x2, y2);
}

bool Graphics::RoomLayoutManager::set(int mapWidth, int mapHeight, double visibleRooms, bool repeatMap) throw () {
    const double oldScale = playfield_scale;

    map_w = mapWidth;
    map_h = mapHeight;

    visible_rooms_x = visible_rooms_y = visibleRooms;
    if (!repeatMap) {
        visible_rooms_x = min<double>(visible_rooms_x, map_w);
        visible_rooms_y = min<double>(visible_rooms_y, map_h);
    }
    room_w = static_cast<int>(g.playfield_w / visible_rooms_x);
    room_h = static_cast<int>(g.playfield_h / visible_rooms_y);
    // at least visible_rooms_* must fit in each direction -> to correct aspect ratio, shrink one of room_w,h
    const int reverse_room_w = iround(double(room_h) * plw / plh);
    const int reverse_room_h = iround(double(room_w) * plh / plw);
    if (room_w > reverse_room_w)
        room_w = reverse_room_w;
    else if (room_h > reverse_room_h)
        room_h = reverse_room_h;
    nAssert(room_w > 0 && room_h > 0);

    visible_rooms_x = double(g.playfield_w) / room_w;
    visible_rooms_y = double(g.playfield_h) / room_h;
    if (!repeatMap) {
        visible_rooms_x = min<double>(visible_rooms_x, map_w);
        visible_rooms_y = min<double>(visible_rooms_y, map_h);
    }

    playfield_scale = (double(room_w) / plw + double(room_h) / plh) / 2.;

    plx = g.playfield_x + (g.playfield_w - static_cast<int>(room_w * visible_rooms_x)) / 2;
    ply = g.playfield_y + (g.playfield_h - static_cast<int>(room_h * visible_rooms_y)) / 2;
    nAssert(plx >= g.playfield_x && ply >= g.playfield_y);

    #if 0
    g.log("RLM::set(%d, %d, %f -> %f %f %f %d %d %f %d %d",
          mapWidth, mapHeight, visibleRooms,
          visible_rooms, visible_rooms_x, visible_rooms_y, room_w, room_h, playfield_scale, plx, ply);
    #endif

    return fabs(playfield_scale - oldScale) > oldScale * 1e-8;
}

int Graphics::RoomLayoutManager::pf_scale(double value) const throw () {
    return iround(playfield_scale * value);
}

double Graphics::RoomLayoutManager::pf_scaled(double value) const throw () {
    return playfield_scale * value;
}

vector<int> Graphics::RoomLayoutManager::scale_x(int roomx, double lx) const throw () {
    vector<int> pos = room_offset_x(roomx);
    const int delta = pf_scale(lx);
    for (vector<int>::iterator pi = pos.begin(); pi != pos.end(); ++pi)
        *pi += delta;
    return pos;
}

vector<int> Graphics::RoomLayoutManager::scale_y(int roomy, double ly) const throw () {
    vector<int> pos = room_offset_y(roomy);
    const int delta = pf_scale(ly);
    for (vector<int>::iterator pi = pos.begin(); pi != pos.end(); ++pi)
        *pi += delta;
    return pos;
}

static double calculateDistanceFromScreen(double coord, double viewStart, double viewSize, int mapSize) throw () {
    if (positiveFmod(coord - viewStart, mapSize) <= viewSize)
        return 0;
    const double diffFromViewCenter = positiveFmod(coord - (viewStart + viewSize / 2), mapSize);
    if (diffFromViewCenter < mapSize / 2) // nearer in the positive direction
        return diffFromViewCenter - viewSize / 2;
    else
        return diffFromViewCenter - mapSize + viewSize / 2;
}

double Graphics::RoomLayoutManager::distanceFromScreenX(int rx, double lx) const throw () {
    return calculateDistanceFromScreen(rx + lx / plw, topLeft.px + topLeft.x / plw, visible_rooms_x, map_w);
}

double Graphics::RoomLayoutManager::distanceFromScreenY(int ry, double ly) const throw () {
    return calculateDistanceFromScreen(ry + ly / plh, topLeft.py + topLeft.y / plh, visible_rooms_y, map_h);
}

bool Graphics::RoomLayoutManager::on_screen(int rx, int ry) const throw () {
    return positiveModulo(rx - topLeft.px, map_w) <= visible_rooms_x &&
           positiveModulo(ry - topLeft.py, map_h) <= visible_rooms_y; // <= instead of <, because the first room may be visible only in part
}

vector<int> Graphics::RoomLayoutManager::room_offset_x(int rx) const throw () {
    vector<int> pos;
    for (int x = ((rx - topLeft.px + map_w) % map_w) * room_w - pf_scale(topLeft.x); x < visible_rooms_x * room_w; x += map_w * room_w)
        pos.push_back(plx + x);
    return pos;
}

vector<int> Graphics::RoomLayoutManager::room_offset_y(int ry) const throw () {
    vector<int> pos;
    for (int y = ((ry - topLeft.py + map_h) % map_h) * room_h - pf_scale(topLeft.y); y < visible_rooms_y * room_h; y += map_h * room_h)
        pos.push_back(ply + y);
    return pos;
}

class BackgroundMasker { // helper for Graphics::BackgroundManager::draw_background
public:
    class YSegment {
    public:
        int y0, y1;
        typedef pair<int, int> Section;
        typedef list<Section> SectionList;
        SectionList unmasked;

        YSegment(int y0_, int y1_) throw () : y0(y0_), y1(y1_) { unmasked.push_back(Section(0, SCREEN_W - 1)); }
        YSegment(int y0_, int y1_, const SectionList& unmasked_) throw () : y0(y0_), y1(y1_), unmasked(unmasked_) { }

        void addMask(int y0, int y1) throw () {
            SectionList::iterator i = unmasked.begin();
            while (i != unmasked.end() && i->second < y0) // skip unmasked areas before the mask
                ++i;
            if (i != unmasked.end() && i->first < y0) { // adjust unmasked area where the mask begins
                if (i->second > y1) { // split area that the mask falls in the middle of
                    i = unmasked.insert(i, Section(i->first, y0 - 1));
                    ++i;
                    i->first = y1 + 1;
                    return;
                }
                i->second = y0 - 1;
                ++i;
            }
            while (i != unmasked.end() && i->second <= y1) // delete unmasked areas in the middle of mask
                i = unmasked.erase(i);
            if (i != unmasked.end() && i->first <= y1) // adjust unmasked area where the mask ends
                i->first = y1 + 1;
        }
    };
    typedef list<YSegment> SegList;
    SegList ySeg;

    BackgroundMasker() throw () { ySeg.push_back(YSegment(0, SCREEN_H - 1)); }

    void addMask(int x0, int y0, int x1, int y1) throw () {
        nAssert(x0 <= x1 && y0 <= y1 && x0 >= 0 && y0 >= 0 && x1 < SCREEN_W && y1 < SCREEN_H);
        SegList::iterator i = ySeg.begin();
        while (i->y1 < y0) // skip segments before the mask
            ++i;
        if (i->y0 < y0) { // split segment where the mask begins
            i = ySeg.insert(i, YSegment(i->y0, y0 - 1, i->unmasked));
            ++i;
            i->y0 = y0;
        }
        while (i != ySeg.end() && i->y1 <= y1) { // add mask to segments in the middle of the mask area
            i->addMask(x0, x1);
            ++i;
        }
        if (i != ySeg.end() && i->y0 <= y1) { // split segment where the mask ends
            i = ySeg.insert(i, YSegment(i->y0, y1, i->unmasked));
            i->addMask(x0, x1);
            ++i;
            i->y0 = y1 + 1;
        }
    }
};

static void tileBlit(BITMAP* target, int x1, int y1, int x2, int y2, BITMAP* tex) throw () {
    ++x2; ++y2; // makes calculating widths easier
    const int txs = x1 % tex->w; int ws = tex->w - txs, we = x2 % tex->w, xe = x2 - we;
    const int tys = y1 % tex->h; int hs = tex->h - tys, he = y2 % tex->h, ye = y2 - he;
    if (x1 + ws >= x2) {
        ws = x2 - x1;
        xe = x1;
    }
    if (y1 + hs >= y2) {
        hs = y2 - y1;
        ye = y1;
    }
    for (int y = y1;;) {
        const int h = y == y1 ? hs : y == ye ? he : tex->h;
        const int ty = y % tex->h;
        blit(tex, target, txs, ty, x1, y, ws, h);
        for (int x = x1 + ws; x < xe; x += tex->w)
            blit(tex, target, 0, ty, x, y, tex->w, h);
        if (we)
            blit(tex, target, 0, ty, xe, y, we, h);
        if (y == ye)
            break;
        y += h;
    }
}

void Graphics::BackgroundManager::draw_background(BITMAP* drawbuf, bool draw_map, bool reserve_playfield) throw () {
    const int pfx0 = g.roomLayout.x0(), pfy0 = g.roomLayout.y0(), pfxm = g.roomLayout.xMax(), pfym = g.roomLayout.yMax();

    // calculate the area that needs to be filled with background (what ever is not fully drawn in the rest of the method or in draw_playfield_background if reserve_playfield is set)
    BackgroundMasker bkMask;
    if (reserve_playfield)
        bkMask.addMask(pfx0, pfy0, pfxm, pfym);
    if (draw_map)
        bkMask.addMask(g.minimap_x, g.minimap_y, g.minimap_x + g.minimap_w - 1, g.minimap_y + g.minimap_h - 1);

    // draw the required strips of background
    /* tileBlit is used to accomplish this faster (both with or without a bg_texture):
    if (g.bg_texture)
        drawing_mode(DRAW_MODE_COPY_PATTERN, g.bg_texture, 0, 0);
    rectfill(drawbuf, xsi->first, ysi->y0, xsi->second, ysi->y1, g.colour[Colour::screen_background]);
    solid_mode();
    */
    if (!g.bg_texture || (g.bg_texture->w < 128 && g.bg_texture->w * g.bg_texture->h < 4096)) {
        int w, h;
        if (!g.bg_texture) {
            w = 128; // these numbers were found to be nicely within the sweet spot without wasting memory in some testing (but it depends on a lot of things, even the screen layout is a significant factor)
            h = 4;
        }
        else {
            w = g.bg_texture->w;
            h = g.bg_texture->h;
            nAssert(w && h);
            while (w < 128 && w * h < 4096)
                w *= 2;
        }
        BITMAP* new_bg = create_bitmap(w, h); //#opt: it might matter a lot in flipped modes if we could get this from video memory
        nAssert(new_bg);
        if (g.bg_texture) {
            tileBlit(new_bg, 0, 0, w - 1, h - 1, g.bg_texture);
            g.bg_texture.free();
        }
        else
            clear_to_color(new_bg, g.colour[Colour::screen_background]);
        g.bg_texture = new_bg;
    }

    #if 0
    // Performance test
    static bool hasRun = false;
    if (!hasRun && draw_map && reserve_playfield) {
        hasRun = true;
        for (int y = 1; y <= 256; y *= 2)
            for (int x = 8; x < 512; x *= 2) {
                Bitmap stuff = create_bitmap(x, y);
                nAssert(stuff);
                unsigned tReps = 0;
                TimeCounter timer;
                timer.setZero();
                while (timer.read() < 2.) {
                    for (int rep = 0; rep < 10; ++rep)
                        for (BackgroundMasker::SegList::const_iterator ysi = bkMask.ySeg.begin(); ysi != bkMask.ySeg.end(); ++ysi)
                            for (BackgroundMasker::YSegment::SectionList::const_iterator xsi = ysi->unmasked.begin(); xsi != ysi->unmasked.end(); ++xsi)
                                tileBlit(drawbuf, xsi->first, ysi->y0, xsi->second, ysi->y1, stuff);
                    tReps += 10;
                    timer.refresh();
                }
                std::cout << setw(3) << x << "×" << setw(3) << y << " (" << setw(6) << x * y << "): " << setw(3) << int(tReps / timer.read()) << " RPS\n";
            }
    }
    #endif

    for (BackgroundMasker::SegList::const_iterator ysi = bkMask.ySeg.begin(); ysi != bkMask.ySeg.end(); ++ysi)
        for (BackgroundMasker::YSegment::SectionList::const_iterator xsi = ysi->unmasked.begin(); xsi != ysi->unmasked.end(); ++xsi)
            tileBlit(drawbuf, xsi->first, ysi->y0, xsi->second, ysi->y1, g.bg_texture);

    if (draw_map)
        blit(g.minibg, drawbuf, 0, 0, g.minimap_x, g.minimap_y, g.minimap_w, g.minimap_h);
}

void Graphics::BackgroundManager::allocateRoomCache(int room_w, int room_h, int minRooms, int maxRooms) throw () {
    nAssert(minRooms <= maxRooms);
    nAssert(roomCache.empty() && !roomCacheMemoryBitmap && roomCacheBitmap);
    const bool useB2 = (roomCacheBitmap->w / room_w) * (roomCacheBitmap->h / room_h) >= 2 * minRooms;
    BitmapRegion b1(0, 0, 0, 0, 0); // initialized to please GCC
    bool b1set = false;
    const int fogColor = g.colour[Colour::playfield_fog];
    for (int x0 = 0; x0 + room_w <= roomCacheBitmap->w; x0 += room_w) {
        for (int y0 = 0; y0 + room_h <= roomCacheBitmap->h; y0 += room_h) {
            if (!b1set) {
                b1 = BitmapRegion(roomCacheBitmap, x0, y0, room_w, room_h);
                if (useB2)
                    b1set = true;
                else
                    roomCache.push_back(CachedRoomGfx(b1, BitmapRegion(), fogColor));
            }
            else {
                BitmapRegion b2(roomCacheBitmap, x0, y0, room_w, room_h);
                roomCache.push_back(CachedRoomGfx(b1, b2, fogColor));
                b1set = false;
            }
            if ((int)roomCache.size() >= maxRooms)
                return;
        }
    }
    if ((int)roomCache.size() < minRooms) {
        nAssert(!useB2);
        roomCacheMemoryBitmap = create_bitmap(room_w * (minRooms - roomCache.size()), room_h);
        nAssert(roomCacheMemoryBitmap);
        for (int x0 = 0; x0 < roomCacheMemoryBitmap->w; x0 += room_w)
            roomCache.push_back(CachedRoomGfx(BitmapRegion(roomCacheMemoryBitmap, x0, 0, room_w, room_h), BitmapRegion(), fogColor));
        nAssert((int)roomCache.size() == minRooms);
    }
}

void Graphics::BackgroundManager::draw_playfield_background(BITMAP* drawbuf, const Map& map, const VisibilityMap& roomVis, bool continuousTextures, bool mapInfoMode) throw () {
    const WorldCoords& topLeft = g.roomLayout.topLeftCoords();
    const int room_w = g.roomLayout.roomWidth();
    const int room_h = g.roomLayout.roomHeight();

    const int xRooms = static_cast<int>(ceil(g.roomLayout.visibleRoomsX() + topLeft.x / plw - .0001));
    const int yRooms = static_cast<int>(ceil(g.roomLayout.visibleRoomsY() + topLeft.y / plh - .0001));
    const unsigned needLockedRooms = min(map.w, xRooms) * min(map.h, yRooms);

    if (continuousTextures != previousContinuousTextures || mapInfoMode != previousMapInfoMode || needLockedRooms > roomCache.size() || DISABLE_ROOM_CACHE) {
        invalidateRoomCache();
        previousContinuousTextures = continuousTextures;
        previousMapInfoMode = mapInfoMode;
    }

    if (roomCache.empty()) {
        allocateRoomCache(room_w, room_h, needLockedRooms, map.w * map.h);
        roomCacheIndex.resize(map.w);
        for (int x = 0; x < map.w; ++x)
            roomCacheIndex[x].resize(map.h);
    }
    else if (topLeft.px != previousRoom0x || topLeft.py != previousRoom0y || xRooms != previousXrooms || yRooms != previousYrooms) {
        // update room locks, so that cached but now unused rooms can be reclaimed, while those in use won't
        for (int x = 0; x < map.w; ++x) {
            const bool xIn = (x - topLeft.px + map.w) % map.w < xRooms;
            for (int y = 0; y < map.h; ++y) {
                CachedRoomGfx* roomp = roomCacheIndex[x][y];
                if (!roomp)
                    continue;
                CachedRoomGfx& room = *roomp;
                const bool in = xIn && (y - topLeft.py + map.h) % map.h < yRooms;
                if (room.locked != in) {
                    room.locked = in;
                    if (!in)
                        room.lastUse = cacheTimestamp;
                }
            }
        }
        ++cacheTimestamp;
    }
    previousRoom0x = topLeft.px; previousRoom0y = topLeft.py; previousXrooms = xRooms; previousYrooms = yRooms;

    const int pfx0 = g.roomLayout.x0(), pfy0 = g.roomLayout.y0(), pfxm = g.roomLayout.xMax(), pfym = g.roomLayout.yMax();

    TemporaryClipRect clipRestorer(drawbuf, pfx0, pfy0, pfxm, pfym, false);
    for (int dx = 0; dx < xRooms; ++dx) {
        const int roomx = (topLeft.px + dx) % map.w;
        const int rx0 = pfx0 - g.pf_scale(topLeft.x) + dx * room_w;
        for (int dy = 0; dy < yRooms; ++dy) {
            const int roomy = (topLeft.py + dy) % map.h;
            const int ry0 = pfy0 - g.pf_scale(topLeft.y) + dy * room_h;
            drawRoom(map, roomx, roomy, continuousTextures, mapInfoMode, roomVis[roomx][roomy] < 255, drawbuf, rx0, ry0);
        }
    }
}

bool Graphics::BackgroundManager::allocate(bool videoMemory, int cachePages) throw () {
    nAssert(cachePages > 0);
    if (videoMemory)
        roomCacheBitmap = create_video_bitmap(SCREEN_W, cachePages * SCREEN_H);
    else
        roomCacheBitmap = create_bitmap(SCREEN_W, cachePages * SCREEN_H);
    return !!roomCacheBitmap;
}

void Graphics::BackgroundManager::cacheRoom(const Map& map, int roomx, int roomy, bool continuousTextures, bool mapInfoMode) throw () {
    vector<CachedRoomGfx>::iterator ci, oldest;
    unsigned oldestTime = cacheTimestamp + 1;
    for (ci = roomCache.begin(); ci != roomCache.end() && ci->used(); ++ci)
        if (!ci->locked && ci->lastUse < oldestTime) {
            oldestTime = ci->lastUse;
            oldest = ci;
        }
    if (ci == roomCache.end()) { // no unused entries were found
        nAssert(oldestTime <= cacheTimestamp);
        ci = oldest;
    }
    if (roomCacheIndex[ci->x()][ci->y()] == &*ci)
        roomCacheIndex[ci->x()][ci->y()] = 0;
    roomCacheIndex[roomx][roomy] = &*ci;
    BitmapRegion& area = ci->getAreaForWriting(roomx, roomy); // this locks the room, so we don't need to update lastUse yet
    Bitmap roombg = create_sub_bitmap(area.b, area.x0, area.y0, area.w, area.h);
    acquire_bitmap(roombg);
    g.drawRoomBackground(roombg, map, roomx, roomy, continuousTextures ? roomx : 0, continuousTextures ? roomy : 0, mapInfoMode);
    release_bitmap(roombg);
}

void Graphics::BackgroundManager::drawRoom(const Map& map, int roomx, int roomy, bool continuousTextures, bool mapInfoMode, bool fogged, BITMAP* target, int tx0, int ty0) throw () {
    if (!roomCacheIndex[roomx][roomy])
        cacheRoom(map, roomx, roomy, continuousTextures, mapInfoMode);
    const CachedRoomGfx& room = *roomCacheIndex[roomx][roomy];
    if (fogged)
        room.drawFogged(target, tx0, ty0);
    else
        room.drawUnfogged(target, tx0, ty0);
}

void Graphics::BackgroundManager::BitmapRegion::blitTo(BITMAP* target, int tx0, int ty0) const throw () {
    blit(const_cast<BITMAP*>(b), target, x0, y0, tx0, ty0, w, h);
}

void Graphics::BackgroundManager::CachedRoomGfx::generateFogged(BITMAP* target, int tx0, int ty0, bool fastFog) const throw () {
    baseArea.blitTo(target, tx0, ty0);
    if (fastFog) {
        const int ld = 3; // distance between lines
        for (int y = ld - 1 - (ty0 + ld - 1) % ld; y < baseArea.h; y += ld)
            hline(target, tx0, ty0 + y, tx0 + baseArea.w - 1, fogColor);
    }
    else {
        set_trans_mode(playfieldFogOfWarAlpha);
        rectfill(target, tx0, ty0, tx0 + baseArea.w - 1, ty0 + baseArea.h - 1, fogColor);
        solid_mode();
    }
}

void Graphics::BackgroundManager::CachedRoomGfx::drawFogged(BITMAP* target, int tx0, int ty0) const throw () {
    nAssert(baseGenerated);
    if (foggedGenerated)
        foggedArea.blitTo(target, tx0, ty0);
    else if (!foggedArea.b)
        generateFogged(target, tx0, ty0, true);
    else {
        generateFogged(foggedArea.b, foggedArea.x0, foggedArea.y0, false);
        foggedGenerated = true;
        foggedArea.blitTo(target, tx0, ty0);
    }
}
