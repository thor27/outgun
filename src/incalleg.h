/*
 *  incalleg.h
 *
 *  Copyright (C) 2004, 2008 - Niko Ritari
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

#ifdef DEDICATED_SERVER_ONLY
#define INCALLEG_H_INC // disable this entire file
#endif

#ifndef INCALLEG_H_INC
#define INCALLEG_H_INC

#include <string>

#include <allegro.h>
#ifdef ALLEGRO_WINDOWS
#include <winalleg.h>
#undef min
#undef max
#endif

inline int text_length(const FONT* f, const std::string& str) throw () {
    return text_length(f, str.c_str());
}

#if ALLEGRO_VERSION == 4 && ALLEGRO_SUB_VERSION == 0

#include "utility.h" // for PRINTF_FORMAT

// these are implemented in utility.cpp
void textprintf_ex(struct BITMAP* bmp, AL_CONST FONT *f, int x, int y, int color, int bg, AL_CONST char* format, ...) throw () PRINTF_FORMAT(7, 8);
void textprintf_centre_ex(struct BITMAP* bmp, AL_CONST FONT *f, int x, int y, int color, int bg, AL_CONST char* format, ...) throw () PRINTF_FORMAT(7, 8);
void textprintf_right_ex(struct BITMAP* bmp, AL_CONST FONT *f, int x, int y, int color, int bg, AL_CONST char* format, ...) throw () PRINTF_FORMAT(7, 8);
void textout_ex(struct BITMAP* bmp, AL_CONST FONT *f, AL_CONST char* text, int x, int y, int color, int bg) throw ();
void textout_centre_ex(struct BITMAP* bmp, AL_CONST FONT *f, AL_CONST char* text, int x, int y, int color, int bg) throw ();
void textout_right_ex(struct BITMAP* bmp, AL_CONST FONT *f, AL_CONST char* text, int x, int y, int color, int bg) throw ();

inline void set_close_button_callback(void (*fn)()) throw () {
    set_window_close_hook(fn);
}

#endif  // ALLEGRO_VERSION == 4 && ALLEGRO_SUB_VERSION == 0

#if ALLEGRO_VERSION == 4 && (ALLEGRO_SUB_VERSION == 0 || (ALLEGRO_SUB_VERSION == 1 && ALLEGRO_WIP_VERSION <= 11))

inline void set_clip_rect(BITMAP* bitmap, int x1, int y1, int x2, int y2) throw () {
    set_clip(bitmap, x1, y1, x2, y2);
}

inline void get_clip_rect(BITMAP* bitmap, int* x1, int* y1, int* x2, int* y2) throw () {
    *x1 = bitmap->cl;
    *y1 = bitmap->ct;
    *x2 = bitmap->cr - 1;
    *y2 = bitmap->cb - 1;
}

#endif

// simple "extensions": implemented in utility.cpp
void set_trans_mode(int alpha) throw ();

inline void textout_ex(BITMAP* bmp, AL_CONST FONT* f, const std::string& text, int x, int y, int color, int bg) throw () {
    textout_ex(bmp, f, text.c_str(), x, y, color, bg);
}

inline void textout_centre_ex(BITMAP* bmp, AL_CONST FONT* f, const std::string& text, int x, int y, int color, int bg) throw () {
    textout_centre_ex(bmp, f, text.c_str(), x, y, color, bg);
}

inline void textout_right_ex(BITMAP* bmp, AL_CONST FONT* f, const std::string& text, int x, int y, int color, int bg) throw () {
    textout_right_ex(bmp, f, text.c_str(), x, y, color, bg);
}

int makecolBounded(int r, int g, int b) throw ();

int set_gfx_mode_if_new(int depth, int card, int w, int h, int v_w, int v_h) throw ();

#endif  // INCALLEG_H_INC
