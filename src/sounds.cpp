/*
 *  sounds.cpp
 *
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004, 2006 - Niko Ritari
 *  Copyright (C) 2003, 2004, 2005 - Jani Rivinoja
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

#include <algorithm>

#include "language.h"
#include "platform.h"
#include "sounds.h"

using std::ifstream;
using std::string;
using std::sort;
using std::vector;

Sounds::Sounds(LogSet logs) throw () :
    log(logs),
    enabled(false),
    allegroSoundInitialized(false),
    volume(255)
{
    //no samples loaded -- important so unload_samples don't crash
    for (int i = 0; i < NUM_OF_SAMPLES; i++)
        sample[i] = 0;
}

Sounds::~Sounds() throw () {
    unload_samples();
}

void Sounds::search_themes(LineReceiver& dst) const throw () {
    vector<string> themes;
    FileFinder* themeDirs = platMakeFileFinder(wheregamedir + "sound", "", true);
    while (themeDirs->hasNext())
        themes.push_back(themeDirs->next());
    delete themeDirs;
    if (themes.empty()) {
        dst(_("<no themes found>"));
        return;
    }
    sort(themes.begin(), themes.end());
    for (vector<string>::const_iterator ti = themes.begin(); ti != themes.end(); ++ti)
        dst(*ti);
}

void Sounds::select_theme(const string& dir) throw () {
    unload_samples();

    themedir = dir;

    if (dir == _("<no themes found>")) {
        themename.clear();
        return;
    }

    const string path = wheregamedir + "sound" + directory_separator + dir + directory_separator;

    ifstream in((path + "theme.txt").c_str());
    if (!getline_skip_comments(in, themename))
        themename = _("(unnamed theme)");

    if (enabled) {
        load_samples(path);
        log("Loaded sound theme '%s'.", dir.c_str());
        play(rand() % NUM_OF_SAMPLES, 1000);
    }
}

bool Sounds::setEnable(bool enable) throw () {
    if (enable == enabled)
        return true;
    if (enable) {
        if (!try_init())
            return false;
        enabled = true;
        if (!themedir.empty())
            select_theme(themedir);
    }
    else {
        unload_samples();
        enabled = false;
        remove_sound();
        allegroSoundInitialized = false;
    }
    return true;
}

bool Sounds::try_init() throw () {
    if (allegroSoundInitialized)
        return true;
    log("Initializing sound.");
    set_volume_per_voice(0);
    if (install_sound(DIGI_AUTODETECT, MIDI_NONE, 0)) {
        log("Install_sound failed. Sound disabled.");
        return false;
    }
    else {
        log("Install_sound ok.");
        allegroSoundInitialized = true;
        return true;
    }
}

void Sounds::load_samples(const string& path) throw () {
    if (!enabled)
        return;
    nAssert(allegroSoundInitialized);

    load_outgun_sample(path, "fire", SAMPLE_FIRE);
    load_outgun_sample(path, "hit", SAMPLE_HIT);
    load_outgun_sample(path, "wallhit", SAMPLE_WALLHIT);
    load_outgun_sample(path, "qwallhit", SAMPLE_POWERWALLHIT);
    load_outgun_sample(path, "colldam", SAMPLE_COLLISION_DAMAGE);

    load_outgun_sample(path, "getdb", SAMPLE_GETDEATHBRINGER);
    load_outgun_sample(path, "usedb", SAMPLE_USEDEATHBRINGER);
    load_outgun_sample(path, "hitdb", SAMPLE_HITDEATHBRINGER);
    load_outgun_sample(path, "diedb", SAMPLE_DIEDEATHBRINGER);

    load_outgun_sample(path, "death1", SAMPLE_DEATH);
    load_outgun_sample(path, "death2", SAMPLE_DEATH_2);

    load_outgun_sample(path, "entergam", SAMPLE_ENTERGAME);
    load_outgun_sample(path, "leftgam", SAMPLE_LEFTGAME);
    load_outgun_sample(path, "chanteam", SAMPLE_CHANGETEAM);
    load_outgun_sample(path, "talk", SAMPLE_TALK);
    load_outgun_sample(path, "wabounce", SAMPLE_WALLBOUNCE);
    load_outgun_sample(path, "plbounce", SAMPLE_PLAYERBOUNCE);

    load_outgun_sample(path, "weaponup", SAMPLE_WEAPON_UP);
    load_outgun_sample(path, "megaheal", SAMPLE_MEGAHEALTH);
    load_outgun_sample(path, "shieldp", SAMPLE_SHIELD_POWERUP);
    load_outgun_sample(path, "shieldd", SAMPLE_SHIELD_DAMAGE);
    load_outgun_sample(path, "shieldl", SAMPLE_SHIELD_LOST);
    load_outgun_sample(path, "speedon", SAMPLE_TURBO_ON);
    load_outgun_sample(path, "speedoff", SAMPLE_TURBO_OFF);
    load_outgun_sample(path, "quadon", SAMPLE_POWER_ON);
    load_outgun_sample(path, "quadfire", SAMPLE_POWER_FIRE);
    load_outgun_sample(path, "quadoff", SAMPLE_POWER_OFF);
    load_outgun_sample(path, "helmon", SAMPLE_SHADOW_ON);
    load_outgun_sample(path, "helmoff", SAMPLE_SHADOW_OFF);

    load_outgun_sample(path, "got", SAMPLE_CTF_GOT);
    load_outgun_sample(path, "lost", SAMPLE_CTF_LOST);
    load_outgun_sample(path, "return", SAMPLE_CTF_RETURN);
    load_outgun_sample(path, "capture", SAMPLE_CTF_CAPTURE);
    load_outgun_sample(path, "gameover", SAMPLE_CTF_GAMEOVER);

    load_outgun_sample(path, "5_min", SAMPLE_5_MIN_LEFT);
    load_outgun_sample(path, "1_min", SAMPLE_1_MIN_LEFT);

    load_outgun_sample(path, "spree", SAMPLE_KILLING_SPREE);
}

SAMPLE* Sounds::load_outgun_sample(const string& path, const string& fname, int slot, bool try_redirect) throw () {
    const string fileName = path + fname + ".wav";

    SAMPLE* ret = sample[slot] = load_sample(fileName.c_str());

    if (try_redirect && ret == 0) { // if not found, look for .txt redirect
        const string textName = path + fname + ".txt";

        ifstream in(textName.c_str());
        if (in) {
            string redir_name;
            getline_skip_comments(in, redir_name);
            in.close();

            return load_outgun_sample(path, redir_name.c_str(), slot, false);   // no more redirections (avoid endless loops)
        }
    }

    return ret;
}

void Sounds::unload_samples() throw () {
    if (!allegroSoundInitialized)
        return;
    for (int i = 0; i < NUM_OF_SAMPLES; i++)
        if (sample[i]) {
            destroy_sample(sample[i]);
            sample[i] = 0;
        }
}

void Sounds::play(int s, int f) const throw () {
    nAssert(s >= 0 && s < NUM_OF_SAMPLES);
    if (enabled && sample[s]) {
        nAssert(allegroSoundInitialized);
        stop_sample(sample[s]); // kill any voice playing that sample
        play_sample(sample[s], volume, 127, f, false);   // regular play
    }
}
