/*
 *  makedep.cpp - fast and simple dependency to makefile reader
 *
 *  Copyright (C) 2004 - Niko Ritari
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
 * - doesn't recognize include lines with extra spaces
 * - doesn't recognize the first include if it's the first line in the file
 * - leaves all "recursive" work for make (can be considered good or bad)
 */

#include <cstdarg>
#include <stdio.h>
#include <string>

#ifndef __GNUC__
#define __attribute__(a)
#endif

class EOF_Hit { };

class StrError {
    std::string str;

public:
    StrError(const char* fmt, ...) __attribute__ ((format (printf, 2, 3)));
    void print() const {
        fprintf(stderr, "Error: %s\n", str.c_str());
    }
};

StrError::StrError(const char* fmt, ...) {
    char buf[10000];
    va_list args;
    va_start(args, fmt);
    vsprintf(buf, fmt, args);
    va_end(args);
    str = buf;
}

inline char getChar(FILE* src) {
    char ch;
    fread(&ch, sizeof(char), 1, src);
    if (feof(src))
        throw EOF_Hit();
    return ch;
}

/** Skip through source file until '\n#include "' is found.
 * @throws EOF_Hit End of file before encountering an #include.
 */
void skipToInclude(FILE* src) {
    static const char matchStr[] = "\n#include \"";
    static const int matchLen = 11;
    // note: the first match character isn't repeated in the match string
    //       that means we don't have to rewind at any point
    for (;;) {
        if (getChar(src) != matchStr[0])
            continue;
        for (int match = 1;; ++match) {
            if (match == matchLen)
                return; // found
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

std::string readIncludeFileName(FILE* src) {
    std::string name;
    for (;;) {
        char ch = getChar(src);
        if (ch == '"')
            break;
        name += ch;
    }
    if (name.length() < 3 || name.substr(name.length() - 2) != ".h")
        throw StrError("'#include \"%s\"' - should be \"something.h\"", name.c_str());
    name.erase(name.length() - 2);
    return name;
}

/** Replace all 'from' characters by 'to'.
 */
void replaceChars(std::string& str, char from, char to) {
    for (std::string::size_type i = 0; i < str.size(); ++i)
        if (str[i] == from)
            str[i] = to;
}

void replaceSlashes(std::string& str) { replaceChars(str, '/', '_'); }

/** Translate the source file's #include directives into makefile format. Skip other lines.
 * @param dirbase The relative (to dir of Makefile) directory of the source file, empty if it is the same directory; no slash at the end in any case.
 */
void handleFile(FILE* src, FILE* dst, const std::string& dirbase) {
     for (;;) {
         try {
             skipToInclude(src);
         } catch (EOF_Hit) {
             fprintf(dst, "\n");
             return;
         }
         std::string fileName = readIncludeFileName(src);
         std::string path = dirbase;
         int nameReadPos = 0;
         while (fileName.substr(nameReadPos, 3) == "../") {
             nameReadPos += 3;
             std::string::size_type pathsep = path.find_last_of('/');
             if (pathsep == std::string::npos) {
                 if (path.empty())
                     throw StrError("'#include \"%s\"' - invalid parent reference past base directory", fileName.c_str());
                 pathsep = 0;
             }
             path.erase(pathsep);
         }
         if (!path.empty())
             path += '/';
         path += fileName.substr(nameReadPos);
         replaceSlashes(path);
         fprintf(dst, "\t$(%s_inc)", path.c_str());
     }
}

void handleFile(std::string name, FILE* dst, const std::string& compileCommand) {
    replaceChars(name, '\\', '/');  // for DOS users getting '\'s in their paths
    std::string id = name;
    if (id.length() > 2 && id.substr(id.length() - 2) == ".h") {
        replaceSlashes(id);
        id.erase(id.length() - 2);
        fprintf(dst, "%s_inc =\t%s", id.c_str(), name.c_str());
    }
    else if (id.length() > 4 && id.substr(id.length() - 4) == ".cpp") {
        id.erase(id.length() - 4);
        fprintf(dst, "%s.o:\t%s", id.c_str(), name.c_str());
    }
    else
        throw StrError("'%s' - only .h and .cpp files are handled", name.c_str());
    FILE* src = fopen(name.c_str(), "rb");
    if (!src)
        throw StrError("'%s' - can't open for reading", name.c_str());
    std::string path;
    std::string::size_type pathsep = name.find_last_of('/');
    if (pathsep != std::string::npos)
        path = name.substr(0, pathsep);
    try {
        handleFile(src, dst, path);
    } catch (...) {
        fclose(src);
        throw;
    }
    if (!compileCommand.empty())
        fprintf(dst, "\t%s\n", compileCommand.c_str());
}

int main(int argc, const char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "args: %s [\"Compile command\"] .h-or-.cpp-files\n", argv[0]);
        return 1;
    }
    try {
        for (int i = 2; i < argc; ++i)
            handleFile(argv[i], stdout, argv[1]);
    } catch (const StrError& e) {
        e.print();
        return 1;
    }
    return 0;
}
