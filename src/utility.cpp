/*
 *  utility.cpp
 *
 *  Copyright (C) 2003, 2004, 2006, 2008 - Niko Ritari
 *  Copyright (C) 2003, 2004, 2005, 2006, 2008 - Jani Rivinoja
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

#include <fstream>
#include <iomanip>
#include <iostream>
#include <climits>
#include <locale>
#include <sstream>
#include <cstring>

#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <ctime>

#include "incalleg.h"
#include "commont.h"
#include "log.h"
#include "platform.h"
#include "language.h"
#include "utility.h"

using std::dec;
using std::hex;
using std::locale;
using std::min;
using std::numeric_limits;
using std::ofstream;
using std::ostream;
using std::ostringstream;
using std::setfill;
using std::setprecision;
using std::setw;
using std::string;
using std::vector;

ostream& operator<<(ostream& os, ConstDataBlockRef data) throw () {
    STATIC_ASSERT(CHAR_BIT == 8);
    os.write(static_cast<const char*>(data.data()), data.size());
    return os;
}

DataBlock::DataBlock(const DataBlock& source) throw () :
    d(new uint8_t[source.size()], source.size())
{
    memcpy(data(), source.data(), source.size());
}

DataBlock::DataBlock(const ConstDataBlockRef source) throw () :
    d(new uint8_t[source.size()], source.size())
{
    if (source.size()) {
        nAssert(source.data());
        memcpy(data(), source.data(), source.size());
    }
}

DataBlock::~DataBlock() throw () {
    delete[] d.data();
}

DataBlock& DataBlock::operator=(const ConstDataBlockRef source) throw() {
    delete[] d.data();
    d = BlockRef<uint8_t>(new uint8_t[source.size()], source.size());
    if (source.size()) {
        nAssert(source.data());
        memcpy(data(), source.data(), source.size());
    }
    return *this;
}

int atoi(const string& str) throw () {
    return std::atoi(str.c_str());
}

string itoa(int val) throw () {
    ostringstream ss;
    ss << val;
    return ss.str();
}

string itoa_w(int val, int width, bool left) throw () {
    ostringstream ss;
    if (left)
        ss << left;
    ss << setw(width) << val;
    return ss.str();
}

string fcvt(double val) throw () {
    ostringstream ss;
    ss << std::fixed << val;
    return ss.str();
}

string fcvt(double val, int precision) throw () {
    ostringstream ss;
    try {
        locale loc(language.locale().c_str());
        ss.imbue(loc);
    } catch (...) { } // if the locale is not supported, we might get an exception
    ss << std::fixed << setprecision(precision) << val;
    return ss.str();
}

int iround(double value) throw () {
    if (value >= 0)
        return static_cast<int>(value + 0.5);
    else
        return static_cast<int>(value - 0.5);
}

int iround_bound(double value) throw () {
    if (value <= numeric_limits<int>::min())
        return numeric_limits<int>::min();
    else if (value >= numeric_limits<int>::max())
        return numeric_limits<int>::max();
    else
        return iround(value);
}

int numberWidth(int num) throw () {
    if (num == 0)
        return 1;
    int absw = static_cast<int>(floor(std::log10(double(abs(num))))) + 1;
    return (num < 0) ? absw + 1 : absw;
}

double positiveFmod(double val, double modulus) throw () {
    nAssert(modulus > 0);
    return val >= 0 ? fmod(val, modulus) : modulus - fmod(-val, modulus);
}

bool utf8_mode = false;

void check_utf8_mode() throw () {
    char* s;
    if (((s = getenv("LC_ALL")) && *s || (s = getenv("LC_CTYPE")) && *s || (s = getenv("LANG")) && *s) && strstr(s, "UTF-8"))
        utf8_mode = true;
}

string toupper(string str) throw () {
    for (string::iterator s = str.begin(); s != str.end(); ++s)
        *s = latin1_toupper(*s);
    return str;
}

string tolower(string str) throw () {
    for (string::iterator s = str.begin(); s != str.end(); ++s)
        *s = latin1_tolower(*s);
    return str;
}

unsigned char latin1_toupper(unsigned char c) throw () {
    return (c >= 97 && c <= 122 || c >= 224 && c <= 254 && c != 247) ? c - 32 : c;
}

unsigned char latin1_tolower(unsigned char c) throw () {
    return (c >= 65 && c <=  90 || c >= 192 && c <= 222 && c != 215) ? c + 32 : c;
}

string latin1_to_utf8(unsigned char c) throw () {
    string result;
    if (c < 0x80)
        result += c;
    else {
        result += 0xC0 | c >> 6;
        result += 0x80 | c & 0x3F;
    }
    return result;
}

string latin1_to_utf8(const string& str) throw () {
    string result;
    for (string::const_iterator s = str.begin(); s != str.end(); ++s)
        result += latin1_to_utf8(*s);
    return result;
}

string utf8_to_latin1(const string& str) throw () {
    // U-00000000 - U-0000007F:     0xxxxxxx
    // U-00000080 - U-000007FF:     110xxxxx 10xxxxxx
    // U-00000800 - U-0000FFFF:     1110xxxx 10xxxxxx 10xxxxxx
    // U-00010000 - U-001FFFFF:     11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    // U-00200000 - U-03FFFFFF:     111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
    // U-04000000 - U-7FFFFFFF:     1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
    string latin1;
    const char invalid_character = '^';
    for (string::const_iterator s = str.begin(); s != str.end(); ++s) {
        int latin1_char = invalid_character;
        if (!(*s & 0x80))
            latin1_char = *s;
        else if ((*s & 0xE0) == 0xC0) {
            const char c1 = *s;
            if (++s == str.end())
                break;
            if ((*s & 0xC0) == 0x80) {
                const char c2 = *s;
                latin1_char = (c1 & 0x1F) << 6 | c2 & 0x3F;
            }
        }
        else {  // not a Latin 1 character
            if ((*s & 0xF0) == 0xE0)
                s += 2;
            else if ((*s & 0xF8) == 0xF0)
                s += 3;
            else if ((*s & 0xFC) == 0xF8)
                s += 4;
            else if ((*s & 0xFE) == 0xFC)
                s += 5;
        }
        //std::cout << '<' << latin1_char << '>';
        if (latin1_char & 0xFFFFFF00)
            latin1_char = invalid_character;
        latin1 += latin1_char;
    }
    return latin1;
}

bool cmp_case_ins(const string& a, const string& b) throw () {
    return toupper(a) < toupper(b);
}

string trim(string str) throw () {
    str.erase(0, str.find_first_not_of(" \t\n\r\xA0"));
    const string::size_type lastGood = str.find_last_not_of(" \t\n\r\xA0");
    if (lastGood == string::npos)
        return string();
    if (lastGood + 1 < str.length())
        str.erase(lastGood + 1);
    return str;
}

string replace_all(string text, const string& s1, const string& s2) throw () {
    return replace_all_in_place(text, s1, s2);
}

string& replace_all_in_place(string& text, const string& s1, const string& s2) throw () {
    string::size_type pos = 0;
    while ((pos = text.find(s1, pos)) != string::npos) {
        text.replace(pos, s1.length(), s2);
        pos += s2.length();
    }
    return text;
}

string& replace_all_in_place(string& text, char c1, char c2) throw () {
    for (string::size_type pos = 0; pos < text.length(); ++pos)
        if (text[pos] == c1)
            text[pos] = c2;
    return text;
}

string escape_for_html(string text) throw () {
    text = replace_all(text, "&", "&amp;"); // this must be first because entities contain '&'
    text = replace_all(text, "<", "&lt;");
    text = replace_all(text, ">", "&gt;");
    text = replace_all(text, "\"", "&quot;");
    text = replace_all(text, "'", "&#39;");
    return text;
}

string pad_to_size_left (string text, int size, char pad) throw () {
    const int add = size - text.length();
    if (add > 0)
        text.insert(0, string(add, pad));
    return text;
}

string pad_to_size_right(string text, int size, char pad) throw () {
    const int add = size - text.length();
    if (add > 0)
        text.append(add, pad);
    return text;
}

bool find_nonprintable_char(const string& str) throw () {
    for (string::const_iterator s = str.begin(); s != str.end(); ++s)
        if (is_nonprintable_char(*s))
            return true;
    return false;
}

bool is_nonprintable_char(unsigned char c) throw () {
    return c < 32 || (c >= 128 && c <= 159);
}

string formatForLogging(const string& str) throw () {
    ostringstream result;
    for (string::const_iterator s = str.begin(); s != str.end(); ++s) {
        if (is_nonprintable_char(*s)) {
            result << '\\';
            switch (*s) {
            /*break;*/ case '\n': result << 'n';
                break; case '\r': result << 'r';
                break; case '\t': result << 't';
                break; case '\v': result << 'v';
                break; case '\b': result << 'b';
                break; case '\f': result << 'f';
                break; default: result << 'x' << setw(2) << setfill('0') << hex << static_cast<unsigned char>(*s) << dec << setfill(' ');
            }
        }
        else
            result << *s;
    }
    return result.str();
}

// Split string to lines, but only at whitespaces.
vector<string> split_to_lines(const string& source, int lineLength, int indent, bool keep_spaces) throw () {
    vector<string> lines;
    if (source.empty())
        return lines;
    size_t start = 0;
    do {
        size_t end;
        if (source.length() <= start + lineLength)
            end = string::npos;
        else {
            end = source.find_last_of(" \t", start + lineLength);
            if (keep_spaces) {  // Append the extra spaces to the end of the line.
                const size_t next_word = source.find_first_not_of(" \t", end);
                if (next_word != string::npos)
                    end = next_word;
                else
                    end++;
            }
        }
        if (end <= start)
            end = start + lineLength;   // for no forced cutting: source.find_first_of(" \t", start + lineLength);
        string line;
        if (start == 0) // first line
            lineLength -= indent;   // next lines are shorter because of the indent
        else
            line = string(indent, ' '); // apply indentation
        line += source.substr(start, end - start);
        lines.push_back(line);
        if (end == string::npos)
            break;
        start = keep_spaces ? end : source.find_first_not_of(" \t", end);
    } while (start != string::npos);
    return lines;
}

char* strspnp(char* str, const char* charset) throw () {
    for (; *str; ++str)
        if (strchr(charset, *str)==NULL)
            return str;
    return NULL;
}

const char* strspnp(const char* str, const char* charset) throw () {
    return strspnp(const_cast<char*>(str), charset);
}

LogSet& LogSet::operator()(const string& msg   ) throw () { if (  normalLog) {                                        normalLog->put(msg)  ;               } return *this; }
LogSet& LogSet::operator()(const char* fmt, ...) throw () { if (  normalLog) { va_list args; va_start(args, fmt); (*  normalLog)(fmt, args); va_end(args); } return *this; }
LogSet& LogSet::error     (const string& msg   ) throw () { if (   errorLog) {                                         errorLog->put(msg)  ;               } return *this; }
LogSet& LogSet::security  (const char* fmt, ...) throw () { if (securityLog) { va_list args; va_start(args, fmt); (*securityLog)(fmt, args); va_end(args); } return *this; }

bool g_allowBlockingMessages = true;

void messageBox(const string& heading, const string& msg, bool blocking) throw () {
    #ifndef DEDICATED_SERVER_ONLY
    if (g_allowBlockingMessages) {
        platMessageBox(heading, msg, blocking);
        return;
    }
    #else
    (void)blocking;
    #endif
    std::cerr << heading << ":\n" << msg << '\n';
    ofstream os((wheregamedir + "log" + directory_separator + "suppressed_messages.txt").c_str(), std::ios_base::ate);
    // ignore possible error (what could we do?)
    os << date_and_time() << '\n' << heading << ":\n" << msg << "\n\n\n";
}

void criticalError(const string& msg) throw () {
    messageBox(_("Critical error"), msg, true);
    _Exit(-1);
}

void errorMessage(const string& heading, MemoryLog& errorLog, const string& footer) throw () {
    int errors = errorLog.size();
    if (errors) {
        ostringstream msg;
        for (int msgLines = 0; errors > 0 && msgLines < 20; --errors) {
            vector<string> lines = split_to_lines(errorLog.pop(), 60, 4);   // 60 chosen as split point because at least gdialog likes to cut early
            for (vector<string>::const_iterator li = lines.begin(); li != lines.end(); ++li, ++msgLines)
                msg << *li << '\n';
        }
        errors = errorLog.size();
        if (errors)
            msg << _("+ $1 more", itoa(errors));
        msg << footer;
        messageBox(heading, msg.str());
    }
}

void rotate_angle(double& angle, double shift) throw () {
    angle += shift;
    if (angle < 0)
        angle += 360;
    else if (angle >= 360)
        angle -= 360;
}

string date_and_time() throw () {
    const time_t tt = time(0);
    const tm* tmb = localtime(&tt);
    const int time_w = 20;
    char time_str[time_w + 1];
    strftime(time_str, time_w, "%Y-%m-%d %H:%M:%S", tmb);
    return time_str;
}

string approxTime(int seconds) throw () {
    int time = seconds;
    string timeUnit;
    if (time == 0 || (time % 60 != 0 && time <= 200))
        timeUnit = time == 1 ? _("second") : _("seconds");
    else {
        time = (time + 40) / 60;    // 40 chosen because rounding up is favored
        if (time % 60 != 0 && time <= 200)
            timeUnit = time == 1 ? _("minute") : _("minutes");
        else {
            time = (time + 40) / 60;
            if (time % 24 != 0 && time <= 100)
                timeUnit = time == 1 ? _("hour") : _("hours");
            else {
                time = (time + 16) / 24;
                // because a year isn't an integral amount of weeks, it must be handled separately
                if (time % 365 == 0 || time >= 7 * 100) {   // show more than 100 weeks in years
                    time = (time + 250) / 365;
                    timeUnit = time == 1 ? _("year") : _("years");
                }
                else if (time % 7 != 0 && time <= 50)
                    timeUnit = time == 1 ? _("day") : _("days");
                else {
                    time = (time + 5) / 7;
                    timeUnit = time == 1 ? _("week") : _("weeks");
                }
            }
        }
    }
    const string str = itoa(time) + ' ' + timeUnit;
    return str;
}

FileName::FileName(const string& fullName) throw () {
    string::size_type pathSep = fullName.find_last_of(directory_separator);
    if (pathSep != string::npos) {
        path = fullName.substr(0, pathSep);
        ++pathSep; // skip separator
    }
    else
        pathSep = 0;
    string::size_type extSep = fullName.find_last_of('.');
    if (extSep != string::npos && extSep >= pathSep) {
        base = fullName.substr(pathSep, extSep - pathSep);
        ext = fullName.substr(extSep);
    }
    else
        base = fullName.substr(pathSep);
}

string FileName::getFull() const throw () {
    return path + directory_separator + base + ext;
}

// definitions for incalleg.h

#ifndef DEDICATED_SERVER_ONLY

#if ALLEGRO_VERSION == 4 && ALLEGRO_SUB_VERSION == 0
void textprintf_ex(struct BITMAP* bmp, AL_CONST FONT *f, int x, int y, int color, int bg, AL_CONST char* format, ...) throw () {
    text_mode(bg);
    va_list argptr;
    char xbuf[16384];
    va_start(argptr, format);
    platVsnprintf(xbuf, 16384, format, argptr);
    va_end(argptr);
    textout(bmp, f, xbuf, x, y, color);
}
void textprintf_centre_ex(struct BITMAP* bmp, AL_CONST FONT *f, int x, int y, int color, int bg, AL_CONST char* format, ...) throw () {
    text_mode(bg);
    va_list argptr;
    char xbuf[16384];
    va_start(argptr, format);
    platVsnprintf(xbuf, 16384, format, argptr);
    va_end(argptr);
    textout_centre(bmp, f, xbuf, x, y, color);
}
void textprintf_right_ex(struct BITMAP* bmp, AL_CONST FONT *f, int x, int y, int color, int bg, AL_CONST char* format, ...) throw () {
    text_mode(bg);
    va_list argptr;
    char xbuf[16384];
    va_start(argptr, format);
    platVsnprintf(xbuf, 16384, format, argptr);
    va_end(argptr);
    textout_right(bmp, f, xbuf, x, y, color);
}
void textout_ex(struct BITMAP* bmp, AL_CONST FONT *f, AL_CONST char* text, int x, int y, int color, int bg) throw () {
    text_mode(bg);
    textout(bmp, f, text, x, y, color);
}
void textout_centre_ex(struct BITMAP* bmp, AL_CONST FONT *f, AL_CONST char* text, int x, int y, int color, int bg) throw () {
    text_mode(bg);
    textout_centre(bmp, f, text, x, y, color);
}
void textout_right_ex(struct BITMAP* bmp, AL_CONST FONT *f, AL_CONST char* text, int x, int y, int color, int bg) throw () {
    text_mode(bg);
    textout_right(bmp, f, text, x, y, color);
}
#endif // ALLEGRO_VERSION == 4 && ALLEGRO_SUB_VERSION == 0

void set_trans_mode(int alpha) throw () {
    nAssert(alpha >= 0 && alpha <= 255);
    if (alpha != 255) {
        set_trans_blender(0, 0, 0, alpha);
        drawing_mode(DRAW_MODE_TRANS, 0, 0, 0);
    }
    else
        solid_mode();
}

int makecolBounded(int r, int g, int b) throw () {
    return makecol(bound(r, 0, 255), bound(g, 0, 255), bound(b, 0, 255));
}

int set_gfx_mode_if_new(int depth, int card, int w, int h, int v_w, int v_h) throw () {
    static int oldDepth = -1, oldCard = -1, oldW = -1, oldH = -1, oldVw = -1, oldVh = -1;
    if (depth == oldDepth && card == oldCard && w == oldW && h == oldH && v_w == oldVw && v_h == oldVh)
        return 0;
    set_color_depth(depth);
    const int ret = set_gfx_mode(card, w, h, v_w, v_h);
    if (ret == 0) {
        oldDepth = depth;
        oldCard = card;
        oldW = w; oldH = h;
        oldVw = v_w; oldVh = v_h;
    }
    return ret;
}

#endif // !DEDICATED_SERVER_ONLY
