/*
 *  utility.cpp
 *
 *  Copyright (C) 2003, 2004, 2006 - Niko Ritari
 *  Copyright (C) 2003, 2004, 2005, 2006 - Jani Rivinoja
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
#include <sstream>
#include <string>
#include <vector>

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
using std::min;
using std::ofstream;
using std::ostream;
using std::ostringstream;
using std::setfill;
using std::setprecision;
using std::setw;
using std::string;
using std::vector;

int atoi(const string& str) {
    return std::atoi(str.c_str());
}

string itoa(int val) {
    ostringstream ss;
    ss << val;
    return ss.str();
}

string itoa_w(int val, int width, bool left) {
    ostringstream ss;
    if (left)
        ss << left;
    ss << setw(width) << val;
    return ss.str();
}

string fcvt(double val) {
    ostringstream ss;
    ss << std::fixed << val;
    return ss.str();
}

string fcvt(double val, int precision) {
    ostringstream ss;
    ss << std::fixed << setprecision(precision) << val;
    return ss.str();
}

int iround(double value) {
    if (value >= 0)
        return static_cast<int>(value + 0.5);
    else
        return static_cast<int>(value - 0.5);
}

int numberWidth(int num) {
    if (num == 0)
        return 1;
    int absw = static_cast<int>(floor(std::log10(double(abs(num))))) + 1;
    return (num < 0) ? absw + 1 : absw;
}

string toupper(string str) {
    for (string::iterator s = str.begin(); s != str.end(); ++s)
        *s = latin1_toupper(*s);
    return str;
}

unsigned char latin1_toupper(unsigned char c) {
    if (c <= 31 || (c >= 128 && c <= 159))
        return c;
    // Latin 1 characters 32 - 127 and 160 - 255
    static const string upper   = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`ABCDEFGHIJKLMNOPQRSTUVWXYZ{|}~†°¢£§•¶ß®©™´¨≠ÆØ∞±≤≥¥µ∂∑∏π∫ªºΩæø¿¡¬√ƒ≈∆«»… ÀÃÕŒœ–—“”‘’÷◊ÿŸ⁄€‹›ﬁﬂ¿¡¬√ƒ≈∆«»… ÀÃÕŒœ–—“”‘’÷˜ÿŸ⁄€‹›ﬁˇ";
    //static const string chars = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~†°¢£§•¶ß®©™´¨≠ÆØ∞±≤≥¥µ∂∑∏π∫ªºΩæø¿¡¬√ƒ≈∆«»… ÀÃÕŒœ–—“”‘’÷◊ÿŸ⁄€‹›ﬁﬂ‡·‚„‰ÂÊÁËÈÍÎÏÌÓÔÒÚÛÙıˆ˜¯˘˙˚¸˝˛ˇ";
    //static const string lower = " !\"#$%&'()*+,-./0123456789:;<=>?@abcdefghijklmnopqrstuvwxyz[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~†°¢£§•¶ß®©™´¨≠ÆØ∞±≤≥¥µ∂∑∏π∫ªºΩæø‡·‚„‰ÂÊÁËÈÍÎÏÌÓÔÒÚÛÙıˆ◊¯˘˙˚¸˝˛ﬂ‡·‚„‰ÂÊÁËÈÍÎÏÌÓÔÒÚÛÙıˆ˜¯˘˙˚¸˝˛ˇ";
    c -= 32;
    if (c > 127)
        c -= 32;
    return upper[c];
}

bool cmp_case_ins(const string& a, const string& b) {
    return toupper(a) < toupper(b);
}

string trim(string str) {
    str.erase(0, str.find_first_not_of(" \t\n\r\xA0"));
    const string::size_type lastGood = str.find_last_not_of(" \t\n\r\xA0");
    if (lastGood == string::npos)
        return string();
    if (lastGood + 1 < str.length())
        str.erase(lastGood + 1);
    return str;
}

string replace_all(string text, const string& s1, const string& s2) {
    string::size_type pos = 0;
    while ((pos = text.find(s1, pos)) != string::npos) {
        text.replace(pos, s1.length(), s2);
        pos += s2.length();
    }
    return text;
}

string escape_for_html(string text) {
    text = replace_all(text, "&", "&amp;"); // this must be first because entities contain '&'
    text = replace_all(text, "<", "&lt;");
    text = replace_all(text, ">", "&gt;");
    text = replace_all(text, "\"", "&quot;");
    text = replace_all(text, "'", "&#39;");
    return text;
}

string pad_to_size_left (string text, int size, char pad) {
    const int add = size - text.length();
    if (add > 0)
        text.insert(0, string(add, pad));
    return text;
}

string pad_to_size_right(string text, int size, char pad) {
    const int add = size - text.length();
    if (add > 0)
        text.append(add, pad);
    return text;
}

bool find_nonprintable_char(const string& str) {
    for (string::const_iterator s = str.begin(); s != str.end(); ++s)
        if (is_nonprintable_char(*s))
            return true;
    return false;
}

bool is_nonprintable_char(unsigned char c) {
    return c < 32 || (c >= 128 && c <= 159);
}

string formatForLogging(const string& str) {
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
vector<string> split_to_lines(const string& source, int lineLength, int indent) {
    vector<string> lines;
    if (source.empty())
        return lines;
    size_t start = 0;
    do {
        size_t end;
        if (source.length() <= start + lineLength)
            end = string::npos;
        else
            end = source.find_last_of(" \t", start + lineLength);
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
        start = source.find_first_not_of(" \t", end);
    } while (start != string::npos);
    return lines;
}

char* strspnp(char* str, const char* charset) {
    for (; *str; ++str)
        if (strchr(charset, *str)==NULL)
            return str;
    return NULL;
}

const char* strspnp(const char* str, const char* charset) {
    return strspnp(const_cast<char*>(str), charset);
}

void LogSet::operator()(const char* fmt, ... ) { if (!  normalLog) return; va_list args; va_start(args, fmt); (*  normalLog)(fmt, args); va_end(args); }
void LogSet::error     (const std::string msg) { if (!   errorLog) return;                                         errorLog->put(msg)  ;               }
void LogSet::security  (const char* fmt, ... ) { if (!securityLog) return; va_list args; va_start(args, fmt); (*securityLog)(fmt, args); va_end(args); }

bool g_allowBlockingMessages = true;

void messageBox(const string& heading, const string& msg, bool blocking) {
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

void criticalError(const std::string& msg) {
    messageBox(_("Critical error"), msg, true);
    _Exit(-1);
}

void errorMessage(const string& heading, MemoryLog& errorLog, const string& footer) {
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

void rotate_angle(double& angle, double shift) {
    angle += shift;
    if (angle < 0)
        angle += 360;
    else if (angle >= 360)
        angle -= 360;
}

string date_and_time() {
    const time_t tt = time(0);
    const tm* tmb = localtime(&tt);
    const int time_w = 20;
    char time_str[time_w + 1];
    strftime(time_str, time_w, "%Y-%m-%d %H:%M:%S", tmb);
    return time_str;
}

string approxTime(int seconds) {
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

FileName::FileName(const string& fullName) {
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

string FileName::getFull() const {
    return path + directory_separator + base + ext;
}

// definitions for incalleg.h

#if ALLEGRO_VERSION == 4 && ALLEGRO_SUB_VERSION == 0
void textprintf_ex(struct BITMAP* bmp, AL_CONST FONT *f, int x, int y, int color, int bg, AL_CONST char* format, ...) {
    text_mode(bg);
    va_list argptr;
    char xbuf[16384];
    va_start(argptr, format);
    platVsnprintf(xbuf, 16384, format, argptr);
    va_end(argptr);
    textout(bmp, f, xbuf, x, y, color);
}
void textprintf_centre_ex(struct BITMAP* bmp, AL_CONST FONT *f, int x, int y, int color, int bg, AL_CONST char* format, ...) {
    text_mode(bg);
    va_list argptr;
    char xbuf[16384];
    va_start(argptr, format);
    platVsnprintf(xbuf, 16384, format, argptr);
    va_end(argptr);
    textout_centre(bmp, f, xbuf, x, y, color);
}
void textprintf_right_ex(struct BITMAP* bmp, AL_CONST FONT *f, int x, int y, int color, int bg, AL_CONST char* format, ...) {
    text_mode(bg);
    va_list argptr;
    char xbuf[16384];
    va_start(argptr, format);
    platVsnprintf(xbuf, 16384, format, argptr);
    va_end(argptr);
    textout_right(bmp, f, xbuf, x, y, color);
}
void textout_ex(struct BITMAP* bmp, AL_CONST FONT *f, AL_CONST char* text, int x, int y, int color, int bg) {
    text_mode(bg);
    textout(bmp, f, text, x, y, color);
}
void textout_centre_ex(struct BITMAP* bmp, AL_CONST FONT *f, AL_CONST char* text, int x, int y, int color, int bg) {
    text_mode(bg);
    textout_centre(bmp, f, text, x, y, color);
}
void textout_right_ex(struct BITMAP* bmp, AL_CONST FONT *f, AL_CONST char* text, int x, int y, int color, int bg) {
    text_mode(bg);
    textout_right(bmp, f, text, x, y, color);
}
#endif  // ALLEGRO_VERSION == 4 && ALLEGRO_SUB_VERSION == 0
