/*
 *  commont.h
 *
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004, 2005 - Niko Ritari
 *  Copyright (C) 2003, 2004, 2006 - Jani Rivinoja
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

// commont.h : temporary common stuff

#ifndef COMMONT_H_INC
#define COMMONT_H_INC

#include <iostream>
#include <fstream>
#include <string>
#include <cmath>
#include <nl.h>

// Reads a line, stops to \n or \r and skips empty lines.
std::istream& getline_smart(std::istream& in, std::string& str);

// Read a line like getline_smart, but additionally skip lines that begin with a ;
std::istream& getline_skip_comments(std::istream& in, std::string& str);

// Check player name validity.
bool check_name(const std::string& name);

bool isFlood(const std::string& message);

enum Message_type { msg_normal, msg_team, msg_info, msg_warning, msg_server, msg_header };

static const std::string::size_type maxPlayerNameLength = 15;
static const std::string::size_type max_chat_message_length = 200; // How long messages players can send (3 lines).

enum DamageType { DT_rocket, DT_deathbringer, DT_collision };

bool readJoystickButton(int button);    // operates on pseudo button ids that are Allegro button id + 1; returns false on all non-button-mapped indices

bool is_keypad(int sc);

class ClientControls {
public:
    ClientControls() : data(0) { }

    NLubyte toNetwork(bool server) const { if (server) return data & 31; else return data; }
    void fromNetwork(NLubyte d, bool server) { data = d; if (server) data &= 31; }
    void fromKeyboard(bool use_pad, bool use_cursor_keys);
    void fromJoystick(int moving_stick, int run_button, int strafe_button); // uses pseudo button ids like readJoystickButton

    bool isUp    () const { return (data & up    ) != 0; }
    bool isDown  () const { return (data & down  ) != 0; }
    bool isLeft  () const { return (data & left  ) != 0; }
    bool isRight () const { return (data & right ) != 0; }
    bool isRun   () const { return (data & run   ) != 0; }
    bool isStrafe() const { return (data & strafe) != 0; }
    bool idle    () const { return  data           == 0; }

    bool isUpDown   () const { return isUp  () != isDown (); }
    bool isLeftRight() const { return isLeft() != isRight(); }

    bool operator==(const ClientControls& o) { return data == o.data; }
    bool operator!=(const ClientControls& o) { return data != o.data; }

    void clearModifiersIfIdle() { if (!isUpDown() && !isLeftRight()) data &= ~(run | strafe); }

private:
    NLubyte data;

    enum {
        up     =  1,
        down   =  2,
        left   =  4,
        right  =  8,
        run    = 16,
        strafe = 32
    };
};

class ClientLoginStatus {
public:
    ClientLoginStatus(): data(0) { }

    NLubyte toNetwork() const { return data; }
    void fromNetwork(NLubyte byte) { data = byte; }

    std::string strFlags() const {
        std::string s;
        s += token     () ? (masterAuth() ? 'M' : '?') : ' ';
        s += tournament() ? (masterAuth() ? 'T' : '?') : ' ';
        s += localAuth () ? 'S' : ' ';
        s += admin     () ? 'A' : ' ';
        return s;
    }

    bool token     () const { return (data & SB_token     ) != 0; } // client has reported a token
    bool masterAuth() const { return (data & SB_masterAuth) != 0; } // client's token has been authorized by master
    bool tournament() const { return (data & SB_tournament) != 0; } // client's score is being recorded for tournament scoring
    bool localAuth () const { return (data & SB_localAuth ) != 0; } // client has been authorized by the server's auth.txt
    bool admin     () const { return (data & SB_admin     ) != 0; } // client is an admin on this server

    void setToken     (bool b) { data = (data & (~SB_token     )) | (b ? SB_token      : 0); }
    void setMasterAuth(bool b) { data = (data & (~SB_masterAuth)) | (b ? SB_masterAuth : 0); }
    void setTournament(bool b) { data = (data & (~SB_tournament)) | (b ? SB_tournament : 0); }
    void setLocalAuth (bool b) { data = (data & (~SB_localAuth )) | (b ? SB_localAuth  : 0); }
    void setAdmin     (bool b) { data = (data & (~SB_admin     )) | (b ? SB_admin      : 0); }

    bool operator==(const ClientLoginStatus& o) const { return data == o.data; }
    bool operator!=(const ClientLoginStatus& o) const { return data != o.data; }

private:
    enum StatusBit {
        SB_token = 1,
        SB_masterAuth = 2,
        SB_tournament = 4,
        SB_localAuth = 8,
        SB_admin = 16
    };

    NLubyte data;
};

class GlobalDisplaySwitchHook {
    static volatile bool flag;
    friend void GlobalDisplaySwitchHook__callback();

public:
    static void init();
    static void install();
    static bool readAndClear();
};

class LogSet;

class MasterSettings {
    NLaddress masterAddress;
    std::string queryScript;
    std::string submitScript;
    NLaddress bugAddress;
    int configCRC;

public:
    const NLaddress& address() const { return masterAddress; }
    const std::string& query() const { return queryScript; }
    const std::string& submit() const { return submitScript; }
    const NLaddress& bugReportAddress() const { return bugAddress; }
    int crc() const { return configCRC; }

    void load(LogSet& log);
};

extern MasterSettings g_masterSettings;

template<int expBits, int expBias> class SignedByteFloat {
public:
    enum { mantissaBits = 8 - 1 - expBits,  // bits in byte - sign bit - exponent bits
           mantissaLimit = 1 << mantissaBits };

    static unsigned char toByte(double v) {
        const unsigned char sign = (v < 0. ? 0x80 : 0);

        int iVal = static_cast<int>(ldexp(fabs(v), -expBias) + .5);
        if (iVal < mantissaLimit)   // we have a loss of precision; this is a special case without the implicit mantissa-bit
            return sign | iVal; // mark it with exp = 0
        int exp = 1;
        while (iVal > 4 * mantissaLimit) {
            iVal /= 2;
            ++exp;
        }
        while (iVal >= 2 * mantissaLimit) { // it can be repeated once if the rounding bumps it to exactly 2 * mantissaLimit
            iVal = (iVal + 1) / 2;  // round
            ++exp;
        }
        if (exp >= (1 << expBits))  // can't be represented
            return sign | 0x7F; // max exp, max mantissa to approximate
        else
            return sign | (exp << mantissaBits) | (iVal - mantissaLimit);
    }
    static double toDouble(unsigned char b) {
        double val = b & (mantissaLimit - 1);   // use double so that val = -val makes 0 -> -0, it may be useful and otherwise byte 128 is redundant
        const int exp = (b & 0x7F) >> (8 - 1 - expBits);
        if (exp == 0) {
            if (b & 0x80)
                val = -val;
            return ldexp(val, expBias);
        }
        val += mantissaLimit;
        if (b & 0x80)
            val = -val;
        return ldexp(val, exp - 1 + expBias);
    }
};

static const int plw = 472, plh = 354;  // play area width/height

static const int PLAYER_RADIUS = 15;
static const int SHIELD_RADIUS_ADD = 9; // this is added to PLAYER_RADIUS
static const int ROCKET_RADIUS = 4, POWER_ROCKET_RADIUS = 6;

// Game specific strings
#define GAME_STRING "Outgun"
#define GAME_PROTOCOL "1.0"
#define GAME_VERSION "1.0.3"
#define GAME_SHORT_VERSION "1.0.3"   // to keep the entry in the server list menu nice, this should be at most 7 characters; 8 is borderline acceptable
#define GAME_BRANCH "base"  // this only affects the master server communications, to make it tell the correct newest version

#define TK1_VERSION_STRING "v048"

//************************************************************
//  common stuff
//************************************************************

//the default game port
const NLushort DEFAULT_UDP_PORT = 25000;

//directories for save/load maps
#define SERVER_MAPS_DIR "maps"
#define CLIENT_MAPS_DIR "cmaps"

// system directory separator
extern char directory_separator;

//root path (game executable path)
extern std::string wheregamedir;

// number-of-players
static const int MAX_PLAYERS = 32;  // the MAXIMUM MAXIMUM number of players EVER
#define TSIZE (maxplayers/2)    // macro for CTF TEAM SIZE: this is ugly; it relies on a maxplayers variable being accessible, the variable in question will vary by place of use

static const int MAX_ROCKETS = 256; // maximum number of rockets (must be <= 256 while IDs are transmitted as bytes)
static const int MAX_PICKUPS = 32; // the MAXIMUM MAXIMUM number of pickups laying on the ground at one time in the game

static const double N_PI   = 3.14159265358979323846;
static const double N_PI_2 = 1.57079632679489661923;
static const double N_PI_4 = 0.78539816339744830962;

//server_next_map() reasons
enum {
    NEXTMAP_NONE,
    NEXTMAP_CAPTURE_LIMIT,
    NEXTMAP_VOTE_EXIT,
    NUM_OF_NEXTMAP
};

//audio samples : codes
enum {
    SAMPLE_FIRE,
    SAMPLE_HIT,

    SAMPLE_WALLHIT,
    SAMPLE_POWERWALLHIT,

    SAMPLE_DEATH,
    SAMPLE_DEATH_2,
    SAMPLE_ENTERGAME,
    SAMPLE_LEFTGAME,
    SAMPLE_CHANGETEAM,
    SAMPLE_TALK,
    SAMPLE_WALLBOUNCE,
    SAMPLE_PLAYERBOUNCE,

    SAMPLE_WEAPON_UP,

    SAMPLE_MEGAHEALTH,

    SAMPLE_GETDEATHBRINGER,
    SAMPLE_USEDEATHBRINGER,
    SAMPLE_HITDEATHBRINGER,
    SAMPLE_DIEDEATHBRINGER,

    SAMPLE_SHIELD_PICKUP,
    SAMPLE_SHIELD_DAMAGE,
    SAMPLE_SHIELD_LOST,

    SAMPLE_TURBO_ON,
    SAMPLE_TURBO_OFF,

    SAMPLE_POWER_ON,
    SAMPLE_POWER_FIRE,
    SAMPLE_POWER_OFF,

    SAMPLE_SHADOW_ON,
    SAMPLE_SHADOW_OFF,

    SAMPLE_CTF_GOT,
    SAMPLE_CTF_LOST,
    SAMPLE_CTF_RETURN,
    SAMPLE_CTF_CAPTURE,
    SAMPLE_CTF_GAMEOVER,

    SAMPLE_5_MIN_LEFT,
    SAMPLE_1_MIN_LEFT,

    SAMPLE_KILLING_SPREE,

    SAMPLE_COLLISION_DAMAGE,

    NUM_OF_SAMPLES
};

#endif
