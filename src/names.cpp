/*
 *  names.cpp
 *
 *  Copyright (C) 2002 - Renato Hentschke <renato@amok.com.br>
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004 - Niko Ritari
 *  Copyright (C) 2004 - Jani Rivinoja
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

#include <string>
#include "names.h"
#include "utility.h"

using std::string;

int Random(int velho)
{
  return rand()%velho;
}

string Vog()
{
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


string ConsProlSemiVog()
{
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


string PreVog()
{
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

string PosSemiVog()
{
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

string PosConsProl()
{
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

string Silaba()
{
    string retorno;

    retorno = PreVog() + Vog() + PosSemiVog() + PosConsProl();

    return retorno;
}


string Palavra()
{
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

string RandomName(int npal)
{
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

    return trim(nome.substr(0, 15));
}
