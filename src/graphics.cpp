/*
 *  graphics.cpp
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

#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <cmath>

#include "incalleg.h"
#include "antialias.h"
#include "client.h"
#include "commont.h"
#include "effects.h"
#include "language.h"
#include "platform.h"
#include "sounds.h"
#include "timer.h"
#include "world.h"

#include "graphics.h"

const bool TEST_FALL_ON_WALL = false;

// these shouldn't be changed - rather change allegro.cfg
const int  WINMODE = GFX_AUTODETECT_WINDOWED;
const int FULLMODE = GFX_AUTODETECT;

const bool SWITCH_PAUSE_CLIENT = false;

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

Graphics::Graphics(LogSet logs):
    show_chat_messages  (true),
    show_scoreboard     (true),
    show_minimap        (true),
    default_font        (font),
    border_font         (0),
    map_list_size       (20),
    map_list_start      (0),
    team_captures_start (0),
    antialiasing        (true),
    log                 (logs)
{ }

Graphics::~Graphics() {
    unload_bitmaps();
}

bool Graphics::init(int width, int height, int depth, bool windowed, bool flipping) {
    unload_bitmaps();

    if (windowed)
        flipping = false;

    if (!reset_video_mode(width, height, depth, windowed, flipping ? 3 : 1))
        return false;

    page_flipping = flipping;

    if (page_flipping) {
        vidpage1   = create_video_bitmap(SCREEN_W, SCREEN_H);
        vidpage2   = create_video_bitmap(SCREEN_W, SCREEN_H);
        background = create_video_bitmap(SCREEN_W, SCREEN_H);
        if (!vidpage1 || !vidpage2 || !background) {
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
        background = create_bitmap(SCREEN_W, SCREEN_H);
        nAssert(background);
        drawbuf = backbuf;
    }

    reset_playground_colors();
    setColors();

    make_layout();

    return true;
}

void Graphics::make_layout() {
    scr_mul = static_cast<double>(SCREEN_W) / 640;

    // Room background
    const int bottombar_h = 3 * (text_height(font) + 2) + 5;
    if (SCREEN_H < scr_mul * plh + bottombar_h + text_height(font))          // the window is too low for playground and one line for messages
        scr_mul = static_cast<double>(SCREEN_H - bottombar_h - text_height(font)) / plh;
    plx = 0;
    ply = SCREEN_H - scale(plh) - bottombar_h;
    roombg.free();
    roombg = create_sub_bitmap(background, plx, ply, static_cast<int>(ceil(scr_mul * plw)), static_cast<int>(ceil(scr_mul * plh)));

    // Minimap
    minimap_w = minimap_place_w = SCREEN_W - roombg->w - 4; // 4 for left and right margin
    mmx = SCREEN_W - minimap_w - 2;
    if (mmx > text_length(font, "M") * 80) {  // check if minimap fits to the right of chat messages
        mmy = 4;
        minimap_h = minimap_place_h = scale(100) + ply - 4;
    }
    else {
        mmy = ply;
        minimap_h = minimap_place_h = scale(100);
    }
    const int extra_space = SCREEN_H - 450 - (mmy + minimap_place_h);   // 450 = scoreboard max height + FPS line
    if (extra_space > 0)
        minimap_h = minimap_place_h += extra_space;
    minibg.free();
    minibg = create_bitmap(minimap_place_w, minimap_place_h);
    nAssert(minibg);
    minibg_fog.free();
    minibg_fog = create_bitmap(minimap_place_w, minimap_place_h);   // if not created, won't be used

    // Scoreboard
    scoreboard_x1 = plx + roombg->w;
    scoreboard_x2 = SCREEN_W - 1;
    scoreboard_y1 = mmy + minimap_place_h;
    scoreboard_y2 = SCREEN_H - 1;

    // Bottom bar
    indicators_y = ply + roombg->h + 5;
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

    // Textures and sprites
    unload_pictures();
    floor_texture.resize(8);
    wall_texture.resize(8);
    for (int t = 0; t < 2; t++)
        player_sprite[t].resize(MAX_PLAYERS / 2);
    pup_sprite.resize(Powerup::pup_last_real + 1);

    db_effect.free();
    make_db_effect();

    load_pictures();
    load_background();
}

void Graphics::videoMemoryCorrupted() {
    // re-allocate roombg to work around Allegro's bug - apparently sub-bitmaps don't always survive
    roombg.free();
    roombg = create_sub_bitmap(background, plx, ply, static_cast<int>(ceil(scr_mul * plw)), static_cast<int>(ceil(scr_mul * plh)));
}

void Graphics::unload_bitmaps() {
    roombg    .free();  // sub-bitmap => release first
    vidpage1  .free();
    vidpage2  .free();
    backbuf   .free();
    background.free();
    minibg    .free();
    minibg_fog.free();
    db_effect .free();
    unload_pictures();
}

void Graphics::startDraw() {
    acquire_bitmap(drawbuf);
}

void Graphics::endDraw() {
    release_bitmap(drawbuf);
}

void Graphics::startPlayfieldDraw() {
    set_clip_rect(drawbuf, plx, ply, plx + scale(plw) - 1, ply + scale(plh) - 1);
}

void Graphics::endPlayfieldDraw() {
    set_clip_rect(drawbuf, 0, 0, drawbuf->w - 1, drawbuf->h - 1);
}

void Graphics::draw_screen(bool acquireWithFlipping) {
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

void Graphics::setColors() {
    //the first 8 colors are player's colors
    col[COLGREEN]  = makecol(0x00, 0xFF, 0x00);
    col[COLYELLOW] = makecol(0xFF, 0xFF, 0x00);
    col[COLWHITE]  = makecol(0xFF, 0xFF, 0xFF);
    col[COLMAG]    = makecol(0xFF, 0x08, 0xFF); // can't use pure magenta, it will end up transparent in the themes
    col[COLCYAN]   = makecol(0x00, 0xFF, 0xFF);
    col[COLORA]    = makecol(0xFF, 0xA0, 0x00);
    col[COLLRED]   = makecol(0xFF, 0x55, 0x44);
    col[COLLBLUE]  = makecol(0x44, 0x55, 0xFF);
    //MORE player colors
    col[COL9]  = makecol(0x00, 0x80, 0x00);
    col[COL10] = makecol(0xA0, 0xA0, 0xA0);
    col[COL11] = makecol(0x00, 0xA0, 0xA0);
    col[COL12] = makecol(0x80, 0x00, 0x80);
    col[COL13] = makecol(0xA0, 0x60, 0x00);
    col[COL14] = makecol(0x00, 0x00, 0x80);
    col[COL15] = makecol(0x80, 0x00, 0x00);
    col[COL16] = makecol(0x66, 0x66, 0x66);

    // team solid colors
    col[COLBLUE] = makecol(0x00, 0x00, 0xFF);
    col[COLRED]  = makecol(0xFF, 0x00, 0x00);

    // base minimap background colors
    col[COLBBLUE] = makecol(0x00, 0x00, 0x66);
    col[COLBRED]  = makecol(0x66, 0x00, 0x00);

    //other
    col[COLFOGOFWAR]  = makecol(0xFF, 0xFF, 0xFF);
    col[COLMENUWHITE] = makecol(0xC0, 0xC0, 0xC0);
    col[COLMENUGRAY]  = makecol(0x68, 0x68, 0x68);
    col[COLMENUBLACK] = makecol(0x40, 0x40, 0x40);
    col[COLBLACK]     = makecol(0x00, 0x00, 0x00);
    col[COLDARKGRAY]  = makecol(0x30, 0x30, 0x30);
    col[COLSHADOW]    = makecol(0x18, 0x18, 0x18);
    col[COLDARKORA]   = makecol(0xBF, 0x70, 0x00);
    col[COLENER3]     = makecol(0x7D, 0x64, 0xFF);
    col[COLDARKGREEN] = makecol(0x00, 0x77, 0x00);
    col[COLINFO] = col[COLDARKORA];     //color of statusbar non-game info (hostname, IP, net traffic)

    //teams 0 & 1 (playernum(0..15) / 8) colors:
    teamcol[0] = col[COLRED ];
    teamcol[1] = col[COLBLUE];

    // wild flag colour
    teamcol[2] = col[COLGREEN];

    //light colours for text
    teamlcol[0] = col[COLLRED ];
    teamlcol[1] = col[COLLBLUE];

    // dark colours for team text bg
    teamdcol[0] = col[COLBRED ];
    teamdcol[1] = col[COLBBLUE];

    setPlaygroundColors();
}

void Graphics::setPlaygroundColors() {
    col[COLGROUND] = makecol(groundCol[0], groundCol[1], groundCol[2]);
    col[COLWALL] = makecol(wallCol[0], wallCol[1], wallCol[2]);
}

void Graphics::reset_playground_colors() {
    static const int groundInit[] = { 0x10, 0x40, 0x00 };
    static const int wallInit  [] = { 0x30, 0xC0, 0x00 };
    for (int i = 0; i < 3; ++i) {
        groundCol[i] = groundInit[i];
        wallCol  [i] = wallInit  [i];
    }
    setPlaygroundColors();
}

void Graphics::random_playground_colors() {
    for (int i = 0; i < 3; ++i) {
        groundCol[i] = rand() % 256;
        wallCol  [i] = rand() % 256;
    }
    setPlaygroundColors();
}

int Graphics::chat_lines() const {
    return max(1, ply / (text_height(font) + 3));
}

int Graphics::chat_max_lines() const {
    return max(1, (ply + scale(plh)) / (text_height(font) + 3));
}

void Graphics::clear() {
    make_background(drawbuf);
}

bool Graphics::depthAvailable(int depth) const {
    return !getResolutions(depth, false).empty();   //#opt: no need to go through all modes or create a vector
}

vector<ScreenMode> Graphics::getResolutions(int depth, bool forceTryIfNothing) const {  // returns a sorted list of unique resolutions
    vector<ScreenMode> mvec;

    #ifdef ALLEGRO_WINDOWS
    GFX_MODE_LIST* modes = get_gfx_mode_list(GFX_DIRECTX);
    #else
    #ifdef GFX_XWINDOWS_FULLSCREEN
    GFX_MODE_LIST* modes = get_gfx_mode_list(GFX_XWINDOWS_FULLSCREEN);
    #else
    GFX_MODE_LIST* modes = 0;
    #endif
    #endif
    if (modes) {
        int depth2 = (depth == 16) ? 15 : depth;    // 15 and 16 bit modes are considered equal
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
            log.error(_("Syntax error in gfxmodes.txt, line '$1'.", line));
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

bool Graphics::reset_video_mode(int width, int height, int depth, bool windowed, int pages) {
    log("Setting video mode: %d×%d×%d %s", width, height, depth, windowed ? "windowed" : "fullscreen");
    set_color_depth(depth);

    int virtual_w = 0, virtual_h = 0;
    #ifdef ALLEGRO_VRAM_SINGLE_SURFACE
    if (pages > 1) {
        virtual_w = width;
        virtual_h = height * pages;
    }
    #else
    (void)pages;
    #endif

    if (set_gfx_mode(windowed ? WINMODE : FULLMODE, width, height, virtual_w, virtual_h)) {
        log("Error: '%s'", allegro_error);
        if (depth == 16) {  // try equivalent 15-bit mode too
            set_color_depth(15);
            if (set_gfx_mode(windowed ? WINMODE : FULLMODE, width, height, virtual_w, virtual_h)) {
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

void Graphics::predraw(const Room& room, int texRoomX, int texRoomY, const vector< pair<int, const WorldCoords*> >& flags, const vector< pair<int, const WorldCoords*> >& spawns, bool grid) {
    // the room is textured like it's the room at coordinates (texRoomX,texRoomY)
    // this means moving the texture offsetting origin to the top left of room (0,0)
    int texOffsetBaseX = - texRoomX * iround(plw * scr_mul);
    int texOffsetBaseY = - texRoomY * iround(plh * scr_mul);
    acquire_bitmap(background);
    make_background(background);
    if (antialiasing) {
        SceneAntialiaser scene;
        scene.setScaling(.01, .01, scr_mul - .02 / plh);    // cut .01 pixels from both top and bottom edge, .01 from left and something from the right...

        // add bottom ground to drawing system
        scene.addRectangle(0, 0, plw, plh, 0);

        // add additional ground textures
        for (vector<WallBase*>::const_iterator wi = room.readGround().begin(); wi != room.readGround().end(); ++wi)
            scene.addWall(*wi, (*wi)->texture());

        // add flag markers as overlays
        const double fr = flagpos_radius;
        for (int fi = 0; fi < static_cast<int>(flags.size()); ++fi) {
            const double fx = flags[fi].second->x, fy = flags[fi].second->y;
            scene.addRectangle(fx - fr, fy - fr, fx + fr, fy + fr, fi + floor_texture.size() + wall_texture.size(), true);
        }

        // add walls
        const int texShift = floor_texture.size();
        for (vector<WallBase*>::const_iterator wi = room.readWalls().begin(); wi != room.readWalls().end(); ++wi)
            scene.addWall(*wi, (*wi)->texture() + texShift);

        // clip
        scene.setClipping(0, 0, plw, plh);
        scene.clipAll();

        // prepare the textures
        vector<TextureData> textures;

        TextureData backupTexture;
        TextureData td;
        if (floor_texture.front())
            backupTexture.setTexture(floor_texture.front(), texOffsetBaseX, texOffsetBaseY);
        else
            backupTexture.setSolid(col[COLGROUND]);
        for (vector<Bitmap>::const_iterator ti = floor_texture.begin(); ti != floor_texture.end(); ++ti) {
            if (*ti) { td.setTexture(*ti, texOffsetBaseX, texOffsetBaseY); textures.push_back(td); }
            else textures.push_back(backupTexture);
        }

        if (wall_texture.front())
            backupTexture.setTexture(wall_texture.front(), texOffsetBaseX, texOffsetBaseY);
        else
            backupTexture.setSolid(col[COLWALL]);
        for (vector<Bitmap>::const_iterator ti = wall_texture.begin(); ti != wall_texture.end(); ++ti) {
            if (*ti) { td.setTexture(*ti, texOffsetBaseX, texOffsetBaseY); textures.push_back(td); }
            else textures.push_back(backupTexture);
        }

        for (vector< pair<int, const WorldCoords*> >::const_iterator fi = flags.begin(); fi != flags.end(); ++fi) {
            td.setFlagmarker(teamcol[fi->first], fi->second->x * scr_mul, fi->second->y * scr_mul, flagpos_radius * scr_mul);   // note: assumes 0,0,1. scaling
            textures.push_back(td);
        }
        // draw
        Texturizer tex(roombg, 0, 0, textures);
        scene.render(tex);
        tex.finalize();
    }
    else {
        // draw floor
        if (floor_texture.front()) {
            drawing_mode(DRAW_MODE_COPY_PATTERN, floor_texture.front(), texOffsetBaseX, texOffsetBaseY);
            rectfill(roombg, 0, 0, roombg->w - 1, roombg->h - 1, col[COLGROUND]);
            solid_mode();
        }
        else
            clear_to_color(roombg, col[COLGROUND]);
        predraw_room_ground(room, texOffsetBaseX, texOffsetBaseY);
        // draw flag position marks
        for (vector< pair<int, const WorldCoords*> >::const_iterator fi = flags.begin(); fi != flags.end(); ++fi)
            draw_flagpos_mark(fi->first, fi->second->x, fi->second->y);
        // draw walls
        predraw_room_walls(room, texOffsetBaseX, texOffsetBaseY);
    }
    for (vector< pair<int, const WorldCoords*> >::const_iterator si = spawns.begin(); si != spawns.end(); ++si)
        circlefill(roombg, scale(si->second->x), scale(si->second->y), scale(PLAYER_RADIUS), teamcol[si->first]);
    if (grid) {
        for (int y = 1; y < 12; ++y)
            hline(roombg, 0, scale(plh * y / 12.), scale(plw), y == 6 ? col[COLYELLOW] : col[COLWHITE]);
        for (int x = 1; x < 16; ++x)
            vline(roombg, scale(plw * x / 16.), 0, scale(plh), x == 8 ? col[COLYELLOW] : col[COLWHITE]);
    }
    if (TEST_FALL_ON_WALL)
        for (int y = 0; y < plh; y += 2)
            for (int x = 0; x < plw; x += 2)
                putpixel(roombg, scale(x), scale(y), room.fall_on_wall(x, y, 15) ? makecol(255, 0, 0) : makecol(255, 255, 255));
    draw_minimap_background();
    release_bitmap(background);
}

void Graphics::draw_empty_background(bool map_ready) {
    make_background(drawbuf);
    if (map_ready && show_minimap)
        masked_blit(minibg, drawbuf, 0, 0, mmx, mmy, minibg->w, minibg->h);
}

void Graphics::draw_background() {
    blit(background, drawbuf, 0, 0, 0, 0, background->w, background->h);
}

void Graphics::make_background(BITMAP* buffer) {
    if (bg_texture) {
        drawing_mode(DRAW_MODE_COPY_PATTERN, bg_texture, 0, 0);
        rectfill(buffer, 0, 0, buffer->w - 1, buffer->h - 1, 0);
        solid_mode();
    }
    else
        clear_to_color(buffer, col[COLBLACK]);
}

void Graphics::predraw_room_ground(const Room& room, int texOffsetBaseX, int texOffsetBaseY) {
    draw_room_ground(roombg, room, 0, 0, texOffsetBaseX, texOffsetBaseY, scr_mul, col[COLGROUND], true);
}

void Graphics::draw_room_ground(BITMAP* buffer, const Room& room, double x, double y, int texOffsetBaseX, int texOffsetBaseY, double scale, int color, bool texture) {
    for (vector<WallBase*>::const_iterator wi = room.readGround().begin(); wi != room.readGround().end(); ++wi)
        draw_wall(buffer, *wi, x, y, texOffsetBaseX, texOffsetBaseY, scale, color, texture ? get_floor_texture((*wi)->texture()) : 0);
}

void Graphics::predraw_room_walls(const Room& room, int texOffsetBaseX, int texOffsetBaseY) {
    draw_room_walls(roombg, room, 0, 0, texOffsetBaseX, texOffsetBaseY, scr_mul, col[COLWALL], true);
}

void Graphics::draw_room_walls(BITMAP* buffer, const Room& room, double x, double y, int texOffsetBaseX, int texOffsetBaseY, double scale, int color, bool texture) {
    for (vector<WallBase*>::const_iterator wi = room.readWalls().begin(); wi != room.readWalls().end(); ++wi)
        draw_wall(buffer, *wi, x, y, texOffsetBaseX, texOffsetBaseY, scale, color, texture ? get_wall_texture((*wi)->texture()) : 0);
}

void Graphics::draw_wall(BITMAP* buffer, WallBase* wall, double x, double y, int texOffsetBaseX, int texOffsetBaseY, double scale, int color, BITMAP* tex) {
    RectWall* rwp = dynamic_cast<RectWall*>(wall);
    if (rwp) {
        draw_rect_wall(buffer, *rwp, x, y, texOffsetBaseX, texOffsetBaseY, scale, color, tex);
        return;
    }
    TriWall * twp = dynamic_cast<TriWall *>(wall);
    if (twp) {
        draw_tri_wall (buffer, *twp, x, y, texOffsetBaseX, texOffsetBaseY, scale, color, tex);
        return;
    }
    CircWall* cwp = dynamic_cast<CircWall*>(wall);
    nAssert(cwp);
    draw_circ_wall    (buffer, *cwp, x, y, texOffsetBaseX, texOffsetBaseY, scale, color, tex);
}

void Graphics::draw_rect_wall(BITMAP* buffer, const RectWall& wall, double x0, double y0, int texOffsetBaseX, int texOffsetBaseY, double scale, int color, BITMAP* texture) {
    if (texture)
        drawing_mode(DRAW_MODE_COPY_PATTERN, texture, texOffsetBaseX, texOffsetBaseY);
    rectfill(buffer, iround(x0 + scale * wall.x1()    ), iround(y0 + scale * wall.y1()    ),
                     iround(x0 + scale * wall.x2() - 1), iround(y0 + scale * wall.y2() - 1), color);
    if (texture)
        solid_mode();
}

void Graphics::draw_tri_wall(BITMAP* buffer, const TriWall& wall, double x0, double y0, int texOffsetBaseX, int texOffsetBaseY, double scale, int color, BITMAP* texture) {
    if (texture)
        drawing_mode(DRAW_MODE_COPY_PATTERN, texture, texOffsetBaseX, texOffsetBaseY);
    triangle(buffer,
        iround(x0 + scale * wall.x1()), iround(y0 + scale * wall.y1()),
        iround(x0 + scale * wall.x2()), iround(y0 + scale * wall.y2()),
        iround(x0 + scale * wall.x3()), iround(y0 + scale * wall.y3()), color);
    if (texture)
        solid_mode();
}

void Graphics::draw_circ_wall(BITMAP* buffer, const CircWall& wall, double x0, double y0, int texOffsetBaseX, int texOffsetBaseY, double scale, int color, BITMAP* texture) {
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

//draw a flag  team 0/1   x, y: coord relative to playarea
void Graphics::draw_flag(int team, int x, int y) {
    x = scale(x);
    y = scale(y);
    const Bitmap& sprite = flag_sprite[team];
    if (sprite) {
        draw_sprite(drawbuf, sprite, plx + x - sprite->w / 2, ply + y - sprite->h / 2);
        return;
    }
    y += scale(20);
    //draw flagpole
    rectfill(drawbuf,
        plx + x - scale(3),
        ply + y - scale(40),
        plx + x + scale(3),
        ply + y,
        col[COLYELLOW]
    );
    //draw the flag itself
    rectfill(drawbuf,
        plx + x,
        ply + y - scale(38),
        plx + x + scale(20),
        ply + y - scale(20),
        teamcol[team]
    );
}

// Minimap functions

void Graphics::draw_mini_flag(int team, const Flag& flag, const Map& map) {
    const double px = static_cast<double>(flag.position().px * plw + flag.position().x) / (plw * map.w);
    const double py = static_cast<double>(flag.position().py * plh + flag.position().y) / (plh * map.h);
    const int pix = static_cast<int>(mmx + minimap_start_x + px * minimap_w);
    const int piy = static_cast<int>(mmy + minimap_start_y + py * minimap_h);
    const int scl = minimap_place_w;
    //draw flagpole
    rectfill(drawbuf, pix, piy - scl / 32, pix + scl / 160 - 1, piy, col[COLYELLOW]);
    //draw the flag itself
    rectfill(drawbuf, pix + 1, piy - scl / 32, pix + scl / 32, piy - scl / 80, teamcol[team]);
}

void Graphics::draw_minimap_player(const Map& map, const ClientPlayer& player) {
    const pair<int, int> coords = calculate_minimap_coordinates(map, player);
    const int a = scale(1);
    const int b = a / 2;
    rectfill(drawbuf, coords.first - a, coords.second - a, coords.first + a, coords.second + a, teamcol[player.team()]);
    if (a > 0) // else, only one pixel is shown; let it be the team color
        rectfill(drawbuf, coords.first - b, coords.second - b, coords.first + b, coords.second + b, col[player.color()]);
}

void Graphics::draw_minimap_me(const Map& map, const ClientPlayer& player, double time) {
    const pair<int, int> coords = calculate_minimap_coordinates(map, player);
    if (static_cast<int>(time * 15) % 3 > 0) {
        circlefill(drawbuf, coords.first, coords.second, scale(2), col[COLYELLOW]);
        circlefill(drawbuf, coords.first, coords.second, scale(1), teamlcol[player.team()]);
    }
    else
        circlefill(drawbuf, coords.first, coords.second, scale(2), 0);
}

pair<int, int> Graphics::calculate_minimap_coordinates(const Map& map, const ClientPlayer& player) const {
    const double px = (player.roomx * plw + player.lx) / static_cast<double>(plw * map.w);
    const double py = (player.roomy * plh + player.ly) / static_cast<double>(plh * map.h);
    const int x = static_cast<int>(mmx + px * minimap_w) + minimap_start_x;
    const int y = static_cast<int>(mmy + py * minimap_h) + minimap_start_y;
    return pair<int, int>(x, y);
}

void Graphics::draw_minimap_room(const Map& map, int rx, int ry, float visibility) {
    if (visibility >= .99)
        return;
    const int x1 = mmx + minimap_start_x +  rx      * minimap_w / map.w;
    const int y1 = mmy + minimap_start_y +  ry      * minimap_h / map.h;
    const int x2 = mmx + minimap_start_x + (rx + 1) * minimap_w / map.w - 1;
    const int y2 = mmy + minimap_start_y + (ry + 1) * minimap_h / map.h - 1;
    if ((visibility <= 0.01 || min_transp) && minibg_fog)
        blit(minibg_fog, drawbuf, x1 - mmx, y1 - mmy, x1, y1, x2 - x1 + 1, y2 - y1 + 1);
    else {
        drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
        set_trans_blender(0, 0, 0, static_cast<int>(fogOfWarMaxAlpha * (1. - visibility)));
        rectfill(drawbuf, x1, y1, x2, y2, col[COLFOGOFWAR]);
        solid_mode();
    }
}

void Graphics::draw_minimap_background() {
    if (show_minimap)
        masked_blit(minibg, background, 0, 0, mmx, mmy, minibg->w, minibg->h);
}

void Graphics::update_minimap_background(const Map& map) {
    update_minimap_background(minibg, map);
    if (minibg_fog) {
        clear_to_color(minibg_fog, col[COLBLACK]);
        masked_blit(minibg, minibg_fog, 0, 0, 0, 0, minibg->w, minibg->h);
        drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
        set_trans_blender(0, 0, 0, fogOfWarMaxAlpha);
        rectfill(minibg_fog, 0, 0, minibg_fog->w - 1, minibg_fog->h - 1, col[COLFOGOFWAR]);
        solid_mode();
    }
}

class MinimapHelper {   // small helper solely for Graphics::update_minimap_background
public:
    MinimapHelper(BITMAP* buffer_, double x0_, double y0_, double plw_, double plh_, double scale_)
        : buffer(buffer_), x0(x0_), y0(y0_), plw(plw_), plh(plh_), scale(scale_) { }

    void paintByFlags(int rx, int ry, const vector<const WorldCoords*>* teamFlags, int* teamColor, int color);
    pair<int, int> flagCoords(const WorldCoords& coords) const;

private:
    BITMAP* buffer;
    double x0, y0, plw, plh, scale;
};

// Paint within room (rx,ry) every pixel already of the given color with a team color or black according to which color flag or neither is nearest to it.
void MinimapHelper::paintByFlags(int rx, int ry, const vector<const WorldCoords*>* teamFlags, int* teamColor, int color) {
    const int xmin = static_cast<int>(x0 + plw * scale *  rx         );
    const int xmax = static_cast<int>(x0 + plw * scale * (rx + 1) - 1);
    const int ymin = static_cast<int>(y0 + plh * scale *  ry         );
    const int ymax = static_cast<int>(y0 + plh * scale * (ry + 1) - 1);

    for (int y = ymin; y <= ymax; ++y) {
        const double roomy = (y + 1 - ymin) / scale;
        for (int x = xmin; x <= xmax; ++x) {
            if (getpixel(buffer, x, y) != color)
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
                putpixel(buffer, x, y, 0);  // don't paint about equally distant pixels
        }
    }
}

pair<int, int> MinimapHelper::flagCoords(const WorldCoords& coords) const {
    // the coords are not rounded because the max coord is already (minimap_w - small safety)
    const int x = static_cast<int>(x0 + (coords.px * plw + coords.x) * scale);
    const int y = static_cast<int>(y0 + (coords.py * plh + coords.y) * scale);
    return pair<int, int>(x, y);
}

void Graphics::update_minimap_background(BITMAP* buffer, const Map& map, bool save_map_pic) {
    // transparent background
    clear_to_color(buffer, bitmap_mask_color(buffer));
    const int room_border_col = save_map_pic ? col[COLMENUGRAY] : makecol(0x30, 0x30, 0x30);

    if (map.w == 0 || map.h == 0)
        return;

    // Calculate new minimap size.
    if (map.w * 4 * minimap_place_h > map.h * 3 * minimap_place_w) {
        minimap_w = minimap_place_w;
        minimap_h = static_cast<int>(static_cast<double>(minimap_w * map.h * 3) / (map.w * 4.)) + 1;    // add 1 because w should be the relatively smaller one for safety (because it's used in determining 'scale' below
    }
    else {
        minimap_h = minimap_place_h;
        minimap_w = static_cast<int>(static_cast<double>(minimap_h * map.w * 4) / (map.h * 3.));    // truncate to make sure w is the relatively smaller one
    }

    minimap_start_x = (minimap_place_w - minimap_w) / 2;
    minimap_start_y = (minimap_place_h - minimap_h) / 2;

    const double actual_start_x = minimap_start_x + .005;   // .005 is a safety to make sure we stay within the bitmap even with small calculation errors
    const double actual_start_y = minimap_start_y + .005 * map.h / map.w;   // in proportion, because scale (below) is calculated from x axis, and affects y in a different amount of pixels

    const double maxx = plw * map.w;
    const double maxy = plh * map.h;
    const double scale = (minimap_w - .01) / maxx;  // -.01 is a safety to make sure we stay within the bitmap even with small calculation errors

    SceneAntialiaser scene;
    scene.setScaling(actual_start_x, actual_start_y, scale);

    // add background (all floors)
    scene.addRectangle(0, 0, maxx, maxy, 0);

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
        const double by = actual_start_y + y * plh * scale;
        for (int x = 0; x < map.w; x++) {
            const double bx = actual_start_x + x * plw * scale;
            scene.setScaling(bx, by, scale);
            scene.setClipping(0, 0, plw, plh);
            const Room& room = map.room[x][y];
            for (vector<WallBase*>::const_iterator wi = room.readWalls().begin(); wi != room.readWalls().end(); ++wi)
                scene.addWallClipped(*wi, 1);
        }
    }

    // draw
    vector<TextureData> colors;
    TextureData td;
    td.setSolid(0);                 colors.push_back(td);   // ground
    td.setSolid(col[COLDARKGREEN]); colors.push_back(td);   // walls
    td.setSolid(room_border_col);   colors.push_back(td);   // room boundaries
    Texturizer tex(buffer, 0, 0, colors);
    scene.render(tex);
    tex.finalize();

    // colorize bases
    MinimapHelper helper(buffer, actual_start_x, actual_start_y, plw, plh, scale);
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
                        if (getpixel(buffer, coords.first, coords.second) != 0)
                            onWall = true;
                    }
                }
            if (teamFlags[0].empty() && teamFlags[1].empty())
                continue;
            if (onWall) {
                // The normal area-connected-to-the-flag paint algorithm can't be used for any flags in this case.
                // Instead, paint every floor pixel in the room with the nearest flag's color.
                helper.paintByFlags(rx, ry, teamFlags, teamdcol, 0);
                continue;
            }
            // Spread paint from each flag pos to all reachable space in the room (by floodfill).
            // When flags of both teams would fill the same space, paint each pixel with the nearest flag's color.
            bool bothFlags = (!teamFlags[0].empty() && !teamFlags[1].empty());
            for (int t = 0; t < 2; ++t)
                for (vector<const WorldCoords*>::const_iterator fi = teamFlags[t].begin(); fi != teamFlags[t].end(); ++fi) {
                    const WorldCoords& flag = **fi;
                    const pair<int, int> coords = helper.flagCoords(flag);
                    if (getpixel(buffer, coords.first, coords.second) != 0) // this only happens when the flag has already been fully taken care of
                        continue;
                    if (!bothFlags) {
                        floodfill(buffer, coords.first, coords.second, teamdcol[t]);
                        continue;
                    }
                    const int tempColor = makecol(0, 255, 255); // this is an opportunity for bugs if someone decides to use bright cyan as a regular map color
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
                        helper.paintByFlags(rx, ry, problemFlags, teamdcol, tempColor);
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
                rectfill(buffer, px, py - static_cast<int>(room_w / 12), px + static_cast<int>(room_w / 60) - 1, py, col[COLYELLOW]);
                //draw the flag
                rectfill(buffer, px + static_cast<int>(room_w / 60), py - static_cast<int>(room_w / 12),
                                 px + static_cast<int>(room_w / 12), py - static_cast<int>(room_w / 30), teamcol[t]);
            }
            else
                circle(buffer, px, py, minimap_place_w / 50, teamcol[t]);
        }
    }
}

//draws a basic player object
void Graphics::draw_player(int x, int y, int team, int pli, int gundir, double hitfx, bool item_power, int alpha, double time) {
    x = scale(x);
    y = scale(y);
    int pc1 = teamcol[team];
    int pc2 = col[pli];
    //blink player when hit
    const double deltafx = hitfx - time;
    if (deltafx > 0) {
        const int rgb = static_cast<int>(70.0 + deltafx * 600.0);  // var 180
        pc1 = pc2 = makecol(rgb, rgb, rgb);
    }
    else if (item_power) {
        if (static_cast<int>(time * 10) % 2) {
            pc1 = col[COLWHITE];
            pc2 = col[COLCYAN];
        }
    }

    const int player_radius = scale(PLAYER_RADIUS);

    BITMAP* sprite = 0;
    if (item_power && player_sprite_power && static_cast<int>(time * 10) % 2)
        sprite = player_sprite_power;
    else {
        nAssert(team == 0 || team == 1);
        nAssert(pli >= 0 && pli < MAX_PLAYERS / 2);
        sprite = player_sprite[team][pli];
    }
    if (sprite) {
        if (alpha < 255)
            rotate_trans_sprite(drawbuf, sprite, plx + x, ply + y, itofix(gundir * 32), alpha);
        else
            rotate_sprite(drawbuf, sprite, plx + x - sprite->w / 2, ply + y - sprite->h / 2, itofix(gundir * 32));
        return;
    }

    if (alpha < 255) {
        set_trans_blender(0, 0, 0, alpha);
        drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
    }

    // outer color: team color
    circlefill(drawbuf, plx + x, ply + y, player_radius, pc1);
    // inner color: self color
    circlefill(drawbuf, plx + x, ply + y, player_radius * 2 / 3, pc2);
    // gun direction
    int xg, yg;
    switch (gundir) {
    /*break;*/ case 0: xg = 40; yg =  0;
        break; case 1: xg = 28; yg = 28;
        break; case 2: xg =  0; yg = 40;
        break; case 3: xg =-28; yg = 28;
        break; case 4: xg =-40; yg =  0;
        break; case 5: xg =-28; yg =-28;
        break; case 6: xg =  0; yg =-40;
        break; case 7: xg = 28; yg =-28;
        break; default: xg = 0; yg =  0;
    }
    xg = scale(xg * 0.7);
    yg = scale(yg * 0.7);
    // draw the gun
    switch (gundir) {
    /*break;*/ case 0: case 4:
            rectfill(drawbuf, plx + x + xg / 2, ply + y + yg - 1, plx + x + xg, ply + y + yg + 1, pc1);
        break; case 2: case 6:
            rectfill(drawbuf, plx + x + xg - 1, ply + y + yg / 2, plx + x + xg + 1, ply + y + yg, pc1);
        break; case 1: case 5:
            line(drawbuf, plx + x + xg / 2 + 0, ply + y + yg / 2 + 1, plx + x + xg - 1, ply + y + yg + 0, pc1);
            line(drawbuf, plx + x + xg / 2 + 0, ply + y + yg / 2 + 0, plx + x + xg + 0, ply + y + yg + 0, pc1);
            line(drawbuf, plx + x + xg / 2 + 1, ply + y + yg / 2 + 0, plx + x + xg + 0, ply + y + yg - 1, pc1);
        break; case 3: case 7:
            line(drawbuf, plx + x + xg / 2 + 0, ply + y + yg / 2 - 1, plx + x + xg - 1, ply + y + yg + 0, pc1);
            line(drawbuf, plx + x + xg / 2 + 0, ply + y + yg / 2 + 0, plx + x + xg + 0, ply + y + yg + 0, pc1);
            line(drawbuf, plx + x + xg / 2 + 1, ply + y + yg / 2 + 0, plx + x + xg + 0, ply + y + yg + 1, pc1);
    }

    solid_mode();
}

void Graphics::set_alpha_channel(BITMAP* bitmap, BITMAP* alpha) {
    set_write_alpha_blender();
    drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
    if (alpha)
        draw_trans_sprite(bitmap, alpha, 0, 0);
    else    // maximum alpha level
        rectfill(bitmap, 0, 0, bitmap->w - 1, bitmap->h - 1, 255);
    solid_mode();
}

void Graphics::rotate_trans_sprite(BITMAP* bmp, BITMAP* sprite, int x, int y, fixed angle, int alpha) { // x,y are destination coords of the sprite center
    nAssert(sprite);
    nAssert(sprite->w == sprite->h);    // if otherwise, would have to use max(sprite->w, sprite->h) below, and use more complex coords in rotate
    // make room so that rotating won't clip the corners off
    const int size = sprite->h + sprite->h / 2;
    Bitmap buffer = create_bitmap(size, size);
    nAssert(buffer);
    clear_to_color(buffer, bitmap_mask_color(buffer));
    rotate_sprite(buffer, sprite, sprite->h / 4, sprite->h / 4, angle);
    drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
    set_trans_blender(0, 0, 0, alpha);
    draw_trans_sprite(bmp, buffer, x - buffer->w / 2, y - buffer->h / 2);
    solid_mode();
}

void Graphics::rotate_alpha_sprite(BITMAP* bmp, BITMAP* sprite, int x, int y, fixed angle) {    // x,y are destination coords of the sprite center
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

void Graphics::draw_player_dead(const ClientPlayer& player) {
    const int x = scale(player.lx);
    const int y = scale(player.ly);
    BITMAP* sprite = dead_sprite[player.team()];
    if (sprite)
        rotate_alpha_sprite(drawbuf, sprite, plx + x, ply + y, itofix(player.gundir * 32));
    else {
        drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
        set_trans_blender(0, 0, 0, 90);
        const int plrScale = scale(PLAYER_RADIUS * 10);
        ellipsefill(drawbuf, plx + x - plrScale / 25, ply + y + plrScale / 20, plrScale / 9, plrScale / 10, col[COLRED]);
        ellipsefill(drawbuf, plx + x                , ply + y - plrScale / 30, plrScale / 8, plrScale / 10, col[COLRED]);
        ellipsefill(drawbuf, plx + x + plrScale / 50, ply + y + plrScale / 40, plrScale / 8, plrScale / 10, col[COLRED]);
        solid_mode();
    }
}

void Graphics::draw_virou_sorvete(int x, int y) {
    x = scale(x);
    y = scale(y);
    if (ice_cream)
        draw_sprite(drawbuf, ice_cream, plx + x - ice_cream->w / 2, ply + y - ice_cream->h / 2);
    else {
        ellipsefill(drawbuf, plx + x           , ply + y            , scale(6), scale(15), col[COLORA  ]);
        circlefill (drawbuf, plx + x - scale(8), ply + y - scale(10), scale(8),            col[COLBLUE ]);
        circlefill (drawbuf, plx + x + scale(8), ply + y - scale(10), scale(8),            col[COLMAG  ]);
        circlefill (drawbuf, plx + x           , ply + y - scale(20), scale(8),            col[COLGREEN]);
        textout_centre_ex(drawbuf, font, _("VIROU"   ).c_str(), plx + x, ply + y - scale(38) - 10, col[COLWHITE], -1);
        textout_centre_ex(drawbuf, font, _("SORVETE!").c_str(), plx + x, ply + y - scale(38)     , col[COLWHITE], -1);
    }
}

void Graphics::draw_gun_explosion(int x, int y, int rad, int team) {
    x = scale(x);
    y = scale(y);
    const int c = makecol(team == 0 ? rand() % 128 + 128 : rand() % 256,
                          rand() % 256,
                          team == 1 ? rand() % 128 + 128 : rand() % 256);
    circle(drawbuf, plx + x, ply + y, scale(rad), c);
}

void Graphics::draw_deathbringer_smoke(int x, int y, double time, double alpha) {
    x = scale(x);
    y = scale(y);
    const int effAlpha = static_cast<int>((120 - time * 200.0) * alpha);
    if (effAlpha <= 0 || (min_transp && effAlpha <= 10))
        return;
    int c;
    const double drad = 3.0 + 9.0 * (0.6 - time);
    int rad = scale(drad);
    if (min_transp) {
        const int rgb = 120 - effAlpha;
        c = makecol(rgb, rgb, rgb);
        rad /= 2;
    }
    else {
        c = col[COLBLACK];
        drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
        set_trans_blender(0, 0, 0, effAlpha);
    }
    const int subdist = scale(96.0 - drad * 8.0);
    circlefill(drawbuf, plx + x, ply + y - subdist, rad, c);
    solid_mode();
}

void Graphics::draw_deathbringer(int x, int y, int team, double time) {
    x = scale(x);
    y = scale(y);
    //radius
    int rad;
    if (time < 1.0)
        rad = scale(time * 100);
    else
        rad = scale(100 + (time - 1.0) * (time - 1.0) * 800);
    const int maxxd = max(x, scale(plw) - x);
    const int maxyd = max(y, scale(plh) - y);
    if (maxxd * maxxd + maxyd * maxyd >= rad * rad) {
        //brightening ring
        for (int e = 0; e < scale(30); e++, rad++) {
            int co;
            if (team == 0)
                co = makecol(14 + static_cast<int>(8 * e / scr_mul), 0, 0);
            else
                co = makecol(0, 0, 14 + static_cast<int>(8 * e / scr_mul));
            circle(drawbuf, plx + x, ply + y, rad, co);
        }
        //darkening ring
        for (int e = 0; e < scale(10); e++, rad++) {
            int co;
            if (team == 0)
                co = makecol(255 - static_cast<int>(14 * e / scr_mul), 0, 0);
            else
                co = makecol(0, 0, 255 - static_cast<int>(14 * e / scr_mul));
            circle(drawbuf, plx + x    , ply + y    , rad, co);
            circle(drawbuf, plx + x + 1, ply + y    , rad, co);
            circle(drawbuf, plx + x    , ply + y + 1, rad, co);
        }
    }
}

void Graphics::draw_deathbringer_affected(int x, int y, int team, int alpha) {
    x = scale(x);
    y = scale(y);
    drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
    set_trans_blender(0, 0, 0, alpha / 2);
    for (int i = 0; i < 5; i++)
        circlefill(drawbuf, plx + x + scale(rand() % 40 - 20), ply + y + scale(rand() % 40 - 20), scale(15), teamcol[team]);
    for (int i = 0; i < 5; i++)
        circlefill(drawbuf, plx + x + scale(rand() % 40 - 20), ply + y + scale(rand() % 40 - 20), scale(15), 0);
    solid_mode();
}

void Graphics::draw_deathbringer_carrier_effect(int x, int y, int alpha) {
    if (min_transp)
        return;
    x = scale(x);
    y = scale(y);
    BITMAP* buffer;
    if (alpha == 255)
        buffer = db_effect;
    else {
        buffer = create_bitmap_ex(32, db_effect->w, db_effect->h);
        clear_to_color(buffer, col[COLBLACK]);
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
    draw_trans_sprite(drawbuf, buffer, plx + x - db_effect->w / 2, ply + y - db_effect->h / 2);
    solid_mode();
    if (buffer != db_effect)
        destroy_bitmap(buffer);
}

void Graphics::draw_shield(int x, int y, int r, int alpha, int team, int direction) {
    x = scale(x);
    y = scale(y);
    r = scale(r);
    if ((team == 0 || team == 1) && shield_sprite[team]) {
        BITMAP* sprite = shield_sprite[team];
        if (alpha < 255)
            rotate_trans_sprite(drawbuf, sprite, plx + x, ply + y, itofix(direction * 32), alpha);
        else
            rotate_sprite(drawbuf, sprite, plx + x - sprite->w / 2, ply + y - sprite->h / 2, itofix(direction * 32));
        return;
    }
    const int v[] = { scale(3), scale(5), scale(9) };
    if (alpha < 255) {
        drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
        set_trans_blender(0, 0, 0, alpha);
    }
    if (team == 0)
        for (int i = 0, c = rand() % 256; i < 3; i++)
            ellipse(drawbuf, plx + x, ply + y, r + rand() % v[i], r + rand() % v[i], makecol(255, c, c));
    else if (team == 1)
        for (int i = 0, c = rand() % 256; i < 3; i++)
            ellipse(drawbuf, plx + x, ply + y, r + rand() % v[i], r + rand() % v[i], makecol(c, c, 255));
    else    // powerup lying on the ground
        for (int i = 0; i < 3; i++)
            ellipse(drawbuf, plx + x, ply + y, r + rand() % v[i], r + rand() % v[i], makecol(rand() % 256, rand() % 256, rand() % 256));
    solid_mode();
}

void Graphics::draw_player_name(const string& name, int x, int y, int team, bool highlight) {
    x = scale(x);
    y = scale(y);
    const int c = highlight ? col[COLYELLOW] : col[COLWHITE];
    print_text_border_centre(name, plx + x, ply + y - scale(3 * PLAYER_RADIUS / 2) - text_height(font), c, teamdcol[team], -1);
}

void Graphics::draw_rocket(const Rocket& rocket, bool shadow, double time) {
    const int x = scale(rocket.x);
    const int y = scale(rocket.y);
    BITMAP* sprite = (rocket.power ? power_rocket_sprite[rocket.team] : rocket_sprite[rocket.team]);
    if (sprite)
        rotate_sprite(drawbuf, sprite, plx + x - sprite->w / 2, ply + y - sprite->h / 2, itofix(rocket.direction * 32));
    else if (rocket.power) {
        //draw rocket shadow
        if (shadow)
            ellipsefill(drawbuf, plx + x, ply + y + scale(POWER_ROCKET_RADIUS + 8), scale(POWER_ROCKET_RADIUS), scale(3), col[COLSHADOW]);
        //draw the rocket
        if (static_cast<int>(time * 30) % 2)
            circlefill(drawbuf, plx + x, ply + y, scale(POWER_ROCKET_RADIUS), col[COLWHITE]);
        else
            circlefill(drawbuf, plx + x, ply + y, scale(POWER_ROCKET_RADIUS), teamlcol[rocket.team]);
    }
    else {
        //draw rocket shadow
        if (shadow)
            ellipsefill(drawbuf, plx + x, ply + y + scale(ROCKET_RADIUS + 8), scale(ROCKET_RADIUS), scale(2), col[COLSHADOW]);
        //draw the rocket
        circlefill(drawbuf, plx + x, ply + y, scale(ROCKET_RADIUS), teamcol[rocket.team]);
    }
}

void Graphics::draw_flagpos_mark(int team, int flag_x, int flag_y) {
    drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
    const int radius = scale(flagpos_radius);
    const int step = 300 / radius;
    for (int y = scale(flag_y) - radius; y < scale(flag_y) + radius; y++)
        for (int x = scale(flag_x) - radius; x < scale(flag_x) + radius; x++) {
            const int dx = scale(flag_x) - x;
            const int dy = scale(flag_y) - y;
            const double dist = sqrt(static_cast<double>(dx * dx + dy * dy));
            if (dist > radius)
                continue;
            const int alpha = static_cast<int>(step * (radius - dist));
            set_trans_blender(0, 0, 0, min(alpha, 255));
            putpixel(roombg, x, y, teamcol[team]);
        }
    solid_mode();
}

void Graphics::draw_pup(const Powerup& pup, double time) {
    nAssert(pup.kind >= 0 && pup.kind < static_cast<int>(pup_sprite.size()));
    BITMAP* sprite = pup_sprite[pup.kind];
    if (sprite)
        draw_sprite(drawbuf, sprite, plx + scale(pup.x) - sprite->w / 2, ply + scale(pup.y) - sprite->h / 2);
    else
        switch (pup.kind) {
        /*break;*/ case Powerup::pup_shield:       draw_pup_shield      (pup.x, pup.y      );
            break; case Powerup::pup_turbo:        draw_pup_turbo       (pup.x, pup.y      );
            break; case Powerup::pup_shadow:       draw_pup_shadow      (pup.x, pup.y, time);
            break; case Powerup::pup_power:        draw_pup_power       (pup.x, pup.y, time);
            break; case Powerup::pup_weapon:       draw_pup_weapon      (pup.x, pup.y, time);
            break; case Powerup::pup_health:       draw_pup_health      (pup.x, pup.y, time);
            break; case Powerup::pup_deathbringer: draw_pup_deathbringer(pup.x, pup.y      );
            break; default: nAssert(0);
        }
}

void Graphics::draw_pup_shield(int x, int y) {
    draw_shield(x, y, 14);
    circlefill(drawbuf, plx + scale(x), ply + scale(y), scale(12), col[COLGREEN]);
}

void Graphics::draw_pup_turbo(int x, int y) {
    const int r = scale(12);
    circlefill(drawbuf, plx + scale(x + rand() %  6 - 3), ply + scale(y + rand() %  6 - 3), r, col[COLDARKORA]);
    circlefill(drawbuf, plx + scale(x + rand() %  8 - 4), ply + scale(y + rand() %  8 - 4), r, col[COLORA    ]);
    circlefill(drawbuf, plx + scale(x + rand() % 12 - 6), ply + scale(y + rand() % 12 - 6), r, col[COLYELLOW ]);
}

void Graphics::draw_pup_shadow(int x, int y, double time) {
    drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
    int alpha = static_cast<int>(time * 600.0) % 400;
    if (alpha > 200)
        alpha = 400 - alpha;
    set_trans_blender(0, 0, 0, 55 + alpha);
    circlefill(drawbuf, plx + scale(x), ply + scale(y), scale(12), col[COLMAG]);
    solid_mode();
}

void Graphics::draw_pup_power(int x, int y, double time) {
    if (static_cast<int>(time * 30) % 2)
        circlefill(drawbuf, plx + scale(x), ply + scale(y), scale(13), col[COLWHITE]);
    else
        circlefill(drawbuf, plx + scale(x), ply + scale(y), scale(11), col[COLCYAN]);
}

void Graphics::draw_pup_weapon(int x, int y, double time) {
    // rotate item
    for (int b = 0; b < 4; b++) {
        // deg: 0..360
        double deg = static_cast<int>(time * 1000) % 1000;      //thousand ticks
        deg /= 1000.0;      // normalise between 0...1
        deg *= 2 * N_PI;    // 360°
        deg += N_PI_2 * b;  // 90°

        // position
        double dx = 10 * cos(deg);
        double dy = 10 * sin(deg);

        // choose colour
        int c;
        switch (b) {
        /*break;*/ case 0: c = col[COLGREEN ];
            break; case 1: c = col[COLBLUE  ];
            break; case 2: c = col[COLRED   ];
            break; case 3: c = col[COLYELLOW];
            break; default: c = 0; nAssert(0);
        }
        // draw a ball
        circlefill(drawbuf, plx + scale(x + dx), ply + scale(y + dy), scale(4), c);
    }
}

void Graphics::draw_pup_health(int x, int y, double time) {
    x = scale(x);
    y = scale(y);
    //caixa de saude pulsante
    int varia = static_cast<int>(time * 15) % 10;
    if (varia > 5)
        varia = 10 - varia;
    const int itemsize = scale(11 + varia);
    const int crossize = scale(8 + varia);
    const int crosslar = scale(3); //aria/2;
    // health box black border
    rectfill(drawbuf, plx + x - itemsize - 2, ply + y - itemsize - 2, plx + x + itemsize + 2, ply + y + itemsize + 2, 0);
    // health box
    rectfill(drawbuf, plx + x - itemsize, ply + y - itemsize, plx + x + itemsize, ply + y + itemsize, col[COLWHITE]);
    // red cross
    rectfill(drawbuf, plx + x - crossize, ply + y - crosslar, plx + x + crossize, ply + y + crosslar, col[COLRED]);
    rectfill(drawbuf, plx + x - crosslar, ply + y - crossize, plx + x + crosslar, ply + y + crossize, col[COLRED]);
}

void Graphics::draw_pup_deathbringer(int x, int y) {
    circlefill(drawbuf, plx + scale(x), ply + scale(y), scale(12), makecol(0x22, 0x33, 0x22));
}

void Graphics::draw_waiting_map_message(const string& caption, const string& map) {
    print_text_border_centre_check_bg(caption, plx + scale(plw) / 2, ply + scale(plh / 2 + 20), col[COLGREEN], col[COLBLACK], -1);
    print_text_border_centre_check_bg(map, plx + scale(plw) / 2, ply + scale(plh / 2 + 50), col[COLORA], col[COLBLACK], -1);
}

void Graphics::draw_loading_map_message(const string& text) {
    print_text_border_centre_check_bg(text, plx + scale(plw / 2), ply + scale(plh / 2 + 70), col[COLGREEN], col[COLBLACK], -1);
}

void Graphics::draw_scores(const string& text, int team, int score1, int score2) {
    int c;
    switch (team) {
    /*break;*/ case 0: case 1: c = teamlcol[team];
        break; default: c = col[COLMENUGRAY];
    }
    print_text_border_centre_check_bg(text, plx + scale(plw / 2), ply + scale(plh / 2 - 40), c, col[COLBLACK], -1);
    print_text_border_centre_check_bg(_("SCORE $1 - $2", itoa(score1), itoa(score2)), plx + scale(plw / 2), ply + scale(plh / 2 - 20), c, col[COLBLACK], -1);
}

void Graphics::draw_scoreboard(const vector<ClientPlayer*>& players, const Team* teams, int maxplayers, bool pings, bool underlineMasterAuthenticated, bool underlineServerAuthenticated) {
    if (!show_scoreboard)
        return;

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
    const int teambg [2] = { makecol(0x1A, 0x00, 0x00), makecol(0x00, 0x00, 0x1A) };
    const int linecol[2] = { makecol(0x3A, 0x3A, 0x3A), makecol(0x3A, 0x3A, 0x3A) };
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
    const string teamName[2] = { _("Red Team"), _("Blue Team") };
    for (int team = 0; team < 2; ++team) {
        ostringstream os;
        os << teamName[team];
        if (pings)
            os << setw(caption_width - teamName[team].length()) << _("pings");
        else
            os << setw(caption_width - teamName[team].length()) << _("$1 capt", itoa_w(teams[team].score(), 3));
        textout_ex(drawbuf, sbfont, os.str().c_str(), sbx, teamy[team], col[COLWHITE], -1);
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
            vline(drawbuf, x, starty, starty + pole, col[COLYELLOW]);
            rectfill(drawbuf, x + 1, starty, x + 1 + flag, starty + flag, player.stats().has_wild_flag() ? teamcol[2] : teamcol[1 - player.team()]);
        }
        line[player.team()]++;
    }
}

void Graphics::draw_scoreboard_name(const FONT* sbfont, const string& name, int x, int y, int pcol, bool underline) {
    if (underline)
        hline(drawbuf, x, y + text_height(font), x + text_length(font, name) - 2, pcol);
    textout_ex(drawbuf, sbfont, name.c_str(), x, y, pcol, -1);
}

void Graphics::draw_scoreboard_points(const FONT* sbfont, int points, int x, int y, int team) {
    textprintf_right_ex(drawbuf, sbfont, x, y, teamlcol[team], -1, "%d", points);
}

void Graphics::team_statistics(const Team* teams) {
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
        if (stats_alpha < 255) {
            drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
            set_trans_blender(0, 0, 0, stats_alpha);
        }
        rectfill(drawbuf, x1, y1, x2, y2, 0);
        // caption backgrounds
        int line = 1;
        rectfill(drawbuf, x1, y1 + line * line_height - 4, x2, y1 + (line + 1) * line_height, col[COLDARKGREEN]);
        line += 2;
        rectfill(drawbuf, x1, y1 + line * line_height - 4, mx - 1, y1 + (line + 1) * line_height, teamdcol[0]);
        rectfill(drawbuf, mx, y1 + line * line_height - 4, x2, y1 + (line + 1) * line_height, teamdcol[1]);
        solid_mode();
    }

    int line = 1;
    textout_centre_ex(drawbuf, stfont, _("Team stats").c_str(), mx               , y1 + line * line_height, col[COLWHITE], -1);
    line += 2;
    textout_centre_ex(drawbuf, stfont, _("Red Team"  ).c_str(), (3 * x1 + x2) / 4, y1 + line * line_height, col[COLWHITE], -1);
    textout_centre_ex(drawbuf, stfont, _("Blue Team" ).c_str(), (x1 + 3 * x2) / 4, y1 + line * line_height, col[COLWHITE], -1);

    const int start_line = line + 2;
    line = start_line;
    textout_centre_ex(drawbuf, stfont, _("Captures"      ).c_str(), mx, y1 + line++ * line_height, col[COLWHITE], -1);
    textout_centre_ex(drawbuf, stfont, _("Kills"         ).c_str(), mx, y1 + line++ * line_height, col[COLWHITE], -1);
    textout_centre_ex(drawbuf, stfont, _("Deaths"        ).c_str(), mx, y1 + line++ * line_height, col[COLWHITE], -1);
    textout_centre_ex(drawbuf, stfont, _("Suicides"      ).c_str(), mx, y1 + line++ * line_height, col[COLWHITE], -1);
    textout_centre_ex(drawbuf, stfont, _("Flags taken"   ).c_str(), mx, y1 + line++ * line_height, col[COLWHITE], -1);
    textout_centre_ex(drawbuf, stfont, _("Flags dropped" ).c_str(), mx, y1 + line++ * line_height, col[COLWHITE], -1);
    textout_centre_ex(drawbuf, stfont, _("Flags returned").c_str(), mx, y1 + line++ * line_height, col[COLWHITE], -1);
    textout_centre_ex(drawbuf, stfont, _("Shots"         ).c_str(), mx, y1 + line++ * line_height, col[COLWHITE], -1);
    textout_centre_ex(drawbuf, stfont, _("Hit accuracy"  ).c_str(), mx, y1 + line++ * line_height, col[COLWHITE], -1);
    textout_centre_ex(drawbuf, stfont, _("Shots taken"   ).c_str(), mx, y1 + line++ * line_height, col[COLWHITE], -1);
    textout_centre_ex(drawbuf, stfont, _("Movement"      ).c_str(), mx, y1 + line++ * line_height, col[COLWHITE], -1);
    textout_centre_ex(drawbuf, stfont, _("Team power"    ).c_str(), mx, y1 + line++ * line_height, col[COLWHITE], -1);

    for (int t = 0; t < 2; t++) {
        const Team& team = teams[t];
        line = start_line;
        const int x = (t == 0 ? 3 * x1 + x2 : x1 + 3 * x2) / 4;
        textprintf_centre_ex(drawbuf, stfont, x, y1 + line++ * line_height, teamlcol[t], -1, "%d", team.score());
        textprintf_centre_ex(drawbuf, stfont, x, y1 + line++ * line_height, teamlcol[t], -1, "%d", team.kills());
        textprintf_centre_ex(drawbuf, stfont, x, y1 + line++ * line_height, teamlcol[t], -1, "%d", team.deaths());
        textprintf_centre_ex(drawbuf, stfont, x, y1 + line++ * line_height, teamlcol[t], -1, "%d", team.suicides());
        textprintf_centre_ex(drawbuf, stfont, x, y1 + line++ * line_height, teamlcol[t], -1, "%d", team.flags_taken());
        textprintf_centre_ex(drawbuf, stfont, x, y1 + line++ * line_height, teamlcol[t], -1, "%d", team.flags_dropped());
        textprintf_centre_ex(drawbuf, stfont, x, y1 + line++ * line_height, teamlcol[t], -1, "%d", team.flags_returned());
        textprintf_centre_ex(drawbuf, stfont, x, y1 + line++ * line_height, teamlcol[t], -1, "%d", team.shots());
        textprintf_centre_ex(drawbuf, stfont, x, y1 + line++ * line_height, teamlcol[t], -1, "%.0f%%", 100. * team.accuracy());
        textprintf_centre_ex(drawbuf, stfont, x, y1 + line++ * line_height, teamlcol[t], -1, "%d", team.shots_taken());
        textprintf_centre_ex(drawbuf, stfont, x, y1 + line++ * line_height, teamlcol[t], -1, "%.0f u", team.movement() / (2 * PLAYER_RADIUS));
        textprintf_centre_ex(drawbuf, stfont, x, y1 + line++ * line_height, teamlcol[t], -1, "%.2f", team.power());
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
        scrollbar(x, y, height, bar_y, bar_h, col[COLGREEN], col[COLDARKGREEN]);
    }
}

void Graphics::draw_statistics(const vector<ClientPlayer*>& players, int page, int time, int maxplayers, int max_world_rank) {
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
        if (stats_alpha < 255) {
            drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
            set_trans_blender(0, 0, 0, stats_alpha);
        }
        rectfill(drawbuf, x1, y1, x2, y2, 0);
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
                       caption2 = _("Shots   | Taken  Movement     Speed");
                       //           |00000 100% 0000  000000 u  00.00 u/s    |
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
    textout_ex(drawbuf, stfont, red_name.c_str(), x_left, team1y + line_h / 2, col[COLWHITE], -1);
    textout_ex(drawbuf, stfont, caption1.c_str(), x_capt, team1y         , col[COLWHITE], -1);
    textout_ex(drawbuf, stfont, caption2.c_str(), x_capt, team1y + line_h, col[COLWHITE], -1);
    textout_ex(drawbuf, stfont, blue_name.c_str(), x_left, team2y + line_h / 2, col[COLWHITE], -1);
    textout_ex(drawbuf, stfont, caption1.c_str(), x_capt, team2y         , col[COLWHITE], -1);
    textout_ex(drawbuf, stfont, caption2.c_str(), x_capt, team2y + line_h, col[COLWHITE], -1);

    int i = 0;
    int teamLineY[2] = { team1y + 3 * line_h, team2y + 3 * line_h };
    for (vector<ClientPlayer*>::const_iterator pi = players.begin(); pi != players.end(); ++pi, ++i) {
        const ClientPlayer& player = **pi;
        draw_player_statistics(stfont, player, x_left, teamLineY[player.team()], page, time);
        teamLineY[player.team()] += line_h;
    }

    if (page == 3 && max_world_rank > 0)
        textout_ex(drawbuf, stfont, _("$1 players in the tournament.", itoa(max_world_rank)).c_str(), x_left, pageNumY, col[COLGREEN], -1);

    ostringstream page_num;
    page_num << page + 1 << '/' << 4;
    textout_right_ex(drawbuf, stfont, page_num.str().c_str(), x2 - x_margin, pageNumY, col[COLGREEN], -1);
}

void Graphics::draw_player_statistics(const FONT* stfont, const ClientPlayer& player, int x, int y, int page, int time) {
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
            //  Shots   | Taken  Movement     Speed
            // |00000 100% 0000  000000 u  00.00 u/s    |
            stats << setw(5) << st.shots()
                  << setw(4) << static_cast<int>(100. * st.accuracy() + 0.5) << '%'
                  << setw(5) << st.shots_taken()
                  << setw(8) << static_cast<int>(st.movement()) / (2 * PLAYER_RADIUS) << " u"
                  << setw(7) << setprecision(2) << std::fixed << st.old_speed() << " u/s";
        break; case 3:
            //            Average        Tournament
            //  Playtime lifetime    rank  power  score
            // |00000 min   00:00    0000  00.00 -00000 |
            stats << setw(5) << static_cast<int>(st.playtime(time)) / 60 << " min"
                  << setw(5) << static_cast<int>(st.average_lifetime(time)) / 60 << ':'
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
                           const vector<vector<pair<int, int> > >& sticks, const vector<int>& buttons) {
    clear_to_color(drawbuf, col[COLBLACK]);

    int line = 1;
    const int line_h = 10;
    const int margin = 8;
    for (vector<ClientPlayer>::const_iterator player = players.begin(); player != players.end(); ++player, ++line) {
        const int c = (me == line - 1) ? col[COLYELLOW] : col[COLWHITE];
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
    textout_ex(drawbuf, font, axis_info.str().c_str(), margin, line++ * line_h, col[COLINFO], -1);

    line++;
    ostringstream button_info;
    button_info << _("Joystick buttons") << ':';
    for (vector<int>::const_iterator but = buttons.begin(); but != buttons.end(); ++but)
        button_info << ' ' << *but;
    textout_ex(drawbuf, font, button_info.str().c_str(), margin, line++ * line_h, col[COLINFO], -1);

    line++;
    const int bpstraffic = bpsin + bpsout;
    textout_ex(drawbuf, font, _("Traffic: $1 B/s"      , itoa_w(bpstraffic, 4)              ).c_str(), margin, line++ * line_h, col[COLINFO], -1);
    textout_ex(drawbuf, font, _("in $1 B/s, out $2 B/s", itoa_w(bpsin, 4), itoa_w(bpsout, 4)).c_str(), margin, line++ * line_h, col[COLINFO], -1);
}

void Graphics::draw_fps(double fps) {
    const string text = _("FPS:$1", itoa_w((int)fps, 3));
    print_text_border_check_bg(text, SCREEN_W - 2 - text_length(font, text), SCREEN_H - text_height(font) - 2, col[COLMENUGRAY], col[COLBLACK], -1);
}

void Graphics::map_list(const vector<MapInfo>& maps, int current, int own_vote, const string& edit_vote) {
    const FONT* mlfont;
    if (text_length(font, "i") != text_length(font, "M"))   // map list works only with monospace font
        mlfont = default_font;
    else
        mlfont = font;
    const int line_height = text_height(mlfont) + 4;
    const int w = min(SCREEN_W, 67 * text_length(mlfont, "M") + 4);
    const int extra_space = 8 * line_height;
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
        if (stats_alpha < 255) {
            drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
            set_trans_blender(0, 0, 0, stats_alpha);
        }
        rectfill(drawbuf, x1, y1, x2, y2, 0);
        // caption background
        rectfill(drawbuf, x1, y1 + line_height - 4, x2, y1 + 2 * line_height, col[COLDARKGREEN]);
        solid_mode();
    }

    textout_centre_ex(drawbuf, mlfont, _("Server map list").c_str(), mx, y1 + line_height, col[COLWHITE], -1);

    ostringstream caption;
    caption << _(" Nr Vote Title                Size  Author");
    //           |000 00 * <--------20--------> 00×00 <------------27----------->|
    textout_ex(drawbuf, mlfont, caption.str().c_str(), x_left, y1 + 3 * line_height, col[COLWHITE], -1);

    if (map_list_start >= static_cast<int>(maps.size()) - map_list_size)
        map_list_start = maps.size() - map_list_size;
    if (map_list_start < 0)
        map_list_start = 0;

    for (int i = map_list_start; i < static_cast<int>(maps.size()) && i < map_list_start + map_list_size; ++i) {
        ostringstream mapline;
        mapline << setw(3) << i + 1 << ' ' << setw(2);
        const MapInfo& map = maps[i];
        if (map.votes > 0)
            mapline << map.votes;
        else
            mapline << '-';
        if (own_vote == i)
            mapline << " *";
        else
            mapline << "  ";
        mapline << ' ' << setw(20) << left << map.title.substr(0, 20) << right << ' ';
        mapline << setw(2) << map.width << '×' << setw(2) << left << map.height << right << ' ';
        mapline << map.author.substr(0, 27);
        const int y = y1 + 5 * line_height + line_height * (i - map_list_start);
        int c;
        if (i == current)
            c = col[COLYELLOW];
        else if (map.highlight)
            c = col[COLGREEN];
        else
            c = col[COLWHITE];
        textout_ex(drawbuf, mlfont, mapline.str().c_str(), x_left, y, c, -1);
    }
    // draw scrollbar if there are more maps than visible on the screen
    if (map_list_size < static_cast<int>(maps.size())) {
        const int x = x2 - 30;
        const int y = y1 + 5 * line_height - 4;
        const int height = map_list_size * line_height;
        const int bar_y = static_cast<int>(static_cast<double>(height * map_list_start) / maps.size() + 0.5);
        const int bar_h = static_cast<int>(static_cast<double>(height * map_list_size ) / maps.size() + 0.5);
        scrollbar(x, y, height, bar_y, bar_h, col[COLGREEN], col[COLDARKGREEN]);
    }
    ostringstream vote;
    vote << _("Vote map number") << ": " << edit_vote << '_';
    const int y = y1 + (5 + map_list_size + 1) * line_height;
    textout_ex(drawbuf, mlfont, vote.str().c_str(), x_left, y, col[COLGREEN], -1);
}

void Graphics::draw_player_power(double val) {
    draw_powerup_time(0, _("Power"), val, col[COLCYAN]);
}

void Graphics::draw_player_turbo(double val) {
    draw_powerup_time(1, _("Turbo"), val, col[COLYELLOW]);
}

void Graphics::draw_player_shadow(double val) {
    draw_powerup_time(2, _("Shadow"), val, col[COLMAG]);
}

void Graphics::draw_powerup_time(int line, const string& caption, double val, int c) {
    const int y = indicators_y + line * (text_height(font) + 2);
    print_text_border_check_bg(caption, pups_x, y, c, col[COLBLACK], -1);
    const string value = itoa_w(iround(val), 3);
    print_text_border_check_bg(value, pups_val_x - text_length(font, value), y, c, col[COLBLACK], -1);
}

void Graphics::draw_player_weapon(int level) {
    print_text_border_check_bg(_("Weapon $1", itoa(level)), weapon_x, indicators_y, col[COLWHITE], col[COLBLACK], -1);
}

void Graphics::map_time(int seconds) {
    ostringstream ost;
    ost << seconds / 60 << ':' << setw(2) << setfill('0') << seconds % 60;
    print_text_border_check_bg(ost.str(), time_x - text_length(font, ost.str()), time_y, col[COLGREEN], col[COLBLACK], -1);
}

void Graphics::draw_change_team_message(double time) {
    int c;
    if (static_cast<int>(time * 2.0) % 2)   // blink!
        c = col[COLRED];
    else
        c = col[COLWHITE];
    textout_centre_ex(drawbuf, font, _("CHANGE").c_str(), plx + scale(plw - 6 * 8 + 10), ply + scale(plh - 18), c, -1);
    textout_centre_ex(drawbuf, font, _("TEAMS" ).c_str(), plx + scale(plw - 6 * 8 + 10), ply + scale(plh -  9), c, -1);
}

void Graphics::draw_change_map_message(double time, bool delayed) {
    const int x = plx + scale(plw - 64 - 6 * 8), y1 = ply + scale(plh - 18), y2 = ply + scale(plh - 9);
    const string text1 = _("EXIT"), text2 = _("MAP");
    if (delayed) {
        print_text_border_centre(text1, x, y1, col[COLMENUGRAY], 0, -1);
        print_text_border_centre(text2, x, y2, col[COLMENUGRAY], 0, -1);
    }
    else {
        int c;
        if (static_cast<int>(time * 2.0) % 2)   // blink!
            c = col[COLRED];
        else
            c = col[COLWHITE];
        print_text_centre(text1, x, y1, c, -1);
        print_text_centre(text2, x, y2, c, -1);
    }
}

void Graphics::draw_player_health(int value) {
    const int x0 = health_x;
    const int y0 = indicators_y;
    draw_bar(x0, y0, _("Health"), value, col[COLRED], col[COLYELLOW], col[COLMAG]);
}

void Graphics::draw_player_energy(int value) {
    const int x0 = energy_x;
    const int y0 = indicators_y;
    draw_bar(x0, y0, _("Energy"), value, col[COLBLUE], col[COLGREEN], col[COLENER3]);
}

void Graphics::draw_bar(int x, int y, const string& caption, int value, int c100, int c200, int c300) {
    print_text_border_check_bg(caption, x, y, col[COLWHITE], col[COLBLACK], -1);
    const string val_str = itoa_w(value, 3);
    const int width = scale(100);
    const int bar_y1 = y + 3 * text_height(font) / 2;
    const int bar_y2 = bar_y1 + scale(10);

    const int val_x = max(x + text_length(font, caption) + text_length(font, " "), x + width - text_length(font, val_str));
    print_text_border_check_bg(val_str, val_x, y, col[COLWHITE], col[COLBLACK], -1);

    rectfill(drawbuf, x, bar_y1, x + width, bar_y2, col[COLBLACK]);
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

void Graphics::print_chat_messages(list<Message>::const_iterator msg, const list<Message>::const_iterator& end, const string& talkbuffer) {
    if (!show_chat_messages)
        return;
    const int line_height = text_height(font) + 3;
    const int margin = 3;
    int line = 0;
    for (; msg != end; ++msg, ++line) {
        if (msg->text().empty())
            continue;
        print_chat_message(msg->type(), msg->text(), margin, margin + line * line_height, msg->highlighted());
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
        if (talkbuffer[0] == '.')
            message << talkbuffer.substr(1);
        else
            message << talkbuffer;
        message << '_';
        const vector<string> lines = split_to_lines(message.str(), 79, 0);
        for (vector<string>::const_iterator li = lines.begin(); li != lines.end(); ++li, ++line)
            print_chat_input(*li, margin, margin + line * line_height);
    }
}

void Graphics::print_chat_message(Message_type type, const string& message, int x, int y, bool highlight) {
    int c;
    switch (type) {
    /*break;*/ case msg_warning: c = col[COLLRED];
        break; case msg_team: c = col[COLYELLOW];
        break; case msg_info: c = col[COLGREEN];
        break; case msg_header: c = makecol(0xAA, 0xFF, 0xFF);
        break; case msg_server: c = col[COLCYAN];
        break; case msg_normal: default: c = col[COLORA];
    }
    if (highlight && type != msg_team)
        c = col[COLWHITE];
    // Check if the border is needed.
    if (!bg_texture && y + text_height(font) < ply - scale(PLAYER_RADIUS + 10))
        textout_ex(drawbuf, font, message.c_str(), x, y, c, -1);
    else
        print_text_border(message, x, y, c, 0, -1);
}

void Graphics::print_chat_input(const string& message, int x, int y) {
    print_text_border(message, x, y, col[COLWHITE], 0, -1);
}

void Graphics::print_text(const std::string& text, int x, int y, int textcol, int bgcol) {
    textout_ex(drawbuf, font, text.c_str(), x, y, textcol, bgcol);
}

void Graphics::print_text_centre(const std::string& text, int x, int y, int textcol, int bgcol) {
    textout_centre_ex(drawbuf, font, text.c_str(), x, y, textcol, bgcol);
}

void Graphics::print_text_border(const string& text, int x, int y, int textcol, int bordercol, int bgcol) {
    print_text_border(text, x, y, textcol, bordercol, bgcol, false);
}

void Graphics::print_text_border_centre(const string& text, int x, int y, int textcol, int bordercol, int bgcol) {
    print_text_border(text, x, y, textcol, bordercol, bgcol, true);
}

void Graphics::print_text_border_check_bg(const string& text, int x, int y, int textcol, int bordercol, int bgcol) {
    if (bg_texture)
        print_text_border(text, x, y, textcol, bordercol, bgcol, false);
    else
        textout_ex(drawbuf, font, text.c_str(), x, y, textcol, bgcol);
}

void Graphics::print_text_border_centre_check_bg(const string& text, int x, int y, int textcol, int bordercol, int bgcol) {
    if (bg_texture)
        print_text_border(text, x, y, textcol, bordercol, bgcol, true);
    else
        textout_centre_ex(drawbuf, font, text.c_str(), x, y, textcol, bgcol);
}

void Graphics::print_text_border(const string& text, int x, int y, int textcol, int bordercol, int bgcol, bool centring) {
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

void Graphics::scrollbar(int x, int y, int height, int bar_y, int bar_h, int col1, int col2) {
    const int width = 10;
    if (height > 0) {
        rectfill(drawbuf, x, y, x + width - 1, y + height - 1, col2);
        if (bar_h > 0)
            rectfill(drawbuf, x, y + bar_y, x + width - 1, y + bar_y + bar_h - 1, col1);
    }
}

bool Graphics::save_screenshot(const string& filename) const {
    PALETTE pal;
    get_palette(pal);
    if (page_flipping) {
        BITMAP* current_screen = drawbuf == vidpage1 ? vidpage2 : vidpage1;
        Bitmap temp = create_bitmap(current_screen->w, current_screen->h);
        nAssert(temp);
        blit(current_screen, temp, 0, 0, 0, 0, current_screen->w, current_screen->h);
        return !save_bitmap(filename.c_str(), temp, pal);
    }
    return !save_bitmap(filename.c_str(), drawbuf, pal);
}

// client side effects

//clear clientside fx's
void Graphics::clear_fx() {
    cfx.clear();
}

//create rocket explosion fx
void Graphics::create_wallexplo(int x, int y, int px, int py, int team) {
    GraphicsEffect fx;

    fx.type = FX_WALL_EXPLOSION;
    fx.x = x;
    fx.y = y;
    fx.time = get_time();
    fx.px = px;
    fx.py = py;
    fx.team = team;

    cfx.push_back(fx);
}

//create power rocket explosion fx
void Graphics::create_powerwallexplo(int x, int y, int px, int py, int team) {
    GraphicsEffect fx;

    fx.type = FX_POWER_WALL_EXPLOSION;
    fx.x = x;
    fx.y = y;
    fx.time = get_time();
    fx.px = px;
    fx.py = py;
    fx.team = team;

    cfx.push_back(fx);
}

// Create deathbringer powerup smoke, but only if there is no deathbringer sprite.
void Graphics::create_smoke(int x, int y, int px, int py) {
    if (!pup_sprite[Powerup::pup_deathbringer])
        create_deathcarrier(x, y, px, py, 255);
}

//create deathbringer carrier trail fx
void Graphics::create_deathcarrier(int x, int y, int px, int py, int alpha) {
    GraphicsEffect fx;

    fx.type = FX_DEATHCARRIER_SMOKE;
    fx.x = x;
    fx.y = y;
    fx.px = px;
    fx.py = py;
    fx.time = get_time();
    fx.col1 = 0;    // black
    fx.alpha = alpha / 255.;

    cfx.push_back(fx);
}

void Graphics::create_turbofx(int x, int y, int px, int py, int col1, int col2, int gundir, int alpha) {
    GraphicsEffect fx;

    fx.type = FX_TURBO;
    fx.x = x;
    fx.y = y;
    fx.px = px;
    fx.py = py;
    fx.time = get_time();

    fx.alpha = alpha / 255.;
    fx.col1 = col1;
    fx.col2 = col2;
    fx.gundir = gundir;

    fx.team = 0; // to please GCC

    cfx.push_back(fx);
}

//create deathbringer explosion fx
void Graphics::create_deathbringer(int team, double start_time, int x, int y, int px, int py) {
    GraphicsEffect fx;

    fx.team = team;
    fx.type = FX_DEATHBRINGER_EXPLOSION;
    fx.x = x;
    fx.y = y;
    fx.time = start_time;
    fx.px = px;
    fx.py = py;

    cfx.push_back(fx);
}

//create explosion fx
void Graphics::create_gunexplo(int x, int y, int px, int py, int team) {
    GraphicsEffect fx;

    fx.type = FX_GUN_EXPLOSION;
    fx.x = x;
    fx.y = y;
    fx.time = get_time();
    fx.px = px;
    fx.py = py;
    fx.team = team;

    cfx.push_back(fx);
}

void Graphics::draw_effects(int room_x, int room_y, double time) {
    for (list<GraphicsEffect>::iterator fx = cfx.begin(); fx != cfx.end(); ) {
        if (fx->px != room_x || fx->py != room_y) { // different room
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
                        draw_gun_explosion(fx->x, fx->y, rad, fx->team);
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
                        const int rad = 4 + e + (int)(delta * 40);
                        draw_gun_explosion(fx->x, fx->y, rad, fx->team);
                    }
                    ++fx;
                }
            break; case FX_POWER_WALL_EXPLOSION:
                if (delta > 0.2)
                    fx = cfx.erase(fx);
                else {
                    for (int e = 0; e < 3; e++) {
                        const int rad = 4 + e + (int)(delta * 60);
                        draw_gun_explosion(fx->x, fx->y, rad, fx->team);
                    }
                    ++fx;
                }
            break; case FX_DEATHBRINGER_EXPLOSION:
                if (delta > 3.0)
                    fx = cfx.erase(fx);
                else {
                    draw_deathbringer(fx->x, fx->y, fx->team, delta);
                    ++fx;
                }
            break; case FX_DEATHCARRIER_SMOKE:
                if (delta > 0.6)
                    fx = cfx.erase(fx);
                else {
                    draw_deathbringer_smoke(fx->x, fx->y, delta, fx->alpha);
                    ++fx;
                }
            break; default:
                nAssert(0);
        }
    }
}

void Graphics::draw_turbofx(int room_x, int room_y, double time) {
    if (min_transp)
        return;
    for (list<GraphicsEffect>::iterator fx = cfx.begin(); fx != cfx.end(); ) {
        if (fx->px != room_x || fx->py != room_y || fx->type != FX_TURBO) { // different room or wrong type
            ++fx;
            continue;
        }
        const double delta = time - fx->time;
        if (delta > 0.3)
            fx = cfx.erase(fx);
        else {
            const int alpha = static_cast<int>(fx->alpha * (90 - delta * 300));
            draw_player(fx->x, fx->y, fx->col1, fx->col2, fx->gundir, time, false, alpha, time);
            ++fx;
        }
    }
}

bool Graphics::save_map_picture(const string& filename, const Map& map) {
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
    return !save_bitmap(filename.c_str(), buffer, pal);
}

void Graphics::make_db_effect() {
    db_effect.free();
    const int size = 2 * scale(2 * PLAYER_RADIUS);
    db_effect = create_bitmap_ex(32, size, size);
    nAssert(db_effect);

    clear_to_color(db_effect, col[COLBLACK]);

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

void Graphics::search_themes(LineReceiver& dst_theme, LineReceiver& dst_bg) const {
    dst_theme(_("<no theme>"));
    dst_bg(_("<no background>"));

    vector<string> themes;
    FileFinder* themeDirs = platMakeFileFinder(wheregamedir + "graphics", "", true);
    while (themeDirs->hasNext())
        themes.push_back(themeDirs->next());
    delete themeDirs;
    sort(themes.begin(), themes.end(), cmp_case_ins);
    for (vector<string>::const_iterator ti = themes.begin(); ti != themes.end(); ++ti) {
        dst_theme(*ti);
        // Check if the theme has a background image
        const string bg = wheregamedir + "graphics" + directory_separator + *ti + directory_separator + "background.pcx";
        if (platIsFile(bg))
            dst_bg(*ti);
    }
}

void Graphics::select_theme(const string& dir, const string& bg_dir, bool use_theme_bg) {
    unload_pictures();
    if (dir == _("<no theme>"))
        theme_path.clear();
    else {
        theme_path = wheregamedir + "graphics" + directory_separator + dir + directory_separator;
        load_pictures();
        log("Loaded graphics theme '%s'.", dir.c_str());
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
}

void Graphics::load_pictures() {
    if (floor_texture.empty())  // kludge: select_theme -> load_pictures might be called before init
        return;
    if (theme_path.empty())
        return;
    load_floor_textures(theme_path);
    load_wall_textures (theme_path);
    load_player_sprites(theme_path);
    load_shield_sprites(theme_path);
    load_dead_sprites  (theme_path);
    load_rocket_sprites(theme_path);
    load_flag_sprites  (theme_path);
    load_pup_sprites   (theme_path);
}

void Graphics::load_background() {
    if (floor_texture.empty())  // kludge: select_theme -> load_background might be called before init
        return;
    bg_texture.free();
    if (!bg_path.empty())
        bg_texture = load_bitmap((bg_path + "background.pcx").c_str(), NULL);
}

void Graphics::load_floor_textures(const string& path) {
    int t = 0;
    floor_texture[t++] = load_bitmap((path + "floor_normal1.pcx").c_str(), NULL);
    floor_texture[t++] = load_bitmap((path + "floor_normal2.pcx").c_str(), NULL);
    floor_texture[t++] = load_bitmap((path + "floor_normal3.pcx").c_str(), NULL);
    floor_texture[t++] = load_bitmap((path + "floor_red.pcx"    ).c_str(), NULL);
    floor_texture[t++] = load_bitmap((path + "floor_blue.pcx"   ).c_str(), NULL);
    floor_texture[t++] = load_bitmap((path + "floor_ice.pcx"    ).c_str(), NULL);
    floor_texture[t++] = load_bitmap((path + "floor_sand.pcx"   ).c_str(), NULL);
    floor_texture[t++] = load_bitmap((path + "floor_mud.pcx"    ).c_str(), NULL);
    // Check that width and height are powers of 2.
    for (int i = 0; i < 8; i++) {
        Bitmap& texture = floor_texture[i];
        if (texture && ((texture->w & texture->w - 1) || (texture->h & texture->h - 1))) {
            log.error(_("Width and height of textures must be powers of 2; floor texture $1 is $2×$3.", itoa(i), itoa(texture->w), itoa(texture->h)));
            texture.free();
        }
    }
}

void Graphics::load_wall_textures(const string& path) {
    int t = 0;
    wall_texture[t++] = load_bitmap((path + "wall_normal1.pcx").c_str(), NULL);
    wall_texture[t++] = load_bitmap((path + "wall_normal2.pcx").c_str(), NULL);
    wall_texture[t++] = load_bitmap((path + "wall_normal3.pcx").c_str(), NULL);
    wall_texture[t++] = load_bitmap((path + "wall_red.pcx"    ).c_str(), NULL);
    wall_texture[t++] = load_bitmap((path + "wall_blue.pcx"   ).c_str(), NULL);
    wall_texture[t++] = load_bitmap((path + "wall_metal.pcx"  ).c_str(), NULL);
    wall_texture[t++] = load_bitmap((path + "wall_wood.pcx"   ).c_str(), NULL);
    wall_texture[t++] = load_bitmap((path + "wall_rubber.pcx" ).c_str(), NULL);
    // Check that width and height are powers of 2.
    for (int i = 0; i < 8; i++) {
        Bitmap& texture = wall_texture[i];
        if (texture && ((texture->w & texture->w - 1) || (texture->h & texture->h - 1))) {
            log.error(_("Width and height of textures must be powers of 2; wall texture $1 is $2×$3.", itoa(i), itoa(texture->w), itoa(texture->h)));
            texture.free();
        }
    }
}

BITMAP* Graphics::get_floor_texture(int texid) {
    if (floor_texture[texid])
        return floor_texture[texid];
    else
        return floor_texture.front();
}

BITMAP* Graphics::get_wall_texture(int texid) {
    if (wall_texture[texid])
        return wall_texture[texid];
    else
        return wall_texture.front();
}

void Graphics::load_player_sprites(const string& path) {
    const int size = scale(2 * 2 * PLAYER_RADIUS);
    const Bitmap common   = scale_sprite      (path + "player.pcx"         , size, size);
    const Bitmap team     = scale_alpha_sprite(path + "player_team.pcx"    , size, size);
    const Bitmap personal = scale_alpha_sprite(path + "player_personal.pcx", size, size);
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
        combine_sprite(player_sprite_power, common, team, personal, col[COLWHITE], col[COLCYAN]);
    }
}

void Graphics::overlayColor(BITMAP* bmp, BITMAP* alpha, int color) {    // alpha must be an 8-bit bitmap; give the color in same format as bmp
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

void Graphics::combine_sprite(BITMAP* sprite, BITMAP* common, BITMAP* team, BITMAP* personal, int tcol, int pcol) { // give the colors in same format as sprite
    nAssert(sprite && common);
    blit(common, sprite, 0, 0, 0, 0, common->w, common->h);
    overlayColor(sprite, personal, pcol);
    overlayColor(sprite, team, tcol);
}

void Graphics::load_shield_sprites(const string& path) {
    const int size = scale(2 * 2 * PLAYER_RADIUS);
    Bitmap picture = scale_sprite(path + "player_shield.pcx", size, size);
    if (!picture)
        return;
    Bitmap team  = scale_alpha_sprite(path + "player_shield_team.pcx", size, size);
    for (int t = 0; t < 2; t++) {
        shield_sprite[t] = create_bitmap(size, size);
        nAssert(shield_sprite[t]);
        combine_sprite(shield_sprite[t], picture, team, 0, teamcol[t], 0);
    }
}

void Graphics::load_dead_sprites(const string& path) {
    const int size = scale(2 * 2 * PLAYER_RADIUS);
    ice_cream = scale_sprite(path + "ice_cream.pcx", size, size);
    Bitmap picture = scale_sprite(path + "dead.pcx", size, size);
    if (!picture)
        return;
    Bitmap alpha = scale_alpha_sprite(path + "dead_alpha.pcx", size, size);
    Bitmap team  = scale_alpha_sprite(path + "dead_team.pcx" , size, size);
    for (int t = 0; t < 2; t++) {
        dead_sprite[t] = create_bitmap_ex(32, size, size);
        nAssert(dead_sprite[t]);
        combine_sprite(dead_sprite[t], picture, team, 0, colorTo32(teamcol[t]), 0);
        set_alpha_channel(dead_sprite[t], alpha);
    }
}

// Make rocket sprites by combining rocket image with team colour.
void Graphics::load_rocket_sprites(const string& path) {
    const int size = scale(2 * 2 * ROCKET_RADIUS);
    Bitmap normal = scale_sprite(path + "rocket.pcx", size, size);
    if (normal) {
        Bitmap team = scale_alpha_sprite(path + "rocket_team.pcx", size, size);
        for (int t = 0; t < 2; t++) {
            rocket_sprite[t] = create_bitmap(size, size);
            nAssert(rocket_sprite[t]);
            combine_sprite(rocket_sprite[t], normal, team, 0, teamcol[t], 0);
        }
        normal.free();
    }
    Bitmap power = scale_sprite(path + "rocket_pow.pcx", size, size);
    if (power) {
        Bitmap team = scale_alpha_sprite(path + "rocket_pow_team.pcx", size, size);
        for (int t = 0; t < 2; t++) {
            power_rocket_sprite[t] = create_bitmap(size, size);
            nAssert(power_rocket_sprite[t]);
            combine_sprite(power_rocket_sprite[t], power, team, 0, teamcol[t], 0);
        }
    }
}

// Make flag sprites by combining flag image with team colour.
void Graphics::load_flag_sprites(const string& path) {
    const int size = scale(100);
    Bitmap flag = scale_sprite(path + "flag.pcx", size, size);
    if (flag) {
        Bitmap team = scale_alpha_sprite(path + "flag_team.pcx", size, size);
        for (int t = 0; t < 3; t++) {
            flag_sprite[t] = create_bitmap(size, size);
            nAssert(flag_sprite[t]);
            combine_sprite(flag_sprite[t], flag, team, 0, teamcol[t], 0);
        }
    }
}

void Graphics::load_pup_sprites(const string& path) {
    const int size = scale(2 * PLAYER_RADIUS);
    pup_sprite[Powerup::pup_shield      ] = scale_sprite(path + "shield.pcx"      , size, size);
    pup_sprite[Powerup::pup_turbo       ] = scale_sprite(path + "turbo.pcx"       , size, size);
    pup_sprite[Powerup::pup_shadow      ] = scale_sprite(path + "shadow.pcx"      , size, size);
    pup_sprite[Powerup::pup_power       ] = scale_sprite(path + "power.pcx"       , size, size);
    pup_sprite[Powerup::pup_weapon      ] = scale_sprite(path + "weapon.pcx"      , size, size);
    pup_sprite[Powerup::pup_health      ] = scale_sprite(path + "health.pcx"      , size, size);
    pup_sprite[Powerup::pup_deathbringer] = scale_sprite(path + "deathbringer.pcx", size, size);
}

BITMAP* Graphics::scale_sprite(const string& filename, int x, int y) const {
    Bitmap temp = load_bitmap(filename.c_str(), NULL);
    if (!temp)
        return 0;
    BITMAP* target = create_bitmap(x, y);
    nAssert(target);
    stretch_blit(temp, target, 0, 0, temp->w, temp->h, 0, 0, target->w, target->h);
    return target;
}

BITMAP* Graphics::scale_alpha_sprite(const string& filename, int x, int y) const {
    set_color_conversion(COLORCONV_NONE);
    Bitmap temp = load_bitmap(filename.c_str(), NULL);
    set_color_conversion(COLORCONV_TOTAL);
    if (!temp)
        return 0;
    if (bitmap_color_depth(temp) != 8) {
        log.error(_("Alpha bitmaps must be 8-bit grayscale images; $1 is $2-bit.", filename, itoa(bitmap_color_depth(temp))));
        return 0;
    }
    BITMAP* target = create_bitmap_ex(8, x, y);
    nAssert(target);
    stretch_blit(temp, target, 0, 0, temp->w, temp->h, 0, 0, target->w, target->h);
    return target;
}

void Graphics::unload_pictures() {
    bg_texture.free();
    unload_floor_textures();
    unload_wall_textures();
    unload_player_sprites();
    unload_shield_sprites();
    unload_dead_sprites();
    unload_rocket_sprites();
    unload_flag_sprites();
    unload_pup_sprites();
}

void Graphics::unload_floor_textures() {
    for (vector<Bitmap>::iterator pl = floor_texture.begin(); pl != floor_texture.end(); ++pl)
        pl->free();
}

void Graphics::unload_wall_textures() {
    for (vector<Bitmap>::iterator pl = wall_texture.begin(); pl != wall_texture.end(); ++pl)
        pl->free();
}

void Graphics::unload_player_sprites() {
    for (int t = 0; t < 2; t++)
        for (vector<Bitmap>::iterator pl = player_sprite[t].begin(); pl != player_sprite[t].end(); ++pl)
            pl->free();
    player_sprite_power.free();
}

void Graphics::unload_shield_sprites() {
    for (int i = 0; i < 2; i++)
        shield_sprite[i].free();
}

void Graphics::unload_dead_sprites() {
    for (int i = 0; i < 2; i++)
        dead_sprite[i].free();
    ice_cream.free();
}

void Graphics::unload_rocket_sprites() {
    for (int i = 0; i < 2; i++) {
        rocket_sprite[i].free();
        power_rocket_sprite[i].free();
    }
}

void Graphics::unload_flag_sprites() {
    for (int i = 0; i < 3; i++)
        flag_sprite[i].free();
}

void Graphics::unload_pup_sprites() {
    for (vector<Bitmap>::iterator pup = pup_sprite.begin(); pup != pup_sprite.end(); ++pup)
        pup->free();
}

// Font functions

void Graphics::search_fonts(LineReceiver& dst_font) const {
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

void Graphics::select_font(const string& file) {
    if (file == string() + '<' + _("default") + '>') {
        font = default_font;
        border_font = 0;
    }
    else
        load_font(file);
}

void Graphics::load_font(const string& file) {
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

inline int Graphics::scale(double value) const {
    return static_cast<int>(scr_mul * value + 0.5);
}
