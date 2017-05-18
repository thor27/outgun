/*
 *  sounds.h
 *
 *  Copyright (C) 2004 - Niko Ritari
 *  Copyright (C) 2004 - Jani Rivinoja
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

#ifndef SOUNDS_H_INC
#define SOUNDS_H_INC

#include "incalleg.h"
#include "commont.h"
#include "log.h"
#include "utility.h"

class Sounds {
public:
    Sounds(LogSet logs);
    ~Sounds();

    void play(int s) const;

    bool sampleExists(int s) const { return sample[s] != 0; }

    void search_themes(LineReceiver& dst) const;
    void select_theme(const std::string& dir);

    bool setEnable(bool enable);
    void setVolume(int vol) { nAssert(vol >= 0 && vol <= 255); volume = vol; }

private:
    bool try_init();

    void load_samples(const std::string& path);
    SAMPLE* load_outgun_sample(const std::string& path, const std::string& fname, int slot, bool try_redirect = true);
    void unload_samples();

    mutable LogSet log;
    SAMPLE* sample[NUM_OF_SAMPLES];

    std::string themedir;
    std::string themename;

    bool enabled;
    bool allegroSoundInitialized;
    int volume; // 0 to 255
};

#endif // SOUNDS_H_INC
