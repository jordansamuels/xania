/*************************************************************************/
/*  Xania (M)ulti(U)ser(D)ungeon server source code                      */
/*  (C) 1995-2000 Xania Development Team                                    */
/*  See the header to file: merc.h for original code copyrights          */
/*                                                                       */
/*  act_info.c: standard information functions                           */
/*                                                                       */
/*************************************************************************/

#include "Descriptor.hpp"
#include "DescriptorList.hpp"
#include "TimeInfoData.hpp"
#include "WeatherData.hpp"
#include "buffer.h"
#include "comm.hpp"
#include "db.h"
#include "fight.hpp"
#include "handler.hpp"
#include "interp.h"
#include "merc.h"
#include "string_utils.hpp"

#include <fmt/format.h>
#include <range/v3/iterator/operations.hpp>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <range/v3/algorithm/find_if.hpp>
#include <sys/time.h>

using namespace std::literals;
using namespace fmt::literals;

const char *where_name[] = {"<used as light>     ", "<worn on finger>    ", "<worn on finger>    ",
                            "<worn around neck>  ", "<worn around neck>  ", "<worn on body>      ",
                            "<worn on head>      ", "<worn on legs>      ", "<worn on feet>      ",
                            "<worn on hands>     ", "<worn on arms>      ", "<worn as shield>    ",
                            "<worn about body>   ", "<worn about waist>  ", "<worn around wrist> ",
                            "<worn around wrist> ", "<wielded>           ", "<held>              "};

/* for do_count */
size_t max_on = 0;

/*
 * Local functions.
 */
std::string format_obj_to_char(const OBJ_DATA *obj, const CHAR_DATA *ch, bool fShort);
void show_list_to_char(const OBJ_DATA *list, const CHAR_DATA *ch, bool fShort, bool fShowNothing);
void show_char_to_char_0(const CHAR_DATA *victim, const CHAR_DATA *ch);
void show_char_to_char_1(CHAR_DATA *victim, CHAR_DATA *ch);
void show_char_to_char(const CHAR_DATA *list, const CHAR_DATA *ch);
bool check_blind(const CHAR_DATA *ch);

/* Mg's funcy shun */
void set_prompt(CHAR_DATA *ch, const char *prompt);

std::string format_obj_to_char(const OBJ_DATA *obj, const CHAR_DATA *ch, bool fShort) {
    std::string buf;
    if (IS_OBJ_STAT(obj, ITEM_INVIS))
        buf += "(|cInvis|w) ";
    if (ch->has_detect_evil() && IS_OBJ_STAT(obj, ITEM_EVIL))
        buf += "(|rRed Aura|w) ";
    if (ch->has_detect_magic() && IS_OBJ_STAT(obj, ITEM_MAGIC))
        buf += "(|gMagical|w) ";
    if (IS_OBJ_STAT(obj, ITEM_GLOW))
        buf += "(|WGlowing|w) ";
    if (IS_OBJ_STAT(obj, ITEM_HUM))
        buf += "(|yHumming|w) ";

    if (fShort) {
        if (obj->short_descr)
            buf += obj->short_descr;
    } else {
        if (obj->description)
            buf += obj->description;
    }

    if (buf.empty()) {
        buf = "This object has no description. Please inform the IMP.";
        bug("Object %d has no description", obj->pIndexData->vnum);
    }

    return buf;
}

/*
 * Show a list to a character.
 * Can coalesce duplicated items.
 */
void show_list_to_char(const OBJ_DATA *list, const CHAR_DATA *ch, bool fShort, bool fShowNothing) {
    if (!ch->desc)
        return;

    struct DescAndCount {
        std::string desc;
        int count{1};
    };
    std::vector<DescAndCount> to_show;

    const bool show_counts = ch->is_npc() || IS_SET(ch->comm, COMM_COMBINE);

    // Format the list of objects.
    for (auto *obj = list; obj != nullptr; obj = obj->next_content) {
        if (obj->wear_loc == WEAR_NONE && can_see_obj(ch, obj)) {
            auto desc = format_obj_to_char(obj, ch, fShort);
            auto combined_same = false;

            if (show_counts) {
                // Look for duplicates, case sensitive.
                if (auto existing = ranges::find_if(to_show, [&](const auto &x) { return x.desc == desc; });
                    existing != to_show.end()) {
                    existing->count++;
                    combined_same = true;
                }
            }

            // Couldn't combine, or didn't want to.
            if (!combined_same)
                to_show.emplace_back(DescAndCount{std::move(desc)});
        }
    }

    // Output the formatted list.
    std::string buffer;
    auto indent = "     "sv;
    for (const auto &[name, count] : to_show) {
        if (show_counts)
            buffer += count > 1 ? "({:2}) "_format(count) : indent;
        buffer += name + "\n\r";
    }

    if (fShowNothing && to_show.empty()) {
        if (show_counts)
            buffer += indent;
        buffer += "Nothing.\n\r";
    }

    ch->page_to(buffer);
}

void show_char_to_char_0(const CHAR_DATA *victim, const CHAR_DATA *ch) {
    std::string buf;

    if (IS_AFFECTED(victim, AFF_INVISIBLE))
        buf += "(|WInvis|w) ";
    if (victim->is_pc() && IS_SET(victim->act, PLR_WIZINVIS))
        buf += "(|RWizi|w) ";
    if (victim->is_pc() && IS_SET(victim->act, PLR_PROWL))
        buf += "(|RProwl|w) ";
    if (IS_AFFECTED(victim, AFF_HIDE))
        buf += "(|WHide|w) ";
    if (IS_AFFECTED(victim, AFF_CHARM))
        buf += "(|yCharmed|w) ";
    if (IS_AFFECTED(victim, AFF_PASS_DOOR))
        buf += "(|bTranslucent|w) ";
    if (IS_AFFECTED(victim, AFF_FAERIE_FIRE))
        buf += "(|PPink Aura|w) ";
    if (IS_AFFECTED(victim, AFF_OCTARINE_FIRE))
        buf += "(|GOctarine Aura|w) ";
    if (victim->is_evil() && IS_AFFECTED(ch, AFF_DETECT_EVIL))
        buf += "(|rRed Aura|w) ";
    if (IS_AFFECTED(victim, AFF_SANCTUARY))
        buf += "(|WWhite Aura|w) ";
    if (victim->is_pc() && IS_SET(victim->act, PLR_KILLER))
        buf += "(|RKILLER|w) ";
    if (victim->is_pc() && IS_SET(victim->act, PLR_THIEF))
        buf += "(|RTHIEF|w) ";

    if (is_affected(ch, gsn_bless)) {
        if (IS_SET(victim->act, ACT_UNDEAD)) {
            buf += "(|bUndead|w) ";
        }
    }

    if (victim->position == victim->start_pos && !victim->long_descr.empty()) {
        buf += victim->long_descr;
        ch->send_to(buf);
        return;
    }

    buf += pers(victim, ch);
    if (victim->is_pc() && !IS_SET(ch->comm, COMM_BRIEF))
        buf += victim->pcdata->title;

    switch (victim->position) {
    case POS_DEAD: buf += " is |RDEAD|w!!"; break;
    case POS_MORTAL: buf += " is |Rmortally wounded.|w"; break;
    case POS_INCAP: buf += " is |rincapacitated.|w"; break;
    case POS_STUNNED: buf += " is |rlying here stunned.|w"; break;
    case POS_SLEEPING: buf += " is sleeping here."; break;
    case POS_RESTING: buf += " is resting here."; break;
    case POS_SITTING: buf += " is sitting here."; break;
    case POS_STANDING:
        if (victim->riding != nullptr) {
            buf += " is here, riding {}."_format(victim->riding->name);
        } else {
            buf += " is here.";
        }
        break;
    case POS_FIGHTING:
        buf += " is here, fighting ";
        if (victim->fighting == nullptr)
            buf += "thin air??";
        else if (victim->fighting == ch)
            buf += "|RYOU!|w";
        else if (victim->in_room == victim->fighting->in_room) {
            buf += "{}."_format(pers(victim->fighting, ch));
        } else
            buf += "somone who left??";
        break;
    }

    buf += "\n\r";
    buf[0] = UPPER(buf[0]);
    ch->send_to(buf);
}

void show_char_to_char_1(CHAR_DATA *victim, CHAR_DATA *ch) {
    char buf[MAX_STRING_LENGTH];
    char buf2[MAX_STRING_LENGTH];
    OBJ_DATA *obj;
    int iWear;
    bool found;

    if (can_see(victim, ch)) {
        if (ch == victim)
            act("$n looks at $mself.", ch);
        else {
            act("$n looks at you.", ch, nullptr, victim, To::Vict);
            act("$n looks at $N.", ch, nullptr, victim, To::NotVict);
        }
    }

    if (victim->description[0] != '\0') {
        send_to_char(victim->description, ch);
    } else {
        act("You see nothing special about $M.", ch, nullptr, victim, To::Char);
    }

    send_to_char(describe_fight_condition(*victim), ch);

    found = false;
    for (iWear = 0; iWear < MAX_WEAR; iWear++) {
        if ((obj = get_eq_char(victim, iWear)) != nullptr && can_see_obj(ch, obj)) {
            if (!found) {
                send_to_char("\n\r", ch);
                act("$N is using:", ch, nullptr, victim, To::Char);
                found = true;
            }
            if (obj->wear_string != nullptr) {
                snprintf(buf2, sizeof(buf2), "<%s>", obj->wear_string);
                snprintf(buf, sizeof(buf), "%-20s", buf2);
                send_to_char(buf, ch);
            } else {
                send_to_char(where_name[iWear], ch);
            }
            send_to_char(format_obj_to_char(obj, ch, true), ch);
            send_to_char("\n\r", ch);
        }
    }

    if (victim != ch && ch->is_pc() && number_percent() < get_skill_learned(ch, gsn_peek)
        && IS_SET(ch->act, PLR_AUTOPEEK)) {
        send_to_char("\n\rYou peek at the inventory:\n\r", ch);
        check_improve(ch, gsn_peek, true, 4);
        show_list_to_char(victim->carrying, ch, true, true);
    }
}

void do_peek(CHAR_DATA *ch, const char *argument) {
    CHAR_DATA *victim;
    char arg1[MAX_INPUT_LENGTH];

    if (ch->desc == nullptr)
        return;

    if (ch->position < POS_SLEEPING) {
        send_to_char("You can't see anything but stars!\n\r", ch);
        return;
    }

    if (ch->position == POS_SLEEPING) {
        send_to_char("You can't see anything, you're sleeping!\n\r", ch);
        return;
    }

    if (!check_blind(ch))
        return;

    if (ch->is_pc() && !IS_SET(ch->act, PLR_HOLYLIGHT) && room_is_dark(ch->in_room)) {
        send_to_char("It is pitch black ... \n\r", ch);
        show_char_to_char(ch->in_room->people, ch);
        return;
    }

    argument = one_argument(argument, arg1);

    if ((victim = get_char_room(ch, arg1)) != nullptr) {
        if (victim != ch && ch->is_pc() && number_percent() < get_skill_learned(ch, gsn_peek)) {
            send_to_char("\n\rYou peek at their inventory:\n\r", ch);
            check_improve(ch, gsn_peek, true, 4);
            show_list_to_char(victim->carrying, ch, true, true);
        }
    } else
        send_to_char("They aren't here.\n\r", ch);
}

void show_char_to_char(const CHAR_DATA *list, const CHAR_DATA *ch) {
    for (auto *rch = list; rch != nullptr; rch = rch->next_in_room) {
        if (rch == ch)
            continue;

        if (rch->is_pc() && IS_SET(rch->act, PLR_WIZINVIS) && ch->get_trust() < rch->invis_level)
            continue;

        if (can_see(ch, rch)) {
            show_char_to_char_0(rch, ch);
        } else if (room_is_dark(ch->in_room) && IS_AFFECTED(rch, AFF_INFRARED)) {
            send_to_char("You see |Rglowing red|w eyes watching |RYOU!|w\n\r", ch);
        }
    }
}

bool check_blind(const CHAR_DATA *ch) {
    if (!ch->is_blind() || ch->has_holylight())
        return true;

    ch->send_to("You can't see a thing!\n\r");
    return false;
}

/* changes your scroll */
void do_scroll(CHAR_DATA *ch, const char *argument) {
    char arg[MAX_INPUT_LENGTH];
    char buf[100];
    int lines;

    one_argument(argument, arg);

    if (arg[0] == '\0') {
        if (ch->lines == 0) {
            send_to_char("|cPaging is set to maximum.|w\n\r", ch);
            ch->lines = 52;
        } else {
            snprintf(buf, sizeof(buf), "|cYou currently display %d lines per page.|w\n\r", ch->lines + 2);
            send_to_char(buf, ch);
        }
        return;
    }

    if (!is_number(arg)) {
        send_to_char("|cYou must provide a number.|w\n\r", ch);
        return;
    }

    lines = atoi(arg);

    if (lines == 0) {
        send_to_char("|cPaging at maximum.|w\n\r", ch);
        ch->lines = 52;
        return;
    }

    /* Pager limited to 52 due to memory complaints relating to
     * buffer code ...short term fix :) --Faramir
     */

    if (lines < 10 || lines > 52) {
        send_to_char("|cYou must provide a reasonable number.|w\n\r", ch);
        return;
    }

    snprintf(buf, sizeof(buf), "|cScroll set to %d lines.|w\n\r", lines);
    send_to_char(buf, ch);
    ch->lines = lines - 2;
}

/* RT does socials */
void do_socials(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    char buf[MAX_STRING_LENGTH];
    int iSocial;
    int col;

    col = 0;

    for (iSocial = 0; social_table[iSocial].name[0] != '\0'; iSocial++) {
        snprintf(buf, sizeof(buf), "%-12s", social_table[iSocial].name);
        send_to_char(buf, ch);
        if (++col % 6 == 0)
            send_to_char("\n\r", ch);
    }

    if (col % 6 != 0)
        send_to_char("\n\r", ch);
}

/* RT Commands to replace news, motd, imotd, etc from ROM */

void do_motd(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    do_help(ch, "motd");
}

void do_imotd(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    do_help(ch, "imotd");
}

void do_rules(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    do_help(ch, "rules");
}

void do_story(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    do_help(ch, "story");
}

void do_changes(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    do_help(ch, "changes");
}

void do_wizlist(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    do_help(ch, "wizlist");
}

/* RT this following section holds all the auto commands from ROM, as well as
   replacements for config */

void do_autolist(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    /* lists most player flags */
    if (ch->is_npc())
        return;

    send_to_char("   action     status\n\r", ch);
    send_to_char("---------------------\n\r", ch);

    send_to_char("ANSI colour    ", ch);
    if (ch->pcdata->colour)
        send_to_char("|RON|w\n\r", ch);
    else
        send_to_char("|ROFF|w\n\r", ch);

    send_to_char("autoaffect     ", ch);
    if (IS_SET(ch->comm, COMM_AFFECT))
        send_to_char("|RON|w\n\r", ch);
    else
        send_to_char("|ROFF|w\n\r", ch);

    send_to_char("autoassist     ", ch);
    if (IS_SET(ch->act, PLR_AUTOASSIST))
        send_to_char("|RON|w\n\r", ch);
    else
        send_to_char("|ROFF|w\n\r", ch);

    send_to_char("autoexit       ", ch);
    if (IS_SET(ch->act, PLR_AUTOEXIT))
        send_to_char("|RON|w\n\r", ch);
    else
        send_to_char("|ROFF|w\n\r", ch);

    send_to_char("autogold       ", ch);
    if (IS_SET(ch->act, PLR_AUTOGOLD))
        send_to_char("|RON|w\n\r", ch);
    else
        send_to_char("|ROFF|w\n\r", ch);

    send_to_char("autoloot       ", ch);
    if (IS_SET(ch->act, PLR_AUTOLOOT))
        send_to_char("|RON|w\n\r", ch);
    else
        send_to_char("|ROFF|w\n\r", ch);

    send_to_char("autopeek       ", ch);
    if (IS_SET(ch->act, PLR_AUTOPEEK))
        send_to_char("|RON|w\n\r", ch);
    else
        send_to_char("|ROFF|w\n\r", ch);

    send_to_char("autosac        ", ch);
    if (IS_SET(ch->act, PLR_AUTOSAC))
        send_to_char("|RON|w\n\r", ch);
    else
        send_to_char("|ROFF|w\n\r", ch);

    send_to_char("autosplit      ", ch);
    if (IS_SET(ch->act, PLR_AUTOSPLIT))
        send_to_char("|RON|w\n\r", ch);
    else
        send_to_char("|ROFF|w\n\r", ch);

    send_to_char("prompt         ", ch);
    if (IS_SET(ch->comm, COMM_PROMPT))
        send_to_char("|RON|w\n\r", ch);
    else
        send_to_char("|ROFF|w\n\r", ch);

    send_to_char("combine items  ", ch);
    if (IS_SET(ch->comm, COMM_COMBINE))
        send_to_char("|RON|w\n\r", ch);
    else
        send_to_char("|ROFF|w\n\r", ch);

    if (!IS_SET(ch->act, PLR_CANLOOT))
        send_to_char("Your corpse is safe from thieves.\n\r", ch);
    else
        send_to_char("Your corpse may be looted.\n\r", ch);

    if (IS_SET(ch->act, PLR_NOSUMMON))
        send_to_char("You cannot be summoned.\n\r", ch);
    else
        send_to_char("You can be summoned.\n\r", ch);

    if (IS_SET(ch->act, PLR_NOFOLLOW))
        send_to_char("You do not welcome followers.\n\r", ch);
    else
        send_to_char("You accept followers.\n\r", ch);

    if (IS_SET(ch->comm, COMM_BRIEF))
        send_to_char("Only brief descriptions are being shown.\n\r", ch);
    else
        send_to_char("Full descriptions are being shown.\n\r", ch);

    if (IS_SET(ch->comm, COMM_COMPACT))
        send_to_char("Compact mode is set.\n\r", ch);
    else
        send_to_char("Compact mode is not set.\n\r", ch);

    if (IS_SET(ch->comm, COMM_SHOWAFK))
        send_to_char("Messages sent to you will be shown when afk.\n\r", ch);
    else
        send_to_char("Messages sent to you will not be shown when afk.\n\r", ch);

    if (IS_SET(ch->comm, COMM_SHOWDEFENCE))
        send_to_char("Shield blocks, parries, and dodges are being shown.\n\r", ch);
    else
        send_to_char("Shield blocks, parries, and dodges are not being shown.\n\r", ch);
}

void do_autoaffect(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    if (ch->is_npc())
        return;

    if (IS_SET(ch->comm, COMM_AFFECT)) {
        send_to_char("Autoaffect removed.\n\r", ch);
        REMOVE_BIT(ch->comm, COMM_AFFECT);
    } else {
        send_to_char("Affects will now be shown in score.\n\r", ch);
        SET_BIT(ch->comm, COMM_AFFECT);
    }
}
void do_autoassist(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    if (ch->is_npc())
        return;

    if (IS_SET(ch->act, PLR_AUTOASSIST)) {
        send_to_char("Autoassist removed.\n\r", ch);
        REMOVE_BIT(ch->act, PLR_AUTOASSIST);
    } else {
        send_to_char("You will now assist when needed.\n\r", ch);
        SET_BIT(ch->act, PLR_AUTOASSIST);
    }
}

void do_autoexit(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    if (ch->is_npc())
        return;

    if (IS_SET(ch->act, PLR_AUTOEXIT)) {
        send_to_char("Exits will no longer be displayed.\n\r", ch);
        REMOVE_BIT(ch->act, PLR_AUTOEXIT);
    } else {
        send_to_char("Exits will now be displayed.\n\r", ch);
        SET_BIT(ch->act, PLR_AUTOEXIT);
    }
}

void do_autogold(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    if (ch->is_npc())
        return;

    if (IS_SET(ch->act, PLR_AUTOGOLD)) {
        send_to_char("Autogold removed.\n\r", ch);
        REMOVE_BIT(ch->act, PLR_AUTOGOLD);
    } else {
        send_to_char("Automatic gold looting set.\n\r", ch);
        SET_BIT(ch->act, PLR_AUTOGOLD);
    }
}

void do_autoloot(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    if (ch->is_npc())
        return;

    if (IS_SET(ch->act, PLR_AUTOLOOT)) {
        send_to_char("Autolooting removed.\n\r", ch);
        REMOVE_BIT(ch->act, PLR_AUTOLOOT);
    } else {
        send_to_char("Automatic corpse looting set.\n\r", ch);
        SET_BIT(ch->act, PLR_AUTOLOOT);
    }
}

void do_autopeek(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    if (ch->is_npc())
        return;

    if (IS_SET(ch->act, PLR_AUTOPEEK)) {
        send_to_char("Autopeeking removed.\n\r", ch);
        REMOVE_BIT(ch->act, PLR_AUTOPEEK);
    } else {
        send_to_char("Automatic peeking set.\n\r", ch);
        SET_BIT(ch->act, PLR_AUTOPEEK);
    }
}

void do_autosac(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    if (ch->is_npc())
        return;

    if (IS_SET(ch->act, PLR_AUTOSAC)) {
        send_to_char("Autosacrificing removed.\n\r", ch);
        REMOVE_BIT(ch->act, PLR_AUTOSAC);
    } else {
        send_to_char("Automatic corpse sacrificing set.\n\r", ch);
        SET_BIT(ch->act, PLR_AUTOSAC);
    }
}

void do_autosplit(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    if (ch->is_npc())
        return;

    if (IS_SET(ch->act, PLR_AUTOSPLIT)) {
        send_to_char("Autosplitting removed.\n\r", ch);
        REMOVE_BIT(ch->act, PLR_AUTOSPLIT);
    } else {
        send_to_char("Automatic gold splitting set.\n\r", ch);
        SET_BIT(ch->act, PLR_AUTOSPLIT);
    }
}

void do_brief(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    if (IS_SET(ch->comm, COMM_BRIEF)) {
        send_to_char("Full descriptions activated.\n\r", ch);
        REMOVE_BIT(ch->comm, COMM_BRIEF);
    } else {
        send_to_char("Short descriptions activated.\n\r", ch);
        SET_BIT(ch->comm, COMM_BRIEF);
    }
}

void do_colour(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    if (ch->is_npc())
        return;

    if (ch->pcdata->colour) {
        send_to_char("You feel less COLOURFUL.\n\r", ch);

        ch->pcdata->colour = false;
    } else {
        ch->pcdata->colour = true;
        send_to_char("You feel more |RC|GO|BL|rO|gU|bR|cF|YU|PL|W!.|w\n\r", ch);
    }
}

void do_showafk(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    if (IS_SET(ch->comm, COMM_SHOWAFK)) {
        send_to_char("Messages sent to you will now not be shown when afk.\n\r", ch);
        REMOVE_BIT(ch->comm, COMM_SHOWAFK);
    } else {
        send_to_char("Messages sent to you will now be shown when afk.\n\r", ch);
        SET_BIT(ch->comm, COMM_SHOWAFK);
    }
}
void do_showdefence(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    if (IS_SET(ch->comm, COMM_SHOWDEFENCE)) {
        send_to_char("Shield blocks, parries and dodges will not be shown during combat.\n\r", ch);
        REMOVE_BIT(ch->comm, COMM_SHOWDEFENCE);
    } else {
        send_to_char("Shield blocks, parries and dodges will be shown during combat.\n\r", ch);
        SET_BIT(ch->comm, COMM_SHOWDEFENCE);
    }
}

void do_compact(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    if (IS_SET(ch->comm, COMM_COMPACT)) {
        send_to_char("Compact mode removed.\n\r", ch);
        REMOVE_BIT(ch->comm, COMM_COMPACT);
    } else {
        send_to_char("Compact mode set.\n\r", ch);
        SET_BIT(ch->comm, COMM_COMPACT);
    }
}

void do_prompt(CHAR_DATA *ch, const char *argument) {

    /* PCFN 24-05-97  Oh dear - it seems that you can't set prompt while switched
       into a MOB.  Let's change that.... */
    if (ch = ch->player(); !ch)
        return;

    if (str_cmp(argument, "off") == 0) {
        send_to_char("You will no longer see prompts.\n\r", ch);
        REMOVE_BIT(ch->comm, COMM_PROMPT);
        return;
    }
    if (str_cmp(argument, "on") == 0) {
        send_to_char("You will now see prompts.\n\r", ch);
        SET_BIT(ch->comm, COMM_PROMPT);
        return;
    }

    /* okay that was the old stuff */
    set_prompt(ch, smash_tilde(argument).c_str());
    send_to_char("Ok - prompt set.\n\r", ch);
    SET_BIT(ch->comm, COMM_PROMPT);
}

void do_combine(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    if (IS_SET(ch->comm, COMM_COMBINE)) {
        send_to_char("Long inventory selected.\n\r", ch);
        REMOVE_BIT(ch->comm, COMM_COMBINE);
    } else {
        send_to_char("Combined inventory selected.\n\r", ch);
        SET_BIT(ch->comm, COMM_COMBINE);
    }
}

void do_noloot(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    if (ch->is_npc())
        return;

    if (IS_SET(ch->act, PLR_CANLOOT)) {
        send_to_char("Your corpse is now safe from thieves.\n\r", ch);
        REMOVE_BIT(ch->act, PLR_CANLOOT);
    } else {
        send_to_char("Your corpse may now be looted.\n\r", ch);
        SET_BIT(ch->act, PLR_CANLOOT);
    }
}

void do_nofollow(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    if (ch->is_npc())
        return;

    if (IS_SET(ch->act, PLR_NOFOLLOW)) {
        send_to_char("You now accept followers.\n\r", ch);
        REMOVE_BIT(ch->act, PLR_NOFOLLOW);
    } else {
        send_to_char("You no longer accept followers.\n\r", ch);
        SET_BIT(ch->act, PLR_NOFOLLOW);
        die_follower(ch);
    }
}

void do_nosummon(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    if (ch->is_npc()) {
        if (IS_SET(ch->imm_flags, IMM_SUMMON)) {
            send_to_char("You are no longer immune to summon.\n\r", ch);
            REMOVE_BIT(ch->imm_flags, IMM_SUMMON);
        } else {
            send_to_char("You are now immune to summoning.\n\r", ch);
            SET_BIT(ch->imm_flags, IMM_SUMMON);
        }
    } else {
        if (IS_SET(ch->act, PLR_NOSUMMON)) {
            send_to_char("You are no longer immune to summon.\n\r", ch);
            REMOVE_BIT(ch->act, PLR_NOSUMMON);
        } else {
            send_to_char("You are now immune to summoning.\n\r", ch);
            SET_BIT(ch->act, PLR_NOSUMMON);
        }
    }
}

void do_lore(CHAR_DATA *ch, OBJ_DATA *obj, const char *pdesc) {
    int sn = skill_lookup("identify");

    if (ch->is_pc() && number_percent() > get_skill_learned(ch, skill_lookup("lore"))) {
        send_to_char(pdesc, ch);
        check_improve(ch, gsn_lore, false, 1);
    } else {
        if (ch->is_mortal())
            WAIT_STATE(ch, skill_table[sn].beats);
        send_to_char(pdesc, ch);
        check_improve(ch, gsn_lore, true, 1);
        (*skill_table[sn].spell_fun)(sn, ch->level, ch, (void *)obj);
    }
}

namespace {

void room_look(const CHAR_DATA &ch, bool force_full) {
    ch.send_to("|R{}\n\r|w"_format(ch.in_room->name));
    if (force_full || !ch.is_comm_brief()) {
        ch.send_to("  {}"_format(ch.in_room->description));
    }

    if (ch.should_autoexit()) {
        ch.send_to("\n\r");
        do_exits(&ch, "auto");
    }

    show_list_to_char(ch.in_room->contents, &ch, false, false);
    show_char_to_char(ch.in_room->people, &ch);
}

void look_in_object(const CHAR_DATA &ch, const OBJ_DATA &obj) {
    switch (obj.item_type) {
    default: ch.send_to("That is not a container.\n\r"); break;

    case ITEM_DRINK_CON:
        if (obj.value[1] <= 0) {
            ch.send_to("It is empty.\n\r");
            break;
        }

        ch.send_to("It's {} full of a {} liquid.\n\r"_format(
            obj.value[1] < obj.value[0] / 4 ? "less than" : obj.value[1] < 3 * obj.value[0] / 4 ? "about" : "more than",
            liq_table[obj.value[2]].liq_color));
        break;

    case ITEM_CONTAINER:
    case ITEM_CORPSE_NPC:
    case ITEM_CORPSE_PC:
        if (IS_SET(obj.value[1], CONT_CLOSED)) {
            ch.send_to("It is closed.\n\r");
            break;
        }

        act("$p contains:", &ch, &obj, nullptr, To::Char);
        show_list_to_char(obj.contains, &ch, true, true);
        break;
    }
}

const char *try_get_descr(const OBJ_DATA &obj, std::string_view name) {
    if (auto *pdesc = get_extra_descr(name, obj.extra_descr))
        return pdesc;
    if (auto *pdesc = get_extra_descr(name, obj.pIndexData->extra_descr))
        return pdesc;
    return nullptr;
}

bool handled_as_look_at_object(CHAR_DATA &ch, std::string_view first_arg) {
    auto &&[number, obj_desc] = number_argument(first_arg);
    const auto sn = skill_lookup("lore");
    int count = 0;
    for (auto *obj = ch.carrying; obj; obj = obj->next_content) {
        if (!ch.can_see(*obj))
            continue;
        if (auto *pdesc = try_get_descr(*obj, obj_desc)) {
            if (++count == number) {
                if (sn < 0 || (ch.is_pc() && ch.level < ch.get_skill(sn))) {
                    ch.send_to(pdesc);
                    return true;
                } else {
                    do_lore(&ch, obj, pdesc);
                    return true;
                }
            } else
                continue;
        } else if (is_name(obj_desc, obj->name)) {
            if (++count == number) {
                ch.send_to("{}\n\r"_format(obj->description));
                do_lore(&ch, obj, "");
                return true;
            }
        }
    }

    for (auto *obj = ch.in_room->contents; obj; obj = obj->next_content) {
        if (!ch.can_see(*obj))
            continue;
        if (auto *pdesc = try_get_descr(*obj, obj_desc)) {
            if (++count == number) {
                ch.send_to(pdesc);
                return true;
            }
        }
        if (is_name(obj_desc, obj->name))
            if (++count == number) {
                ch.send_to("{}\n\r"_format(obj->description));
                return true;
            }
    }

    if (count > 0 && count != number) {
        if (count == 1)
            ch.send_to("You only see one {} here.\n\r"_format(obj_desc));
        else
            ch.send_to("You only see {} {}s here.\n\r"_format(count, obj_desc));
        return true;
    }
    return false;
}

void look_direction(const CHAR_DATA &ch, Direction door) {
    const auto *pexit = ch.in_room->exit[door];
    if (!pexit) {
        ch.send_to("Nothing special there.\n\r");
        return;
    }

    if (pexit->description && pexit->description[0] != '\0')
        ch.send_to(pexit->description);
    else
        ch.send_to("Nothing special there.\n\r");

    if (pexit->keyword && pexit->keyword[0] != '\0' && pexit->keyword[0] != ' ') {
        if (IS_SET(pexit->exit_info, EX_CLOSED)) {
            act("The $d is closed.", &ch, nullptr, pexit->keyword, To::Char);
        } else if (IS_SET(pexit->exit_info, EX_ISDOOR)) {
            act("The $d is open.", &ch, nullptr, pexit->keyword, To::Char);
        }
    }
}

}

void do_look(CHAR_DATA *ch, const char *arguments) {
    if (ch->desc == nullptr)
        return;

    if (ch->position < POS_SLEEPING) {
        ch->send_to("You can't see anything but stars!\n\r");
        return;
    }

    if (ch->position == POS_SLEEPING) {
        ch->send_to("You can't see anything, you're sleeping!\n\r");
        return;
    }

    if (!check_blind(ch))
        return;

    if (!ch->has_holylight() && room_is_dark(ch->in_room)) {
        ch->send_to("It is pitch black ... \n\r");
        show_char_to_char(ch->in_room->people, ch);
        return;
    }

    ArgParser args(arguments);
    auto first_arg = args.shift();

    // A normal look, or a look auto to describe the room?
    if (first_arg.empty() || matches(first_arg, "auto")) {
        room_look(*ch, first_arg.empty());
        return;
    }

    // Look in something?
    if (matches_start(first_arg, "in")) {
        if (args.empty()) {
            send_to_char("Look in what?\n\r", ch);
            return;
        }
        if (auto *obj = get_obj_here(ch, args.shift()))
            look_in_object(*ch, *obj);
        else
            send_to_char("You do not see that here.\n\r", ch);
        return;
    }

    // Look at a person?
    if (auto *victim = get_char_room(ch, first_arg)) {
        show_char_to_char_1(victim, ch);
        return;
    }

    // Look at an object?
    if (handled_as_look_at_object(*ch, first_arg))
        return;

    // Look at something in the extra description of the room?
    if (auto *pdesc = get_extra_descr(first_arg, ch->in_room->extra_descr)) {
        ch->send_to(pdesc);
        return;
    }

    // Look in a direction?
    if (auto opt_door = try_parse_direction(first_arg)) {
        look_direction(*ch, *opt_door);
        return;
    }

    ch->send_to("You do not see that here.\n\r");
}

/* RT added back for the hell of it */
void do_read(CHAR_DATA *ch, const char *argument) { do_look(ch, argument); }

void do_examine(CHAR_DATA *ch, const char *argument) {
    char buf[MAX_STRING_LENGTH];
    char arg[MAX_INPUT_LENGTH];
    OBJ_DATA *obj;

    one_argument(argument, arg);

    if (arg[0] == '\0') {
        send_to_char("Examine what?\n\r", ch);
        return;
    }

    do_look(ch, arg);

    if ((obj = get_obj_here(ch, arg)) != nullptr) {
        switch (obj->item_type) {
        default: break;

        case ITEM_DRINK_CON:
        case ITEM_CONTAINER:
        case ITEM_CORPSE_NPC:
        case ITEM_CORPSE_PC:
            send_to_char("When you look inside, you see:\n\r", ch);
            snprintf(buf, sizeof(buf), "in %s", arg);
            do_look(ch, buf);
        }
    }
}

/*
 * Thanks to Zrin for auto-exit part.
 */
void do_exits(const CHAR_DATA *ch, const char *argument) {

    auto fAuto = matches(argument, "auto");

    if (!check_blind(ch))
        return;

    std::string buf = fAuto ? "|W[Exits:" : "Obvious exits:\n\r";

    auto found = false;
    for (auto door : all_directions) {
        if (auto *pexit = ch->in_room->exit[door];
            pexit && pexit->u1.to_room && can_see_room(ch, pexit->u1.to_room) && !IS_SET(pexit->exit_info, EX_CLOSED)) {
            found = true;
            if (fAuto) {
                buf += " {}"_format(to_string(door));
            } else {
                buf += "{:<5} - {}\n\r"_format(capitalize(to_string(door)), room_is_dark(pexit->u1.to_room)
                                                                                ? "Too dark to tell"
                                                                                : pexit->u1.to_room->name);
            }
        }
    }

    if (!found)
        buf += fAuto ? " none" : "None.\n\r";

    if (fAuto)
        buf += "]\n\r|w";

    ch->send_to(buf);
}

void do_worth(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    char buf[MAX_STRING_LENGTH];

    if (ch->is_npc()) {
        snprintf(buf, sizeof(buf), "You have %d gold.\n\r", (int)ch->gold);
        send_to_char(buf, ch);
        return;
    }

    snprintf(buf, sizeof(buf), "You have %d gold, and %d experience (%d exp to level).\n\r", (int)ch->gold,
             (int)ch->exp, (int)((ch->level + 1) * exp_per_level(ch, ch->pcdata->points) - ch->exp));

    send_to_char(buf, ch);
}

int final_len(char *string) {
    int count = 0;
    while (*string) {
        if (*string == '|') {
            string++;
            if (*string == '|') {
                count++;
                string++;
            } else {
                if (*string)
                    string++;
            }
        } else {
            count++;
            string++;
        }
    }
    return count;
}

#define SC_COLWIDTH 24

char *next_column(char *buf, int col_width) {
    int eff_len = final_len(buf);
    int len = strlen(buf);
    int num_spaces = (eff_len < col_width) ? (col_width - eff_len) : 1;
    memset(buf + len, ' ', num_spaces);
    return buf + len + num_spaces;
}

const char *position_desc[] = {"dead",    "mortally wounded", "incapacitated", "stunned", "sleeping",
                               "resting", "sitting",          "fighting",      "standing"};

const char *armour_desc[] = {"divinely armoured against", "almost invulnerable to",     "superbly armoured against",
                             "heavily armoured against",  "very well-armoured against", "well-armoured against",
                             "armoured against",          "somewhat armoured against",  "slightly armoured against",
                             "barely protected from",     "defenseless against",        "hopelessly vulnerable to"};

void describe_armour(CHAR_DATA *ch, int type, const char *name) {
    char buf[MAX_STRING_LENGTH];
    int armour_index = (GET_AC(ch, type) + 120) / 20;

    if (armour_index < 0)
        armour_index = 0;
    if (armour_index > 11)
        armour_index = 11;

    if (ch->level < 25)
        snprintf(buf, sizeof(buf), "You are |y%s |W%s|w.\n\r", armour_desc[armour_index], name);
    else
        snprintf(buf, sizeof(buf), "You are |y%s |W%s|w. (|W%d|w)\n\r", armour_desc[armour_index], name,
                 GET_AC(ch, type));
    send_to_char(buf, ch);
}

static void describe_condition(CHAR_DATA *ch) {
    char buf[MAX_STRING_LENGTH];
    int drunk = (ch->pcdata->condition[COND_DRUNK] > 10);
    int hungry = (ch->pcdata->condition[COND_FULL] == 0);
    int thirsty = (ch->pcdata->condition[COND_THIRST] == 0);
    static const char *delimiters[] = {"", " and ", ", "};

    if (!drunk && !hungry && !thirsty)
        return;
    snprintf(buf, sizeof(buf), "You are %s%s%s%s%s.\n\r", drunk ? "|Wdrunk|w" : "",
             drunk ? delimiters[hungry + thirsty] : "", hungry ? "|Whungry|w" : "", (thirsty && hungry) ? " and " : "",
             thirsty ? "|Wthirsty|w" : "");
    send_to_char(buf, ch);
}

static const char *align_descriptions[] = {"|Rsatanic", "|Rdemonic", "|Yevil",    "|Ymean",   "|Mneutral",
                                           "|Gkind",    "|Ggood",    "|Wsaintly", "|Wangelic"};

const char *get_align_description(int align) {
    int index = (align + 1000) * 8 / 2000;
    if (index < 0) /* Let's be paranoid, eh? */
        index = 0;
    if (index > 8)
        index = 8;
    return align_descriptions[index];
}

void do_score(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    char buf[MAX_STRING_LENGTH];

    ch->send_to("|wYou are: |W{}{}|w.\n\r"_format(ch->name, ch->is_npc() ? "" : ch->pcdata->title));

    if (ch->get_trust() == ch->level)
        snprintf(buf, sizeof(buf), "Level: |W%d|w", ch->level);
    else
        snprintf(buf, sizeof(buf), "Level: |W%d|w (trust |W%d|w)", ch->level, ch->get_trust());
    using namespace std::chrono;
    snprintf(next_column(buf, SC_COLWIDTH), sizeof(buf), "Age: |W%d|w years (|W%ld|w hours)\n\r", get_age(ch),
             duration_cast<hours>(ch->total_played()).count());
    send_to_char(buf, ch);

    snprintf(buf, sizeof(buf), "Race: |W%s|w", race_table[ch->race].name);
    snprintf(next_column(buf, SC_COLWIDTH), sizeof(buf), "Class: |W%s|w\n\r",
             ch->is_npc() ? "mobile" : class_table[ch->class_num].name);
    send_to_char(buf, ch);

    snprintf(buf, sizeof(buf), "Sex: |W%s|w", ch->sex == 0 ? "sexless" : ch->sex == 1 ? "male" : "female");
    snprintf(next_column(buf, SC_COLWIDTH), sizeof(buf), "Position: |W%s|w\n\r", position_desc[ch->position]);
    send_to_char(buf, ch);

    snprintf(buf, sizeof(buf), "Items: |W%d|w / %d", ch->carry_number, can_carry_n(ch));
    snprintf(next_column(buf, SC_COLWIDTH), sizeof(buf), "Weight: |W%d|w / %d\n\r", ch->carry_weight, can_carry_w(ch));
    send_to_char(buf, ch);

    snprintf(buf, sizeof(buf), "Gold: |W%d|w\n\r", (int)ch->gold);
    send_to_char(buf, ch);

    snprintf(buf, sizeof(buf), "Wimpy: |W%d|w", ch->wimpy);
    if (ch->is_pc() && ch->level < LEVEL_HERO)
        snprintf(next_column(buf, SC_COLWIDTH), sizeof(buf), "Score: |W%d|w (|W%d|w to next level)\n\r", (int)ch->exp,
                 (unsigned int)((ch->level + 1) * exp_per_level(ch, ch->pcdata->points) - ch->exp));
    else
        snprintf(next_column(buf, SC_COLWIDTH), sizeof(buf), "Score: |W%d|w\n\r", (int)ch->exp);
    send_to_char(buf, ch);
    send_to_char("\n\r", ch);

    snprintf(buf, sizeof(buf), "Hit: |W%d|w / %d", ch->hit, ch->max_hit);
    snprintf(next_column(buf, SC_COLWIDTH), sizeof(buf), "Mana: |W%d|w / %d", ch->mana, ch->max_mana);
    snprintf(next_column(buf, 2 * SC_COLWIDTH), sizeof(buf), "Move: |W%d|w / %d\n\r", ch->move, ch->max_move);
    send_to_char(buf, ch);

    describe_armour(ch, AC_PIERCE, "piercing");
    describe_armour(ch, AC_BASH, "bashing");
    describe_armour(ch, AC_SLASH, "slashing");
    describe_armour(ch, AC_EXOTIC, "magic");

    if (ch->level >= 15) {
        snprintf(buf, sizeof(buf), "Hit roll: |W%d|w", GET_HITROLL(ch));
        snprintf(next_column(buf, SC_COLWIDTH), sizeof(buf), "Damage roll: |W%d|w\n\r", GET_DAMROLL(ch));
        send_to_char(buf, ch);
    }
    send_to_char("\n\r", ch);

    snprintf(buf, sizeof(buf), "Strength: %d (|W%d|w)", ch->perm_stat[Stat::Str], get_curr_stat(ch, Stat::Str));
    snprintf(next_column(buf, SC_COLWIDTH), sizeof(buf), "Intelligence: %d (|W%d|w)", ch->perm_stat[Stat::Int],
             get_curr_stat(ch, Stat::Int));
    snprintf(next_column(buf, 2 * SC_COLWIDTH), sizeof(buf), "Wisdom: %d (|W%d|w)\n\r", ch->perm_stat[Stat::Wis],
             get_curr_stat(ch, Stat::Wis));
    send_to_char(buf, ch);

    snprintf(buf, sizeof(buf), "Dexterity: %d (|W%d|w)", ch->perm_stat[Stat::Dex], get_curr_stat(ch, Stat::Dex));
    snprintf(next_column(buf, SC_COLWIDTH), sizeof(buf), "Constitution: %d (|W%d|w)\n\r", ch->perm_stat[Stat::Con],
             get_curr_stat(ch, Stat::Con));
    send_to_char(buf, ch);

    snprintf(buf, sizeof(buf), "Practices: |W%d|w", ch->practice);
    snprintf(next_column(buf, SC_COLWIDTH), sizeof(buf), "Training sessions: |W%d|w\n\r", ch->train);
    send_to_char(buf, ch);

    if (ch->level >= 10)
        snprintf(buf, sizeof(buf), "You are %s|w (alignment |W%d|w).\n\r", get_align_description(ch->alignment),
                 ch->alignment);
    else
        snprintf(buf, sizeof(buf), "You are %s|w.\n\r", get_align_description(ch->alignment));
    send_to_char(buf, ch);

    if (ch->is_pc())
        describe_condition(ch);

    if (ch->is_immortal()) {
        send_to_char("\n\r", ch);
        snprintf(buf, sizeof(buf), "Holy light: |W%s|w", IS_SET(ch->act, PLR_HOLYLIGHT) ? "on" : "off");
        if (IS_SET(ch->act, PLR_WIZINVIS)) {
            snprintf(next_column(buf, SC_COLWIDTH), sizeof(buf), "Invisible: |W%d|w", ch->invis_level);
        }
        if (IS_SET(ch->act, PLR_PROWL)) {
            snprintf(next_column(buf, SC_COLWIDTH * (IS_SET(ch->act, PLR_WIZINVIS) ? 2 : 1)), sizeof(buf),
                     "Prowl: |W%d|w", ch->invis_level);
        }
        strcat(buf, "\n\r");
        send_to_char(buf, ch);
    }

    if (IS_SET(ch->comm, COMM_AFFECT)) {
        send_to_char("\n\r", ch);
        do_affected(ch, nullptr);
    }
}

void do_affected(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    char buf[MAX_STRING_LENGTH];
    AFFECT_DATA *paf;
    int flag = 0;

    send_to_char("You are affected by:\n\r", ch);

    if (ch->affected != nullptr) {
        for (paf = ch->affected; paf != nullptr; paf = paf->next) {
            if ((paf->type == gsn_sneak) || (paf->type == gsn_ride)) {
                snprintf(buf, sizeof(buf), "Skill: '%s'", skill_table[paf->type].name);
                flag = 1;
            } else {
                snprintf(buf, sizeof(buf), "Spell: '%s'", skill_table[paf->type].name);
                flag = 0;
            }
            send_to_char(buf, ch);

            if (ch->level >= 20) {
                if (flag == 0) {
                    snprintf(buf, sizeof(buf), " modifies %s by %d for %d hours", affect_loc_name(paf->location),
                             paf->modifier, paf->duration);
                    send_to_char(buf, ch);
                } else {
                    snprintf(buf, sizeof(buf), " modifies %s by %d", affect_loc_name(paf->location), paf->modifier);
                    send_to_char(buf, ch);
                }
            }

            send_to_char(".\n\r", ch);
        }
    } else {
        send_to_char("Nothing.\n\r", ch);
    }
}

void do_time(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    char buf[MAX_STRING_LENGTH];

    // TODO(#134) this whole thing should use the user's TZ.
    send_to_char("{}\n\rXania started up at {}Z.\n\rThe system time is {}Z.\n\r"_format(
                     time_info.describe(), secs_only(boot_time), secs_only(current_time)),
                 ch);

    if (ch = ch->player(); !ch)
        return;

    // TODO(#95) now we have an actual time library we can replace this with a timezone and format accordingly.
    if (ch->pcdata->houroffset || ch->pcdata->minoffset) {
        time_t ch_timet;
        char buf2[32];
        struct tm *ch_time;

        ch_timet = time(0);

        ch_time = gmtime(&ch_timet);
        ch_time->tm_min += ch->pcdata->minoffset;
        ch_time->tm_hour += ch->pcdata->houroffset;

        ch_time->tm_hour -= (ch_time->tm_min / 60);
        ch_time->tm_min = (ch_time->tm_min % 60);
        if (ch_time->tm_min < 0) {
            ch_time->tm_min += 60;
            ch_time->tm_hour -= 1;
        }
        ch_time->tm_hour = (ch_time->tm_hour % 24);
        if (ch_time->tm_hour < 0)
            ch_time->tm_hour += 24;

        strftime(buf2, 63, "%H:%M:%S", ch_time);

        snprintf(buf, sizeof(buf), "Your local time is %s\n\r", buf2);
        send_to_char(buf, ch);
    }
}

void do_weather(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    if (!IS_OUTSIDE(ch)) {
        send_to_char("You can't see the weather indoors.\n\r", ch);
        return;
    }

    send_to_char(weather_info.describe() + "\n\r", ch);
}

void do_help(CHAR_DATA *ch, const char *argument) {
    HELP_DATA *pHelp;
    char argall[MAX_INPUT_LENGTH], argone[MAX_INPUT_LENGTH];

    if (argument[0] == '\0')
        argument = "summary";

    /* this parts handles help a b so that it returns help 'a b' */
    argall[0] = '\0';
    while (argument[0] != '\0') {
        argument = one_argument(argument, argone);
        if (argall[0] != '\0')
            strcat(argall, " ");
        strcat(argall, argone);
    }

    for (pHelp = help_first; pHelp != nullptr; pHelp = pHelp->next) {
        if (pHelp->level > ch->get_trust())
            continue;

        if (is_name(argall, pHelp->keyword)) {
            if (pHelp->level >= 0 && str_cmp(argall, "imotd")) {
                send_to_char(pHelp->keyword, ch);
                send_to_char("\n\r", ch);
            }

            /*
             * Strip leading '.' to allow initial blanks.
             */
            if (pHelp->text[0] == '.')
                page_to_char(pHelp->text + 1, ch);
            else
                page_to_char(pHelp->text, ch);
            return;
        }
    }

    send_to_char("No help on that word.\n\r", ch);
}

namespace {

std::string_view who_class_name_of(const CHAR_DATA &wch) {
    switch (wch.level) {
    case MAX_LEVEL - 0: return "|WIMP|w"sv; break;
    case MAX_LEVEL - 1: return "|YCRE|w"sv; break;
    case MAX_LEVEL - 2: return "|YSUP|w"sv; break;
    case MAX_LEVEL - 3: return "|GDEI|w"sv; break;
    case MAX_LEVEL - 4: return "|GGOD|w"sv; break;
    case MAX_LEVEL - 5: return "|gIMM|w"sv; break;
    case MAX_LEVEL - 6: return "|gDEM|w"sv; break;
    case MAX_LEVEL - 7: return "ANG"sv; break;
    case MAX_LEVEL - 8: return "AVA"sv; break;
    }
    return class_table[wch.class_num].who_name;
}

std::string_view who_race_name_of(const CHAR_DATA &wch) {
    return wch.race < MAX_PC_RACE ? pc_race_table[wch.race].who_name : "     "sv;
}

std::string_view who_clan_name_of(const CHAR_DATA &wch) { return wch.clan() ? wch.clan()->whoname : ""sv; }

std::string who_line_for(const CHAR_DATA &to, const CHAR_DATA &wch) {
    return "[{:3} {} {}] {}{}{}{}{}{}|w{}{}\n\r"_format(
        wch.level, who_race_name_of(wch), who_class_name_of(wch), who_clan_name_of(wch),
        IS_SET(wch.act, PLR_KILLER) ? "(|RKILLER|w) " : "", IS_SET(wch.act, PLR_THIEF) ? "(|RTHIEF|w) " : "",
        IS_SET(wch.act, PLR_AFK) ? "(|cAFK|w) " : "", wch.name, wch.is_pc() ? wch.pcdata->title : "",
        wch.is_wizinvis() && to.is_immortal() ? " |g(Wizi at level {})|w"_format(wch.invis_level) : "",
        wch.is_prowlinvis() && to.is_immortal() ? " |g(Prowl level {})|w"_format(wch.invis_level) : "");
}

}

/* whois command */
void do_whois(CHAR_DATA *ch, std::string_view argument) {
    ArgParser args(argument);

    if (args.empty()) {
        ch->send_to("You must provide a name.\n\r");
        return;
    }

    std::string output;
    auto filter = args.shift();
    for (auto &d : descriptors().all_visible_to(*ch)) {
        auto *wch = d.person();
        // TODO: can or should this be part of all_visible_to?
        if (!can_see(ch, wch))
            continue;

        if (matches_start(filter, wch->name))
            output += who_line_for(*ch, *wch);
    }

    if (output.empty()) {
        ch->send_to("No one of that name is playing.\n\r");
        return;
    }

    ch->page_to(output);
}

/*
 * New 'who' command originally by Alander of Rivers of Mud.
 */
void do_who(CHAR_DATA *ch, const char *argument) {
    int iClass;
    int iRace;
    int iLevelLower;
    int iLevelUpper;
    int nNumber;
    int nMatch;
    bool rgfClass[MAX_CLASS];
    bool rgfRace[MAX_PC_RACE];
    std::unordered_set<const CLAN *> rgfClan;
    bool fClassRestrict;
    bool fRaceRestrict;
    bool fClanRestrict;
    bool fImmortalOnly;
    char arg[MAX_STRING_LENGTH];

    /*
     * Set default arguments.
     */
    iLevelLower = 0;
    iLevelUpper = MAX_LEVEL;
    fClassRestrict = false;
    fRaceRestrict = false;
    fClanRestrict = false;
    fImmortalOnly = false;
    for (iClass = 0; iClass < MAX_CLASS; iClass++)
        rgfClass[iClass] = false;
    for (iRace = 0; iRace < MAX_PC_RACE; iRace++)
        rgfRace[iRace] = false;

    /*
     * Parse arguments.
     */
    nNumber = 0;
    for (;;) {

        argument = one_argument(argument, arg);
        if (arg[0] == '\0')
            break;

        if (is_number(arg)) {
            switch (++nNumber) {
            case 1: iLevelLower = atoi(arg); break;
            case 2: iLevelUpper = atoi(arg); break;
            default: send_to_char("Only two level numbers allowed.\n\r", ch); return;
            }
        } else {

            /*
             * Look for classes to turn on.
             */
            if (arg[0] == 'i') {
                fImmortalOnly = true;
            } else {
                iClass = class_lookup(arg);
                if (iClass == -1) {
                    iRace = race_lookup(arg);
                    if (iRace == 0 || iRace >= MAX_PC_RACE) {
                        /* Check if clan exists */
                        const CLAN *clan_ptr = nullptr; // TODO this could be much better phrased
                        for (auto &clan : clantable) {
                            if (is_name(arg, clan.name))
                                clan_ptr = &clan;
                        }
                        /* Check for NO match on clans */
                        if (!clan_ptr) {
                            send_to_char("That's not a valid race, class, or clan.\n\r", ch);
                            return;
                        } else
                        /* It DID match! */
                        {
                            fClanRestrict = true;
                            rgfClan.emplace(clan_ptr);
                        }
                    } else {
                        fRaceRestrict = true;
                        rgfRace[iRace] = true;
                    }
                } else {
                    fClassRestrict = true;
                    rgfClass[iClass] = true;
                }
            }
        }
    }

    /*
     * Now show matching chars.
     */
    nMatch = 0;
    std::string output;
    for (auto &d : descriptors().all_visible_to(*ch)) {
        // Check for match against restrictions.
        // Don't use trust as that exposes trusted mortals.
        // added Faramir 13/8/96 because switched imms were visible to all
        if (!can_see(ch, d.person()))
            continue;

        auto *wch = d.person();
        if (wch->level < iLevelLower || wch->level > iLevelUpper || (fImmortalOnly && wch->level < LEVEL_HERO)
            || (fClassRestrict && !rgfClass[wch->class_num]) || (fRaceRestrict && !rgfRace[wch->race]))
            continue;
        if (fClanRestrict) {
            if (!wch->clan() || rgfClan.count(wch->clan()) == 0)
                continue;
        }

        nMatch++;

        output += who_line_for(*ch, *wch);
    }

    output += "\n\rPlayers found: {}\n\r"_format(nMatch);
    ch->page_to(output);
}

void do_count(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    auto count = static_cast<size_t>(ranges::distance(descriptors().all_visible_to(*ch)));
    max_on = std::max(count, max_on);

    if (max_on == count)
        send_to_char("There are {} characters on, the most so far today.\n\r"_format(count), ch);
    else
        send_to_char("There are {} characters on, the most on today was {}.\n\r"_format(count, max_on), ch);
}

void do_inventory(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    send_to_char("You are carrying:\n\r", ch);
    show_list_to_char(ch->carrying, ch, true, true);
}

void do_equipment(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    OBJ_DATA *obj;
    int iWear;
    bool found;
    char buf[MAX_STRING_LENGTH];
    char buf2[MAX_STRING_LENGTH];

    send_to_char("You are using:\n\r", ch);
    found = false;
    for (iWear = 0; iWear < MAX_WEAR; iWear++) {
        if ((obj = get_eq_char(ch, iWear)) == nullptr)
            continue;

        if (obj->wear_string != nullptr) {
            snprintf(buf2, sizeof(buf2), "<%s>", obj->wear_string);
            snprintf(buf, sizeof(buf), "%-20s", buf2);
            send_to_char(buf, ch);
        } else {
            send_to_char(where_name[iWear], ch);
        }

        if (can_see_obj(ch, obj)) {
            send_to_char(format_obj_to_char(obj, ch, true), ch);
            send_to_char("\n\r", ch);
        } else {
            send_to_char("something.\n\r", ch);
        }
        found = true;
    }

    if (!found)
        send_to_char("Nothing.\n\r", ch);
}

void do_compare(CHAR_DATA *ch, const char *argument) {
    char arg1[MAX_INPUT_LENGTH];
    char arg2[MAX_INPUT_LENGTH];
    OBJ_DATA *obj1;
    OBJ_DATA *obj2;
    int value1;
    int value2;
    const char *msg;

    argument = one_argument(argument, arg1);
    argument = one_argument(argument, arg2);
    if (arg1[0] == '\0') {
        send_to_char("Compare what to what?\n\r", ch);
        return;
    }

    if ((obj1 = get_obj_carry(ch, arg1)) == nullptr) {
        send_to_char("You do not have that item.\n\r", ch);
        return;
    }

    if (arg2[0] == '\0') {
        for (obj2 = ch->carrying; obj2 != nullptr; obj2 = obj2->next_content) {
            if (obj2->wear_loc != WEAR_NONE && can_see_obj(ch, obj2) && obj1->item_type == obj2->item_type
                && (obj1->wear_flags & obj2->wear_flags & ~ITEM_TAKE) != 0)
                break;
        }

        if (obj2 == nullptr) {
            send_to_char("You aren't wearing anything comparable.\n\r", ch);
            return;
        }
    }

    else if ((obj2 = get_obj_carry(ch, arg2)) == nullptr) {
        send_to_char("You do not have that item.\n\r", ch);
        return;
    }

    msg = nullptr;
    value1 = 0;
    value2 = 0;

    if (obj1 == obj2) {
        msg = "You compare $p to itself.  It looks about the same.";
    } else if (obj1->item_type != obj2->item_type) {
        msg = "You can't compare $p and $P.";
    } else {
        switch (obj1->item_type) {
        default: msg = "You can't compare $p and $P."; break;

        case ITEM_ARMOR:
            value1 = obj1->value[0] + obj1->value[1] + obj1->value[2];
            value2 = obj2->value[0] + obj2->value[1] + obj2->value[2];
            break;

        case ITEM_WEAPON:
            value1 = (1 + obj1->value[2]) * obj1->value[1];
            value2 = (1 + obj2->value[2]) * obj2->value[1];
            break;
        }
    }

    if (msg == nullptr) {
        if (value1 == value2)
            msg = "$p and $P look about the same.";
        else if (value1 > value2)
            msg = "$p looks better than $P.";
        else
            msg = "$p looks worse than $P.";
    }

    act(msg, ch, obj1, obj2, To::Char);
}

void do_credits(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    do_help(ch, "diku");
}

void do_where(CHAR_DATA *ch, const char *argument) {
    char arg[MAX_INPUT_LENGTH];

    one_argument(argument, arg);

    if (arg[0] == '\0') {
        ch->send_to("|cYou are in {}\n\rPlayers near you:|w\n\r"_format(ch->in_room->area->areaname));
        auto found = false;
        for (auto &victim : descriptors().all_visible_to(*ch) | DescriptorFilter::except(*ch)
                                | DescriptorFilter::same_area(*ch) | DescriptorFilter::to_character()) {
            if (victim.is_pc()) {
                found = true;
                ch->send_to("|W{:<28}|w {}\n\r"_format(victim.name, victim.in_room->name));
            }
        }
        if (!found)
            ch->send_to("None\n\r");
        if (ch->pet && ch->pet->in_room->area == ch->in_room->area) {
            ch->send_to("You sense that your pet is near {}.\n\r"_format(ch->pet->in_room->name));
        }
    } else {
        auto found = false;
        for (auto *victim = char_list; victim != nullptr; victim = victim->next) {
            if (victim->in_room != nullptr && victim->in_room->area == ch->in_room->area
                && !IS_AFFECTED(victim, AFF_HIDE) && !IS_AFFECTED(victim, AFF_SNEAK) && can_see(ch, victim)
                && victim != ch && is_name(arg, victim->name)) {
                found = true;
                ch->send_to("|W{:<28}|w {}\n\r"_format(pers(victim, ch), victim->in_room->name));
                break;
            }
        }
        if (!found)
            act("You didn't find any $T.", ch, nullptr, arg, To::Char);
    }
}

void do_consider(CHAR_DATA *ch, const char *argument) {
    char arg[MAX_INPUT_LENGTH];
    CHAR_DATA *victim;
    const char *msg;
    int diff;

    one_argument(argument, arg);

    if (arg[0] == '\0') {
        send_to_char("Consider killing whom?\n\r", ch);
        return;
    }

    if ((victim = get_char_room(ch, arg)) == nullptr) {
        send_to_char("They're not here.\n\r", ch);
        return;
    }

    if (is_safe(ch, victim)) {
        send_to_char("Don't even think about it.\n\r", ch);
        return;
    }

    diff = victim->level - ch->level;

    if (diff <= -10)
        msg = "You can kill $N naked and weaponless.";
    else if (diff <= -5)
        msg = "$N is no match for you.";
    else if (diff <= -2)
        msg = "$N looks like an easy kill.";
    else if (diff <= 1)
        msg = "The perfect match!";
    else if (diff <= 4)
        msg = "$N says 'Do you feel lucky, punk?'.";
    else if (diff <= 9)
        msg = "$N laughs at you mercilessly.";
    else
        msg = "|RDeath will thank you for your gift.|w";

    act(msg, ch, nullptr, victim, To::Char);
    if (ch->level >= LEVEL_CONSIDER)
        do_mstat(ch, argument);
}

void set_prompt(CHAR_DATA *ch, const char *prompt) {
    if (ch->is_npc()) {
        bug("Set_prompt: NPC.");
        return;
    }
    ch->pcdata->prompt = prompt;
}

void do_title(CHAR_DATA *ch, const char *argument) {
    if (ch->is_npc())
        return;

    if (argument[0] == '\0') {
        send_to_char("Change your title to what?\n\r", ch);
        return;
    }

    auto new_title = smash_tilde(argument);
    if (new_title.length() > 45)
        new_title.resize(45);

    ch->set_title(new_title);
    send_to_char("Ok.\n\r", ch);
}

void do_description(CHAR_DATA *ch, const char *argument) {
    if (auto desc_line = smash_tilde(argument); !desc_line.empty()) {
        if (desc_line.front() == '+') {
            ch->description += ltrim(desc_line.substr(1)) + "\n\r";
        } else if (desc_line == "-") {
            if (ch->description.empty()) {
                ch->send_to("You have no description.\n\r");
                return;
            }
            ch->description = remove_last_line(ch->description);
        } else {
            ch->description = desc_line + "\n\r";
        }
        if (ch->description.size() >= MAX_STRING_LENGTH - 2) {
            ch->send_to("Description too long.\n\r");
            return;
        }
    }

    ch->send_to("Your description is:\n\r{}"_format(ch->description.empty() ? "(None).\n\r" : ch->description));
}

void do_report(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    char buf[MAX_INPUT_LENGTH];

    snprintf(buf, sizeof(buf), "You say 'I have %d/%d hp %d/%d mana %d/%d mv %d xp.'\n\r", ch->hit, ch->max_hit,
             ch->mana, ch->max_mana, ch->move, ch->max_move, (int)ch->exp);

    send_to_char(buf, ch);

    snprintf(buf, sizeof(buf), "$n says 'I have %d/%d hp %d/%d mana %d/%d mv %d xp.'", ch->hit, ch->max_hit, ch->mana,
             ch->max_mana, ch->move, ch->max_move, (int)ch->exp);

    act(buf, ch);
}

void do_practice(CHAR_DATA *ch, const char *argument) {
    char buf[MAX_STRING_LENGTH];
    int sn;

    if (ch->is_npc())
        return;

    if (argument[0] == '\0') {
        int col;

        col = 0;
        for (sn = 0; sn < MAX_SKILL; sn++) {
            if (skill_table[sn].name == nullptr)
                break;
            if (ch->level < get_skill_level(ch, sn)
                || ch->pcdata->learned[sn] < 1 /* skill is not known (NOT get_skill_learned) */)
                continue;

            snprintf(buf, sizeof(buf), "%-18s %3d%%  ", skill_table[sn].name,
                     ch->pcdata->learned[sn]); // NOT get_skill_learned
            send_to_char(buf, ch);
            if (++col % 3 == 0)
                send_to_char("\n\r", ch);
        }

        if (col % 3 != 0)
            send_to_char("\n\r", ch);

        snprintf(buf, sizeof(buf), "You have %d practice sessions left.\n\r", ch->practice);
        send_to_char(buf, ch);
    } else {
        CHAR_DATA *mob;
        int adept;

        if (!IS_AWAKE(ch)) {
            send_to_char("In your dreams, or what?\n\r", ch);
            return;
        }

        for (mob = ch->in_room->people; mob != nullptr; mob = mob->next_in_room) {
            if (mob->is_npc() && IS_SET(mob->act, ACT_PRACTICE))
                break;
        }

        if (mob == nullptr) {
            send_to_char("You can't do that here.\n\r", ch);
            return;
        }

        if (ch->practice <= 0) {
            send_to_char("You have no practice sessions left.\n\r", ch);
            return;
        }

        if ((sn = skill_lookup(argument)) < 0
            || (ch->is_pc()
                && ((ch->level < get_skill_level(ch, sn) || (ch->pcdata->learned[sn] < 1))))) // NOT get_skill_learned
        {
            send_to_char("You can't practice that.\n\r", ch);
            return;
        }

        adept = ch->is_npc() ? 100 : class_table[ch->class_num].skill_adept;

        if (ch->pcdata->learned[sn] >= adept) // NOT get_skill_learned
        {
            snprintf(buf, sizeof(buf), "You are already learned at %s.\n\r", skill_table[sn].name);
            send_to_char(buf, ch);
        } else {
            ch->practice--;
            if (get_skill_trains(ch, sn) < 0) {
                ch->pcdata->learned[sn] += int_app[get_curr_stat(ch, Stat::Int)].learn / 4;
            } else {
                ch->pcdata->learned[sn] += int_app[get_curr_stat(ch, Stat::Int)].learn / get_skill_difficulty(ch, sn);
            }
            if (ch->pcdata->learned[sn] < adept) // NOT get_skill_learned
            {
                act("You practice $T.", ch, nullptr, skill_table[sn].name, To::Char);
                act("$n practices $T.", ch, nullptr, skill_table[sn].name, To::Room);
            } else {
                ch->pcdata->learned[sn] = adept;
                act("You are now learned at $T.", ch, nullptr, skill_table[sn].name, To::Char);
                act("$n is now learned at $T.", ch, nullptr, skill_table[sn].name, To::Room);
            }
        }
    }
}

/*
 * 'Wimpy' originally by Dionysos.
 */
void do_wimpy(CHAR_DATA *ch, const char *argument) {
    char buf[MAX_STRING_LENGTH];
    char arg[MAX_INPUT_LENGTH];
    int wimpy;

    one_argument(argument, arg);

    if (arg[0] == '\0')
        wimpy = ch->max_hit / 5;
    else
        wimpy = atoi(arg);

    if (wimpy < 0) {
        send_to_char("Your courage exceeds your wisdom.\n\r", ch);
        return;
    }

    if (wimpy > ch->max_hit / 2) {
        send_to_char("Such cowardice ill becomes you.\n\r", ch);
        return;
    }

    ch->wimpy = wimpy;
    snprintf(buf, sizeof(buf), "Wimpy set to %d hit points.\n\r", wimpy);
    send_to_char(buf, ch);
}

void do_password(CHAR_DATA *ch, const char *argument) {
    char arg1[MAX_INPUT_LENGTH];
    char arg2[MAX_INPUT_LENGTH];
    char *pArg;
    char *pwdnew;
    char *p;
    char cEnd;

    if (ch->is_npc())
        return;

    /*
     * Can't use one_argument here because it smashes case.
     * So we just steal all its code.  Bleagh.
     */
    pArg = arg1;
    while (isspace(*argument))
        argument++;

    cEnd = ' ';
    if (*argument == '\'' || *argument == '"')
        cEnd = *argument++;

    while (*argument != '\0') {
        if (*argument == cEnd) {
            argument++;
            break;
        }
        *pArg++ = *argument++;
    }
    *pArg = '\0';

    pArg = arg2;
    while (isspace(*argument))
        argument++;

    cEnd = ' ';
    if (*argument == '\'' || *argument == '"')
        cEnd = *argument++;

    while (*argument != '\0') {
        if (*argument == cEnd) {
            argument++;
            break;
        }
        *pArg++ = *argument++;
    }
    *pArg = '\0';

    if (arg1[0] == '\0' || arg2[0] == '\0') {
        send_to_char("Syntax: password <old> <new>.\n\r", ch);
        return;
    }

    if (!ch->pcdata->pwd.empty() && strcmp(crypt(arg1, ch->pcdata->pwd.c_str()), ch->pcdata->pwd.c_str())) {
        WAIT_STATE(ch, 40);
        send_to_char("Wrong password.  Wait 10 seconds.\n\r", ch);
        return;
    }

    if ((int)strlen(arg2) < 5) {
        send_to_char("New password must be at least five characters long.\n\r", ch);
        return;
    }

    /*
     * No tilde allowed because of player file format.
     */
    pwdnew = crypt(arg2, ch->name);
    for (p = pwdnew; *p != '\0'; p++) {
        if (*p == '~') {
            send_to_char("New password not acceptable, try again.\n\r", ch);
            return;
        }
    }

    ch->pcdata->pwd = pwdnew;
    save_char_obj(ch);
    send_to_char("Ok.\n\r", ch);
}

/* RT configure command SMASHED */

/* MrG Scan command */

void do_scan(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    ROOM_INDEX_DATA *current_place;
    CHAR_DATA *current_person;
    CHAR_DATA *next_person;
    EXIT_DATA *pexit;
    int count_num_rooms;
    int num_rooms_scan = UMAX(1, ch->level / 10);
    bool found_anything = false;
    std::vector<sh_int> found_rooms{ch->in_room->vnum};

    ch->send_to("You can see around you :\n\r");
    /* Loop for each point of the compass */

    for (auto direction : all_directions) {
        /* No exits in that direction */

        current_place = ch->in_room;

        /* Loop for the distance see-able */

        for (count_num_rooms = 0; count_num_rooms < num_rooms_scan; count_num_rooms++) {

            if ((pexit = current_place->exit[direction]) == nullptr || (current_place = pexit->u1.to_room) == nullptr
                || !can_see_room(ch, pexit->u1.to_room) || IS_SET(pexit->exit_info, EX_CLOSED))
                break;
            // Eliminate cycles in labyrinthine areas.
            if (std::find(found_rooms.begin(), found_rooms.end(), pexit->u1.to_room->vnum) != found_rooms.end()) {
                break;
            }
            found_rooms.push_back(pexit->u1.to_room->vnum);

            /* This loop goes through each character in a room and says
                        whether or not they are visible */

            for (current_person = current_place->people; current_person != nullptr; current_person = next_person) {
                next_person = current_person->next_in_room;
                if (ch->can_see(*current_person)) {
                    ch->send_to("{} {:<5}: |W{}|w\n\r"_format(count_num_rooms + 1, capitalize(to_string(direction)),
                                                              current_person->short_name()));
                    found_anything = true;
                }
            } /* Closes the for_each_char_loop */

        } /* Closes the for_each distance seeable loop */

    } /* closes main loop for each direction */
    if (!found_anything)
        ch->send_to("Nothing of great interest.\n\r");
}

/*
 * alist to list all areas
 */

void do_alist(CHAR_DATA *ch, const char *argument) {
    (void)argument;
    AREA_DATA *pArea;
    BUFFER *buffer = buffer_create();

    buffer_addline_fmt(buffer, "%3s %-29s %-5s-%5s %-10s\n\r", "Num", "Area Name", "Lvnum", "Uvnum", "Filename");

    for (pArea = area_first; pArea; pArea = pArea->next) {
        buffer_addline_fmt(buffer, "%3d %-29.29s %-5d-%5d %-12.12s\n\r", pArea->vnum, pArea->areaname, pArea->lvnum,
                           pArea->uvnum, pArea->filename);
    }
    buffer_send(buffer, ch);
}
