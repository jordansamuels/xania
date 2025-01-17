/*************************************************************************/
/*  Xania (M)ulti(U)ser(D)ungeon server source code                      */
/*  (C) 2021 Xania Development Team                                      */
/*  See merc.h and README for original copyrights                        */
/*************************************************************************/
#pragma once

class Char;

bool parse_gen_groups(Char *ch, const char *argument);
void list_available_group_costs(Char *ch);
unsigned int exp_per_level(const Char *ch, int points);
void check_improve(Char *ch, int sn, bool success, int multiplier);
int group_lookup(const char *name);
void gn_add(Char *ch, int gn);
void gn_remove(Char *ch, int gn);
void group_add(Char *ch, const char *name, bool deduct);
void group_remove(Char *ch, const char *name);
int get_skill_level(const Char *ch, int gsn);
int get_skill_difficulty(Char *ch, int gsn);
int get_skill_trains(Char *ch, int gsn);
int get_group_trains(Char *ch, int gsn);
int get_group_level(Char *ch, int gsn);
