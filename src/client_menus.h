/*
 *  client_menus.h
 *
 *  Copyright (C) 2004, 2005, 2006, 2008 - Niko Ritari
 *  Copyright (C) 2004, 2005, 2006, 2008 - Jani Rivinoja
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

#ifndef CLIENT_MENUS_H_INC
#define CLIENT_MENUS_H_INC

#include <string>
#include <vector>

#include "graphics.h"   // for mode fetching functions
#include "menu.h"

enum ClientCfgSetting {
    CCS_PlayerName,
    CCS_Tournament,
    CCS_Favorites,
    CCS_FavoriteColors,
    CCS_LagPrediction,
    CCS_LagPredictionAmount,
    CCS_Joystick,
    CCS_MessageLogging,
    CCS_SaveStats,
    CCS_Windowed,
    CCS_GFXMode,
    CCS_Flipping,
    CCS_FPSLimit,
    CCS_GFXTheme,
    CCS_Antialiasing,
    CCS_StatsBgAlpha,
    CCS_ShowNames,
    CCS_SoundEnabled,
    CCS_Volume,
    CCS_SoundTheme,
    CCS_ShowStats,
    CCS_AutoGetServerList,
    CCS_ShowServerInfo,
    CCS_KeypadMoving,
    CCS_JoystickMove,
    CCS_JoystickShoot,
    CCS_JoystickRun,
    CCS_JoystickStrafe,
    CCS_ContinuousTextures,
    CCS_UnderlineMasterAuth,
    CCS_UnderlineServerAuth,
    CCS_ServerPublic,
    CCS_ServerPort,
    CCS_KeyboardLayout,
    CCS_ServerAddress,
    CCS_AutodetectAddress,
    CCS_ArrowKeysInStats,
    CCS_MinimapPlayers,
    CCS_AlternativeFlipping,
    CCS_StayDeadInMenus,
    CCS_MinTransp,
    CCS_UseThemeBackground,
    CCS_Background,
    CCS_Font,
    CCS_ShowFlagMessages,
    CCS_ShowKillMessages,
    CCS_HighlightReturnedFlag,
    CCS_ArrowKeysInTextInput,
    CCS_SpawnHighlight,
    CCS_NeighborMarkersPlay,
    CCS_NeighborMarkersReplay,
    CCS_BoxRoomsWhenPlaying,
    CCS_ViewOverMapBorder,
    CCS_EmphasizeFlags,
    CCS_MouseSensitivity,
    CCS_MouseShoot,
    CCS_MouseRun,
    CCS_AimMode,
    CCS_MoveRelativity,
    CCS_TurningSpeed,
    CCS_MinimapBandwidth,
    CCS_RepeatMap,
    CCS_Scroll,
    CCS_VisibleRoomsPlay,
    CCS_VisibleRoomsReplay,
    CCS_OldFlagPositions,
    CCS_Colours,
    CCS_UseThemeColours,
    CCS_EndOfCommands
};

class SettingCollector {
public:
    virtual ~SettingCollector() throw () { }

    class SaverLoader {
    public:
        virtual ~SaverLoader() throw () { }
        virtual void save(std::ostream& o) const throw () = 0;
        virtual void load(const std::string& s) throw () = 0;
    };

    virtual void add(ClientCfgSetting key, SaverLoader* sl) throw () = 0; // ownership of sl is transferred
};

class Menu_addServer {
public:
    IPfield     address;
    Checkbox    save;

    Menu menu;

    Menu_addServer() throw ();
    void initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw ();
};

class Menu_serverList {
    std::vector<std::pair<Network::Address, Textarea> > servers;   // address and server info

public:
    Textarea            update;
    Textarea            refresh;
    StaticText          refreshStatus;
    Checkbox            favorites;
    Menu_addServer      addServer;
    IPfield             manualEntry;
    StaticText          keyHelp;
    StaticText          caption;

    Menu menu;

    Menu_serverList() throw ();
    void initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw ();

    void add(const Network::Address& address, const std::string& serverInfo) throw ();
    void reset() throw ();
    void addHooks(MenuHookable<Textarea>::HookFunctionT* hook, KeyHookable<Textarea>::HookFunctionT* keyHook) throw ();
    Network::Address getAddress(const Textarea& target) throw ();
};

class Menu_player {
public:
    Textfield   name;
    Textarea    randomName;
    Colorselect favoriteColors;
    Textfield   password;
    StaticText  namestatus;
    Checkbox    tournament;
    Textarea    removePasswords;

    Menu menu;

    Menu_player() throw ();
    void initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw ();
};

class Menu_game {
public:
    enum MessageLoggingMode {
        ML_none,
        ML_full,
        ML_chat
    };
    enum ShowStatsMode {
        SS_none,
        SS_teams,
        SS_players
    };

    Checkbox    lagPrediction;
    Slider      lagPredictionAmount;
    Slider      minimapBandwidth;
    Select<MessageLoggingMode> messageLogging;
    Checkbox    showFlagMessages;
    Checkbox    showKillMessages;
    Checkbox    saveStats;
    Select<ShowStatsMode> showStats;
    Checkbox    showServerInfo;
    Checkbox    stayDead;
    Checkbox    underlineMasterAuth;
    Checkbox    underlineServerAuth;
    Checkbox    autoGetServerList;

    Menu menu;

    Menu_game() throw ();
    void initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw ();
};

class Menu_controls {
public:
    enum ArrowKeysInStatsMode { AS_useMenu, AS_movePlayer };
    enum AimMode { AM_8way, AM_Turn, AM_Mouse };

    Select<std::string> keyboardLayout;
    Checkbox            keypadMoving;
    Select<ArrowKeysInStatsMode> arrowKeysInStats;
    Checkbox            arrowKeysInTextInput;

    Select<AimMode>     aimMode;
    Select<AccelerationMode> moveRelativity;
    Slider              turningSpeed;

    Checkbox            joystick;
    Slider              joyMove;
    StaticText          joyText;
    Slider              joyShoot;
    Slider              joyRun;
    Slider              joyStrafe;

    StaticText          mouseText;
    Slider              mouseSensitivity;
    Slider              mouseShoot;
    Slider              mouseRun;

    StaticText          activeControls;
    StaticText          activeJoystick;
    StaticText          activeMouse;

    Menu menu;

    Menu_controls() throw ();
    void initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw ();
};

class Menu_screenMode {
    ScreenMode oldMode;
    int oldDepth;
    bool oldWin, oldFlip;

    void reloadChoices(const Graphics& gfx) throw ();

public:
    Select<int>         colorDepth;
    StaticText          desktopDepth;
    Select<ScreenMode>  resolution;
    Checkbox            windowed;
    Checkbox            flipping;
    Checkbox            alternativeFlipping;
    StaticText          refreshRate;
    Textarea            apply;

    Menu menu;

    Menu_screenMode() throw ();
    void initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw ();

    void init(const Graphics& gfx) throw ();   // call just once, before calling update
    void update(const Graphics& gfx) throw (); // tries to keep the selected resolution
    bool newMode() throw (); // returns true if the current selection differs from the one at last call
};

class Menu_theme {
    void reloadChoices(const Graphics& gfx) throw ();

public:
    Select<std::string> theme;
    Select<std::string> background;
    Checkbox            useThemeBackground;
    Select<std::string> colours;
    Checkbox            useThemeColours;
    Select<std::string> font;

    Menu menu;

    Menu_theme() throw ();
    void initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw ();

    void init(const Graphics& gfx) throw (); // call just once, before calling update
    void update(const Graphics& gfx) throw ();   // tries to keep the selected choices
};

class Menu_graphics {
public:
    enum NameMode { N_Never, N_SameRoom, N_Always, N_COUNT };
    enum MinimapPlayerMode { MP_Fade, MP_EarlyCut, MP_LateCut, MP_COUNT };
    enum FlagEmphasizeMode { FE_Never, FE_MultiRoom, FE_Always, FE_COUNT };
    enum NeighborMarkerMode { NM_Never, NM_OneRoom, NM_Always, NM_COUNT };
    enum ViewOverBorderMode { VOB_Never, VOB_MapDoesntFit, VOB_MapWraps, VOB_Always, VOB_COUNT };

    Select<NameMode>    showNames;
    Slider              visibleRoomsPlay;
    Slider              visibleRoomsReplay;
    Checkbox            scroll;
    Checkbox            antialiasing;
    Checkbox            minTransp;
    Checkbox            contTextures;
    Select<MinimapPlayerMode> minimapPlayers;
    Checkbox            highlightReturnedFlag;
    Select<FlagEmphasizeMode> emphasizeFlags;
    Checkbox            oldFlagPositions;
    Checkbox            spawnHighlight;
    Select<NeighborMarkerMode> neighborMarkersPlay;
    Select<NeighborMarkerMode> neighborMarkersReplay;
    Checkbox            boxRoomsWhenPlaying;
    Select<ViewOverBorderMode> viewOverMapBorder;
    Checkbox            repeatMap;
    Slider              statsBgAlpha;
    Slider              fpsLimit;
    Checkbox            mapInfoMode;

    Menu menu;

    Menu_graphics() throw ();
    void initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw ();

    bool showName(bool sameRoom) const throw () { return showNames() != N_Never && (sameRoom || showNames() == N_Always); }
    bool emphasizeFlag(double visible_rooms) const throw () { return emphasizeFlags() != FE_Never && (visible_rooms >= 2. || emphasizeFlags() == FE_Always); }
    bool showNeighborMarkers(bool replaying, double visible_rooms) const throw ();
};

class Menu_sounds {
public:
    Checkbox            enabled;
    Slider              volume;
    Select<std::string> theme;

    Menu menu;

    Menu_sounds() throw ();
    void initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw ();

    void init(const Sounds& snd) throw ();
    void update(const Sounds& snd) throw (); // tries to keep the selected theme
};

class Menu_language {
    std::vector<std::pair<std::string, Textarea> > languages;

public:
    Menu menu;

    Menu_language() throw ();
    void initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw ();

    void add(const std::string& code, const std::string& name) throw ();
    void reset() throw ();
    void addHooks(MenuHookable<Textarea>::HookFunctionT* hook) throw ();
    const std::string& getCode(const Textarea& target) throw ();
};

class Menu_bugReportPolicy {
    std::vector<std::string> lines;

    void init() throw ();

public:
    Textobject text;
    Select<AutoBugReporting> policy;

    Menu menu;
    void initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw ();

    Menu_bugReportPolicy() throw ();
    void clear() throw () { lines.clear(); }
    void addLine(const std::string& line) throw ();
};

class Menu_options {
public:
    Menu_player          player;
    Menu_game            game;
    Menu_controls        controls;
    Menu_screenMode      screenMode;
    Menu_theme           theme;
    Menu_graphics        graphics;
    Menu_sounds          sounds;
    Menu_language        language;
    Menu_bugReportPolicy bugReports;

    Menu menu;

    Menu_options() throw ();
    void initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw ();
};

class Menu_help {
    std::vector<std::string> lines;

public:
    Textobject text;

    Menu menu;

    Menu_help() throw ();
    void initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw ();

    void clear() throw () { lines.clear(); }
    void addLine(const std::string& line) throw ();
};

class Menu_ownServer {
public:
    Checkbox        pub;
    NumberEntry     port;
    IPfield         address;
    Checkbox        autoIP;
    Textarea        start;
    Textarea        play;
    Textarea        stop;

    Menu menu;

    Menu_ownServer() throw ();
    void initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw ();

    void init(const std::string& detectedAddress) throw ();
    void refreshCaption(bool serverRunning) throw ();
    void refreshEnables(bool serverRunning, bool connected) throw ();

private:
    std::string detectedIP;
};

class Menu_replays {
public:
    std::vector<std::pair<std::string, Textarea> > items;
    Textarea        caption;

    Menu menu;

    Menu_replays() throw ();
    void initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw ();

    void add(const std::string& replay, const std::string& text) throw ();
    void reset() throw ();
    void addHooks(MenuHookable<Textarea>::HookFunctionT* hook) throw ();
    const std::string& getFile(const Textarea& target) throw ();
};

class Menu_main {
public:
    StaticText      newVersion;
    Menu_serverList connect;
    Textarea        disconnect;
    Menu_options    options;
    Menu_ownServer  ownServer;
    Menu_replays    replays;
    Menu_help       help;
    Textarea        exitOutgun;

    Menu menu;

    Menu_main() throw ();
    void initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw ();
};

class Menu_text {
    std::vector<StaticText> lines;

public:
    Textarea                accept;
    Textarea                cancel;
    // either accept or cancel visible at a time

    Menu menu;

    Menu_text() throw ();
    void initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw ();

    void clear() throw () { lines.clear(); }
    void addLine(const std::string& line, bool cancelable = false) throw ();
    void addLine(const std::string& caption, const std::string& value, bool cancelable = false, bool passive = false) throw (); // passive means neither accept nor cancel is used
    void wrapLine(const std::string& line, bool cancelable = false, int wrapPos = 68) throw ();
};

class Menu_playerPassword {
public:
    Textfield   password;
    Checkbox    save;

    Menu menu;

    Menu_playerPassword() throw ();
    void setup(const std::string& plyName, bool saveChecked) throw ();
};

class Menu_serverPassword {
public:
    Textfield password;

    Menu menu;

    Menu_serverPassword() throw ();
};

#endif  // CLIENT_MENUS_H_INC
