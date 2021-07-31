/*************************************************************************/
/*  Xania (M)ulti(U)ser(D)ungeon server source code                      */
/*  (C) 1995-2000 Xania Development Team                                 */
/*  See the header to file: merc.h for original code copyrights          */
/*                                                                       */
/*  imm/wiznet.c: wiznet debugging and general information facility      */
/*                                                                       */
/*************************************************************************/

#include "Char.hpp"
#include "CommandSet.hpp"
#include "Constants.hpp"
#include "Descriptor.hpp"
#include "interp.h"

#include <fmt/format.h>

#include "DescriptorList.hpp"
#include "comm.hpp"

namespace {

std::string channel_state_label(std::string_view master_name, const bool master_state, const bool state) {
    /* Check if the channel argument is the Quiet mode one */
    if (state) {
        if (master_state) {
            return "|gON|w";
        } else {
            return fmt::format("|rON ({})|w", master_name);
        }
    } else {
        return "|rOFF|w";
    }
}

}

void print_status(const Char *ch, std::string_view name, const bool master_state, std::string_view master_name,
                  const bool state) {
    ch->send_line("{:<16}{}", name, channel_state_label(master_name, master_state, state));
}

static void print_wiznet_statusline(Char *ch, std::string_view name, const bool state) {
    print_status(ch, name, ch->is_set_extra(EXTRA_WIZNET_ON), "wiznet is off", state);
}

static void print_wiznet_status(Char *ch) {
    ch->send_line("|Woption          status|w");
    ch->send_line("----------------------------");

    print_wiznet_statusline(ch, "bug", ch->is_set_extra(EXTRA_WIZNET_BUG));
    print_wiznet_statusline(ch, "debug", ch->is_set_extra(EXTRA_WIZNET_DEBUG));
    print_wiznet_statusline(ch, "immortal", ch->is_set_extra(EXTRA_WIZNET_IMM));
    print_wiznet_statusline(ch, "mortal", ch->is_set_extra(EXTRA_WIZNET_MORT));
    print_wiznet_statusline(ch, "tick", ch->is_set_extra(EXTRA_WIZNET_TICK));
    print_status(ch, "wiznet", ch->is_set_extra(EXTRA_WIZNET_ON), "", true);
}

using wiznet_fn = std::function<void(Char *ch)>;
static CommandSet<wiznet_fn> wiznet_commands;

void wiznet_on(Char *ch) {
    ch->set_extra(EXTRA_WIZNET_ON);
    ch->send_line("|cWIZNET is now |gON|c.|w");
}

void wiznet_off(Char *ch) {
    ch->remove_extra(EXTRA_WIZNET_ON);
    ch->send_line("|cWIZNET is now |rOFF|c.|w");
}

static void toggle_wizchan(Char *ch, int flag, const char *name) {
    if (ch->is_set_extra(flag)) {
        ch->send_line("|GWIZNET {} is now |rOFF|G.|w", name);
        ch->remove_extra(flag);
    } else {
        ch->send_line("|GWIZNET {} is now {}|G.|w", name,
                      ch->is_set_extra(EXTRA_WIZNET_ON) ? "|gON" : "|rON (WIZNET OFF)");
        ch->set_extra(flag);
    }
}

void wiznet_bug(Char *ch) { toggle_wizchan(ch, EXTRA_WIZNET_BUG, "bug"); }

void wiznet_debug(Char *ch) { toggle_wizchan(ch, EXTRA_WIZNET_DEBUG, "debug"); }

void wiznet_mortal(Char *ch) { toggle_wizchan(ch, EXTRA_WIZNET_MORT, "mortal"); }

void wiznet_immortal(Char *ch) { toggle_wizchan(ch, EXTRA_WIZNET_IMM, "immortal"); }

void wiznet_tick(Char *ch) { toggle_wizchan(ch, EXTRA_WIZNET_TICK, "tick"); }

void wiznet_initialise() {
    wiznet_commands.add("on", wiznet_on, 0);
    wiznet_commands.add("off", wiznet_off, 0);
    wiznet_commands.add("bug", wiznet_bug, 0);
    wiznet_commands.add("debug", wiznet_debug, 0);
    wiznet_commands.add("mortal", wiznet_mortal, 0);
    wiznet_commands.add("immortal", wiznet_immortal, 0);
    wiznet_commands.add("tick", wiznet_tick, 0);
}

void do_wiznet(Char *ch, const char *argument) {
    char arg[MAX_INPUT_LENGTH];
    argument = one_argument(argument, arg);

    auto wiznet_fn = wiznet_commands.get(arg, 0);
    if (wiznet_fn.has_value()) {
        (*wiznet_fn)(ch);
    } else {
        print_wiznet_status(ch);
    }
}
