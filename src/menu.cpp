/*
 *  menu.cpp
 *
 *  Copyright (C) 2004, 2006 - Niko Ritari
 *  Copyright (C) 2004, 2006, 2008 - Jani Rivinoja
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

#include <cmath>

#include "incalleg.h"
#include "graphics.h"
#include "language.h" // needed by IPfield
#include "network.h"  // needed by IPfield
#include "timer.h" // for blinking cursor

#include "menu.h"

using std::find;
using std::max;
using std::min;
using std::string;
using std::swap;
using std::vector;

// character width and line height in pixels

inline int char_w() throw () {
    return text_length(font, "M");
}

inline int line_h() throw () {
    return text_height(font) + 8;
}

void scrollbar(BITMAP* buffer, int x, int y, int height, int bar_y, int bar_h, int col1, int col2) throw () {
    const int width = 10;
    if (height > 0) {
        rectfill(buffer, x, y, x + width - 1, y + height - 1, col2);
        if (bar_h > 0)
            rectfill(buffer, x, y + bar_y, x + width - 1, y + bar_y + bar_h - 1, col1);
    }
}

void drawKeySymbol(BITMAP* buffer, int x, int y, const string& text) throw () {
    const int width  = text_length(font, text);
    const int height = text_height(font);
    hline(buffer, x - 4, y - 1, x + width + 3, makecol(0xEE, 0xEE, 0xEE));
    vline(buffer, x - 4, y - 1, y + height, makecol(0xEE, 0xEE, 0xEE));
    hline(buffer, x - 4, y + height, x + width + 3, makecol(0x22, 0x22, 0x22));
    vline(buffer, x + width + 3, y - 1, y + height, makecol(0x22, 0x22, 0x22));
    rectfill(buffer, x - 3, y, x + width + 2, y + height - 1, makecol(0x99, 0x99, 0x99));
    textout_ex(buffer, font, text.c_str(), x, y, 0, -1);
}

int Component::captionColor(bool active, const Colour_manager& col) const throw () {
    if (!isEnabled())
        return col[Colour::menu_disabled];
    if (active)
        return col[Colour::menu_active];
    return col[Colour::menu_component_caption];
}

void Menu::ensure_valid_selection() throw () {
    nAssert(!components.empty());
    if (!components[selected_item]->isEnabled())
        home();
}

void Menu::home() throw () {
    nAssert(!components.empty());
    start = 0;
    for (selected_item = 0; !components[selected_item]->isEnabled(); ++selected_item)
        if (selected_item >= static_cast<int>(components.size()) - 1) {
            selected_item = 0;  // maybe this isn't needed
            return;
        }
}

void Menu::end() throw () {
    nAssert(!components.empty());
    for (selected_item = static_cast<int>(components.size()) - 1; !components[selected_item]->isEnabled(); --selected_item)
        if (selected_item == 0)
            break;
}

bool Menu::prev() throw () {
    nAssert(!components.empty());
    const int original = selected_item;
    do {
        --selected_item;
        if (selected_item < 0) {
            selected_item = original;
            --start;    // allow seeing unselectable components
            return false;
        }
    } while (!components[selected_item]->isEnabled());
    return true;
}

bool Menu::next() throw () {
    nAssert(!components.empty());
    const int original = selected_item;
    do {
        ++selected_item;
        if (selected_item >= static_cast<int>(components.size())) {
            selected_item = original;
            ++start;    // allow seeing unselectable components
            return false;
        }
    } while (!components[selected_item]->isEnabled());
    return true;
}

void Menu::setSelection(int selection) throw () {
    nAssert(selection >= 0);
    if (selection >= (int)components.size())
        selected_item = components.size() - 1;
    else
        selected_item = selection;
}

void Menu::draw(BITMAP* buffer, const Colour_manager& col) throw () {
    //#fix: handle colors and other drawing details with a separate class connected with Graphics
    // colors are initialized in every draw because they must be initialized after every color depth change
    const int col_background       = col[Colour::menu_background];
    const int col_borderShadow     = col[Colour::menu_border_shadow];
    const int col_borderHighlight  = col[Colour::menu_border_highlight];
    const int col_menuCaption      = col[Colour::menu_caption];
    const int col_menuCaptionBg    = col[Colour::menu_caption_bg];
    const int col_scrollbar        = col[Colour::scrollbar];
    const int col_scrollbarBg      = col[Colour::scrollbar_bg];
    const int col_shortcutDisabled = col[Colour::menu_shortcut_disabled];
    const int col_shortcutEnabled  = col[Colour::menu_shortcut_enabled];

    drawHook.call(*this);

    nAssert(!components.empty());
    nAssert(selected_item >= 0 && selected_item < static_cast<int>(components.size()));

    if (!components.empty() && !components[selected_item]->isEnabled()) // a disabled component can not be active
        if (!next())    // try moving down first - feels intuitive
            prev(); // if this fails too, so be it

    // calculate menu width and height
    const int padding = 4 * char_w() - 2;
    const int max_width = buffer->w - 2;    // 2 pixels for borders
    const int min_width = total_width() + 2 * padding;
    const int max_height = buffer->h - 2;   // 2 pixels for borders
    const int min_height = total_height() + 2 * padding;
    const int w = min(max_width, min_width);
    const int h = min(max_height, min_height);
    const int x1 = (buffer->w - w) / 2;
    const int y1 = (buffer->h - h) / 2;
    const int x2 = x1 + w;
    const int y2 = y1 + h;
    const int mx = (x1 + x2) / 2;

    rectfill(buffer, x1, y1, x2, y2, col_background);
    hline(buffer, x1 - 1, y1 - 1, x2 + 1, col_borderHighlight);
    vline(buffer, x1 - 1, y1 - 1, y2 + 1, col_borderHighlight);
    hline(buffer, x1 - 1, y2 + 1, x2 + 1, col_borderShadow);
    vline(buffer, x2 + 1, y1 - 1, y2 + 1, col_borderShadow);

    const int x_start = x1 + padding;
    int y = y1 + padding;

    // draw caption
    rectfill(buffer, x1, y + (text_height(font) - line_h()) / 2, x2, y + (text_height(font) + line_h()) / 2, col_menuCaptionBg);
    textout_centre_ex(buffer, font, caption.c_str(), mx, y, col_menuCaption, -1);
    y += 2 * line_h();

    const int scrollbar_start_y = y;
    const int scrollbar_end_y   = y2 - padding;

    // draw components and calculate visible items
    if (h == min_height)    // everything fits
        start = 0;
    else {
        if (selected_item < start)
            start = selected_item;
        if (selected_item >= start + visible_items)
            start = selected_item - visible_items + 1;
        if (start >= static_cast<int>(components.size()) - visible_items)
            start = components.size() - visible_items;
        if (start < 0)
            start = 0;
    }
    visible_items = 0;
    int selecti = 1;
    for (int ci = 0; ci < start; ++ci)  // find the initial selecti for next loop at compi = start
        if (components[ci]->canBeEnabled())
            ++selecti;
    const int shortcutColor = components[selected_item]->needsNumberKeys() ? col_shortcutDisabled : col_shortcutEnabled;
    int active_x = 0, active_y = 0, active_h = 0;
    for (int compi = start; compi < static_cast<int>(components.size()); ++compi) {
        Component* component = components[compi];
        if (y + component->minHeight() > y2 - padding)
            break;

        if (components[compi]->canBeEnabled()) {
            if (selecti <= 10 && shortcuts)
                textprintf_right_ex(buffer, font, x_start - char_w(), y, shortcutColor, -1, "%d", selecti % 10);
            ++selecti;
        }

        const int h = min(component->height(), y2 - padding - y);
        if (compi == selected_item) {
            active_x = x_start;
            active_y = y;
            active_h = h;
            // draw later
        }
        else
            component->draw(buffer, x_start, y, h, compi == selected_item, col);
        y += component->height();
        visible_items++;
    }

    // draw scrollbar if everything didn't fit
    if (visible_items != static_cast<int>(components.size())) {
        const int x = x2 - padding + char_w();
        const int y = scrollbar_start_y;
        const int height = scrollbar_end_y - y;
        const int bar_y = static_cast<int>(static_cast<double>(height * start) / components.size() + 0.5);
        const int bar_h = static_cast<int>(static_cast<double>(height * visible_items) / components.size() + 0.5);
        scrollbar(buffer, x, y, height, bar_y, bar_h, col_scrollbar, col_scrollbarBg);
    }

    // draw the active component
    components[selected_item]->draw(buffer, active_x, active_y, active_h, true, col);
}

bool Menu::handleKeypress(char scan, unsigned char chr) throw () {
    nAssert(!components.empty());
    nAssert(selected_item >= 0 && selected_item < static_cast<int>(components.size()));
    if (components[selected_item]->isEnabled() && components[selected_item]->handleKey(scan, chr))
        return true;
    if (selected_item >= 2 && dynamic_cast<Textobject*>(components[selected_item - 2]))  // hack
        components[selected_item - 2]->handleKey(scan, chr);
    bool handled = true;
    if (scan == KEY_UP || (scan == KEY_TAB && (key[KEY_LSHIFT] || key[KEY_RSHIFT])))
        prev();
    else if (scan == KEY_DOWN || scan == KEY_TAB)
        next();
    else if (scan == KEY_HOME)
        home();
    else if (scan == KEY_END)
        end();
    else if (shortcuts && ((isdigit(chr) && !components[selected_item]->needsNumberKeys()) || chr == 0)) {  // check for number, and Alt + number
        int shortcut;
        if (chr == 0)   // with alt
            switch (scan) {
            /*break;*/ case KEY_1: shortcut = 0;
                break; case KEY_2: shortcut = 1;
                break; case KEY_3: shortcut = 2;
                break; case KEY_4: shortcut = 3;
                break; case KEY_5: shortcut = 4;
                break; case KEY_6: shortcut = 5;
                break; case KEY_7: shortcut = 6;
                break; case KEY_8: shortcut = 7;
                break; case KEY_9: shortcut = 8;
                break; case KEY_0: shortcut = 9;
                break; default: shortcut = -1; handled = false;
            }
        else if (chr == '0')
            shortcut = 9;
        else
            shortcut = chr - '1';   // relies on "123456789" being sequential and in that order (as in ASCII)
        if (shortcut != -1) {
            bool found = false;
            int newsel;
            for (newsel = 0; newsel < static_cast<int>(components.size()); ++newsel)
                if (components[newsel]->canBeEnabled())
                    if (shortcut-- == 0) {
                        found = true;
                        break;
                    }
            if (found && components[newsel]->isEnabled()) {
                selected_item = newsel;
                nAssert(selected_item >= 0 && selected_item < static_cast<int>(components.size()));
                components[selected_item]->shortcutActivated();
                return true;
            }
        }
    }
    else if (scan == KEY_ENTER || scan == KEY_ENTER_PAD)
        okHook.call(*this);
    else
        handled = false;

    return handled;
}

int Menu::width() const throw () {
    return text_length(font, caption);
}

int Menu::height() const throw () {
    return line_h();
}

void Menu::draw(BITMAP* buffer, int x, int y, int h, bool active, const Colour_manager& col) const throw () {
    if (h < minHeight())
        return;
    textout_ex(buffer, font, caption.c_str(), x, y, captionColor(active, col), -1);
}

bool Menu::handleKey(char scan, unsigned char chr) throw () {
    (void)chr;
    if (!(scan == KEY_ENTER || scan == KEY_ENTER_PAD || scan == KEY_SPACE))
        return false;
    callHook(*this);
    return true;
}

void Menu::shortcutActivated() throw () {
    nAssert(isEnabled());
    callHook(*this);
}

int Menu::total_width() const throw () {
    int min_width = text_length(font, caption);
    for (vector<Component*>::const_iterator comp = components.begin(); comp != components.end(); ++comp)
        min_width = max(min_width, (*comp)->width());
    return min_width;
}

int Menu::total_height() const throw () {
    int height = 2 * line_h();    // for caption
    for (vector<Component*>::const_iterator comp = components.begin(); comp != components.end(); ++comp)
        height += (*comp)->height();
    return height;
}


bool MenuStack::close(Menu* menu) throw () {
    vector<Menu*>::iterator mi = find(st.begin(), st.end(), menu);
    if (mi == st.end())
        return false;
    menu->close();
    st.erase(mi);
    return true;
}


int Spacer::height() const throw () {
    return space * line_h() / 10;
}


void TextfieldBase::draw(BITMAP* buffer, int x, int y, int h, bool active, const Colour_manager& col) const throw () {
    if (h < minHeight())
        return;
    textout_ex(buffer, font, caption.c_str(), x, y, captionColor(active, col), -1);
    x += text_length(font, caption);
    textout_ex(buffer, font, ":", x, y, captionColor(active, col), -1);
    x += text_length(font, ":") + char_w();
    const string text = maskChar ? string(value.length(), maskChar) : value;
    textout_ex(buffer, font, text.c_str(), x, y, col[Colour::menu_value], -1);
    if (active)
        if (int(2 * (get_time() - blinkTime)) % 2 == 0)
            vline(buffer, x + text_length(font, text.substr(0, cursor_pos)), y - 1, y + text_height(font) + 2, col[Colour::menu_value]);
    x += text_length(font, text);
    textout_ex(buffer, font, tail.c_str(), x, y, col[Colour::menu_value], -1);
}

int TextfieldBase::width() const throw () {
    return text_length(font, caption) + maxlen * char_w() + max(tailSpace * char_w(), text_length(font, tail)) +
           text_length(font, ":") + char_w(); // space between caption and value
}

int TextfieldBase::height() const throw () {
    return line_h();
}

bool TextfieldBase::handleKey(char scan, unsigned char chr) throw () {
    unblink();
    bool stateChange = false;
    if (scan == KEY_BACKSPACE) {
        if (cursor_pos > 0) {
            value.erase(cursor_pos - 1, 1);
            cursor_pos--;
            stateChange = true;
        }
    }
    else if (scan == KEY_DEL) {
        if (cursor_pos < static_cast<int>(value.length())) {
            value.erase(cursor_pos, 1);
            stateChange = true;
        }
    }
    else if (scan == KEY_LEFT) {
        if (cursor_pos > 0)
            cursor_pos--;
    }
    else if (scan == KEY_RIGHT) {
        if (cursor_pos < static_cast<int>(value.length()))
            cursor_pos++;
    }
    else if (scan == KEY_HOME && !key[KEY_LCONTROL] && !key[KEY_RCONTROL])
        cursor_pos = 0;
    else if (scan == KEY_END && !key[KEY_LCONTROL] && !key[KEY_RCONTROL])
        cursor_pos = value.length();
    else if (!is_nonprintable_char(chr) && (charset.empty() || charset.find(chr) != string::npos)) {
        if ((int)value.length() < maxlen) {
            value.insert(cursor_pos, 1, chr);
            cursor_pos++;
            stateChange = true;
        }
    }
    else
        return virtualCallKeyHook(scan, chr);   // note: virtualCallHook is not executed regardless of the return
    if (stateChange)
        virtualCallHook();
    return true;
}

void TextfieldBase::unblink() throw () {
    blinkTime = get_time();
}

IPfield::IPfield(const string& caption_, bool acceptPort_, bool printUnknown_) throw () :
    TextfieldBase(caption_, "", acceptPort_ ? 21 : 15, 0, acceptPort_ ? 14 : 20), // max. IP address 123.123.123.123 = 15 chars, :port 6 chars either in address or tail; reserve 14 extra characters in tail for comment
    acceptPort(acceptPort_),
    printUnknown(printUnknown_)
{
    if (acceptPort)
        limitToCharacters("1234567890.:");
    else
        limitToCharacters("1234567890.");
    updateTail();
}

bool IPfield::handleKey(char scan, unsigned char chr) throw () {
    const bool ret = TextfieldBase::handleKey(scan, chr);
    if (ret)
        updateTail();
    return ret;
}

void IPfield::updateTail() throw () {
    const string& address = operator()();
    if (address.empty())
        setTail(printUnknown ? _("unknown") : "");
    else {
        string tail = portStr;
        if (!isValidIP(address, acceptPort, 1))
            tail += " (" + _("invalid") + ')';
        else if (check_private_IP(address))
            tail += " (" + _("private") + ')';
        setTail(tail);
    }
}

void SelectBase::draw(BITMAP* buffer, int x, int y, int h, bool active, const Colour_manager& col) const throw () {
    if (h < minHeight())
        return;
    textout_ex(buffer, font, caption.c_str(), x, y, captionColor(active, col), -1);
    x += text_length(font, caption);
    textout_ex(buffer, font, ":", x, y, captionColor(active, col), -1);
    x += text_length(font, ":") + char_w();
    nAssert(!options.empty());
    nAssert(selected >= 0 && selected < static_cast<int>(options.size()));
    if (active && selected > 0)
        drawKeySymbol(buffer, x, y, "<");
    x += text_length(font, "<") + char_w();
    const int list_x = x - char_w();
    textout_ex(buffer, font, options[selected].c_str(), x, y, col[Colour::menu_value], -1);
    x += text_length(font, options[selected]) + char_w();
    if (active && selected + 1 < static_cast<int>(options.size()))
        drawKeySymbol(buffer, x, y, ">");
    if (!active || !open) {
        open = false;
        return;
    }
    const int line_height = text_height(font) + 4;
    const int list_w = maxSelLength() + 2 * char_w();
    int list_h = options.size() * line_height + line_height / 2;
    int visible_items;
    static int pendingStart = 0;
    if (list_h > SCREEN_H) {
        visible_items = (SCREEN_H - line_height / 2) / line_height;
        list_h = visible_items * line_height + line_height / 2;
        if (pendingSelection > pendingStart + visible_items - 1)
            pendingStart = pendingSelection - visible_items + 1;
        else if (pendingSelection < pendingStart)
            pendingStart = pendingSelection;
        else if (pendingStart + visible_items >= static_cast<int>(options.size()))
            pendingStart = options.size() - visible_items;
    }
    else {
        visible_items = options.size();
        pendingStart = 0;
    }
    int list_y;
    if (y + h + list_h > buffer->h) {
        list_y = y - list_h - line_height / 2;
        if (list_y < 0)
            list_y = (SCREEN_H - list_h) / 2;
    }
    else
        list_y = y + h;
    const int sb_space = visible_items != static_cast<int>(options.size()) ? 9 : 0;
    rect(buffer, list_x, list_y, list_x + list_w + 1 + sb_space, list_y + list_h + 1, col[Colour::menu_border_highlight]);
    rectfill(buffer, list_x + 1, list_y + 1, list_x + list_w + sb_space, list_y + list_h, col[Colour::menu_background]);
    x = list_x + char_w();
    y = list_y + line_height / 2;
    for (int i = pendingStart; i < static_cast<int>(options.size()) && i < pendingStart + visible_items; ++i) {
        const string& option = options[i];
        const int c = (pendingSelection == i ? col[Colour::menu_active] : col[Colour::menu_value]);
        textout_ex(buffer, font, option.c_str(), x, y, c, -1);
        y += line_height;
    }

    // draw scrollbar if everything didn't fit
    if (visible_items != static_cast<int>(options.size())) {
        const int x = list_x + list_w;
        const int y = list_y + 1;
        const int height = list_h;
        const int bar_y = static_cast<int>(static_cast<double>(height * pendingStart) / options.size() + 0.5);
        const int bar_h = static_cast<int>(static_cast<double>(height * visible_items) / options.size() + 0.5);
        scrollbar(buffer, x, y, height, bar_y, bar_h, col[Colour::scrollbar], col[Colour::scrollbar_bg]);
    }
}

int SelectBase::maxSelLength() const throw () {  //#todo: precache
    int max_len = 0;
    for (vector<string>::const_iterator si = options.begin(); si != options.end(); ++si) {
        const int len = text_length(font, *si);
        if (len > max_len)
            max_len = len;
    }
    return max_len;
}

int SelectBase::width() const throw () {
    return text_length(font, caption) + maxSelLength() + text_length(font, ":<>") + 3 * char_w();
}

int SelectBase::height() const throw () {
    return line_h();
}

bool SelectBase::handleKey(char scan, unsigned char chr) throw () {
    (void)chr;
    bool changed = false;
    if (key[KEY_ALT] && (scan == KEY_UP || scan == KEY_DOWN)
        || scan == KEY_SPACE
        || open && (scan == KEY_ENTER || scan == KEY_ENTER_PAD))
    {
        open = !open;
        if (open)
            pendingSelection = selected;
        else if (pendingSelection != selected) {
            selected = pendingSelection;
            changed = true;
        }
    }
    else if ((chr >= 33 && chr <= 127 || chr >= 160) && !(!open && chr >= '0' && chr <= '9')) {
        int& sel = open ? pendingSelection : selected;
        for (int i = sel + 1; ; ++i) {
            if (i >= static_cast<int>(options.size()))
                i = 0;
            if (i == sel)
                break;
            if (!options.empty() && latin1_toupper(options[i][0]) == latin1_toupper(chr)) {
                sel = i;
                changed = !open;
                break;
            }
        }
    }
    else if (open) {
        if (scan == KEY_UP || scan == KEY_LEFT) {
            if (pendingSelection > 0)
                --pendingSelection;
        }
        else if (scan == KEY_DOWN || scan == KEY_RIGHT) {
            if (pendingSelection + 1 < static_cast<int>(options.size()))
                ++pendingSelection;
        }
        else if (scan == KEY_HOME)
            pendingSelection = 0;
        else if (scan == KEY_END)
            pendingSelection = options.size() - 1;
        else if (key[KEY_ESC])
            open = false;
        else
            return false;
    }
    else {
        if (scan == KEY_LEFT) {
            if (selected > 0) {
                --selected;
                changed = true;
            }
        }
        else if (scan == KEY_RIGHT) {
            if (selected + 1 < static_cast<int>(options.size())) {
                ++selected;
                changed = true;
            }
        }
        else
            return false;
    }
    if (changed)
        virtualCallHook();
    return true;
}


void Colorselect::draw(BITMAP* buffer, int x, int y, int h, bool active, const Colour_manager& col) const throw () {
    if (h < minHeight())
        return;
    textout_ex(buffer, font, caption.c_str(), x, y, captionColor(active, col), -1);
    x += text_length(font, caption);
    textout_ex(buffer, font, ":", x, y, captionColor(active, col), -1);
    x += text_length(font, ":") + char_w();
    if (active)
        drawKeySymbol(buffer, x, y, "+");
    x += 2 * char_w();
    nAssert(!options.empty());
    nAssert(selected >= 0 && selected < static_cast<int>(options.size()));
    const int bw = 10;
    const int bh = line_h() - 1;
    for (int i = 0; i < static_cast<int>(options.size()); i++)
        rectfill(buffer, x + (bw + 2) * i + 2, y - 2, x + (bw + 2) * i + bw - 2, y + bh - 6, graphics->player_color(options[i]));
    if (active) {   // mark selection
        const int i = selected;
        rect    (buffer, x + (bw + 2) * i    , y - 4, x + (bw + 2) * i + bw    , y + bh - 4, captionColor(active, col));
    }
    if (active) {
        x += options.size() * (bw + 2) + bw - 2;
        drawKeySymbol(buffer, x, y, "-");
    }
}

int Colorselect::width() const throw () {
    return text_length(font, caption) + text_length(font, ":") + 2 * char_w() + options.size() * 12 + 2 * 2 * char_w();
}

int Colorselect::height() const throw () {
    return line_h();
}

bool Colorselect::handleKey(char scan, unsigned char chr) throw () {
    if ((scan == KEY_LEFT || chr == '+') && selected > 0) {
        if (key[KEY_LCONTROL] || key[KEY_RCONTROL] || chr == '+')
            swap(options[selected], options[selected - 1]);
        --selected;
    }
    else if ((scan == KEY_RIGHT || chr == '-') && selected + 1 < static_cast<int>(options.size())) {
        if (key[KEY_LCONTROL] || key[KEY_RCONTROL] || chr == '-')
            swap(options[selected], options[selected + 1]);
        ++selected;
    }
    else
        return false;
    callHook(*this);
    return true;
}


void Checkbox::draw(BITMAP* buffer, int x, int y, int h, bool active, const Colour_manager& col) const throw () {
    if (h < minHeight())
        return;
    textout_ex(buffer, font, "[", x, y, col[Colour::menu_value], -1);
    x += text_length(font, "[");
    if (checked)
        textout_ex(buffer, font, "×", x, y, col[Colour::menu_value], -1);
    x += text_length(font, "×");
    textout_ex(buffer, font, "]", x, y, col[Colour::menu_value], -1);
    x += text_length(font, "]") + char_w();
    textout_ex(buffer, font, caption.c_str(), x, y, captionColor(active, col), -1);
}

int Checkbox::width() const throw () {
    return text_length(font, "[×]") + char_w() + text_length(font, caption);
}

int Checkbox::height() const throw () {
    return line_h();
}

bool Checkbox::handleKey(char scan, unsigned char chr) throw () {
    (void)chr;
    if (scan == KEY_SPACE || scan == KEY_X)
        toggle();
    else
        return false;
    callHook(*this);
    return true;
}

void Checkbox::shortcutActivated() throw () {
    nAssert(isEnabled());
    toggle();
    callHook(*this);
}


Slider::Slider(const std::string caption_, bool graphic_, int vmin_, int vmax_) throw () :
    Component(caption_), vmin(vmin_), vmax(vmax_), val(vmin_),
    step(1),
    graphic(graphic_)
{ }

Slider::Slider(const std::string caption_, bool graphic_, int vmin_, int vmax_, int init_value, int step_, bool lockToStep_) throw () :
    Component(caption_), vmin(vmin_), vmax(vmax_), val(init_value), step(step_), graphic(graphic_), lockToStep(lockToStep_)
{
    nAssert(init_value >= vmin_ && init_value <= vmax_);
    nAssert(step >= 0);
    nAssert(!lockToStep || (step > 0 && (vmax - vmin) % step == 0));
}

void Slider::boundSet(int value) throw () {
    val = bound(value, vmin, vmax);
    if (lockToStep)
        val -= (val - vmin) % step;
}

int Slider::width() const throw () {
    int fieldWidth;
    if (graphic)
        fieldWidth = 20;    // arbitrary bar length
    else
        fieldWidth = max(numberWidth(vmin), numberWidth(vmax));
    return text_length(font, caption) + text_length(font, ":") + char_w() + fieldWidth * char_w();
}

int Slider::height() const throw () {
    return line_h();
}

void Slider::draw(BITMAP* buffer, int x, int y, int h, bool active, const Colour_manager& col) const throw () {
    if (h < minHeight())
        return;
    const string::size_type end = caption.find_last_not_of(' '); // used to bring the ':' next to the text if the text is padded to the right
    nAssert(end != string::npos);
    textout_ex(buffer, font, (caption.substr(0, end + 1) + ':').c_str(), x, y, captionColor(active, col), -1);
    const int x0 = x + text_length(font, caption) + text_length(font, ":") + char_w();
    if (graphic) {
        const int barLength = (x + width() - 2 - x0) * (val - vmin) / (vmax - vmin);
        const int yb = y - 4;
        rect(buffer, x0, yb, x + width() - 1, yb + height() - 1, captionColor(active, col));
        if (barLength)
            rectfill(buffer, x0 + 1, yb + 1, x0 + barLength, yb + height() - 2, col[Colour::menu_value]);
        textprintf_centre_ex(buffer, font, (x0 + x + width() - 1) / 2, y, col[Colour::menu_disabled], -1, "%.0f%%", 100 * static_cast<double>(val - vmin) / (vmax - vmin));
    }
    else
        textprintf_ex(buffer, font, x0, y, col[Colour::menu_value], -1, "%d", val);
}

bool Slider::handleKey(char scan, unsigned char chr) throw () {
    if ((scan == KEY_LEFT || chr == '-') && val > vmin) {
        if (!lockToStep && (key[KEY_LCONTROL] || key[KEY_RCONTROL] || chr == '-'))
            --val;
        else if (step == 0) // logarithmic
            val -= (val - vmin) / 11 + 1;   // /11 to have ++,-- or --,++ result in the original value; it's magic ;)
        else
            val -= step;
        if (val < vmin)
            val = vmin;
    }
    else if ((scan == KEY_RIGHT || chr == '+') && val < vmax) {
        if (!lockToStep && (key[KEY_LCONTROL] || key[KEY_RCONTROL] || chr == '+'))
            ++val;
        else if (step == 0) // logarithmic
            val += (val - vmin) / 10 + 1;
        else
            val += step;
        if (val > vmax)
            val = vmax;
    }
    else
        return false;
    callHook(*this);
    return true;
}


void NumberEntry::boundSet(int value) throw () {
    entry = val = bound(value, vmin, vmax);
}

int NumberEntry::width() const throw () {
    int fieldWidth; // don't count the cursor to this
    // in basic case space for val_ is needed
    // for the case of entry < vmin = val, space is needed for "entry_ (val)" (this can't happen when vmin == 0)
    if (vmin > 0) {
        const int withEntry = numberWidth(vmin - 1) + numberWidth(vmin) + 3;  // the widest value for entry in this case is vmin - 1
        fieldWidth = max(withEntry, numberWidth(vmax));
    }
    else
        fieldWidth = numberWidth(vmax);
    return text_length(font, caption) + text_length(font, ":_") + char_w() + fieldWidth * char_w();
}

int NumberEntry::height() const throw () {
    return line_h();
}

void NumberEntry::draw(BITMAP* buffer, int x, int y, int h, bool active, const Colour_manager& col) const throw () {
    if (h < minHeight())
        return;
    textout_ex(buffer, font, caption.c_str(), x, y, captionColor(active, col), -1);
    x += text_length(font, caption);
    textout_ex(buffer, font, ":", x, y, captionColor(active, col), -1);
    x += text_length(font, ":") + char_w();
    if (entry != val)
        textprintf_ex(buffer, font, x, y, col[Colour::menu_value], -1, "%d%s (%d)", entry, active ? "_" : "", val);
    else
        textprintf_ex(buffer, font, x, y, col[Colour::menu_value], -1, "%d%s", val, active ? "_" : "");
}

bool NumberEntry::handleKey(char scan, unsigned char chr) throw () {
    if ((scan == KEY_LEFT || chr == '-') && entry > vmin)
        --entry;
    else if ((scan == KEY_RIGHT || chr == '+') && entry < vmax)
        ++entry;
    else if (scan == KEY_BACKSPACE)
        entry /= 10;    // discard the last digit
    else if (chr >= '0' && chr <= '9') {    // assuming sequentiality and ASCII order "0123456789"
        entry = entry * 10 + (chr - '0');
        if (entry > vmax) {
            entry /= 10;    // back up to where we were
            return false;
        }
    }
    else
        return false;
    int oldVal = val;
    if (entry >= vmin)
        val = entry;
    else
        val = vmin;
    if (val != oldVal)
        callHook(*this);
    return true;
}


int Textarea::width() const throw () {
    return text_length(font, caption);
}

int Textarea::height() const throw () {
    return line_h();
}

void Textarea::draw(BITMAP* buffer, int x, int y, int h, bool active, const Colour_manager& col) const throw () {
    if (h < minHeight())
        return;
    textout_ex(buffer, font, caption.c_str(), x, y, captionColor(active, col), -1);
}

bool Textarea::handleKey(char scan, unsigned char chr) throw () {
    (void)chr;
    if (scan == KEY_ENTER || scan == KEY_ENTER_PAD || scan == KEY_SPACE) {
        callHook(*this);
        return true;
    }
    else if (callKeyHook(*this, scan, chr))
        return true;
    return false;
}

void Textarea::shortcutActivated() throw () {
    nAssert(isEnabled());
    callHook(*this);
}


int StaticText::width() const throw () {
    int len = text_length(font, caption) + text_length(font, text);
    if (!caption.empty() && !text.empty())
        len += text_length(font, ":") + char_w();
    return len;
}

int StaticText::height() const throw () {
    return line_h();
}

void StaticText::draw(BITMAP* buffer, int x, int y, int h, bool active, const Colour_manager& col) const throw () {
    active = false; // don't draw as active even if caller thinks this should be active
    if (h < minHeight())
        return;
    textout_ex(buffer, font, caption.c_str(), x, y, captionColor(active, col), -1);
    if (!caption.empty() && !text.empty()) {
        x += text_length(font, caption);
        textout_ex(buffer, font, ":", x, y, captionColor(active, col), -1);
        x += text_length(font, ":") + char_w();
    }
    textout_ex(buffer, font, text.c_str(), x, y, col[Colour::menu_value], -1);
}


int Textobject::width() const throw () {
    int len = 0;
    for (vector<string>::const_iterator li = lines.begin(); li != lines.end(); ++li) {
        len = max(len, min(text_length(font, *li), 70 * char_w()));
        if (len == 70 * char_w())
            break;
    }
    return len;
}

int Textobject::height() const throw () {
    // leave some space for hack purpose
    // 2 paddings, 2 lines for caption, 1 line for Textobject itself, spacer, 2 pixels for menu borders
    const int padding = 4 * char_w() - 2;
    const int spacer = 5 * line_h() / 10;
    const unsigned max_h = max(objLineHeight(), SCREEN_H - 2 - 3 * line_h() - spacer - 2 * padding);

    const int linew = min((SCREEN_W - 2 * (1 + padding)) / char_w(), 70);
    if (linew != old_linew) {
        old_linew = linew;
        splitted.clear();
        for (vector<string>::const_iterator li = lines.begin(); li != lines.end(); ++li) {
            const string::size_type indent = li->find_first_not_of(' ');
            if (indent == string::npos)
                splitted.push_back(string());
            else {
                const vector<string> sublines = split_to_lines(*li, linew, indent);
                splitted.insert(splitted.end(), sublines.begin(), sublines.end());
            }
        }
    }

    return min<unsigned>(splitted.size() * objLineHeight(), max_h);
}

int Textobject::objLineHeight() const throw () {
    return text_height(font) + 4;
}

void Textobject::draw(BITMAP* buffer, int x, int y0, int h, bool active, const Colour_manager& col) const throw () {
    (void)active;
    if (start > static_cast<int>(splitted.size()) - h / objLineHeight())
        start = splitted.size() - h / objLineHeight();
    if (start < 0)
        start = 0;
    visible_lines = 0;
    for (int i = start, y = y0; i < static_cast<int>(splitted.size()); ++i) {
        if (y + objLineHeight() > y0 + h)
            break;
        textout_ex(buffer, font, splitted[i].c_str(), x, y, col[Colour::menu_value], -1);
        ++visible_lines;
        y += objLineHeight();
    }

    // draw scrollbar if everything didn't fit
    if (visible_lines != static_cast<int>(splitted.size())) {
        const int sbx = min(x + width() + char_w(), buffer->w - 12);
        const int bar_y = static_cast<int>(static_cast<double>(h * start) / splitted.size() + 0.5);
        const int bar_h = static_cast<int>(static_cast<double>(h * visible_lines) / splitted.size() + 0.5);
        scrollbar(buffer, sbx, y0, h, bar_y, bar_h, col[Colour::scrollbar], col[Colour::scrollbar_bg]);
    }
}

bool Textobject::handleKey(char scan, unsigned char chr) throw () {
    (void)chr;
    // If start goes out of range, draw method fixes it.
    if (scan == KEY_UP)
        --start;
    else if (scan == KEY_DOWN)
        ++start;
    else if (scan == KEY_HOME)
        start = 0;
    else if (scan == KEY_END)
        start = INT_MAX;
    else if (scan == KEY_PGUP)
        start -= visible_lines;
    else if (scan == KEY_PGDN)
        start += visible_lines;
    else
        return false;
    return true;
}
