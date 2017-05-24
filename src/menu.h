/*
 *  menu.h
 *
 *  Copyright (C) 2004, 2006 - Niko Ritari
 *  Copyright (C) 2004, 2006 - Jani Rivinoja
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

#ifndef MENU_H_INC
#define MENU_H_INC

#include <string>
#include <vector>
#include <stack>

#include "function_utility.h"
#include "nassert.h"

class Graphics;
class BITMAP;

void scrollbar(BITMAP* buffer, int x, int y, int height, int bar_y, int bar_h, int col1, int col2) throw ();
void drawKeySymbol(BITMAP* buffer, int x, int y, const std::string& text) throw ();

// Base class of menu components
class Component {
public:
    Component(const std::string& caption_) throw () : caption(caption_), enabled(true) { }
    virtual ~Component() throw () { };

    void setCaption(const std::string& text) throw () { caption = text; }

    void setEnable(bool state) throw () { enabled = state; }

    virtual bool canBeEnabled() const throw () { return true; }
    virtual bool isEnabled() const throw () { return enabled; }
    virtual bool needsNumberKeys() const throw () { return false; }

    virtual void draw(BITMAP* buffer, int x, int y, int height, bool active, const Colour_manager& col) const throw () = 0;
    virtual int width() const throw () = 0;
    virtual int height() const throw () = 0;
    virtual int minHeight() const throw () { return height(); }
    virtual bool handleKey(char, unsigned char) throw () { return false; }   // an object should either have an active handleKey() or override so that !isEnabled()
    virtual void shortcutActivated() throw () { }

protected:
    int captionColor(bool active, const Colour_manager& col) const throw ();    // decides a color based on isEnabled() and active

    std::string caption;
    bool enabled;
};

template<class CallerT>
class MenuHookable : public Hookable1<void, CallerT&> { };

template<class CallerT>
class MenuHook : public Hook1<void, CallerT&> { };

template<class CallerT>
class KeyHookable {
    // can't privately inherit KeyHookable from Hookable3 because the compiler still makes ambiguities of the functions when inheriting from KeyHookable!
    class Helper : public Hookable3<bool, CallerT&, char, unsigned char> {
    public:
        bool callHook_helper(CallerT& a1, char a2, unsigned char a3) throw () { return callHook(a1, a2, a3); }
    };
    Helper helper;

public:
    typedef typename Hookable3<bool, CallerT&, char, unsigned char>::HookFunctionT HookFunctionT;

    void setKeyHook(HookFunctionT* fn) throw () { helper.setHook(fn); }  // the ownership is transferred
    bool isKeyHooked() const throw () { return helper.isHooked(); }

protected:
    bool callKeyHook(CallerT& a1, char a2, unsigned char a3) throw () { return helper.callHook_helper(a1, a2, a3); }
};

template<class CallerT>
class KeyHook : public Hook3<bool, CallerT&, char, unsigned char> { };

// Menu being a Component is a hack: the Component part is a link to the menu
class Menu : public Component, public MenuHookable<Menu> {
public:
    // visible_items to 20 is there to prevent scrollbar when starting the game and pressing down arrow same time
    Menu(const std::string& caption_, bool useShortcuts) throw () : Component(caption_), start(0), selected_item(0), visible_items(20), shortcuts(useShortcuts) { }
    ~Menu() throw () { }

    void clear_components() throw () { selected_item = 0; components.clear(); }
    void add_component(Component* comp) throw () { components.push_back(comp); }

    int selection() const throw () { return selected_item; }
    void setSelection(int selection) throw ();

    void open() throw () { openHook.call(*this); ensure_valid_selection(); }
    void close() throw () { closeHook.call(*this); }

    void draw(BITMAP* buffer, const Colour_manager& col) throw ();  // no const because drawHook might modify the menu
    bool handleKeypress(char scan, unsigned char chr) throw ();

    void  setDrawHook(MenuHook<Menu>::FunctionT* fn) throw () {  drawHook.set(fn); } // called before drawing
    void  setOpenHook(MenuHook<Menu>::FunctionT* fn) throw () {  openHook.set(fn); } // called by open()
    void setCloseHook(MenuHook<Menu>::FunctionT* fn) throw () { closeHook.set(fn); } // called by close()
    void    setOkHook(MenuHook<Menu>::FunctionT* fn) throw () {    okHook.set(fn); } // called when enter is pressed (and not handled by active entry)

    // inherited interface
    int width() const throw ();
    int height() const throw ();
    void draw(BITMAP* buffer, int x, int y, int height, bool active, const Colour_manager& col) const throw ();
    bool handleKey(char, unsigned char) throw ();
    void shortcutActivated() throw ();

private:
    int total_width() const throw ();
    int total_height() const throw ();

    void ensure_valid_selection() throw (); /// If preselection is valid, stay there, else go home.

    void home() throw ();    // moves the cursor to topmost selectable item
    void end() throw ();     // moves the cursor to the last selectable item
    bool prev() throw ();
    bool next() throw ();

    std::vector<Component*> components;
    int start;
    int selected_item;
    int visible_items;
    bool shortcuts; // use keys 1 to 0 as shortcuts in this menu

    MenuHook<Menu> drawHook, openHook, closeHook, okHook;
};

class MenuStack {
public:
    bool empty() const throw () { return st.empty(); }
    Menu* top() const throw () { nAssert(!st.empty()); return st.back(); }
    Menu* safeTop() const throw () { if (st.empty()) return 0; return st.back(); }
    void open(Menu* menu) throw () { close(menu); menu->open(); st.push_back(menu); }
    bool close(Menu* menu) throw (); // from anywhere in the stack; returns true if found (and closed)
    void close() throw () { nAssert(!empty()); Menu* menu = st.back(); st.pop_back(); menu->close(); }
    void clear() throw () { while (!empty()) close(); }
    void draw(BITMAP* buf, const Colour_manager& col) throw () { nAssert(!empty()); st.back()->draw(buf, col); }
    bool handleKeypress(char scan, unsigned char chr) throw () { nAssert(!empty()); return st.back()->handleKeypress(scan, chr); }

private:
    std::vector<Menu*> st;  // a "real" std::stack can't be used because it doesn't allow removal from the middle by close(Menu*)
};

class Spacer : public Component {
public:
    Spacer(int space_) throw () : Component(""), space(space_) { }   // space in tenths of line height

    // inherited interface
    bool canBeEnabled() const throw () { return false; }
    bool isEnabled() const throw () { return false; }
    void draw(BITMAP*, int, int, int, bool, const Colour_manager&) const throw () { }
    int width() const throw () { return 0; }
    int height() const throw ();

private:
    int space;
};

class TextfieldBase : public Component {
public:
    TextfieldBase(const std::string& caption_, const std::string& init_text, int fieldLength, char mask = 0, int reserveTailLength = 0) throw ()
        : Component(caption_), value(init_text), maxlen(fieldLength), tailSpace(reserveTailLength), maskChar(mask), cursor_pos(0) { unblink(); }
    virtual ~TextfieldBase() throw () { }
    virtual void set(const std::string& text) throw () { value = text; cursor_pos = text.length(); unblink(); }
    virtual const std::string& operator()() const throw () { return value; }

    void setTail(const std::string& text) throw () { tail = text; }
    void limitToCharacters(const std::string& chars) throw () { charset = chars; } // set to empty to accept all printable characters

    // inherited interface
    virtual bool needsNumberKeys() const throw () { return true; }
    virtual int width() const throw ();
    virtual int height() const throw ();
    virtual void draw(BITMAP* buffer, int x, int y, int height, bool active, const Colour_manager& col) const throw ();
    virtual bool handleKey(char scan, unsigned char chr) throw ();

private:
    std::string value, tail;
    std::string charset; // characters that are allowed to be input
    int maxlen, tailSpace;
    char maskChar;  // 0 for no masking
    int cursor_pos;
    double blinkTime;

    void unblink() throw ();
    virtual void virtualCallHook() throw () = 0;
    virtual bool virtualCallKeyHook(char scan, unsigned char chr) throw () = 0;
};

// a keyhook is only called with keys not handled otherwise (= non-printables other than backspace, plus those outside limited characters [if set])
class Textfield : public TextfieldBase, public MenuHookable<Textfield>, public KeyHookable<Textfield> {
public:
    Textfield(const std::string& caption_, const std::string& init_text, int fieldLength, char mask = 0, int reserveTailLength = 0) throw ()
        : TextfieldBase(caption_, init_text, fieldLength, mask, reserveTailLength) { }

    // the public interface is entirely defined in TextfieldBase

private:
    void virtualCallHook() throw () { callHook(*this); }
    bool virtualCallKeyHook(char scan, unsigned char chr) throw () { return callKeyHook(*this, scan, chr); }
};

// a keyhook is only called with keys not handled otherwise (= non-printables other than backspace, plus those outside limited characters)
class IPfield : public TextfieldBase, public MenuHookable<IPfield>, public KeyHookable<IPfield> {
public:
    IPfield(const std::string& caption_, bool acceptPort_, bool printUnknown_) throw ();
    ~IPfield() throw () { }
    void set(const std::string& text) throw () { TextfieldBase::set(text); updateTail(); }
    void setFixedPortString(const std::string& text) throw () { portStr = text; updateTail(); } // this is intended for :port, space for 6 characters is allocated (only if !acceptPort)
    const std::string& operator()() const throw () { return TextfieldBase::operator()(); }

    // inherited interface (what's overridden from TextfieldBase)
    bool handleKey(char scan, unsigned char chr) throw ();

private:
    std::string portStr;
    bool acceptPort;
    bool printUnknown;

    void updateTail() throw ();

    void virtualCallHook() throw () { callHook(*this); }
    bool virtualCallKeyHook(char scan, unsigned char chr) throw () { return callKeyHook(*this, scan, chr); }
};

class SelectBase : public Component {
public:
    virtual ~SelectBase() throw () { }
    int size() const throw () { return options.size(); }

    // inherited interface
    bool needsNumberKeys() const throw () { return open; }
    int width() const throw ();
    int height() const throw ();
    void draw(BITMAP* buffer, int x, int y, int height, bool active, const Colour_manager& col) const throw ();
    bool handleKey(char scan, unsigned char chr) throw ();

    virtual void virtualCallHook() throw () = 0;

protected:
    SelectBase(const std::string caption_) throw () : Component(caption_), selected(0), open(false), pendingSelection(0) { }

    int maxSelLength() const throw ();

    std::vector<std::string> options;
    int selected;
    mutable bool open; //#fix: remove mutable by introducing a proper lostFocus-type method
    int pendingSelection;   // in list view; only when open
};

template<class ValueT>
class Select : public SelectBase, public MenuHookable< Select<ValueT> > {
public:
    Select(const std::string caption_) throw () : SelectBase(caption_) { }
    ~Select() throw () { }
    void clearOptions() throw () { options.clear(); values.clear(); selected = 0; open = false; }
    void addOption(const std::string& text, const ValueT& value) throw () { options.push_back(text); values.push_back(value); }
    bool set(const ValueT& value) throw ();  // returns false if there is no value in the options
//  void set(int selection) throw () { nAssert(selection >= 0 && selection < static_cast<int>(options.size())); selected = selection; }
    const ValueT& operator()() const throw () { return values[selected]; }

private:
    void virtualCallHook() throw () { callHook(*this); }
    std::vector<ValueT> values; // should always be in sync with options
};

class Colorselect : public Component, public MenuHookable<Colorselect> {
public:
    Colorselect(const std::string caption_) throw () : Component(caption_), selected(0), graphics(0) { }
    ~Colorselect() throw () { }
    void setGraphicsCallBack(const Graphics& graph) throw () { graphics = &graph; }
    void clearOptions() throw () { options.clear(); selected = 0; }
    void addOption(int col) throw () { options.push_back(col); }
    void set(int selection) throw () { nAssert(selection >= 0 && selection < static_cast<int>(options.size())); selected = selection; }
    int operator()() const throw () { return selected; }
    int asInt() const throw () { return options[selected]; }
    const std::vector<int>& values() const throw () { return options; }

    // inherited interface
    int width() const throw ();
    int height() const throw ();
    void draw(BITMAP* buffer, int x, int y, int height, bool active, const Colour_manager& col) const throw ();
    bool handleKey(char scan, unsigned char chr) throw ();

private:
    std::vector<int> options;
    int selected;
    const Graphics* graphics;
};

class Checkbox : public Component, public MenuHookable<Checkbox> {
public:
    Checkbox(const std::string& caption_, bool init_value = false) throw () : Component(caption_), checked(init_value) { }
    void toggle() throw () { checked = !checked; }
    void set(bool value) throw () { checked = value; }
    bool operator()() const throw () { return checked; }

    // inherited interface
    int width() const throw ();
    int height() const throw ();
    void draw(BITMAP* buffer, int x, int y, int height, bool active, const Colour_manager& col) const throw ();
    bool handleKey(char scan, unsigned char chr) throw ();
    void shortcutActivated() throw ();

private:
    bool checked;
};

class Slider : public Component, public MenuHookable<Slider> {
public:
    Slider(const std::string caption_, bool graphic_, int vmin_, int vmax_) throw ();
    Slider(const std::string caption_, bool graphic_, int vmin_, int vmax_, int init_value, int step_ = 1, bool lockToStep_ = false) throw ();
    // a step of 0 results in a logarithmic behavior
    // lockToStep means that the value can never be between the steps (this can't be set with logarithmic stepping)

    void set(int value) throw () { nAssert(value >= vmin && value <= vmax); val = value; }
    void boundSet(int value) throw ();
    int operator()() const throw () { return val; }

    // inherited interface
    int width() const throw ();
    int height() const throw ();
    void draw(BITMAP* buffer, int x, int y, int height, bool active, const Colour_manager& col) const throw ();
    bool handleKey(char scan, unsigned char chr) throw ();

private:
    int vmin, vmax, val, step;
    bool graphic, lockToStep;
};

class NumberEntry : public Component, public MenuHookable<NumberEntry> {
public:
    NumberEntry(const std::string caption_, int vmin_, int vmax_) throw ()   // currently limited to positive numbers!
        : Component(caption_), vmin(vmin_), vmax(vmax_), val(vmin_), entry(vmin_) { nAssert(vmin_ >= 0 && vmax_ >= vmin_); }
    NumberEntry(const std::string caption_, int vmin_, int vmax_, int init_value) throw ()   // currently limited to positive numbers!
        : Component(caption_), vmin(vmin_), vmax(vmax_), val(init_value), entry(init_value) { nAssert(vmin_ >= 0 && vmax_ >= vmin_ && init_value >= vmin_ && init_value <= vmax_); }

    void set(int value) throw () { nAssert(value >= vmin && value <= vmax); val = entry = value; }
    void boundSet(int value) throw ();
    int operator()() const throw () { return val; }

    // inherited interface
    bool needsNumberKeys() const throw () { return true; }
    int width() const throw ();
    int height() const throw ();
    void draw(BITMAP* buffer, int x, int y, int height, bool active, const Colour_manager& col) const throw ();
    bool handleKey(char scan, unsigned char chr) throw ();

private:
    int vmin, vmax, val, entry; // entry is the current state of editing; it differs from val exactly when it's smaller than vmin
};

class Textarea : public Component, public MenuHookable<Textarea>, public KeyHookable<Textarea> {
public:
    Textarea(const std::string& caption_) throw () : Component(caption_) { }

    // inherited interface
    int width() const throw ();
    int height() const throw ();
    void draw(BITMAP* buffer, int x, int y, int height, bool active, const Colour_manager& col) const throw ();
    bool handleKey(char scan, unsigned char chr) throw ();
    void shortcutActivated() throw ();

    // override isEnabled() : can't be enabled if not hooked
    bool isEnabled() const throw () { return Component::isEnabled() && isHooked(); }
};

class StaticText : public Component {
public:
    StaticText(const std::string& caption_, const std::string& text_ = std::string()) throw () : Component(caption_), text(text_) { }
    ~StaticText() throw () { }
    void set(const std::string& val) throw () { text = val; }

    // inherited interface
    bool canBeEnabled() const throw () { return false; }
    bool isEnabled() const throw () { return false; }
    int width() const throw ();
    int height() const throw ();
    void draw(BITMAP* buffer, int x, int y, int height, bool active, const Colour_manager& col) const throw ();

private:
    std::string text;
};

class Textobject : public Component {
public:
    Textobject() throw () : Component(""), start(0), visible_lines(0), old_linew(-1) { }
    ~Textobject() throw () { }
    void addLine(const std::string& text) throw () { lines.push_back(text); }

    // inherited interface
    int width() const throw ();
    int height() const throw ();
    int minHeight() const throw () { return objLineHeight(); }    // one line
    int objLineHeight() const throw ();
    void draw(BITMAP* buffer, int x, int y, int height, bool active, const Colour_manager& col) const throw ();
    bool handleKey(char scan, unsigned char chr) throw ();

private:
    std::vector<std::string> lines;
    mutable int start;                          // these may change in drawing
    mutable int visible_lines;
    mutable std::vector<std::string> splitted;  // these may change in height()
    mutable int old_linew;
};

// this template does the necessary wrapping of member function references to be given to Components as callbacks
// MenuCallback<UserClass>::A<ComponentType, &UserClass::handlerFunction>(userClassInstance)
//      calls void userClassInstance.handlerFunction(ComponentType&)
// MenuCallback<UserClass>::N<ComponentType, &UserClass::handlerFunction>(userClassInstance)
//      calls void userClassInstance.handlerFunction()
//      used when the component reference is not needed
template<class CallClassT>
class MenuCallback {
public:
    template<class ArgT, void (CallClassT::*memFun)(ArgT&)>
    class A : public HookFunctionBase1<void, ArgT&> {
    public:
        A(CallClassT* host_) throw () : host(host_) { }
        void operator()(ArgT& obj) const throw () { (host->*memFun)(obj); }
        A* clone() const throw () { return new A(host); }

    private:
        CallClassT* host;
    };

    template<class ArgT, void (CallClassT::*memFun)()>
    class N : public HookFunctionBase1<void, ArgT&> {
    public:
        N(CallClassT* host_) throw () : host(host_) { }
        void operator()(ArgT&) const throw () { (host->*memFun)(); }
        N* clone() const throw () { return new N(host); }

    private:
        CallClassT* host;
    };
};

template<class CallClassT>
class MenuKeyCallback {
public:
    template<class ArgT, bool (CallClassT::*memFun)(ArgT&, char, unsigned char)>
    class A : public HookFunctionBase3<bool, ArgT&, char, unsigned char> {
    public:
        A(CallClassT* host_) throw () : host(host_) { }
        bool operator()(ArgT& obj, char scan, unsigned char chr) const throw () { return (host->*memFun)(obj, scan, chr); }
        A* clone() const throw () { return new A(host); }

    private:
        CallClassT* host;
    };

    template<class ArgT, bool (CallClassT::*memFun)(char, unsigned char)>
    class N : public HookFunctionBase3<bool, ArgT&, char, unsigned char> {
    public:
        N(CallClassT* host_) throw () : host(host_) { }
        bool operator()(ArgT&, char scan, unsigned char chr) const throw () { return (host->*memFun)(scan, chr); }
        N* clone() const throw () { return new N(host); }

    private:
        CallClassT* host;
    };
};

// template implementation

template<class ValueT>
bool Select<ValueT>::set(const ValueT& value) throw () {
    for (int i = 0; i < (int)values.size(); ++i)
        if (values[i] == value) {
            selected = i;
            return true;
        }
    return false;
}

#endif // MENU_H_INC
