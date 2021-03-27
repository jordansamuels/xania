/*************************************************************************/
/*  Xania (M)ulti(U)ser(D)ungeon server source code                      */
/*  (C) 1995-2000 Xania Development Team                                    */
/*  See the header to file: merc.h for original code copyrights          */
/*                                                                       */
/*  challeng.h: supervised player combat. Originally by Rohan and        */
/*              Wandera. Revised by Oshea 26/8/96                        */
/*************************************************************************/

#pragma once
#include "merc.h"

int fighting_duel(Char *ch, Char *victim);
int in_duel(const Char *ch);
void do_chal_tick();
int do_check_chal(Char *ch);
void raw_kill(Char *victim);
