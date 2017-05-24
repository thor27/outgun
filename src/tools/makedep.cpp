/*
 *  makedep.cpp - fast and simple dependency to makefile reader
 *
 *  Copyright (C) 2004, 2008 - Niko Ritari
 *
 *  Makedep is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Makedep is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Makedep; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/* "features":
 * - doesn't notice <> type includes at all, and doesn't know about system
 *   headers (works well when <> is used for system headers, "" for own ones)
 * - leaves all "recursive" work for make (can be considered good or bad)
 */

#include <cstdarg>
#include <stdio.h>
#include <string>
#include <cstring>
#include <vector>

using std::string;

#ifndef __GNUC__
#define __attribute__(a)
#endif

class EOF_Hit { };

class StrError {
    string str;

public:
    StrError(const string& s) throw () : str(s) { }
    StrError(const char* fmt, ...) throw () __attribute__ ((format (printf, 2, 3)));
    const string& description() const throw () { return str; }
};

StrError::StrError(const char* fmt, ...) throw () {
    char buf[10000];
    va_list args;
    va_start(args, fmt);
    vsprintf(buf, fmt, args);
    va_end(args);
    str = buf;
}

inline char getChar(FILE* src) throw (EOF_Hit) {
    char ch;
    fread(&ch, sizeof(char), 1, src);
    if (feof(src))
        throw EOF_Hit();
    return ch;
}

/** Replace all 'from' characters by 'to'.
 */
void replaceChars(string& str, char from, char to) throw () {
    for (string::size_type i = 0; i < str.size(); ++i)
        if (str[i] == from)
            str[i] = to;
}

string makeId(string filename) throw () {
    replaceChars(filename, '/', '_');
    replaceChars(filename, '.', '_');
    return filename;
}

char readBack(FILE* src, long& pos) throw (EOF_Hit) { // internal to skipToInclude
    for (;;) {
        if (pos <= 0)
            return '\n';
        fseek(src, --pos, SEEK_SET);
        const char ch = getChar(src);
        if (ch != ' ' && ch != '\t')
            return ch;
    }
}

/** Skip through source file until /^ *# *include +"/ is found.
 * @throws EOF_Hit End of file before encountering an #include.
 */
void skipToInclude(FILE* src) throw (EOF_Hit) {
    static const char matchStr[] = "include ";
    static const int matchLen = 8;
    // note: the first match character isn't repeated in the match string
    //       that means we don't have to rewind at any point when only searching for this
    for (;;) {
        if (getChar(src) != matchStr[0])
            continue;
        for (int match = 1;; ++match) {
            if (match == matchLen) { // 'include ' found; verify that the whole regexp is matched
                long pos0 = ftell(src) - matchLen;
                char ch;
                do {
                    ch = getChar(src);
                } while (ch == ' ');
                if (ch != '"')
                    break; // no match
                const long endPos = ftell(src);
                const bool match = readBack(src, pos0) == '#' && readBack(src, pos0) == '\n';
                fseek(src, endPos, SEEK_SET);
                if (!match)
                    break;
                return;
            }
            const char ch = getChar(src);
            if (ch == matchStr[match])
                continue;
            else if (ch == matchStr[0])
                match = 0;
            else
                break;
        }
    }
}

string readIncludeFileName(FILE* src) throw (StrError) {
    try {
        string name;
        for (;;) {
            char ch = getChar(src);
            if (ch == '"')
                break;
            if (ch == '\n')
                throw StrError("#include missing terminating '\"'");
            name += ch;
        }
        if (name.empty())
            throw StrError("'#include \"\"' not allowed");
        return name;
    } catch (EOF_Hit) {
        throw StrError("end of file in the middle of an #include");
    }
}

/** Translate the source file's #include directives into makefile format. Skip other lines.
 * @param dirbase The relative (to dir of Makefile) directory of the source file, empty if it is the same directory; no slash at the end in any case.
 */
void handleFile(FILE* src, FILE* dst, const string& dirbase) throw (StrError) {
    for (;;) {
        try {
            skipToInclude(src);
        } catch (EOF_Hit) {
            fprintf(dst, "\n");
            return;
        }
        string fileName = readIncludeFileName(src);
        string path = dirbase;
        int nameReadPos = 0;
        while (fileName.substr(nameReadPos, 3) == "../") {
            nameReadPos += 3;
            string::size_type pathsep = path.find_last_of('/');
            if (pathsep == string::npos) {
                if (path.empty())
                    throw StrError("'#include \"%s\"' - invalid parent reference past base directory", fileName.c_str());
                pathsep = 0;
            }
            path.erase(pathsep);
        }
        if (!path.empty())
            path += '/';
        fprintf(dst, "\t$(%s_inc)", makeId(path + fileName.substr(nameReadPos)).c_str());
    }
}

void handleFile(string name, FILE* dst, const std::vector<string>& objPaths) throw (StrError) {
    replaceChars(name, '\\', '/');  // for DOS users getting '\'s in their paths

    const string::size_type extsep = name.find_last_of('.');
    if (extsep == string::npos || extsep == 0 || extsep == name.length() - 1)
        throw StrError("'%s' - expecting filename.ext", name.c_str());
    const string basename = name.substr(0, extsep);
    const string ext = name.substr(extsep + 1);

    if (ext == "c" || ext == "cpp") {
        for (std::vector<string>::const_iterator opi = objPaths.begin(); opi != objPaths.end(); ++opi)
            fprintf(dst, "%s%s.o ", opi->c_str(), basename.c_str());
        fprintf(dst, ":\t%s", name.c_str());
    }
    else
        fprintf(dst, "%s_inc =\t%s", makeId(name).c_str(), name.c_str());

    FILE* src = fopen(name.c_str(), "rb");
    if (!src)
        throw StrError("'%s' - can't open for reading", name.c_str());
    try {
        string path;
        string::size_type pathsep = name.find_last_of('/');
        if (pathsep != string::npos)
            path = name.substr(0, pathsep);
        try {
            handleFile(src, dst, path);
        } catch (const StrError& e) {
            throw StrError("(reading " + name + ") " + e.description());
        }
    } catch (...) {
        fclose(src);
        throw;
    }
}

int main(int argc, const char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "syntax: %s [-O obj-path [-O obj-path ...]] sourcefile ...\n", argv[0]);
        return 1;
    }
    try {
        std::vector<string> objPaths;
        for (int i = 1; i < argc; ++i) {
            if (!strcmp(argv[i], "-O") && i + 1 < argc) {
                string objPath = argv[++i];
                replaceChars(objPath, '\\', '/');
                if (!objPath.empty() && *objPath.rbegin() != '/')
                    objPath += '/';
                objPaths.push_back(objPath);
            }
            else
                handleFile(argv[i], stdout, objPaths);
        }
    } catch (const StrError& e) {
        fprintf(stderr, "%s error: %s\n", argv[0], e.description().c_str());
        return 1;
    }
    return 0;
}
