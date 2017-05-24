*******************************************************************************
          
    OUTGUN

 A 2D-graphics, 32-player multiplayer, fast-paced capture-the-flag game!

*******************************************************************************
                                                      [ 1.0.3 r336 2008-03-18 ]


-------------------------
Project homepages:
-------------------------

Outgun 1.0 page
   http://koti.mbnet.fi/outgun/1.0/

Outgun website
   http://koti.mbnet.fi/outgun/

Brazilian Outgun website
   http://outgun.sf.net/


-------------------------
Notes for 1.0:
-------------------------

See the HTML help in the doc directory for full documentation.

All available maps aren't used on a server by default. You can select from the
cmaps directory the maps you want to use, and copy them to the maps directory.
Visit http://koti.mbnet.fi/outgun/extras/1.0.html for more maps, and also sound
and graphics themes.

If you are an author of one of the maps on this package, and want your map
removed from the distribution or updated, or the credits changed, contact us at
outgun@mbnet.fi. Also feel free to send us new maps to be included with the
game. Especially good ones. ;)

This game is free software under GNU GPL. See the file 'COPYING' for more
details. Download the sources from http://koti.mbnet.fi/outgun/

There most certainly are at least some bugs in this program, but when a
non-testing version is released we aren't aware of any verified ones. See
http://koti.mbnet.fi/outgun/1.0/known_issues.html for a list of known problems,
including bugs when they are found. If you find a new bug, please report it!
The quality of future versions depends on you.


-------------------------
CREDITS:
-------------------------

Programming for 0.5.0 by
   Fabio "Spinal" Cecin <fcecin@inf.ufrgs.br>
   Random Name Routine(TM) by Renato Hentschke

Programming for 1.0 by
   Niko Ritari (Nix) <npr1@suomi24.fi>
   Jani Rivinoja (Huntta) <janir@mbnet.fi>
   Special player collision effects by Fabio Cecin
   Bots by Peter Kosyh (Gloomy), with modifications by Nix

Graphics themes by
   Jani Rivinoja
   Joonas Rivinoja
   Thomaz de Oliveira dos Reis (ThOR27) <thommy@globo.com>

Fonts by
   Jani Rivinoja

Sound theme by
   Visa-Valtteri Pimiä <visa.pimia@www.fi>

Translations by
   Finnish - Jani Rivinoja and Niko Ritari
   Portuguese (BR) - Thomaz de Oliveira dos Reis
   Italian - Alessandro Ferrentino "Lo Scassatore"

Maps by
   Huntta, Joonas, PHiN, evilKaioh, Flyer, Juggis, Luque, :(, MK-HeadShot, Nix,
   Fabio Cecin, coiote, Devil, Gloomy, Jakke, Nosferatu, ThOR27, and Miague-DH

1.0.0 beta and later bug reports (in decreasing order of bugs reported)
   ThOR27 (very much deserving of his own line),
   Spinal, Caio Monteiro "Nosferatu", Joonas, PHiN, MM-coiote, rFrota,
   Detonador, K4(Maicon), Zigue, Syrus-DH, Miague-DH, SnoOpY

This game uses these libraries:
 * Allegro - http://alleg.sourceforge.net/
 * HawkNL - http://www.hawksoft.com/hawknl/
 * Pthreads-win32 - http://sources.redhat.com/pthreads-win32/

The Windows executable and DLLs have been packed to almost 50% of their
original size using UPX - http://upx.sourceforge.net/

The HawkNL NL.dll shipped with the Windows build of Outgun is modified by Nix
to avoid some problems (sockets that are left open with at least Windows 98 SE,
and "address already in use"). Sources for the modified version are available
at http://koti.mbnet.fi/outgun/HawkNL168src_Nix.zip
