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

void scrollbar(BITMAP* buffer, int x, int y, int height, int bar_y, int bar_h, int col1, int col2);
void drawKeySymbol(BITMAP* buffer, int x, int y, const std::string& text);

// Base class of menu components
class Component {
public:
    Component(const std::string& caption_): caption(caption_), enabled(true) { }
    virtual ~Component() { };

    void setCaption(const std::string& text) { caption = text; }

    void setEnable(bool state) { enabled = state; }

    virtual bool canBeEnabled() const { return true; }
    virtual bool isEnabled() const { return enabled; }
    virtual bool needsNumberKeys() const { return false; }

    virtual void draw(BITMAP* buffer, int x, int y, int height, bool active) const = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual int minHeight() const { return height(); }
    virtual bool handleKey(char, unsigned char) { return false; }   // an object should either have an active handleKey() or override so that !isEnabled()
    virtual void shortcutActivated() { }

protected:
    int captionColor(bool active) const;    // decides a color based on isEnabled() and active

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
        bool callHook_helper(CallerT& a1, char a2, unsigned char a3) { return callHook(a1, a2, a3); }
    };
    Helper helper;

public:
    typedef typename Hookable3<bool, CallerT&, char, unsigned char>::HookFunctionT HookFunctionT;

    void setKeyHook(HookFunctionT* fn) { helper.setHook(fn); }  // the ownership is transferred
    bool isKeyHooked() const { return helper.isHooked(); }

protected:
    bool callKeyHook(CallerT& a1, char a2, unsigned char a3) { return helper.callHook_helper(a1, a2, a3); }
};

template<class CallerT>
class KeyHook : public Hook3<bool, CallerT&, char, unsigned char> { };

// Menu being a Component is a hack: the Component part is a link to the menu
class Menu : public Component, public MenuHookable<Menu> {
public:
    // visible_items to 20 is there to prevent scrollbar when starting the game and pressing down arrow same time
    Menu(const std::string& caption_, bool useShortcuts): Component(caption_), start(0), selected_item(0), visible_items(20), shortcuts(useShortcuts) { }

    void clear_components() { selected_item = 0; components.clear(); }
    void add_component(Component* comp) { components.push_back(comp); }

    int selection() const { return selected_item; }
    void setSelection(int selection);

    void open() { openHook.call(*this); home(); }
    void close() { closeHook.call(*this); }

    void draw(BITMAP* buffer);  // no const because drawHook might modify the menu
    bool handleKeypress(char scan, unsigned char chr);

    void  setDrawHook(MenuHook<Menu>::FunctionT* fn) {  drawHook.set(fn); } // called before drawing
    void  setOpenHook(MenuHook<Menu>::FunctionT* fn) {  openHook.set(fn); } // called by open()
    void setCloseHook(MenuHook<Menu>::FunctionT* fn) { closeHook.set(fn); } // called by close()
    void    setOkHook(MenuHook<Menu>::FunctionT* fn) {    okHook.set(fn); } // called when enter is pressed (and not handled by active entry)

    // inherited interface
    int width() const;
    int height() const;
    void draw(BITMAP* buffer, int x, int y, int height, bool active) const;
    bool handleKey(char, unsigned char);
    void shortcutActivated();

private:
    int total_width() const;
    int total_height() const;

    void home();    // moves the cursor to topmost selectable item
    void end();     // moves the cursor to the last selectable item
    bool prev();
    bool next();

    std::vector<Component*> components;
    int start;
    int selected_item;
    int visible_items;
    bool shortcuts; // use keys 1 to 0 as shortcuts in this menu

    MenuHook<Menu> drawHook, openHook, closeHook, okHook;
};

class MenuStack {
public:
    bool empty() const { return st.empty(); }
    Menu* top() const { nAssert(!st.empty()); return st.back(); }
    Menu* safeTop() const { if (st.empty()) return 0; return st.back(); }
    void open(Menu* menu) { close(menu); menu->open(); st.push_back(menu); }
    bool close(Menu* menu); // from anywhere in the stack; returns true if found (and closed)
    void close() { nAssert(!empty()); Menu* menu = st.back(); st.pop_back(); menu->close(); }
    void clear() { while (!empty()) close(); }
    void draw(BITMAP* buf) { nAssert(!empty()); st.back()->draw(buf); }
    bool handleKeypress(char scan, unsigned char chr) { nAssert(!empty()); return st.back()->handleKeypress(scan, chr); }

private:
    std::vector<Menu*> st;  // a "real" std::stack can't be used because it doesn't allow removal from the middle by close(Menu*)
};

class Spacer : public Component {
public:
    Spacer(int space_) : Component(""), space(space_) { }   // space in tenths of line height

    // inherited interface
    bool canBeEnabled() const { return false; }
    bool isEnabled() const { return false; }
    void draw(BITMAP*, int, int, int, bool) const { }
    int width() const { return 0; }
    int height() const;

private:
    int space;
};

class TextfieldBase : public Component {
public:
    TextfieldBase(const std::string& caption_, const std::string& init_text, int fieldLength, char mask = 0, int reserveTailLength = 0): Component(caption_), value(init_text), maxlen(fieldLength), tailSpace(reserveTailLength), maskChar(mask) { }
    virtual ~TextfieldBase() { }
    void set(const std::string& text) { value = text; }
    const std::string& operator()() const { return value; }

    void setTail(const std::string& text) { tail = text; }
    void limitToCharacters(const std::string& chars) { charset = chars; } // set to empty to accept all printable characters

    // inherited interface
    bool needsNumberKeys() const { return true; }
    int width() const;
    int height() const;
    void draw(BITMAP* buffer, int x, int y, int height, bool active) const;
    bool handleKey(char scan, unsigned char chr);

private:
    std::string value, tail;
    std::string charset; // characters that are allowed to be input
    int maxlen, tailSpace;
    char maskChar;  // 0 for no masking

    virtual void virtualCallHook() = 0;
    virtual bool virtualCallKeyHook(char scan, unsigned char chr) = 0;
};

// a keyhook is only called with keys not handled otherwise (= non-printables other than backspace, plus those outside limited characters [if set])
class Textfield : public TextfieldBase, public MenuHookable<Textfield>, public KeyHookable<Textfield> {
public:
    Textfield(const std::string& caption_, const std::string& init_text, int fieldLength, char mask = 0, int reserveTailLength = 0): TextfieldBase(caption_, init_text, fieldLength, mask, reserveTailLength) { }

    // the public interface is entirely defined in TextfieldBase

private:
    void virtualCallHook() { callHook(*this); }
    bool virtualCallKeyHook(char scan, unsigned char chr) { return callKeyHook(*this, scan, chr); }
};

// a keyhook is only called with keys not handled otherwise (= non-printables other than backspace, plus those outside limited characters)
class IPfield : public TextfieldBase, public MenuHookable<IPfield>, public KeyHookable<IPfield> {
public:
    IPfield(const std::string& caption_, bool acceptPort_, bool printUnknown_);
    void set(const std::string& text) { TextfieldBase::set(text); updateTail(); }
    void setFixedPortString(const std::string& text) { portStr = text; updateTail(); } // this is intended for :port, space for 6 characters is allocated (only if !acceptPort)
    const std::string& operator()() const { return TextfieldBase::operator()(); }

    // inherited interface (what's overridden from TextfieldBase)
    bool handleKey(char scan, unsigned char chr);

private:
    std::string portStr;
    bool acceptPort;
    bool printUnknown;

    void updateTail();

    void virtualCallHook() { callHook(*this); }
    bool virtualCallKeyHook(char scan, unsigned char chr) { return callKeyHook(*this, scan, chr); }
};

class SelectBase : public Component {
public:
    virtual ~SelectBase() { }
    int size() const { return options.size(); }

    // inherited interface
    bool needsNumberKeys() const { return open; }
    int width() const;
    int height() const;
    void draw(BITMAP* buffer, int x, int y, int height, bool active) const;
    bool handleKey(char scan, unsigned char chr);

    virtual void virtualCallHook() = 0;

protected:
    SelectBase(const std::string caption_): Component(caption_), selected(0), open(false), pendingSelection(0) { }

    int maxSelLength() const;

    std::vector<std::string> options;
    int selected;
    mutable bool open; //#fix: remove mutable by introducing a proper lostFocus-type method
    int pendingSelection;   // in list view; only when open
};

template<class ValueT>
class Select : public SelectBase, public MenuHookable< Select<ValueT> > {
public:
    Select(const std::string caption_): SelectBase(caption_) { }
    void clearOptions() { options.clear(); values.clear(); selected = 0; open = false; }
    void addOption(const std::string& text, const ValueT& value) { options.push_back(text); values.push_back(value); }
    bool set(const ValueT& value);  // returns false if there is no value in the options
//  void set(int selection) { nAssert(selection >= 0 && selection < static_cast<int>(options.size())); selected = selection; }
    const ValueT& operator()() const { return values[selected]; }

private:
    void virtualCallHook() { callHook(*this); }
    std::vector<ValueT> values; // should always be in sync with options
};

class Colorselect : public Component, public MenuHookable<Colorselect> {
public:
    Colorselect(const std::string caption_): Component(caption_), selected(0), graphics(0) { }
    void setGraphicsCallBack(const Graphics& graph) { graphics = &graph; }
    void clearOptions() { options.clear(); selected = 0; }
    void addOption(int col) { options.push_back(col); }
    void set(int selection) { nAssert(selection >= 0 && selection < static_cast<int>(options.size())); selected = selection; }
    int operator()() const { return selected; }
    int asInt() const { return options[selected]; }
    const std::vector<int>& values() const { return options; }

    // inherited interface
    int width() const;
    int height() const;
    void draw(BITMAP* buffer, int x, int y, int height, bool active) const;
    bool handleKey(char scan, unsigned char chr);

private:
    std::vector<int> options;
    int selected;
    const Graphics* graphics;
};

class Checkbox : public Component, public MenuHookable<Checkbox> {
public:
    Checkbox(const std::string& caption_, bool init_value = false): Component(caption_), checked(init_value) { }
    void toggle() { checked = !checked; }
    void set(bool value) { checked = value; }
    bool operator()() const { return checked; }

    // inherited interface
    int width() const;
    int height() const;
    void draw(BITMAP* buffer, int x, int y, int height, bool active) const;
    bool handleKey(char scan, unsigned char chr);
    void shortcutActivated();

private:
    bool checked;
};

class Slider : public Component, public MenuHookable<Slider> {
public:
    Slider(const std::string caption_, bool graphic_, int vmin_, int vmax_)
        : Component(caption_), vmin(vmin_), vmax(vmax_), val(vmin_), step(1), graphic(graphic_) { }
    Slider(const std::string caption_, bool graphic_, int vmin_, int vmax_, int init_value, int step_ = 1)  // a step of 0 results in a logarithmic behavior
        : Component(caption_), vmin(vmin_), vmax(vmax_), val(init_value), step(step_), graphic(graphic_) { nAssert(init_value >= vmin_ && init_value <= vmax_); nAssert(step >= 0); }

    void set(int value) { nAssert(value >= vmin && value <= vmax); val = value; }
    void boundSet(int value);
    int operator()() const { return val; }

    // inherited interface
    int width() const;
    int height() const;
    void draw(BITMAP* buffer, int x, int y, int height, bool active) const;
    bool handleKey(char scan, unsigned char chr);

private:
    int vmin, vmax, val, step;
    bool graphic;
};

class NumberEntry : public Component, public MenuHookable<NumberEntry> {
public:
    NumberEntry(const std::string caption_, int vmin_, int vmax_)   // currently limited to positive numbers!
        : Component(caption_), vmin(vmin_), vmax(vmax_), val(vmin_), entry(vmin_) { nAssert(vmin_ >= 0 && vmax_ >= vmin_); }
    NumberEntry(const std::string caption_, int vmin_, int vmax_, int init_value)   // currently limited to positive numbers!
        : Component(caption_), vmin(vmin_), vmax(vmax_), val(init_value), entry(init_value) { nAssert(vmin_ >= 0 && vmax_ >= vmin_ && init_value >= vmin_ && init_value <= vmax_); }

    void set(int value) { nAssert(value >= vmin && value <= vmax); val = entry = value; }
    void boundSet(int value);
    int operator()() const { return val; }

    // inherited interface
    bool needsNumberKeys() const { return true; }
    int width() const;
    int height() const;
    void draw(BITMAP* buffer, int x, int y, int height, bool active) const;
    bool handleKey(char scan, unsigned char chr);

private:
    int vmin, vmax, val, entry; // entry is the current state of editing; it differs from val exactly when it's smaller than vmin
};

class Textarea : public Component, public MenuHookable<Textarea>, public KeyHookable<Textarea> {
public:
    Textarea(const std::string& caption_): Component(caption_) { }

    // inherited interface
    int width() const;
    int height() const;
    void draw(BITMAP* buffer, int x, int y, int height, bool active) const;
    bool handleKey(char scan, unsigned char chr);
    void shortcutActivated();

    // override isEnabled() : can't be enabled if not hooked
    bool isEnabled() const { return Component::isEnabled() && isHooked(); }
};

class StaticText : public Component {
public:
    StaticText(const std::string& caption_, const std::string& text_ = std::string()) : Component(caption_), text(text_) { }
    void set(const std::string& val) { text = val; }

    // inherited interface
    bool canBeEnabled() const { return false; }
    bool isEnabled() const { return false; }
    int width() const;
    int height() const;
    void draw(BITMAP* buffer, int x, int y, int height, bool active) const;

private:
    std::string text;
};

class Textobject : public Component {
public:
    Textobject(): Component(""), start(0), visible_lines(0), old_linew(-1) { }
    void addLine(const std::string& text) { lines.push_back(text); }

    // inherited interface
    int width() const;
    int height() const;
    int minHeight() const { return objLineHeight(); }    // one line
    int objLineHeight() const;
    void draw(BITMAP* buffer, int x, int y, int height, bool active) const;
    bool handleKey(char scan, unsigned char chr);

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
        A(CallClassT* host_) : host(host_) { }
        void operator()(ArgT& obj) { (host->*memFun)(obj); }
        A* clone() { return new A(host); }

    private:
        CallClassT* host;
    };

    template<class ArgT, void (CallClassT::*memFun)()>
    class N : public HookFunctionBase1<void, ArgT&> {
    public:
        N(CallClassT* host_) : host(host_) { }
        void operator()(ArgT&) { (host->*memFun)(); }
        N* clone() { return new N(host); }

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
        A(CallClassT* host_) : host(host_) { }
        bool operator()(ArgT& obj, char scan, unsigned char chr) { return (host->*memFun)(obj, scan, chr); }
        A* clone() { return new A(host); }

    private:
        CallClassT* host;
    };

    template<class ArgT, bool (CallClassT::*memFun)(char, unsigned char)>
    class N : public HookFunctionBase3<bool, ArgT&, char, unsigned char> {
    public:
        N(CallClassT* host_) : host(host_) { }
        bool operator()(ArgT&, char scan, unsigned char chr) { return (host->*memFun)(scan, chr); }
        N* clone() { return new N(host); }

    private:
        CallClassT* host;
    };
};

// template implementation

template<class ValueT>
bool Select<ValueT>::set(const ValueT& value) {
    for (int i = 0; i < (int)values.size(); ++i)
        if (values[i] == value) {
            selected = i;
            return true;
        }
    return false;
}

#endif // MENU_H_INC
