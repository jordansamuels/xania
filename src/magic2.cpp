/*************************************************************************/
/*  Xania (M)ulti(U)ser(D)ungeon server source code                      */
/*  (C) 1995-2000 Xania Development Team                                    */
/*  See the header to file: merc.h for original code copyrights          */
/*************************************************************************/

#include "AFFECT_DATA.hpp"
#include "AffectFlag.hpp"
#include "Char.hpp"
#include "CharActFlag.hpp"
#include "DamageClass.hpp"
#include "DamageTolerance.hpp"
#include "Exit.hpp"
#include "Logging.hpp"
#include "Object.hpp"
#include "ObjectIndex.hpp"
#include "ObjectType.hpp"
#include "Room.hpp"
#include "RoomFlag.hpp"
#include "SkillTables.hpp"
#include "ToleranceFlag.hpp"
#include "VnumMobiles.hpp"
#include "VnumRooms.hpp"
#include "challenge.hpp"
#include "comm.hpp"
#include "common/BitOps.hpp"
#include "db.h"
#include "fight.hpp"
#include "handler.hpp"
#include "interp.h"
#include "lookup.h"
#include "magic.h"
#include "ride.hpp"
#include "string_utils.hpp"

/* Faramir's rather devious tornado spell....watch out for players who
 * use this to annoy others! :)
 */

void tornado_teleport(Char *ch, Char *victim) {
    Room *room;

    for (;;) {
        room = get_room(number_range(0, 65535));
        if (room != nullptr)
            if (can_see_room(ch, room) && !check_enum_bit(room->room_flags, RoomFlag::Private)
                && !check_enum_bit(room->room_flags, RoomFlag::Solitary))
                break;
    }

    if (victim != ch)
        victim->send_line("You are sucked helplessly into the tornado....");

    act("$n is consumed by the tornado and vanishes!", victim);
    char_from_room(victim);
    char_to_room(victim, room);
    victim->send_line("...you appear to have been blown to another part of Xania!");

    if (!ch->riding) {
        act("$n is blown into the room by a sudden blast of wind.", victim);
    } else {
        act("$n is blown into the room by a blast of wind, about 5 feet off the ground!", victim, nullptr, nullptr,
            To::Room);
        act("$n falls to the ground with a thud!", victim);
        act("You fall to the ground with a thud!", victim, nullptr, nullptr, To::Char);
        fallen_off_mount(ch);
    } /* end ride check */
    look_auto(victim);
}

void tornado_dam(Char *ch, Char *victim, int level) {
    int dam;
    int sn = skill_lookup("psychic tornado");

    dam = dice(level, 20);
    damage(ch, victim, dam, &skill_table[sn], DAM_MENTAL);
}

void tornado_mental(Char *ch, Char *victim, int level) {

    AFFECT_DATA af;

    af.type = skill_lookup("psychic tornado");
    af.level = level;
    af.duration = 2;
    af.location = AffectLocation::Hitroll;
    af.modifier = -4;
    af.bitvector = to_int(AffectFlag::Blind);
    affect_to_char(victim, af);

    af.location = AffectLocation::Wis;
    af.modifier = -(1 + (level >= 20) + (level >= 30) + (level >= 50) + (level >= 75) + (level >= 91));
    af.bitvector = 0;
    affect_to_char(victim, af);
    af.location = AffectLocation::Int;
    af.modifier = -(1 + (level >= 20) + (level >= 30) + (level >= 50) + (level >= 75) + (level >= 91));
    affect_to_char(victim, af);
    ch->wait_state(PULSE_VIOLENCE * 2);
    victim->position = Position::Type::Resting;
}

void spell_psy_tornado(int sn, int level, Char *ch, void *vo) {
    (void)sn;
    (void)level;
    Char *victim = (Char *)vo;

    if (victim == ch) {
        ch->send_line("You can't cast that on yourself.");
        return;
    }

    if (victim->is_pc()) {
        ch->send_line("Not on that target.");
        return;
    }

    if (is_safe(ch, victim))
        return;

    if (ch->in_room->vnum == rooms::ChallengeArena) {
        ch->send_line("Not in the challenge room.");
        return;
    }

    if (ch->mana < (ch->max_mana - 100)) {
        ch->send_line("This powerful spell requires all of your psychic powers.");
        return;
    }

    if (check_damage_tolerance(victim, DAM_MENTAL) == DamageTolerance::Immune) {
        act("The great mental fortitude of $N deflects your psychic blast!", ch, nullptr, victim, To::Char);
        act("$n attempts to unleash a psychic blast upon $N, but $s efforts backfire with\n\rcatastrophic effects!", ch,
            nullptr, victim, To::NotVict);
        act("$n attempts to unleash a psychic blast upon you, but your mental fortitude\n\rdeflects it.", ch, nullptr,
            victim, To::Vict);

        /* HIT THE BASTARD! */
        tornado_dam(victim, ch, ch->level);
        tornado_mental(victim, ch, ch->level);
        ch->send_line("You feel sick and disorientated.");
        act("$n is knocked to the ground and stunned.", ch, nullptr, victim, To::Room);
        return;
    } else {
        act("You summon all of your psychic powers and unleash them upon $N!", ch, nullptr, victim, To::Char);

        act("You detect a surge of psychic energy as $n unleashes his will upon $N!", ch, nullptr, victim, To::NotVict);

        act("A terrifying grimace appears on $n's face as $s unleashes a powerful psychic\n\rblast upon you!", ch,
            nullptr, victim, To::Vict);

        tornado_dam(ch, victim, ch->level);
        tornado_mental(ch, victim, ch->level);
        ch->send_line("Your mighty blast spirals out of control, forming a towering tornado of psychic energy!");
        act("$n's psychic blast spirals into a |Rhuge|w tornado of energy!", ch, nullptr, victim, To::Room);

        if (!check_enum_bit(victim->act, CharActFlag::Aggressive)) {
            if (number_percent() > 75)
                tornado_teleport(ch, victim);
        }

        for (auto *vch : char_list) {
            if (vch->in_room == nullptr)
                continue;
            if (vch->in_room == ch->in_room) {
                if (vch != ch && !is_safe_spell(ch, vch, true) && (vch != victim)
                    && (check_damage_tolerance(vch, DAM_MENTAL) != DamageTolerance::Immune))
                    tornado_mental(ch, victim, ch->level);
                continue;
            }

            if (vch->in_room->area == ch->in_room->area)
                vch->send_line("A faint psychic surge dazes you momentarily.");
        }

        for (auto direction : all_directions) {
            /* No exits in that direction */
            if (auto *pexit = ch->in_room->exit[direction]) {
                if (auto *room = pexit->u1.to_room) {
                    for (auto *current_person : room->people) {
                        current_person->send_line("Suddenly, a gale of psychic energy blows through the room!");
                        if (!is_safe_spell(ch, current_person, true)
                            && (check_damage_tolerance(current_person, DAM_MENTAL) != DamageTolerance::Immune))
                            tornado_mental(ch, current_person, ch->level);

                    } /* Closes the for_each_char_loop */
                }
            }
        } /* closes main loop for each direction */
    }
}

void spell_reincarnate(int sn, int level, Char *ch, void *vo) {
    (void)sn;
    (void)level;
    (void)vo;
    /* is of type Target::Ignore so ignore *vo */

    int num_of_corpses = 0, corpse, chance;

    if (ch->is_npc())
        return;

    /* scan the room looking for an appropriate objects...count them */
    for (auto *obj : ch->in_room->contents) {
        if ((obj->objIndex->type == ObjectType::Npccorpse) || (obj->objIndex->type == ObjectType::Pccorpse))
            num_of_corpses++;
    }

    /* Did we find *any* corpses? */
    if (num_of_corpses == 0) {
        ch->send_line("There are no dead in this room to reincarnate!");
        return;
    }

    /* choose a corpse at random & find its corresponding Object */
    corpse = number_range(1, num_of_corpses);
    Object *obj{};
    for (auto *c : ch->in_room->contents) {
        if ((c->objIndex->type == ObjectType::Npccorpse) || (c->objIndex->type == ObjectType::Pccorpse)) {
            if (--corpse == 0) {
                obj = c;
                break;
            }
        }
    }

    if (obj == nullptr) { /* Oh dear */
        bug("spell_reincarnate: Couldn't find corpse!");
        return;
    }

    /* Tell people what happened */
    act("$n summons mighty powers and reanimates $s.", ch, nullptr, obj, To::Room);
    act("You summon mighty powers and reanimate $s.", ch, nullptr, obj, To::Char);

    /* Can we re-animate this corpse? Include check for a non-empty PC corpse */

    chance = std::clamp((50 + ((ch->level - obj->objIndex->level) * 3)), 1, 99);

    if ((number_percent() > chance) || /* if random failed */
        ((obj->objIndex->type == ObjectType::Pccorpse) && !obj->contains.empty()))
    /* if non-empty PC corpse */ {
        act("$s stands, then falls over again - lifeless.", ch, nullptr, obj, To::Room);
        act("$s stands, then falls over again - lifeless.", ch, nullptr, obj, To::Char);
        return;
    } else {
        Char *animated;

        act("$s stands up.", ch, nullptr, obj, To::Room);
        act("$s stands up.", ch, nullptr, obj, To::Char);
        animated = create_mobile(get_mob_index(mobiles::Zombie));
        if (animated == nullptr) {
            bug("spell_reincarnate: Couldn't find corpse vnum!");
            return;
        }

        /* Put the zombie in the room */
        char_to_room(animated, ch->in_room);
        /* Give the zombie the kit that was in the corpse */
        animated->carrying = std::move(obj->contains);
        obj->contains.clear();
        /* Give the zombie its correct name and stuff */
        animated->long_descr = fmt::printf(animated->description, obj->description);
        animated->name = fmt::printf(animated->name, obj->name);

        /* Say byebye to the corpse */
        extract_obj(obj);

        /* Set up the zombie correctly */
        animated->master = ch;
        animated->leader = ch;
        do_follow(animated, ArgParser(ch->name));
    }
}

/* commented out for time being --Fara  */

// void spell_raise_dead (int sn, int level, Char *ch, void *vo)
//{
// Object *victim;
// AFFECT_DATA af;
//
/* no corpse raising in a lawful place please */
//
//  if (check_enum_bit(ch->in_room->room_flags , RoomFlag::Law)) {
//  ch->send_line( "Raising the dead is not permitted here.");
//  return;
//}
//
//
//
/*
 * Find a corpse to work on
 */
//  for (victim = ch->in_room->contents; victim; victim = victim->next_content) {
//  if (victim->mobIndex->vnum == objects::NonPlayerCorpse) {
//    Char *zombie;
//    MobIndexData *zIndex;
//    Object *obj, *objNext;
//    int zLevel, i;
//    float zScale;
//
//    zIndex = get_mob_index (mobiles::Zombie);
//    if (zIndex == nullptr) {
// bug ("Unable to find a zombie mob in spell_raise_dead!");
// ch->send_line ("Urk!  Something terrible has happened!");
// return;
//    }
//
/*
 * Work out some stats for the zombie based on its level
 * The zombie default level is 10, everythin is scaled
 * linearly
//     */
//    zLevel = std::min (ch->level, victim->level) - 5; /* XXX needs sorting out */
//    if (zLevel < 1)
// zLevel = 1;
//    zScale = zLevel / 10.f;
//
//    zombie = create_mobile(zIndex);
//    char_to_room (zombie, ch->in_room);
//
//    zombie->level = zLevel;
//    zombie->hit *= zScale;
//    zombie->max_hit *= zScale;
//    zombie->mana *= zScale;
//    zombie->max_mana *= zScale;
//    zombie->move *= zScale;
//    zombie->max_move *= zScale;
//    zombie->hitroll *= zScale;
//    zombie->damroll *= zScale;
//
//    for (i = 0; i < 4; ++i)
// zombie->armor[i] *= zScale;
//
//    zombie->alignment = std::clamp(ch->alignment - 100, -1000, -100);
//
/*
 * Do some sparks and fizzes XXX
 */
//      act ("$n calls upon black magic to raise the dead!\n\rThe $p comes to life as a zombie!",
//   ch, victim, nullptr, To::Room);
//    ch->send_line ("You disturb the slumber of a corpse!");
//
/* and now make this zombie a member of the group */
/* uses code very similar to spell_charm_person() */
//
//    set_enum_bit(zombie->affected_by, AffectFlag::Charm);
//    add_follower (zombie, ch);
//    zombie->leader = ch;
//    af.type      = sn;
//    af.level    = level;
//    af.duration  = number_fuzzy( level / 4 );
//    af.location  = 0;
//    af.modifier  = 0;
//    af.bitvector = to_int(AffectFlag::Charm);
//    affect_to_char( zombie, af );
//    act( "Isn't $n just so nice?", ch, nullptr, victim, To::Vict );
//    act("$N gazes at you through blood-chilling eye sockets.",ch,nullptr,zombie,To::Char);
//
/*
 * Equip the zombie with the corpse contents
 */
//    for (obj = victim->contains; obj; obj = objNext) {
// objNext = obj->next_content;
// if (obj->type == ObjectType::Money) {
//  zombie->gold += obj->value[0];
//  extract_obj (obj);
//} else {
//  obj_from_obj (obj);
//  obj_to_char (obj, zombie);
//}
//
//    }
//
/*
 * Delete the corpse
 */
//    extract_obj (victim);
//
//    return;
//  }
//}
/*
 * No corpses
 */
// ch->send_line ("Raising the dead requires a fresh corpse to work with.");
// return;
//}
