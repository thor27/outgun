/*
 *  commont.h
 *
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004, 2005, 2008 - Niko Ritari
 *  Copyright (C) 2003, 2004, 2006, 2008 - Jani Rivinoja
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

#include "network.h"

// Reads a line, stops to \n or \r and skips empty lines.
std::istream& getline_smart(std::istream& in, std::string& str) throw ();

// Read a line like getline_smart, but additionally skip lines that begin with a ;
std::istream& getline_skip_comments(std::istream& in, std::string& str) throw ();

// Check player name validity.
bool check_name(const std::string& name) throw ();

bool isFlood(const std::string& message) throw ();

enum Message_type { msg_normal, msg_team, msg_info, msg_warning, msg_server, msg_header, Message_types };

static const std::string::size_type maxPlayerNameLength = 15;
static const std::string::size_type max_chat_message_length = 200; // How long messages players can send (3 lines).

enum DamageType { DT_rocket, DT_deathbringer, DT_collision };

enum AccelerationMode { AM_World, AM_Gun };

bool readJoystickButton(int button) throw ();    // operates on pseudo button ids that are Allegro button id + 1; returns false on all non-button-mapped indices

bool is_keypad(int sc) throw ();

class ClientControls {
public:
    ClientControls() throw () : data(0) { }

    uint8_t toNetwork(bool server) const throw () { if (server) return data & 31; else return data; }
    void fromNetwork(uint8_t d, bool server) throw () { data = d; if (server) data &= 31; }
    void fromKeyboard(bool use_pad, bool use_cursor_keys) throw ();
    void fromJoystick(int moving_stick, int run_button, int strafe_button) throw (); // uses pseudo button ids like readJoystickButton

    bool isUp    () const throw () { return (data & up    ) != 0; }
    bool isDown  () const throw () { return (data & down  ) != 0; }
    bool isLeft  () const throw () { return (data & left  ) != 0; }
    bool isRight () const throw () { return (data & right ) != 0; }
    bool isRun   () const throw () { return (data & run   ) != 0; }
    bool isStrafe() const throw () { return (data & strafe) != 0; }
    bool idle    () const throw () { return  data           == 0; }

    ClientControls& setUp    () throw () { data |= up;     return *this; }
    ClientControls& setDown  () throw () { data |= down;   return *this; }
    ClientControls& setLeft  () throw () { data |= left;   return *this; }
    ClientControls& setRight () throw () { data |= right;  return *this; }
    ClientControls& setRun   () throw () { data |= run;    return *this; }
    ClientControls& setStrafe() throw () { data |= strafe; return *this; }

    void clearLeft () throw () { data &= ~left; }
    void clearRight() throw () { data &= ~right; }

    void clearRun  () throw () { data &= ~run; }

    bool isUpDown   () const throw () { return isUp  () != isDown (); }
    bool isLeftRight() const throw () { return isLeft() != isRight(); }

    bool operator==(const ClientControls& o) const throw () { return data == o.data; }
    bool operator!=(const ClientControls& o) const throw () { return data != o.data; }

    void clearModifiersIfIdle() throw () { if (!isUpDown() && !isLeftRight()) data &= ~(run | strafe); }

    int getDirection() const throw (); // returns -1 for no direction, else between 0..7

private:
    uint8_t data;

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
    ClientLoginStatus() throw () : data(0) { }

    uint8_t toNetwork() const throw () { return data; }
    void fromNetwork(uint8_t byte) throw () { data = byte; }

    std::string strFlags() const throw () {
        std::string s;
        s += token     () ? (masterAuth() ? 'M' : '?') : ' ';
        s += tournament() ? (masterAuth() ? 'T' : '?') : ' ';
        s += localAuth () ? 'S' : ' ';
        s += admin     () ? 'A' : ' ';
        return s;
    }

    bool token     () const throw () { return (data & SB_token     ) != 0; } // client has reported a token
    bool masterAuth() const throw () { return (data & SB_masterAuth) != 0; } // client's token has been authorized by master
    bool tournament() const throw () { return (data & SB_tournament) != 0; } // client's score is being recorded for tournament scoring
    bool localAuth () const throw () { return (data & SB_localAuth ) != 0; } // client has been authorized by the server's auth.txt
    bool admin     () const throw () { return (data & SB_admin     ) != 0; } // client is an admin on this server

    void setToken     (bool b) throw () { data = (data & (~SB_token     )) | (b ? SB_token      : 0); }
    void setMasterAuth(bool b) throw () { data = (data & (~SB_masterAuth)) | (b ? SB_masterAuth : 0); }
    void setTournament(bool b) throw () { data = (data & (~SB_tournament)) | (b ? SB_tournament : 0); }
    void setLocalAuth (bool b) throw () { data = (data & (~SB_localAuth )) | (b ? SB_localAuth  : 0); }
    void setAdmin     (bool b) throw () { data = (data & (~SB_admin     )) | (b ? SB_admin      : 0); }

    bool operator==(const ClientLoginStatus& o) const throw () { return data == o.data; }
    bool operator!=(const ClientLoginStatus& o) const throw () { return data != o.data; }

private:
    enum StatusBit {
        SB_token = 1,
        SB_masterAuth = 2,
        SB_tournament = 4,
        SB_localAuth = 8,
        SB_admin = 16
    };

    uint8_t data;
};

class GlobalDisplaySwitchHook {
    static volatile bool flag;
    friend void GlobalDisplaySwitchHook__callback() throw ();

public:
    static void init() throw ();
    static void install() throw ();
    static bool readAndClear() throw ();
};

#ifndef DEDICATED_SERVER_ONLY

class GlobalMouseHook {
    static volatile unsigned buttonActivityCount[16];
    friend void GlobalMouseHook__callback(int) throw ();

public:
    static void install() throw ();
    static int read(int button) throw () { return buttonActivityCount[button]; }
};

class RegisterMouseClicks {
    unsigned readCounts[16];

public:
    void clear() throw ();
    bool wasClicked(int button) throw ();
};

#endif

class LogSet;

class MasterSettings {
    Network::Address masterAddress;
    std::string hostName;
    std::string queryScript;
    std::string submitScript;
    Network::Address bugAddress;
    int configCRC;

public:
    MasterSettings() throw () : configCRC(0) { }
    const Network::Address& address() const throw () { return masterAddress; }
    const std::string& host() const throw () { return hostName; }
    const std::string& query() const throw () { return queryScript; }
    const std::string& submit() const throw () { return submitScript; }
    const Network::Address& bugReportAddress() const throw () { return bugAddress; }
    int crc() const throw () { return configCRC; }

    void load(LogSet& log) throw ();
};

extern MasterSettings g_masterSettings;

extern volatile bool g_exitFlag;

template<int expBits, int expBias> class SignedByteFloat {
public:
    enum { mantissaBits = 8 - 1 - expBits,  // bits in byte - sign bit - exponent bits
           mantissaLimit = 1 << mantissaBits };

    static unsigned char toByte(double v) throw () {
        const unsigned char sign = (v < 0. ? 0x80 : 0);

        int iVal = static_cast<int>(ldexp(fabs(v), -expBias) + .5);
        if (iVal < mantissaLimit)   // we have a loss of precision; this is a special case without the implicit mantissa-bit
            return sign | iVal; // mark it with exp = 0
        int exp = 1;
        while (iVal >= 4 * mantissaLimit) {
            iVal /= 2;
            ++exp;
        }
        while (iVal >= 2 * mantissaLimit) { // the loop can be repeated once if the rounding bumps iVal to exactly 2 * mantissaLimit
            iVal = (iVal + 1) / 2;  // round
            ++exp;
        }
        if (exp >= (1 << expBits))  // can't be represented
            return sign | 0x7F; // max exp, max mantissa to approximate
        else
            return sign | (exp << mantissaBits) | (iVal - mantissaLimit);
    }
    static double toDouble(unsigned char b) throw () {
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

extern const std::string TK1_VERSION_STRING;

//************************************************************
//  common stuff
//************************************************************

//the default game port
const uint16_t DEFAULT_UDP_PORT = 25000;

//directories for save/load maps
extern const std::string SERVER_MAPS_DIR;
extern const std::string CLIENT_MAPS_DIR;

// system directory separator
extern char directory_separator;

// root path (game executable path)
extern std::string wheregamedir;

// number-of-players
static const int MAX_PLAYERS = 32;  // the MAXIMUM MAXIMUM number of players EVER
#define TSIZE (maxplayers/2)    // macro for CTF TEAM SIZE: this is ugly; it relies on a maxplayers variable being accessible, the variable in question will vary by place of use

static const int MAX_ROCKETS = 256; // maximum number of rockets (must be <= 256 while IDs are transmitted as bytes)
static const int MAX_POWERUPS = 32; // the MAXIMUM MAXIMUM number of powerups laying on the ground at one time in the game

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

    SAMPLE_SHIELD_POWERUP,
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
