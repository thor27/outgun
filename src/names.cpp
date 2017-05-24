/*
 *  names.cpp
 *
 *  Copyright (C) 2002 - Renato Hentschke
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004 - Niko Ritari
 *  Copyright (C) 2004, 2008 - Jani Rivinoja
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

#include <cstdlib>
#include "commont.h"
#include "utility.h"

#include "names.h"

using std::string;
using std::vector;

static int Random(int velho) throw () {
  return rand() % velho;
}

static string Vog() throw () {
    int prob = Random(100);
    if (prob>=0 && prob<=30)
      return "a";
    else
    if (prob>=31 && prob<=35)
      return "y";
    else
    if (prob>=36 && prob<=55)
      return "e";
    else
    if (prob>=56 && prob<=66)
      return "i";
    else
    if (prob>=67 && prob<=90)
      return "o";
    else
    if (prob>=91 && prob<=99)
      return "u";
    else
        return "";//never happens
}

static string ConsProlSemiVog() throw () {
    if (Random(100)<20)
    {
        if (Random(100)<30)
        {
            int prob = Random(100);
            if (prob>=0 && prob<=45)
                return "i";
            if (prob>=46 && prob<=85)
                return "u";
            if (prob>=86 && prob<=95)
                return "y";
            if (prob>=96 && prob<=99)
                return "w";
        }
        else
        {
            int prob = Random(63);
            if (prob>=0 && prob<=10)
                return "h";
            if (prob>=11 && prob<=30)
                return "l";
            if (prob>=31 && prob<=50)
                return "r";
            if (prob>=51 && prob<=57)
                return "s";
            if (prob>=59 && prob<=62)
                return "z";
        }
    }

    return "";
}

static string PreVog() throw () {
    string prevog;

    if (Random(101)<87)
    {
        int prob = Random(100);
        if (prob>=0 && prob<=4)
          prevog = "b";
        if (prob>=5 && prob<=8)
          prevog = "c";
        if (prob>=9 && prob<=13)
          prevog = "d";
        if (prob>=14 && prob<=18)
          prevog = "f";
        if (prob>=19 && prob<=23)
          prevog = "g";
        if (prob>=24 && prob<=27)
          prevog = "h";
        if (prob>=28 && prob<=31)
          prevog = "j";
        if (prob>=32 && prob<=35)
          prevog = "k";
        if (prob>=36 && prob<=40)
          prevog = "l";
        if (prob>=41 && prob<=45)
          prevog = "m";
        if (prob>=46 && prob<=50)
          prevog = "n";
        if (prob>=51 && prob<=55)
          prevog = "p";
        if (prob>=56 && prob<=58)
          prevog = "q";
        if (prob>=59 && prob<=63)
          prevog = "r";
        if (prob>=64 && prob<=68)
          prevog = "s";
        if (prob>=69 && prob<=73)
          prevog = "t";
        if (prob>=74 && prob<=78)
          prevog = "v";
        if (prob>=79 && prob<=83)
          prevog = "w";
        if (prob>=84 && prob<=88)
          prevog = "x";
        if (prob>=89 && prob<=92)
          prevog = "z";
        prevog = prevog + ConsProlSemiVog();
    }
    else
        prevog = "";

    return prevog;
}

static string PosSemiVog() throw () {
    if (Random(101)<13)
    {
        int prob = Random(100);
        if (prob>=0 && prob<=45)
          return "i";
        if (prob>=46 && prob<=85)
          return "u";
        if (prob>=86 && prob<=95)
          return "y";
        if (prob>=96 && prob<=99)
          return "w";
    }

    return "";
}

static string PosConsProl() throw () {
    if (Random(100)<18)
    {
        int prob = Random(169);
        if (prob>=0 && prob<=2)
          return "b";
        if (prob>=3 && prob<=5)
          return "c";
        if (prob>=6 && prob<=8)
          return "d";
        if (prob>=9 && prob<=11)
          return "f";
        if (prob>=12 && prob<=14)
          return "g";
        if (prob>=15 && prob<=30)
          return "h";
        if (prob>=31 && prob<=32)
          return "j";
        if (prob>=33 && prob<=36)
          return "k";
        if (prob>=37 && prob<=55)
          return "l";
        if (prob>=56 && prob<=74)
          return "m";
        if (prob>=75 && prob<=92)
          return "n";
        if (prob>=93 && prob<=95)
          return "p";
        if (prob>=96 && prob<=98)
          return "q";
        if (prob>=99 && prob<=116)
          return "r";
        if (prob>=117 && prob<=136)
          return "s";
        if (prob>=137 && prob<=140)
          return "t";
        if (prob>=141 && prob<=143)
          return "v";
        if (prob>=144 && prob<=154)
          return "w";
        if (prob>=155 && prob<=156)
          return "x";
        if (prob>=157 && prob<=168)
          return "z";
    }

    return "";
}

static string Silaba() throw () {
    string retorno;

    retorno = PreVog() + Vog() + PosSemiVog() + PosConsProl();

    return retorno;
}

static string Palavra() throw () {
    int nsil;

    string palavra;
    palavra = "";

    int prob = Random(100);

    if (prob>=0 && prob<=10)
        nsil = 1;
    else
    if (prob>=11 && prob<=75)
        nsil = 2;
    else
    if (prob>=76 && prob<=99)
        nsil = 3;
    else
        nsil = 4; //never happens

    for (int r=0; r< nsil; r++)
        palavra = palavra + Silaba();

    palavra[0] = palavra[0] - 32;

    return palavra;
}

string RandomName(int npal) throw () {
  int prob;

    if (npal<1)
    {
        prob = Random(100);

        if (prob<=60)
            npal=1;
        else
            npal=2;
    }

    string nome;
    for (int r=0; r<npal-1; r++)
        nome += Palavra() + ' ';
    nome += Palavra();

    return trim(nome.substr(0, maxPlayerNameLength));
}

static string::value_type str_rand(const string& str) throw () {
    if (str.empty())
        return 0;
    return str[rand() % str.length()];
}

static string::value_type consonant() throw () {
    return str_rand("hjklmnprstv");
}

static string::value_type start_consonant(string::value_type last_cons) throw () {
    switch (last_cons) {
    /*break;*/ case 'h': return str_rand("jklmnrtv");   // kahjo, rahka, kahle, kuhmu, vehnä, tahra, tahto, kahva
        break; case 'k': return str_rand("krs");        // takki, vuokra, suksi
        break; case 'l': return str_rand("hjklmpstv");  // kulho, kalju, palko, pallo, palmu, salpa, hylsy, polte, polvi
        break; case 'm': return str_rand("mp");         // kammo, kampa
        break; case 'n': return str_rand("hjknst");     // hanhi, linja, lanko, vanne, kansi, kanto
        break; case 'p': return str_rand("lprs");       // kupla, kuppi, kupru, kipsi
        break; case 'r': return str_rand("hjkmnprstv"); // karhu, karju, karku, karmi, kaarna, turpa, tarra, kärsä, pirtu, kurvi
        break; case 's': return str_rand("kpstv");      // tasku, raspi, lasso, lastu, rasva
        break; case 't': return str_rand("jkrstv");     // ketju, katko, katras, katse, katto, katve
    }
    return consonant();
}

static string::value_type end_consonant(string::value_type last_cons) throw () {
    switch (last_cons) {
    /*break;*/ case 'l': return str_rand("kpst");       // kelkka, tolppa, valssi, valtti
        break; case 'm': return str_rand("p");          // kamppi
        break; case 'n': return str_rand("kst");        // lankku, tanssi, lantti
        break; case 'r': return str_rand("kpst");       // karkki, karppi, hirssi, karttu
    }
    return consonant();
}

enum Vow_type { any, back, front };

static string::value_type vowel(Vow_type type) throw () {
    if (type == back)
        return str_rand("aaeiiou");
    else if (type == front)
        return str_rand("eeiiiyäö");
    else
        return str_rand("aaaeiiioouuyäö");
}

static string::value_type start_vowel(string::value_type last_vowel, Vow_type type) throw () {
    string::value_type vow;
    do {
        vow = vowel(type);
    } while (vow == last_vowel);
    return vow;
}

static string::value_type cont_vowel(string::value_type last_vowel, Vow_type type) throw () {
    if (type == any)
        switch (last_vowel) {
        /*break;*/ case 'a': return str_rand("aaiu");
            break; case 'e': return str_rand("eiiu");
            break; case 'i': return str_rand("eiiu");
            break; case 'o': return str_rand("iou");
            break; case 'u': return str_rand("iouu");
            break; case 'y': return str_rand("iyyö");
            break; case 'ä': return str_rand("iyää");
            break; case 'ö': return str_rand("iyö");
        }
    if (type == back)
        switch (last_vowel) {
        /*break;*/ case 'a': return str_rand("aaiu");
            break; case 'e': return str_rand("eiu");
            break; case 'i': return str_rand("eiiu");
            break; case 'o': return str_rand("iou");
            break; case 'u': return str_rand("iouu");
        }
    if (type == front)
        switch (last_vowel) {
        /*break;*/ case 'e': return str_rand("ei");
            break; case 'i': return str_rand("eii");
            break; case 'y': return str_rand("iyyö");
            break; case 'ä': return str_rand("iyää");
            break; case 'ö': return str_rand("iyö");
        }
    return vowel(type);
}

enum Type { a, aa, aat, at, att, ta, taa, taat, tat, tatt, total_types };

static string make_name() throw () {
    Type type;
    for (int i = 0; i < 5; ++i) {        // Better chances for a type beginning with 't'.
        type = static_cast<Type>(rand() % total_types);
        if (type == ta || type == tat)
            break;
        else if (i >= 3 && (type == taa || type == taat))
            break;
    }

    const int r = rand() % 10;
    int num_groups;
    if (r < 5)
        num_groups = 2;
    else if (r < 8)
        num_groups = 3;
    else if (r < 9)
        num_groups = 1;
    else
        num_groups = 4;
    if (type != taa && num_groups == 1)
        num_groups = rand() % 2 + 2;

    string::value_type end_cons = 0;
    string::value_type end_vow = 0;

    Vow_type vow_type = any;
    string name;
    string::value_type force_cons = 0;
    for (int i = 0; i < num_groups; ++i) {
        string group;
        if (type == ta || type == taa || type == taat || type == tat) {
            if (force_cons)
                group += force_cons;
            else if (!end_cons)
                group += consonant();
            else
                group += start_consonant(end_cons);
        }
        end_cons = force_cons = 0;
        string::value_type vow;
        if (type == ta || type == taa || type == taat || type == tat || !end_vow)
            vow = vowel(vow_type);
        else
            vow = start_vowel(end_vow, vow_type);
        switch (vow) {
        /*break;*/ case 'a': case 'o': case 'u': vow_type = back;
            break; case 'y': case 'ä': case 'ö': vow_type = front;
        }
        group += vow;
        end_vow = 0;
        if (type == aa || type == aat || type == taa || type == taat) {
            vow = cont_vowel(vow, vow_type);
            switch (vow) {
            /*break;*/ case 'a': case 'o': case 'u': vow_type = back;
                break; case 'y': case 'ä': case 'ö': vow_type = front;
            }
            group += vow;
        }
        if (type == aat || type == at || type == taat || type == tat) {
            end_cons = str_rand("hklmnprst");
            group += end_cons;
        }
        else if (type == att || type == tatt) {
            const string::value_type mid_cons = str_rand("lmnr");
            group += mid_cons;
            end_cons = end_consonant(mid_cons);
            group += end_cons;
            force_cons = end_cons;
        }
        else
            end_vow = vow;
        name += group;

        if (i + 1 == num_groups)
            break;

        vector<Type> next;
        next.push_back(ta);
        switch (type) {
        /*break;*/ case a: case aa:
                next.push_back(tat);
                if (i + 2 < num_groups)
                    next.push_back(tatt);
            break; case at:
                next.push_back(taa);
            break; case ta: case taa:
                next.push_back(taa);
                next.push_back(taat);
                next.push_back(tat);
                if (i + 2 < num_groups)
                    next.push_back(tatt);
                next.push_back(a);
                next.push_back(at);
            break; case taat: case tat:
                next.push_back(taa);
                next.push_back(tat);
            break; default: ;
        }
        type = next[rand() % next.size()];
    }

    if (name.find_last_of("hjkmpv") == name.length() - 1)
        name += vowel(vow_type);

    name[0] = latin1_toupper(name[0]);
    return name;
}

string finnish_name(string::size_type max_length) throw () {
    string name;
    do {
        name = make_name();
    } while (name.length() > max_length);
    while (1) {
        const string name2 = make_name();
        if (rand() % 2 && name.length() + name2.length() < max_length)
            name += " " + name2;
        else
            break;
    }
    return trim(name.substr(0, max_length));
}
