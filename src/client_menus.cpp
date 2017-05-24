/*
 *  client_menus.cpp
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

#include <algorithm>
#include <string>
#include <sstream>
#include <vector>

#include "incalleg.h"
#include "debugconfig.h"    // for AutoBugReporting
#include "graphics.h"
#include "language.h"
#include "menu.h"
#include "network.h"
#include "sounds.h"
#include "utility.h"
#include "version.h"

#include "client_menus.h"

using std::ostringstream;
using std::pair;
using std::string;
using std::vector;

// definition for internal use
class StringSelectInserter : public LineReceiver {
    Select<string>& dst;

public:
    StringSelectInserter(Select<string>& dst_) throw () : dst(dst_) { }
    StringSelectInserter& operator()(const string& str) throw () { dst.addOption(str, str); return *this; }
};

class BasicComponentAdder {
    Menu& menu;
    static Spacer sharedSpace;

protected:
    void add(Component* c) throw () { menu.add_component(c); } // easier to use than operator()

public:
    BasicComponentAdder(Menu& menu_) throw () : menu(menu_) { }
    virtual ~BasicComponentAdder() throw () { }

    void operator()(Component* c) throw () { add(c); }
    void space() throw () { add(&sharedSpace); }
};

Spacer BasicComponentAdder::sharedSpace(5);

class DualComponentAdder : public BasicComponentAdder {
    SettingCollector& collector;

    template<class CT> class BaseHandler : public SettingCollector::SaverLoader {
    protected:
        CT& component;

    public:
        BaseHandler(CT& comp) throw () : component(comp) { }
        virtual void save(std::ostream& os) const throw () { os << component(); }
        virtual void load(const string& s) throw () = 0;
    };

    template<class CT> class StraightStringHandler : public BaseHandler<CT> {
    public:
        StraightStringHandler(CT& comp) throw () : BaseHandler<CT>(comp) { }
        void load(const string& s) throw () { BaseHandler<CT>::component.set(s); }
    };

    template<class CT, class ValT> class IntConvertibleHandler : public BaseHandler<CT> {
    public:
        IntConvertibleHandler(CT& comp) throw () : BaseHandler<CT>(comp) { }
        void load(const string& s) throw () { BaseHandler<CT>::component.set(static_cast<ValT>(atoi(s))); }
    };

    template<class CT> class BoundSetIntHandler : public BaseHandler<CT> {
    public:
        BoundSetIntHandler(CT& comp) throw () : BaseHandler<CT>(comp) { }
        void load(const string& s) throw () { BaseHandler<CT>::component.boundSet(atoi(s)); }
    };

public:
    DualComponentAdder(Menu& menu_, SettingCollector& collector_) throw () : BasicComponentAdder(menu_), collector(collector_) { }

    void operator()(Component* c) throw () { BasicComponentAdder::operator()(c); }
    void operator()(TextfieldBase*  c, ClientCfgSetting key) throw () { add(c); collector.add(key, new StraightStringHandler<TextfieldBase>  (*c)); }
    template<class T>
      void operator()(Select<T>*    c, ClientCfgSetting key) throw () { add(c); collector.add(key, new IntConvertibleHandler<Select<T>, T>   (*c)); }
    void operator()(Select<string>* c, ClientCfgSetting key) throw () { add(c); collector.add(key, new StraightStringHandler<Select<string> >(*c)); }
    void operator()(Checkbox*       c, ClientCfgSetting key) throw () { add(c); collector.add(key, new IntConvertibleHandler<Checkbox, bool> (*c)); }
    void operator()(Slider*         c, ClientCfgSetting key) throw () { add(c); collector.add(key, new BoundSetIntHandler<Slider>            (*c)); }
    void operator()(NumberEntry*    c, ClientCfgSetting key) throw () { add(c); collector.add(key, new BoundSetIntHandler<NumberEntry>       (*c)); }
};

Menu_addServer::Menu_addServer() throw () :
    address     (_("IP address"), true, false),
    save        (_("Add to favorite list")),

    menu        (_("Add server"), false)
{ }

void Menu_addServer::initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw () {
    menu.setHook(opener);
    DualComponentAdder add(menu, collector);
    add(&address);
    add(&save);
}

Menu_serverList::Menu_serverList() throw () :
    update          (_("Update server list")),
    refresh         (_("Refresh servers")),
    refreshStatus   (_("Refresh status")),

    favorites       (_("Show favorite servers")),
    addServer       (),
    manualEntry     (_("Manually enter IP"), true, false),

    keyHelp         (_("Insert = add to favorites    Delete = remove server")),

    caption         (_("IP address            Ping D Players Vers. Host name")),

    menu            (_("Server list"), true)
{
    reset();
}

void Menu_serverList::initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw () {
    menu.setHook(opener);
    addServer.initialize(opener->clone(), collector);
    DualComponentAdder add(menu, collector);
    add(&favorites, CCS_Favorites);
    // meaningful adding of components is only in reset()
}

void Menu_serverList::add(const Network::Address& address, const string& serverInfo) throw () {
    servers.push_back(pair<Network::Address, Textarea>(address, Textarea(serverInfo)));
}

void Menu_serverList::reset() throw () {
    menu.clear_components();
    servers.clear();
    BasicComponentAdder add(menu);
    add(&update);
    add(&refresh);
    add(&refreshStatus);
    add.space();
    add(&favorites);
    add(&addServer.menu);
    add(&manualEntry);
    add.space();
//  add(&keyHelp);
//  add.space();
    add(&caption);
}

void Menu_serverList::addHooks(MenuHookable<Textarea>::HookFunctionT* hook, KeyHookable<Textarea>::HookFunctionT* keyHook) throw () {
    BasicComponentAdder add(menu);
    for (vector<pair<Network::Address, Textarea> >::iterator servi = servers.begin(); servi != servers.end(); ++servi) {
        servi->second.setHook(hook->clone());
        servi->second.setKeyHook(keyHook->clone());
        add(&servi->second);
    }
    delete hook;
    delete keyHook;
}

Network::Address Menu_serverList::getAddress(const Textarea& target) throw () {
    for (vector<pair<Network::Address, Textarea> >::const_iterator servi = servers.begin(); servi != servers.end(); ++servi) {
        if (&servi->second == &target)
            return servi->first;
    }
    nAssert(0);
    return Network::Address();
}

Menu_player::Menu_player() throw () :
    name            (_("Name"), "", 15),
    randomName      (_("Get random name")),

    favoriteColors  (_("Favorite colors")),

    password        (_("Tournament password"), "", 15, '*'),
    namestatus      (_("Registration status")),
    tournament      (_("Take part in the tournament"), true),

    removePasswords (_("Remove server-specific player passwords")),

    menu            (_("Player options"), true)
{ }

void Menu_player::initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw () {
    menu.setHook(opener);
    DualComponentAdder add(menu, collector);
    add(&name);
    add(&randomName);
    add.space();
    add(&favoriteColors);
    #if 0
    add.space();
    add(&password);
    add(&namestatus);
    add(&tournament, CCS_Tournament);
    #endif
    add.space();
    add(&removePasswords);
}

Menu_game::Menu_game() throw () :
    lagPrediction       (_("Lag prediction"), false),
    lagPredictionAmount (_("Lag prediction amount"), true, 0, 10, 10),
    minimapBandwidth    (_("Bandwidth for out-of-room players (cps)"), false, 0, 640, 80, 20, true),

    messageLogging      (_("Save game messages")),
    showFlagMessages    (_("Show flag messages"), true),
    showKillMessages    (_("Show killing messages"), false),

    saveStats           (_("Save game statistics"), false),
    showStats           (_("Show stats after the round")),
    showServerInfo      (_("Show server info when connected"), false),
    stayDead            (_("Stay dead when in a menu at round start"), true),
    underlineMasterAuth (_("Underline master-authenticated players"), true),
    underlineServerAuth (_("Underline server-authenticated players"), false),

    autoGetServerList   (_("Get server list at startup"), true),

    menu                (_("Game options"), true)
{
    messageLogging.addOption(_("off"), ML_none);
    messageLogging.addOption(_("chat only"), ML_chat);
    messageLogging.addOption(_("all messages"), ML_full);
    showStats.addOption(_("off"), SS_none);
    showStats.addOption(_("teams"), SS_teams);
    showStats.addOption(_("players"), SS_players);
}

void Menu_game::initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw () {
    menu.setHook(opener);
    DualComponentAdder add(menu, collector);
    add(&lagPrediction,          CCS_LagPrediction);
    add(&lagPredictionAmount,    CCS_LagPredictionAmount);
    add(&minimapBandwidth,       CCS_MinimapBandwidth);
    add.space();
    add(&messageLogging,         CCS_MessageLogging);
    add(&showFlagMessages,       CCS_ShowFlagMessages);
    add(&showKillMessages,       CCS_ShowKillMessages);
    add.space();
    add(&saveStats,              CCS_SaveStats);
    add(&showStats,              CCS_ShowStats);
    add(&showServerInfo,         CCS_ShowServerInfo);
    add(&stayDead,               CCS_StayDeadInMenus);
    add(&underlineMasterAuth,    CCS_UnderlineMasterAuth);
    add(&underlineServerAuth,    CCS_UnderlineServerAuth);
    add.space();
    add(&autoGetServerList,      CCS_AutoGetServerList);
}

Menu_controls::Menu_controls() throw () :
    keyboardLayout      (_("Keyboard layout")),
    keypadMoving        (_("Use keypad for moving"), true),
    arrowKeysInStats    (_("Arrow keys in statistics")),
    arrowKeysInTextInput(_("Use arrow keys to edit message input"), false),

    aimMode             (_("Preferrably aim with")),
    moveRelativity      (_("With mouse aim move relative to")),
    turningSpeed        (_("Keyboard turning speed"), true, 0, 100, 50, 5),

    joystick            (_("Enable joystick control"), false),
    joyMove             (_("Moving stick"), false, 0, 5, 1),
    joyText             (_("Joystick buttons (0 = disabled)")),
    joyShoot            (_("Shoot "), false, 0, 16, 1),
    joyRun              (_("Run   "), false, 0, 16, 2),
    joyStrafe           (_("Strafe"), false, 0, 16, 3),

    mouseText           (_("Mouse control (buttons: 0 = disabled)")),
    mouseSensitivity    (_("Sensitivity"), true, 1, 101, 50, 0),
    mouseShoot          (_("Shoot "), false, 0, 16, 1),
    mouseRun            (_("Run   "), false, 0, 16, 2),

    activeControls      (_("Active controls")),
    activeJoystick      (_("Active joystick buttons")),
    activeMouse         (_("Active mouse buttons")),

    menu                (_("Controls"), true)
{
    arrowKeysInStats.addOption(_("change stats view"), AS_useMenu);
    arrowKeysInStats.addOption(_("move player"), AS_movePlayer);
    aimMode.addOption(_("keyboard (8-directional)"), AM_8way);
    aimMode.addOption(_("keyboard (smooth turning)"), AM_Turn);
    aimMode.addOption(_("mouse"), AM_Mouse);
    moveRelativity.addOption(_("room (up = up)"), AM_World);
    moveRelativity.addOption(_("aim (up = forward)"), AM_Gun);

    // add keyboard layouts in alphabetical order (depends on translation)
    vector< pair<string, string> > layouts;
    layouts.reserve(20);
    layouts.push_back(pair<string, string>(_("Belgium"),          "be"));
    layouts.push_back(pair<string, string>(_("Brazil"),           "br"));
    layouts.push_back(pair<string, string>(_("Canada (French)"),  "cf"));
    layouts.push_back(pair<string, string>(_("Czech Republic"),   "cz"));
    layouts.push_back(pair<string, string>(_("Denmark"),          "de"));
    layouts.push_back(pair<string, string>(_("Dvorak"),       "dvorak"));
    layouts.push_back(pair<string, string>(_("Finland"),          "fi"));
    layouts.push_back(pair<string, string>(_("France"),           "fr"));
    layouts.push_back(pair<string, string>(_("Germany"),          "de"));
    layouts.push_back(pair<string, string>(_("Italy"),            "it"));
    layouts.push_back(pair<string, string>(_("Norway"),           "no"));
    layouts.push_back(pair<string, string>(_("Poland"),           "pl"));
    layouts.push_back(pair<string, string>(_("Portugal"),         "pt"));
    layouts.push_back(pair<string, string>(_("Russia"),           "ru"));
    layouts.push_back(pair<string, string>(_("Slovakia"),         "sk"));
    layouts.push_back(pair<string, string>(_("Spain"),            "es"));
    layouts.push_back(pair<string, string>(_("Sweden"),           "se"));
    layouts.push_back(pair<string, string>(_("Switzerland"),      "ch"));
    layouts.push_back(pair<string, string>(_("United Kingdom"),   "uk"));
    layouts.push_back(pair<string, string>(_("United States"),    "us"));
    std::sort(layouts.begin(), layouts.end());
    for (vector< pair<string, string> >::const_iterator li = layouts.begin(); li != layouts.end(); ++li)
        keyboardLayout.addOption(li->first, li->second);
    keyboardLayout.set("us");
}

void Menu_controls::initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw () {
    menu.setHook(opener);
    DualComponentAdder add(menu, collector);
    add(&keyboardLayout);
    add(&keypadMoving,         CCS_KeypadMoving);
    add(&arrowKeysInStats,     CCS_ArrowKeysInStats);
    add(&arrowKeysInTextInput, CCS_ArrowKeysInTextInput);
    add.space();
    add(&aimMode,              CCS_AimMode);
    add(&moveRelativity,       CCS_MoveRelativity);
    add(&turningSpeed,         CCS_TurningSpeed);
    add.space();
    add(&joystick,             CCS_Joystick);
    add(&joyMove,              CCS_JoystickMove);
    add(&joyText);
    add(&joyShoot,             CCS_JoystickShoot);
    add(&joyRun,               CCS_JoystickRun);
    add(&joyStrafe,            CCS_JoystickStrafe);
    add.space();
    add(&mouseText);
    add(&mouseSensitivity,     CCS_MouseSensitivity);
    add(&mouseShoot,           CCS_MouseShoot);
    add(&mouseRun,             CCS_MouseRun);
    add.space();
    add(&activeControls);
    add(&activeJoystick);
    add(&activeMouse);
}

void Menu_screenMode::reloadChoices(const Graphics& gfx) throw () {
    const vector<ScreenMode> modes = gfx.getResolutions(colorDepth());
    nAssert(!modes.empty());
    resolution.clearOptions();
    for (vector<ScreenMode>::const_iterator mode = modes.begin(); mode != modes.end(); ++mode)
        resolution.addOption(itoa(mode->width) + '×' + itoa(mode->height), *mode);
}

Menu_screenMode::Menu_screenMode() throw () :
    oldMode(-1, -1),    // guarantees anything to be newMode()

    colorDepth  (_("Color depth")),
    desktopDepth(_(" desktop"), _("$1-bit", itoa(desktop_color_depth()))),
    resolution  (_("Screen size")),
    windowed    (_("Windowed mode"), true),
    flipping    (_("Use page flipping"), false),
    alternativeFlipping(_("Alternative page flipping method"), false),
    refreshRate (_("Current refresh rate")),
    apply       (_("Apply changes")),

    menu        (_("Screen mode"), true)
{ }

void Menu_screenMode::initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw () {
    menu.setHook(opener);
    DualComponentAdder add(menu, collector);
    add(&colorDepth);
    add(&desktopDepth);
    add(&resolution);
    add(&windowed,            CCS_Windowed);
    add(&flipping,            CCS_Flipping);
    add(&alternativeFlipping, CCS_AlternativeFlipping);
    add(&refreshRate);
    add(&apply);
}

void Menu_screenMode::init(const Graphics& gfx) throw () { // call just once, before calling update
    nAssert(colorDepth.size() == 0);
    if (gfx.depthAvailable(16))
        colorDepth.addOption(_("$1-bit", "16"), 16);
    if (gfx.depthAvailable(24))
        colorDepth.addOption(_("$1-bit", "24"), 24);
    if (gfx.depthAvailable(32))
        colorDepth.addOption(_("$1-bit", "32"), 32);
    if (colorDepth.size() == 0)
        colorDepth.addOption(_("$1-bit", "16"), 16);  // this will force Graphics to use a hope-this-works resolution
    colorDepth.set(desktop_color_depth());  // may fail (although it's unlikely); ignore
    reloadChoices(gfx);
    resolution.set(ScreenMode(640, 480));   // default resolution
}

void Menu_screenMode::update(const Graphics& gfx) throw () {   // tries to keep the selected resolution
    ScreenMode oldmode = resolution();
    reloadChoices(gfx);
    resolution.set(oldmode); // may fail; ignore
}

bool Menu_screenMode::newMode() throw () {
    if (oldMode == resolution() && oldDepth == colorDepth() && oldWin == windowed() && oldFlip == flipping())
        return false;
    oldMode = resolution();
    oldDepth = colorDepth();
    oldWin = windowed();
    oldFlip = flipping();
    return true;
}


void Menu_theme::reloadChoices(const Graphics& gfx) throw () {
    theme.clearOptions();
    background.clearOptions();
    colours.clearOptions();
    font.clearOptions();
    StringSelectInserter insTheme(theme), insBg(background), insCol(colours), insFont(font);
    gfx.search_themes(insTheme, insBg, insCol);
    gfx.search_fonts(insFont);
}

Menu_theme::Menu_theme() throw () :
    theme                (_("Theme")),
    background           (_("Background theme")),
    useThemeBackground   (_("Prefer main theme background"), true),
    colours              (_("Colour theme")),
    useThemeColours      (_("Prefer main theme colours"), true),
    font                 (_("Font")),

    menu                 (_("Theme and font"), true)
{ }

void Menu_theme::initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw () {
    menu.setHook(opener);
    DualComponentAdder add(menu, collector);
    add(&theme,                  CCS_GFXTheme);
    add(&background,             CCS_Background);
    add(&useThemeBackground,     CCS_UseThemeBackground);
    add.space();
    add(&colours,                CCS_Colours);
    add(&useThemeColours,        CCS_UseThemeColours);
    add.space();
    add(&font,                   CCS_Font);
}

void Menu_theme::init(const Graphics& gfx) throw () { // call just once, before calling update
    reloadChoices(gfx);
}

void Menu_theme::update(const Graphics& gfx) throw () { // tries to keep the selected choices
    const string oldtheme = theme();
    const string oldbg = background();
    const string oldcol = colours();
    const string oldfont = font();
    reloadChoices(gfx);
    // These all may fail; ignore.
    theme.set(oldtheme);
    background.set(oldbg);
    colours.set(oldcol);
    font.set(oldfont);
}

Menu_graphics::Menu_graphics() throw () :
    showNames            (_("Show player names")),
    visibleRoomsPlay     (_("Rooms on screen in each direction in game"), false, 1, 20, 2),
    visibleRoomsReplay   (_("Rooms on screen in each direction in replay"), false, 1, 20, 20),
    scroll               (_("Scrolling"), true),

    antialiasing         (_("Antialiasing"), true),
    minTransp            (_("Less transparency effects"), false),
    contTextures         (_("Continuous textures between rooms"), true),
    minimapPlayers       (_("Disappeared players on minimap")),
    highlightReturnedFlag(_("Highlight returned and dropped flags"), true),
    emphasizeFlags       (_("Make flags extra-visible")),
    oldFlagPositions     (_("Show flag disappearance positions"), false),
    spawnHighlight       (_("Highlight self after spawn"), true),
    neighborMarkersPlay  (_("Markers for nearby players and flags in game")),
    neighborMarkersReplay(_("Markers for nearby players and flags in replay")),
    boxRoomsWhenPlaying  (_("Box visible area on map in game"), true),
    viewOverMapBorder    (_("Let view follow over map border")),
    repeatMap            (_("Allow parts of map to repeat on screen"), false),
    statsBgAlpha         (_("Stats screen alpha"), true, 0, 255, 255, 15),

    fpsLimit             (_("FPS limit"), false, 1, 10000, 60, 0),
    mapInfoMode          (_("Map info mode"), false),

    menu                 (_("Graphic options"), true)
{
    showNames.addOption(_("never"), N_Never);
    showNames.addOption(_("in same room"), N_SameRoom);
    showNames.addOption(_("always"), N_Always);
    minimapPlayers.addOption(_("fade out"  ), MP_Fade);
    minimapPlayers.addOption(_("hide early"), MP_EarlyCut);
    minimapPlayers.addOption(_("hide late" ), MP_LateCut);
    emphasizeFlags.addOption(_("never"       ), FE_Never);
    emphasizeFlags.addOption(_("multi-roomed"), FE_MultiRoom);
    emphasizeFlags.addOption(_("always"      ), FE_Always);
    neighborMarkersPlay.addOption(_("never"        ), NM_Never);
    neighborMarkersPlay.addOption(_("single-roomed"), NM_OneRoom);
    neighborMarkersPlay.addOption(_("always"       ), NM_Always);
    neighborMarkersReplay.addOption(_("never"        ), NM_Never);
    neighborMarkersReplay.addOption(_("single-roomed"), NM_OneRoom);
    neighborMarkersReplay.addOption(_("always"       ), NM_Always);
    viewOverMapBorder.addOption(_("never"                      ), VOB_Never);
    viewOverMapBorder.addOption(_("when all rooms aren't shown"), VOB_MapDoesntFit);
    viewOverMapBorder.addOption(_("if the border has doorways" ), VOB_MapWraps);
    viewOverMapBorder.addOption(_("always"                     ), VOB_Always);
    viewOverMapBorder.set(VOB_MapWraps);
}

void Menu_graphics::initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw () {
    menu.setHook(opener);
    DualComponentAdder add(menu, collector);
    add(&showNames,              CCS_ShowNames);
    add(&visibleRoomsPlay,       CCS_VisibleRoomsPlay);
    add(&visibleRoomsReplay,     CCS_VisibleRoomsReplay);
    add(&scroll,                 CCS_Scroll);
    add.space();
    add(&antialiasing);
    add(&minTransp,              CCS_MinTransp);
    add(&contTextures,           CCS_ContinuousTextures);
    add(&minimapPlayers,         CCS_MinimapPlayers);
    add(&highlightReturnedFlag,  CCS_HighlightReturnedFlag);
    add(&emphasizeFlags,         CCS_EmphasizeFlags);
    add(&oldFlagPositions,       CCS_OldFlagPositions);
    add(&spawnHighlight,         CCS_SpawnHighlight);
    add(&neighborMarkersPlay,    CCS_NeighborMarkersPlay);
    add(&neighborMarkersReplay,  CCS_NeighborMarkersReplay);
    add(&boxRoomsWhenPlaying,    CCS_BoxRoomsWhenPlaying);
    add(&viewOverMapBorder,      CCS_ViewOverMapBorder);
    add(&repeatMap,              CCS_RepeatMap);
    add(&statsBgAlpha,           CCS_StatsBgAlpha);
    add.space();
    add(&fpsLimit,               CCS_FPSLimit);
    add(&mapInfoMode);
}

bool Menu_graphics::showNeighborMarkers(bool replaying, double visible_rooms) const throw () {
    switch (replaying ? neighborMarkersReplay() : neighborMarkersPlay()) {
    /*break;*/ case NM_Never:   return false;
        break; case NM_OneRoom: return visible_rooms < 2.;
        break; case NM_Always:  return true;
        break; default: nAssert(0);
    }
}

Menu_sounds::Menu_sounds() throw () :
    enabled (_("Sounds enabled"), true),
    volume  (_("Volume"), true, 0, 255, 128, 15),
    theme   (_("Theme")),

    menu    (_("Sound options"), true)
{ }

void Menu_sounds::initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw () {
    menu.setHook(opener);
    DualComponentAdder add(menu, collector);
    add(&enabled, CCS_SoundEnabled);
    add(&volume,  CCS_Volume);
    add(&theme,   CCS_SoundTheme);
}

void Menu_sounds::init(const Sounds& snd) throw () {
    theme.clearOptions();
    StringSelectInserter ins(theme);
    snd.search_themes(ins);
}

void Menu_sounds::update(const Sounds& snd) throw () {   // tries to keep the selected theme
    const string oldTheme = theme();
    init(snd);
    theme.set(oldTheme);
}

Menu_language::Menu_language() throw () :
    menu(_("Choose language"), false)
{
    reset();
}

void Menu_language::initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector&) throw () {
    menu.setHook(opener);
}

void Menu_language::add(const string& code, const string& name) throw () {
    languages.push_back(pair<string, Textarea>(code, Textarea(name)));
}

void Menu_language::reset() throw () {
    menu.clear_components();
    languages.clear();
}

void Menu_language::addHooks(MenuHookable<Textarea>::HookFunctionT* hook) throw () {
    BasicComponentAdder add(menu);
    for (vector<pair<string, Textarea> >::iterator li = languages.begin(); li != languages.end(); ++li) {
        li->second.setHook(hook->clone());
        add(&li->second);
    }
    delete hook;
}

const string& Menu_language::getCode(const Textarea& target) throw () {
    for (vector<pair<string, Textarea> >::const_iterator li = languages.begin(); li != languages.end(); ++li) {
        if (&li->second == &target)
            return li->first;
    }
    nAssert(0);
    return languages.front().first;
}

Menu_bugReportPolicy::Menu_bugReportPolicy() throw () :
    text    (),
    policy  (_("Automatic bug reporting")),

    menu    (_("Bug report policy"), false)
{
    init();
    policy.addOption(_("disabled"), ABR_disabled);
    policy.addOption(_("minimal" ), ABR_minimal);
    policy.addOption(_("complete"), ABR_withDump);
    policy.set(g_autoBugReporting);
}

void Menu_bugReportPolicy::initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw () {
    menu.setHook(opener);
    (void)collector;
}

void Menu_bugReportPolicy::addLine(const string& line) throw () {
    lines.push_back(line);

    const int oldSel = menu.selection();
    menu.clear_components();
    text = Textobject();
    for (vector<string>::iterator li = lines.begin(); li != lines.end(); ++li)
        text.addLine(*li);
    text.setEnable(false);
    init();
    menu.setSelection(oldSel);
}

// If you change this method somehow, check Menu::handleKeypress hack.
void Menu_bugReportPolicy::init() throw () {
    BasicComponentAdder add(menu);
    add(&text);
    add.space();
    add(&policy);
}

Menu_options::Menu_options() throw () :
    player    (),
    game      (),
    controls  (),
    screenMode(),
    theme     (),
    graphics  (),
    sounds    (),
    language  (),
    bugReports(),

    menu      (_("Options"), true)
{ }

void Menu_options::initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw () {
    menu.setHook(opener);
    player    .initialize(opener->clone(), collector);
    game      .initialize(opener->clone(), collector);
    controls  .initialize(opener->clone(), collector);
    screenMode.initialize(opener->clone(), collector);
    theme     .initialize(opener->clone(), collector);
    graphics  .initialize(opener->clone(), collector);
    sounds    .initialize(opener->clone(), collector);
    language  .initialize(opener->clone(), collector);
    bugReports.initialize(opener->clone(), collector);
    DualComponentAdder add(menu, collector);
    add(&player.menu);
    add(&game.menu);
    add(&controls.menu);
    add.space();
    add(&screenMode.menu);
    add(&theme.menu);
    add(&graphics.menu);
    add.space();
    add(&sounds.menu);
    add.space();
    add(&language.menu);
    add(&bugReports.menu);
}

Menu_ownServer::Menu_ownServer() throw () :
    pub     (_("Add to public serverlist"), false),
    port    (_("Server port"), 1, 65535, DEFAULT_UDP_PORT),
    address (_("IP address"), false, true),
    autoIP  (_("Autodetect IP"), true),

    start   (_("Start server")),
    play    (_("Play on the server")),
    stop    (_("Stop server")),

    menu    (_("Local server"), true)
{ }

void Menu_ownServer::initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw () {
    menu.setHook(opener);
    DualComponentAdder add(menu, collector);
    add(&pub,     CCS_ServerPublic);
    add(&port,    CCS_ServerPort);
    add(&address, CCS_ServerAddress);
    add(&autoIP,  CCS_AutodetectAddress);
    add.space();
    add(&start);
    add(&play);
    add(&stop);
}

void Menu_ownServer::init(const string& detectedAddress) throw () {
    detectedIP = detectedAddress;
}

void Menu_ownServer::refreshCaption(bool serverRunning) throw () {
    if (serverRunning)
        menu.setCaption(_("Local server (running)"));
    else
        menu.setCaption(_("Local server"));
    // this could be a separate function but it really doesn't hurt to update the address field here
    if (autoIP())
        address.set(detectedIP);
    if (port() != DEFAULT_UDP_PORT)
        address.setFixedPortString(":" + itoa(port()));
    else
        address.setFixedPortString("");
}

void Menu_ownServer::refreshEnables(bool serverRunning, bool connected) throw () {
    if (serverRunning) {
        pub.setEnable(false);
        port.setEnable(false);
        address.setEnable(false);
        autoIP.setEnable(false);
        start.setEnable(false);
        play.setEnable(!connected);
        stop.setEnable(true);
    }
    else {
        pub.setEnable(true);
        port.setEnable(true);
        address.setEnable(!autoIP());
        autoIP.setEnable(true);
        start.setEnable(true);
        play.setEnable(false);
        stop.setEnable(false);
    }
}

Menu_replays::Menu_replays() throw () :
    caption (_("Date - Server - Map")),

    menu    (_("Replays"), false)
{
    reset();
}

void Menu_replays::initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw () {
    menu.setHook(opener);
    (void)collector;
}

void Menu_replays::add(const string& replay, const string& text) throw () {
    items.push_back(pair<string, Textarea>(replay, Textarea(text)));
}

void Menu_replays::reset() throw () {
    menu.clear_components();
    items.clear();
    BasicComponentAdder add(menu);
    add(&caption);
}

void Menu_replays::addHooks(MenuHookable<Textarea>::HookFunctionT* hook) throw () {
    BasicComponentAdder add(menu);
    for (vector<pair<string, Textarea> >::iterator item = items.begin(); item != items.end(); ++item) {
        item->second.setHook(hook->clone());
        add(&item->second);
    }
    delete hook;
}

const string& Menu_replays::getFile(const Textarea& target) throw () {
    for (vector<pair<string, Textarea> >::const_iterator item = items.begin(); item != items.end(); ++item) {
        if (&item->second == &target)
            return item->first;
    }
    nAssert(0);
    return items.front().first;
}

Menu_main::Menu_main() throw () :
    newVersion  (""),

    connect     (),
    disconnect  (_("Disconnect")),

    options     (),

    ownServer   (),

    replays     (),

    help        (),
    exitOutgun  (_("Exit Outgun")),

    menu        ("Outgun " + getVersionString(), true)
{ }

void Menu_main::initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw () {
    menu.setHook(opener);
    connect.initialize(opener->clone(), collector);
    options.initialize(opener->clone(), collector);
    ownServer.initialize(opener->clone(), collector);
    replays.initialize(opener->clone(), collector);
    help.initialize(opener->clone(), collector);
    DualComponentAdder add(menu, collector);
    add(&newVersion);
    add(&connect.menu);
    add(&disconnect);
    add.space();
    add(&options.menu);
    add.space();
    add(&ownServer.menu);
    add.space();
    add(&replays.menu);
    add.space();
    add(&help.menu);
    add(&exitOutgun);
}

Menu_text::Menu_text() throw () :
    accept  (_("OK")),
    cancel  (_("Cancel")),

    menu    ("Outgun " + getVersionString(), false)
{ }

void Menu_text::initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw () {
    menu.setHook(opener);
    (void)collector;
}

void Menu_text::addLine(const string& line, bool cancelable) throw () {
    addLine(line, "", cancelable);
}

void Menu_text::addLine(const string& caption, const string& value, bool cancelable, bool passive) throw () {
    lines.push_back(StaticText(caption, value));

    const int oldSel = menu.selection();
    menu.clear_components();
    BasicComponentAdder add(menu);
    for (vector<StaticText>::iterator li = lines.begin(); li != lines.end(); ++li)
        add(&*li);
    if (passive)
        return;
    add.space();
    if (cancelable)
        add(&cancel);
    else
        add(&accept);
    menu.setSelection(oldSel);
}

void Menu_text::wrapLine(const string& line, bool cancelable, int wrapPos) throw () {
    vector<string> lines = split_to_lines(line, wrapPos, 5);    // indent continuation lines by 5 characters
    for (vector<string>::const_iterator li = lines.begin(); li != lines.end(); ++li)
        addLine(*li, "", cancelable);
}

Menu_playerPassword::Menu_playerPassword() throw () :
    password    (_("Password"), "", 15, '*'),
    save        (_("Save password")),

    menu        (string(), false)   // caption is set by setup()
{
    BasicComponentAdder add(menu);
    add(&password);
    add(&save);
}

void Menu_playerPassword::setup(const string& plyName, bool saveChecked) throw () {
    menu.setCaption(_("Player password for $1", plyName));
    password.set("");
    save.set(saveChecked);
}

Menu_serverPassword::Menu_serverPassword() throw () :
    password    (_("Password"), "", 15, '*'),

    menu        (_("Server password"), false)
{
    BasicComponentAdder add(menu);
    add(&password);
}

Menu_help::Menu_help() throw () :
    text    (),

    menu    (_("Help"), false)
{ }

void Menu_help::initialize(MenuHookable<Menu>::HookFunctionT* opener, SettingCollector& collector) throw () {
    menu.setHook(opener);
    (void)collector;
}

void Menu_help::addLine(const string& line) throw () {
    lines.push_back(line);

    const int oldSel = menu.selection();
    menu.clear_components();
    text = Textobject();
    for (vector<string>::const_iterator li = lines.begin(); li != lines.end(); ++li)
        text.addLine(*li);
    BasicComponentAdder add(menu);
    add(&text);
    menu.setSelection(oldSel);
}
