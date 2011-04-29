/*
 *  File:       items.cc
 *  Summary:    Misc (mostly) inventory related functions.
 *  Written by: Linley Henzell
 *
 *  Modified for Crawl Reference by $Author: dshaligram $ on $Date: 2007-10-29 07:53:31 +0100 (Mon, 29 Oct 2007) $
 *
 *  Modified for Hexcrawl by Martin Bays, 2007
 *
 *  Change History (most recent first):
 *
 * <9> 7/08/01   MV   Added messages for chunks/corpses rotting
 * <8> 8/07/99   BWR  Added Rune stacking
 * <7> 6/13/99   BWR  Added auto staff detection
 * <6> 6/12/99   BWR  Fixed time system.
 * <5> 6/9/99    DML  Autopickup
 * <4> 5/26/99   JDJ  Drop will attempt to take off armour.
 * <3> 5/21/99   BWR  Upped armour skill learning slightly.
 * <2> 5/20/99   BWR  Added assurance that against inventory count being wrong.
 * <1> -/--/--   LRH  Created
 */

#include "AppHdr.h"
#include "items.h"
#include "clua.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#ifdef DOS
#include <conio.h>
#endif

#include "externs.h"

#include "beam.h"
#include "cloud.h"
#include "debug.h"
#include "delay.h"
#include "direct.h"
#include "dgnevent.h"
#include "effects.h"
#include "food.h"
#include "hiscores.h"
#include "invent.h"
#include "it_use2.h"
#include "item_use.h"
#include "itemname.h"
#include "itemprop.h"
#include "libutil.h"
#include "message.h"
#include "misc.h"
#include "monplace.h"
#include "monstuff.h"
#include "mstuff2.h"
#include "mon-util.h"
#include "mutation.h"
#include "notes.h"
#include "overmap.h"
#include "place.h"
#include "player.h"
#include "randart.h"
#include "religion.h"
#include "shopping.h"
#include "skills.h"
#include "spl-cast.h"
#include "spl-book.h"
#include "spl-util.h"
#include "stuff.h"
#include "stash.h"
#include "terrain.h"
#include "transfor.h"
#include "tutorial.h"
#include "view.h"

static bool invisible_to_player( const item_def& item );
static void item_list_on_square( std::vector<const item_def*>& items,
                                 int obj, bool force_squelch = false );
static void autoinscribe_item( item_def& item );
static void autoinscribe_floor_items();
static void autoinscribe_inventory();

static bool will_autopickup   = false;
static bool will_autoinscribe = false;

// Used to be called "unlink_items", but all it really does is make
// sure item coordinates are correct to the stack they're in. -- bwr
void fix_item_coordinates(void)
{
    int x,y,i;

    // nails all items to the ground (i.e. sets x,y)
    for (x = 0; x < GXM; x++)
    {
        for (y = 0; y < GYM; y++)
        {
            i = igrd[x][y];

            while (i != NON_ITEM)
            {
                mitm[i].x = x;
                mitm[i].y = y;
                i = mitm[i].link;
            }
        }
    }
}

// This function uses the items coordinates to relink all the igrd lists.
void link_items(void)
{
    int i,j;

    // first, initialise igrd array
    for (i = 0; i < GXM; i++)
    {
        for (j = 0; j < GYM; j++)
            igrd[i][j] = NON_ITEM;
    }

    // link all items on the grid, plus shop inventory,
    // DON'T link the huge pile of monster items at (0,0)

    for (i = 0; i < MAX_ITEMS; i++)
    {
        if (!is_valid_item(mitm[i]) || (mitm[i].x == 0 && mitm[i].y == 0))
        {
            // item is not assigned, or is monster item.  ignore.
            mitm[i].link = NON_ITEM;
            continue;
        }

        // link to top
        mitm[i].link = igrd[ mitm[i].x ][ mitm[i].y ];
        igrd[ mitm[i].x ][ mitm[i].y ] = i;
    }
}                               // end link_items()

static bool item_ok_to_clean(int item)
{
    // 5. never clean food or Orbs
    if (mitm[item].base_type == OBJ_FOOD || mitm[item].base_type == OBJ_ORBS)
        return false;

    // never clean runes
    if (mitm[item].base_type == OBJ_MISCELLANY 
        && mitm[item].sub_type == MISC_RUNE_OF_ZOT)
    {
        return false;
    }

    return true;
}

// returns index number of first available space, or NON_ITEM for
// unsuccessful cleanup (should be exceedingly rare!)
int cull_items(void)
{
    // XXX: Not the prettiest of messages, but the player 
    // deserves to know whenever this kicks in. -- bwr
    mpr( "Too many items on level, removing some.", MSGCH_WARN );

    /* rules:
       1. Don't cleanup anything nearby the player
       2. Don't cleanup shops
       3. Don't cleanup monster inventory
       4. Clean 15% of items
       5. never remove food, orbs, runes
       7. uniques weapons are moved to the abyss
       8. randarts are simply lost
       9. unrandarts are 'destroyed', but may be generated again
    */

    int x,y, item, next;
    int first_cleaned = NON_ITEM;

    // 2. avoid shops by avoiding (0,5..9)
    // 3. avoid monster inventory by iterating over the dungeon grid
    for (x = 5; x < GXM; x++)
    {
        for (y = 5; y < GYM; y++)
        {
            // 1. not near player!
            if (x > you.x_pos - 9 && x < you.x_pos + 9
                && y > you.y_pos - 9 && y < you.y_pos + 9)
            {
                continue;
            }

            // iterate through the grids list of items: 
            for (item = igrd[x][y]; item != NON_ITEM; item = next)
            {
                next = mitm[item].link; // in case we can't get it later.

                if (item_ok_to_clean(item) && random2(100) < 15)
                {
                    if (is_fixed_artefact( mitm[item] ))
                    {
                        // 7. move uniques to abyss
                        set_unique_item_status( OBJ_WEAPONS, mitm[item].special,
                                                UNIQ_LOST_IN_ABYSS );
                    }
                    else if (is_unrandom_artefact( mitm[item] ))
                    {
                        // 9. unmark unrandart
                        int z = find_unrandart_index(item);
                        if (z >= 0)
                            set_unrandart_exist(z, false);
                    }

                    // POOF!
                    destroy_item( item );
                    if (first_cleaned == NON_ITEM)
                        first_cleaned = item;
                }
            } // end for item

        } // end y
    } // end x

    return (first_cleaned);
}

// Note:  This function is to isolate all the checks to see if 
//        an item is valid (often just checking the quantity).
//
//        It shouldn't be used a a substitute for those cases 
//        which actually want to check the quantity (as the 
//        rules for unused objects might change).
bool is_valid_item( const item_def &item )
{
    return (item.base_type != OBJ_UNASSIGNED && item.quantity > 0);
}

// Reduce quantity of an inventory item, do cleanup if item goes away.  
//
// Returns true if stack of items no longer exists.
bool dec_inv_item_quantity( int obj, int amount )
{
    bool ret = false;

    if (you.equip[EQ_WEAPON] == obj)
        you.wield_change = true;

    if (you.inv[obj].quantity <= amount)
    {
        for (int i = 0; i < NUM_EQUIP; i++) 
        {
            if (you.equip[i] == obj)
            {
                if (i == EQ_WEAPON)
                {
                    unwield_item();
                    canned_msg( MSG_EMPTY_HANDED );
                }
                you.equip[i] = -1;
            }
        }

        you.inv[obj].base_type = OBJ_UNASSIGNED;
        you.inv[obj].quantity = 0;

        ret = true;
    }
    else
    {
        you.inv[obj].quantity -= amount;
    }

    burden_change();

    return (ret);
}

// Reduce quantity of a monster/grid item, do cleanup if item goes away.  
//
// Returns true if stack of items no longer exists.
bool dec_mitm_item_quantity( int obj, int amount )
{
    if (mitm[obj].quantity <= amount)
    {
        destroy_item( obj );
        return (true);
    }

    mitm[obj].quantity -= amount;

    return (false);
}

void inc_inv_item_quantity( int obj, int amount )
{
    if (you.equip[EQ_WEAPON] == obj)
        you.wield_change = true;

    you.inv[obj].quantity += amount;
    burden_change();
}

void inc_mitm_item_quantity( int obj, int amount )
{
    mitm[obj].quantity += amount;
}

void init_item( int item )
{
    if (item == NON_ITEM)
        return;

    mitm[item].clear();
}

// Returns an unused mitm slot, or NON_ITEM if none available.
// The reserve is the number of item slots to not check. 
// Items may be culled if a reserve <= 10 is specified.
int get_item_slot( int reserve )
{
    ASSERT( reserve >= 0 );

    int item = NON_ITEM;

    for (item = 0; item < (MAX_ITEMS - reserve); item++)
    {
        if (!is_valid_item( mitm[item] ))
            break;
    }

    if (item >= MAX_ITEMS - reserve)
    {
        item = (reserve <= 10) ? cull_items() : NON_ITEM;

        if (item == NON_ITEM)
            return (NON_ITEM);
    }

    ASSERT( item != NON_ITEM );

    init_item( item );

    return (item);
}

void unlink_item( int dest )
{
    int c = 0;
    int cy = 0;

    // Don't destroy non-items, may be called after an item has been
    // reduced to zero quantity however.
    if (dest == NON_ITEM || !is_valid_item( mitm[dest] ))
        return;

    if (mitm[dest].x == 0 && mitm[dest].y == 0) 
    {
        // (0,0) is where the monster items are (and they're unlinked by igrd),
        // although it also contains items that are not linked in yet.
        //
        // Check if a monster has it:
        for (c = 0; c < MAX_MONSTERS; c++)
        {
            struct monsters *monster = &menv[c];

            if (monster->type == -1)
                continue;

            for (cy = 0; cy < NUM_MONSTER_SLOTS; cy++)
            {
                if (monster->inv[cy] == dest)
                {
                    monster->inv[cy] = NON_ITEM;

                    mitm[dest].x = 0;
                    mitm[dest].y = 0;
                    mitm[dest].link = NON_ITEM;

                    // This causes problems when changing levels. -- bwr
                    // if (monster->type == MONS_DANCING_WEAPON)
                    //     monster_die(monster, KILL_RESET, 0);
                    return;
                }
            }
        }

        // Always return because this item might just be temporary.
        return;
    }
    else 
    {
        // Linked item on map:
        //
        // Use the items (x,y) to access the list (igrd[x][y]) where
        // the item should be linked.

        // First check the top:
        if (igrd[ mitm[dest].x ][ mitm[dest].y ] == dest)
        {
            // link igrd to the second item
            igrd[ mitm[dest].x ][ mitm[dest].y ] = mitm[dest].link;

            mitm[dest].x = 0;
            mitm[dest].y = 0;
            mitm[dest].link = NON_ITEM;
            return;
        }

        // Okay, item is buried, find item that's on top of it:
        for (c = igrd[ mitm[dest].x ][ mitm[dest].y ]; c != NON_ITEM; c = mitm[c].link)
        {
            // find item linking to dest item
            if (is_valid_item( mitm[c] ) && mitm[c].link == dest)
            {
                // unlink dest
                mitm[c].link = mitm[dest].link;

                mitm[dest].x = 0;
                mitm[dest].y = 0;
                mitm[dest].link = NON_ITEM;
                return;
            }
        }
    }

#if DEBUG
    // Okay, the sane ways are gone... let's warn the player:
    mpr( "BUG WARNING: Problems unlinking item!!!", MSGCH_DANGER );

    // Okay, first we scan all items to see if we have something 
    // linked to this item.  We're not going to return if we find 
    // such a case... instead, since things are already out of 
    // alignment, let's assume there might be multiple links as well.
    bool  linked = false; 
    int   old_link = mitm[dest].link; // used to try linking the first

    // clean the relevant parts of the object:
    mitm[dest].base_type = OBJ_UNASSIGNED;
    mitm[dest].quantity = 0;
    mitm[dest].x = 0;
    mitm[dest].y = 0;
    mitm[dest].link = NON_ITEM;

    // Look through all items for links to this item.
    for (c = 0; c < MAX_ITEMS; c++)
    {
        if (is_valid_item( mitm[c] ) && mitm[c].link == dest)
        {
            // unlink item
            mitm[c].link = old_link;

            if (!linked)
            {
                old_link = NON_ITEM;
                linked = true;
            }
        }
    }

    // Now check the grids to see if it's linked as a list top.
    for (c = 2; c < (GXM - 1); c++)
    {
        for (cy = 2; cy < (GYM - 1); cy++)
        {
            if (igrd[c][cy] == dest)
            {
                igrd[c][cy] = old_link;

                if (!linked)
                {
                    old_link = NON_ITEM;  // cleaned after the first
                    linked = true;
                }
            }
        }
    }


    // Okay, finally warn player if we didn't do anything.
    if (!linked)
        mpr("BUG WARNING: Item didn't seem to be linked at all.", MSGCH_DANGER);
#endif
}                               // end unlink_item()

void destroy_item( int dest, bool never_created )
{
    // Don't destroy non-items, but this function may be called upon 
    // to remove items reduced to zero quantity, so we allow "invalid"
    // objects in.
    if (dest == NON_ITEM || !is_valid_item( mitm[dest] ))
        return;

    unlink_item( dest );

    if (never_created)
    {
        if (is_fixed_artefact(mitm[dest]))
            set_unique_item_status( mitm[dest].base_type, mitm[dest].special,
                                    UNIQ_NOT_EXISTS );
        else if (is_unrandom_artefact(mitm[dest]))
        {
            const int unrand = find_unrandart_index(dest);
            if (unrand != -1)
                set_unrandart_exist( unrand, false );
        }
    }

    // paranoia, shouldn't be needed
    mitm[dest].clear();
}

void destroy_item_stack( int x, int y )
{
    int o = igrd[x][y];

    igrd[x][y] = NON_ITEM;

    while (o != NON_ITEM)
    {
        int next = mitm[o].link;

        if (is_valid_item( mitm[o] ))
        {
            if (mitm[o].base_type == OBJ_ORBS)
            {   
                set_unique_item_status( OBJ_ORBS, mitm[o].sub_type,
                                        UNIQ_LOST_IN_ABYSS );
            }
            else if (is_fixed_artefact( mitm[o] ))
            {   
                set_unique_item_status( OBJ_WEAPONS, mitm[o].special, 
                                        UNIQ_LOST_IN_ABYSS );
            }

            mitm[o].base_type = OBJ_UNASSIGNED;
            mitm[o].quantity = 0;
        }

        o = next;
    }
}

static bool invisible_to_player( const item_def& item ) {
    return strstr(item.inscription.c_str(), "=k") != 0;
}

static int count_nonsquelched_items( int obj ) {
    int result = 0;
    while ( obj != NON_ITEM )
    {
        if ( !invisible_to_player(mitm[obj]) )
            ++result;
        obj = mitm[obj].link;
    }
    return result;
}

/* Fill items with the items on a square.
   Squelched items (marked with =k) are ignored, unless
   the square contains *only* squelched items, in which case they
   are included. If force_squelch is true, squelched items are
   never displayed.
 */
static void item_list_on_square( std::vector<const item_def*>& items,
                                 int obj, bool force_squelch ) {

    const bool have_nonsquelched = (force_squelch ||
                                    count_nonsquelched_items(obj));

    /* loop through the items */
    while ( obj != NON_ITEM ) {
        /* add them to the items list if they qualify */
        if ( !have_nonsquelched || !invisible_to_player(mitm[obj]) )
        {
            items.push_back( &mitm[obj] );
        }
        obj = mitm[obj].link;
    }
}

bool need_to_autopickup()
{
    return will_autopickup;
}

void request_autopickup(bool do_pickup)
{
    will_autopickup = do_pickup;
}

// 2 - artifact, 1 - glowing/runed, 0 - mundane
static int item_name_specialness(const item_def& item)
{
    // All jewellery is worth looking at.
    // And we can always tell from the name if it's an artefact.
    if (item.base_type == OBJ_JEWELLERY )
        return ( is_artefact(item) ? 2 : 1 );

    if (item.base_type != OBJ_WEAPONS && item.base_type != OBJ_ARMOUR &&
        item.base_type != OBJ_MISSILES)
        return 0;
    if (item_type_known(item))
    {
        if ( is_artefact(item) )
            return 2;

        // XXX Unite with l_item_branded() in clua.cc
        bool branded = false;
        switch (item.base_type)
        {
        case OBJ_WEAPONS:
            branded = get_weapon_brand(item) != SPWPN_NORMAL;
            break;
        case OBJ_ARMOUR:
            branded = get_armour_ego_type(item) != SPARM_NORMAL;
            break;
        case OBJ_MISSILES:
            branded = get_ammo_brand(item) != SPMSL_NORMAL;
            break;
        default:
            break;
        }
        return ( branded ? 1 : 0 );
    }

    if ( item.base_type == OBJ_MISSILES )
        return 0;               // missiles don't get name descriptors

    std::string itname = item.name(DESC_PLAIN, false, false, false);
    lowercase(itname);
    
    const bool item_runed = itname.find("runed ") != std::string::npos;
    const bool heav_runed = itname.find("heavily ") != std::string::npos;
    const bool item_glows = itname.find("glowing") != std::string::npos;

    if ( item_glows || (item_runed && !heav_runed) )
        return 1;

    // You can tell artefacts, because they'll have a description which
    // rules out anything else.
    // XXX Fixedarts and unrandarts might upset the apple-cart, though.
    if ( is_artefact(item) )
        return 2;

    return 0;
}

void item_check(bool verbose)
{

    describe_floor();
    origin_set(you.x_pos, you.y_pos);

    std::ostream& strm = msg::streams(MSGCH_FLOOR_ITEMS);

    std::vector<const item_def*> items;

    item_list_on_square( items, igrd[you.x_pos][you.y_pos], true );

    if (items.size() == 0)
    {
        if ( verbose )
            strm << "There are no items here." << std::endl;
        return;
    }

    if (items.size() == 1 )
    {
        strm << "You see here " << items[0]->name(DESC_NOCAP_A)
             << '.' << std::endl;
        return;
    }

    bool done_init_line = false;
    
    if (static_cast<int>(items.size()) >= Options.item_stack_summary_minimum)
    {
        std::vector<unsigned short int> item_chars;
        for ( unsigned int i = 0; i < items.size() && i < 50; ++i )
        {
            unsigned glyph_char;
            unsigned short glyph_col;
            get_item_glyph( items[i], &glyph_char, &glyph_col );            
            item_chars.push_back( glyph_char * 0x100 +
                                  (10 - item_name_specialness(*(items[i]))) );
        }
        std::sort(item_chars.begin(), item_chars.end());

        std::string out_string = "Items here: ";
        int cur_state = -1;
        for ( unsigned int i = 0; i < item_chars.size(); ++i )
        {
            const int specialness = 10 - (item_chars[i] % 0x100);
            if ( specialness != cur_state )
            {
                switch ( specialness )
                {
                case 2: out_string += "<yellow>"; break; // artefact
                case 1: out_string += "<white>"; break;  // glowing/runed
                case 0: out_string += "<darkgrey>"; break; // mundane
                }
                cur_state = specialness;
            }

            out_string += static_cast<unsigned char>(item_chars[i] / 0x100);
            if (i + 1 < item_chars.size() &&
                (item_chars[i] / 0x100) != (item_chars[i+1] / 0x100))
                out_string += ' ';
        }
        formatted_mpr(formatted_string::parse_string(out_string),
                      MSGCH_FLOOR_ITEMS);
        done_init_line = true;
    }

    if ( verbose || static_cast<int>(items.size() + 1) < crawl_view.msgsz.y )
    {
        if ( !done_init_line )
            strm << "Things that are here:" << std::endl;
        for ( unsigned int i = 0; i < items.size(); ++i )
            strm << items[i]->name(DESC_NOCAP_A) << std::endl;
    }
    else if ( !done_init_line )
        strm << "There are many items here." << std::endl;
    
    if ( items.size() > 5 )
        learned_something_new(TUT_MULTI_PICKUP);
}

void show_items()
{
    std::vector<const item_def*> items;
    item_list_on_square( items, igrd[you.x_pos][you.y_pos], true );

    if ( items.empty() )
        mpr("There are no items here.");
    else {
        select_items( items, "Things that are here:", true );
        redraw_screen();
    }
    
    describe_floor();
}

void pickup_menu(int item_link)
{
    std::vector<const item_def*> items;
    item_list_on_square( items, item_link, false );

    std::vector<SelItem> selected = 
        select_items( items, "Select items to pick up" );
    redraw_screen();

    for (int i = 0, count = selected.size(); i < count; ++i) {
        for (int j = item_link; j != NON_ITEM; j = mitm[j].link) {
            if (&mitm[j] == selected[i].item) {
                if (j == item_link)
                    item_link = mitm[j].link;

                unsigned long oldflags = mitm[j].flags;
                mitm[j].flags &= ~(ISFLAG_THROWN | ISFLAG_DROPPED);
                int result = move_item_to_player( j, selected[i].quantity );

                // If we cleared any flags on the items, but the pickup was
                // partial, reset the flags for the items that remain on the 
                // floor.
                if (is_valid_item(mitm[j]))
                    mitm[j].flags = oldflags;

                if (result == 0)
                {
                    mpr("You can't carry that much weight.");
                    learned_something_new(TUT_HEAVY_LOAD);
                    return;
                }
                else if (result == -1)
                {
                    mpr("You can't carry that many items.");
                    learned_something_new(TUT_HEAVY_LOAD);
                    return;
                }
                break;
            }
        }
    }
}

bool origin_known(const item_def &item)
{
    return (item.orig_place != 0);
}

// We have no idea where the player found this item.
void origin_set_unknown(item_def &item)
{
    if (!origin_known(item))
    {
        item.orig_place  = 0xFFFF;
        item.orig_monnum = 0;
    }
}

// This item is starting equipment.
void origin_set_startequip(item_def &item)
{
    if (!origin_known(item))
    {
        item.orig_place  = 0xFFFF;
        item.orig_monnum = -1;
    }
}

void origin_set_monster(item_def &item, const monsters *monster)
{
    if (!origin_known(item))
    {
        if (!item.orig_monnum)
            item.orig_monnum = monster->type + 1;
        item.orig_place = get_packed_place();
    }
}

void origin_purchased(item_def &item)
{
    // We don't need to check origin_known if it's a shop purchase
    item.orig_place  = get_packed_place();
    // Hackiness
    item.orig_monnum = -1;
}

void origin_acquired(item_def &item, int agent)
{
    // We don't need to check origin_known if it's a divine gift
    item.orig_place  = get_packed_place();
    // Hackiness
    item.orig_monnum = -2 - agent;
}

void origin_set_inventory(void (*oset)(item_def &item))
{
    for (int i = 0; i < ENDOFPACK; ++i)
    {
        if (is_valid_item(you.inv[i]))
            oset(you.inv[i]);
    }
}

static int first_corpse_monnum(int x, int y)
{
    // We could look for a corpse on this square and assume that the
    // items belonged to it, but that is unsatisfactory.
    return (0);
}

#ifdef DGL_MILESTONES
static std::string milestone_rune(const item_def &item)
{
    return std::string("found ") + item.name(DESC_NOCAP_A) + ".";
}

static void milestone_check(const item_def &item)
{
    if (item.base_type == OBJ_MISCELLANY
        && item.sub_type == MISC_RUNE_OF_ZOT)
    {
        mark_milestone("rune", milestone_rune(item));
    }
    else if (item.base_type == OBJ_ORBS && item.sub_type == ORB_ZOT)
    {
        mark_milestone("orb", "found the Orb of Zot!");
    }
}
#endif // DGL_MILESTONES

static void check_note_item(item_def &item)
{
    if (!(item.flags & ISFLAG_NOTED_GET) && is_interesting_item(item))
    {
        take_note(Note(NOTE_GET_ITEM, 0, 0, item.name(DESC_NOCAP_A).c_str(),
                       origin_desc(item).c_str()));
        item.flags |= ISFLAG_NOTED_GET;

        // If it's already fully identified when picked up, don't take
        // further notes.
        if ( fully_identified(item) )
            item.flags |= ISFLAG_NOTED_ID;
    }
}

void origin_set(int x, int y)
{
    int monnum = first_corpse_monnum(x, y);
    unsigned short pplace = get_packed_place();
    for (int link = igrd[x][y]; link != NON_ITEM; link = mitm[link].link)
    {
        item_def &item = mitm[link];
        if (origin_known(item))
            continue;

        if (!item.orig_monnum)
            item.orig_monnum = static_cast<short>( monnum );
        item.orig_place  = pplace;
#ifdef DGL_MILESTONES
        milestone_check(item);
#endif
    }
}

void origin_set_monstercorpse(item_def &item, int x, int y)
{
    item.orig_monnum = first_corpse_monnum(x, y);
}

void origin_freeze(item_def &item, int x, int y)
{
    if (!origin_known(item))
    {
        if (!item.orig_monnum && x != -1 && y != -1)
            origin_set_monstercorpse(item, x, y);
        item.orig_place = get_packed_place();
        check_note_item(item);
#ifdef DGL_MILESTONES
        milestone_check(item);
#endif
    }
}

static std::string origin_monster_name(const item_def &item)
{
    const int monnum = item.orig_monnum - 1;
    if (monnum == MONS_PLAYER_GHOST)
        return ("a player ghost");
    else if (monnum == MONS_PANDEMONIUM_DEMON)
        return ("a demon");
    return mons_type_name(monnum, DESC_NOCAP_A);
}

static std::string origin_monster_desc(const item_def &item)
{
    return (origin_monster_name(item));
}

static std::string origin_place_desc(const item_def &item)
{
    return prep_branch_level_name(item.orig_place);
}

bool is_rune(const item_def &item)
{
    return (item.base_type == OBJ_MISCELLANY &&
            item.sub_type == MISC_RUNE_OF_ZOT);
}

bool origin_describable(const item_def &item)
{
    return (origin_known(item)
            && (item.orig_place != 0xFFFFU || item.orig_monnum == -1)
            && (!is_stackable_item(item) || is_rune(item))
            && item.quantity == 1
            && item.base_type != OBJ_CORPSES
            && (item.base_type != OBJ_FOOD || item.sub_type != FOOD_CHUNK));
}

std::string article_it(const item_def &item)
{
    // "it" is always correct, since gloves and boots also come in pairs.
    return "it";
}

bool origin_is_original_equip(const item_def &item)
{
    return (item.orig_place == 0xFFFFU && item.orig_monnum == -1);
}

std::string origin_desc(const item_def &item)
{
    if (!origin_describable(item))
        return ("");

    if (origin_is_original_equip(item))
        return "Original Equipment";

    std::string desc;
    if (item.orig_monnum)
    {
        if (item.orig_monnum < 0)
        {
            int iorig = -item.orig_monnum - 2;
            switch (iorig)
            {
            case -1:
                desc += "You bought " + article_it(item) + " in a shop ";
                break;
            case AQ_SCROLL:
                desc += "You acquired " + article_it(item) + " ";
                break;
            case AQ_CARD_GENIE:
                desc += "You drew the Genie ";
                break;
            case AQ_WIZMODE:
                desc += "Your wizardly powers created " 
                            + article_it(item) + " ";
                break;
            default:
                if (iorig > GOD_NO_GOD && iorig < NUM_GODS)
                    desc += std::string(god_name(static_cast<god_type>(iorig))) 
                            + " gifted " + article_it(item) + " to you ";
                else
                    // Bug really.
                    desc += "You stumbled upon " + article_it(item) + " ";
                break;
            }
        }
        else if (item.orig_monnum - 1 == MONS_DANCING_WEAPON)
            desc += "You subdued it ";
        else
            desc += "You took " + article_it(item) + " off "
                    + origin_monster_desc(item) + " ";
    }
    else
        desc += "You found " + article_it(item) + " ";
    desc += origin_place_desc(item);
    return (desc);
}

bool pickup_single_item(int link, int qty)
{
    if (you.attribute[ATTR_TRANSFORMATION] == TRAN_AIR 
        && you.duration[DUR_TRANSFORMATION] > 0)
    {
        mpr("You can't pick up anything in this form!");
        return (false);
    }

    if (you.flight_mode() == FL_LEVITATE)
    {
        mpr("You can't reach the floor from up here.");
        return (false);
    }

    if (qty < 1 || qty > mitm[link].quantity)
        qty = mitm[link].quantity;

    unsigned long oldflags = mitm[link].flags;
    mitm[link].flags &= ~(ISFLAG_THROWN | ISFLAG_DROPPED);
    int num = move_item_to_player( link, qty );
    if (is_valid_item(mitm[link]))
        mitm[link].flags = oldflags;

    if (num == -1)
    {
        mpr("You can't carry that many items.");
        learned_something_new(TUT_HEAVY_LOAD);
        return (false);
    }
    else if (num == 0)
    {
        mpr("You can't carry that much weight.");
        learned_something_new(TUT_HEAVY_LOAD);
        return (false);
    }
    
    return (true);
}

void pickup()
{
    int keyin = 'x';

    if (you.attribute[ATTR_TRANSFORMATION] == TRAN_AIR 
        && you.duration[DUR_TRANSFORMATION] > 0)
    {
        mpr("You can't pick up anything in this form!");
        return;
    }

    if (you.flight_mode() == FL_LEVITATE)
    {
        mpr("You can't reach the floor from up here.");
        return;
    }

    int o = igrd[you.x_pos][you.y_pos];
    const int num_nonsquelched = count_nonsquelched_items(o);

    if (o == NON_ITEM)
    {
        mpr("There are no items here.");
    }
    else if (mitm[o].link == NON_ITEM)      // just one item?
    {
        // deliberately allowing the player to pick up
        // a killed item here
        pickup_single_item(o, mitm[o].quantity);
    }
    else if (Options.pickup_mode != -1 &&
             num_nonsquelched >= Options.pickup_mode)
    {
        pickup_menu(o);
    }
    else
    {
        int next;
        mpr("There are several objects here.");
        while ( o != NON_ITEM )
        {
            // must save this because pickup can destroy the item
            next = mitm[o].link;

            if ( num_nonsquelched && invisible_to_player(mitm[o]) )
            {
                o = next;
                continue;
            }

            if (keyin != 'a')
            {
                mprf(MSGCH_PROMPT, "Pick up %s? (y/n/a/*?g,/q)",
                     mitm[o].name(DESC_NOCAP_A).c_str() );
                keyin = get_ch();
            }

            if (keyin == '*' || keyin == '?' || keyin == ',' || keyin == 'g')
            {
                pickup_menu(o);
                break;
            }

            if (keyin == 'q' || keyin == ESCAPE)
                break;

            if (keyin == 'y' || keyin == 'a')
            {
                mitm[o].flags &= ~(ISFLAG_THROWN | ISFLAG_DROPPED);
                int result = move_item_to_player( o, mitm[o].quantity );

                if (result == 0)
                {
                    mpr("You can't carry that much weight.");
                    keyin = 'x';        // resets from 'a'
                }
                else if (result == -1)
                {
                    mpr("You can't carry that many items.");
                    break;
                }
            }
            
            o = next;
        }
    }
}                               // end pickup()

bool is_stackable_item( const item_def &item )
{
    if (!is_valid_item( item ))
        return (false);

    if (item.base_type == OBJ_MISSILES
        || (item.base_type == OBJ_FOOD && item.sub_type != FOOD_CHUNK)
        || item.base_type == OBJ_SCROLLS
        || item.base_type == OBJ_POTIONS
        || item.base_type == OBJ_UNKNOWN_II
        || item.base_type == OBJ_GOLD
        || (item.base_type == OBJ_MISCELLANY 
            && item.sub_type == MISC_RUNE_OF_ZOT))
    {
        return (true);
    }

    return (false);
}

unsigned long ident_flags(const item_def &item)
{
    const unsigned long identmask = full_ident_mask(item);
    unsigned long flags = item.flags & identmask;

    if ((identmask & ISFLAG_KNOW_TYPE) && item_type_known(item))
        flags |= ISFLAG_KNOW_TYPE;

    return (flags);
}

bool items_stack( const item_def &item1, const item_def &item2,
                  bool force_merge )
{
    // both items must be stackable
    if (!force_merge
        && (!is_stackable_item( item1 ) || !is_stackable_item( item2 )))
        return (false);

    // base and sub-types must always be the same to stack
    if (item1.base_type != item2.base_type || item1.sub_type != item2.sub_type)
        return (false);

    ASSERT(force_merge || item1.base_type != OBJ_WEAPONS);

    if (item1.base_type == OBJ_GOLD)
        return (true);

    // These classes also require pluses and special
    if (item1.base_type == OBJ_WEAPONS         // only throwing weapons
        || item1.base_type == OBJ_MISSILES
        || item1.base_type == OBJ_MISCELLANY)  // only runes
    {
        if (item1.plus != item2.plus
             || item1.plus2 != item2.plus2
             || item1.special != item2.special)
        {
            return (false);
        }
    }

    // Check the ID flags
    if (ident_flags(item1) != ident_flags(item2))
        return false;

    // Check the non-ID flags, but ignore dropped, thrown, cosmetic,
    // and note flags
#define NON_IDENT_FLAGS ~(ISFLAG_IDENT_MASK | ISFLAG_COSMETIC_MASK | \
                          ISFLAG_DROPPED | ISFLAG_THROWN | \
                          ISFLAG_NOTED_ID | ISFLAG_NOTED_GET)
    if ((item1.flags & NON_IDENT_FLAGS) !=
        (item2.flags & NON_IDENT_FLAGS))
    {
        return false;
    }

    // Thanks to mummy cursing, we can have potions of decay 
    // that don't look alike... so we don't stack potions 
    // if either isn't identified and they look different.  -- bwr
    if (item1.base_type == OBJ_POTIONS && item1.special != item2.special &&
        (!item_type_known(item1) || !item_type_known(item2)))
    {
        return false;
    }

    // The inscriptions can differ if one of them is blank, but if they
    // are differing non-blank inscriptions then don't stack.
    if (item1.inscription != item2.inscription
        && item1.inscription != "" && item2.inscription != "")
    {
        return false;
    }

    return (true);
}

static int userdef_find_free_slot(const item_def &i)
{
#ifdef CLUA_BINDINGS
    int slot = -1;
    if (!clua.callfn("c_assign_invletter", "u>d", &i, &slot))
        return (-1);
    return (slot);
#else
    return -1;
#endif
}

int find_free_slot(const item_def &i)
{
#define slotisfree(s) \
            ((s) >= 0 && (s) < ENDOFPACK && !is_valid_item(you.inv[s]))

    bool searchforward = false;
    // If we're doing Lua, see if there's a Lua function that can give
    // us a free slot.
    int slot = userdef_find_free_slot(i);
    if (slot == -2 || Options.assign_item_slot == SS_FORWARD)
        searchforward = true;

    if (slotisfree(slot))
        return slot;

    // See if the item remembers where it's been. Lua code can play with
    // this field so be extra careful.
    if ((i.slot >= 'a' && i.slot <= 'z') ||
            (i.slot >= 'A' && i.slot <= 'Z'))
        slot = letter_to_index(i.slot);

    if (slotisfree(slot))
        return slot;

    if (!searchforward)
    {
        // This is the new default free slot search. We look for the last
        // available slot that does not leave a gap in the inventory.
        bool accept_empty = false;
        for (slot = ENDOFPACK - 1; slot >= 0; --slot)
        {
            if (is_valid_item(you.inv[slot]))
            {
                if (!accept_empty && slot + 1 < ENDOFPACK &&
                        !is_valid_item(you.inv[slot + 1]))
                    return (slot + 1);

                accept_empty = true;
            }
            else if (accept_empty)
            {
                return slot;
            }
        }
    }

    // Either searchforward is true, or search backwards failed and
    // we re-try searching the oposite direction.

    // Return first free slot
    for (slot = 0; slot < ENDOFPACK; ++slot) {
        if (!is_valid_item(you.inv[slot]))
            return slot;
    }

    return (-1);
#undef slotisfree
}

// Returns quantity of items moved into player's inventory and -1 if 
// the player's inventory is full.
int move_item_to_player( int obj, int quant_got, bool quiet )
{
    if (item_is_stationary(mitm[obj]))
    {
        mpr("You cannot pick up the net that holds you!");
        return (1);
    }

    int retval = quant_got;

    // Gold has no mass, so we handle it first.
    if (mitm[obj].base_type == OBJ_GOLD)
    {
        you.gold += quant_got;
        dec_mitm_item_quantity( obj, quant_got );
        you.redraw_gold = 1;

        if (!quiet)
        {
            mprf("You pick up %d gold piece%s.",
                 quant_got, (quant_got > 1) ? "s" : "" );
        }

        you.turn_is_over = true;
        return (retval);
    }

    const int unit_mass = item_mass( mitm[obj] );
    if (quant_got > mitm[obj].quantity || quant_got <= 0)
        quant_got = mitm[obj].quantity;
    
    const int imass = unit_mass * quant_got;

    bool partial_pickup = false;

    if (you.burden + imass > carrying_capacity())
    {
        // calculate quantity we can actually pick up
        int part = (carrying_capacity() - you.burden) / unit_mass;

        if (part < 1)
            return (0);

        // only pickup 'part' items
        quant_got = part;
        partial_pickup = true;

        retval = part;
    }
    
    if (is_stackable_item( mitm[obj] ))
    {
        for (int m = 0; m < ENDOFPACK; m++)
        {
            if (items_stack( you.inv[m], mitm[obj] ))
            {
                if (!quiet && partial_pickup)
                    mpr("You can only carry some of what is here.");

                check_note_item(mitm[obj]);

                // If the object on the ground is inscribed, but not
                // the one in inventory, then the inventory object
                // picks up the other's inscription.
                if (mitm[obj].inscription != ""
                    && you.inv[m].inscription == "")
                {
                    you.inv[m].inscription = mitm[obj].inscription;
                }

                inc_inv_item_quantity( m, quant_got );
                dec_mitm_item_quantity( obj, quant_got );
                burden_change();

                if (!quiet)
                    mpr( you.inv[m].name(DESC_INVENTORY).c_str() );

                you.turn_is_over = true;

                return (retval);
            }
        }
    }

    // can't combine, check for slot space
    if (inv_count() >= ENDOFPACK)
        return (-1);

    if (!quiet && partial_pickup)
        mpr("You can only carry some of what is here.");

    int freeslot = find_free_slot(mitm[obj]);
    if (freeslot < 0 || freeslot >= ENDOFPACK 
           || is_valid_item(you.inv[freeslot]))
    {
        // Something is terribly wrong
        return (-1);
    }

    item_def &item = you.inv[freeslot];
    // copy item
    item        = mitm[obj];
    item.x      = -1;
    item.y      = -1;
    item.link   = freeslot;

    if (!item.slot)
        item.slot = index_to_letter(item.link);

    autoinscribe_item( item );

    origin_freeze(item, you.x_pos, you.y_pos);
    check_note_item(item);

    item.quantity = quant_got;
    dec_mitm_item_quantity( obj, quant_got );
    burden_change();

    if (!quiet)
        mpr( you.inv[freeslot].name(DESC_INVENTORY).c_str() );
    
    if (Options.tutorial_left)
    {
        taken_new_item(item.base_type);
        if (is_artefact(item) || get_equip_desc( item ) != ISFLAG_NO_DESC)
            learned_something_new(TUT_SEEN_RANDART);
    }
    
    if (item.base_type == OBJ_ORBS
        && you.char_direction == GDT_DESCENDING)
    {
        if (!quiet)
            mpr("Now all you have to do is get back out of the dungeon!");
        you.char_direction = GDT_ASCENDING;
    }

    you.turn_is_over = true;

    return (retval);
}                               // end move_item_to_player()

void mark_items_non_pickup_at(const coord_def &pos)
{
    int item = igrd(pos);
    while (item != NON_ITEM)
    {
        mitm[item].flags |= ISFLAG_DROPPED;
        mitm[item].flags &= ~ISFLAG_THROWN;
        item = mitm[item].link;
    }
}

// Moves mitm[obj] to (x,y)... will modify the value of obj to 
// be the index of the final object (possibly different).  
//
// Done this way in the hopes that it will be obvious from
// calling code that "obj" is possibly modified.
bool move_item_to_grid( int *const obj, int x, int y )
{
    // must be a valid reference to a valid object
    if (*obj == NON_ITEM || !is_valid_item( mitm[*obj] ))
        return (false);

    // If it's a stackable type...
    if (is_stackable_item( mitm[*obj] ))
    {
        // Look for similar item to stack:
        for (int i = igrd[x][y]; i != NON_ITEM; i = mitm[i].link)
        {
            // check if item already linked here -- don't want to unlink it
            if (*obj == i)
                return (false);            

            if (items_stack( mitm[*obj], mitm[i] ))
            {
                // Add quantity to item already here, and dispose 
                // of obj, while returning the found item. -- bwr
                inc_mitm_item_quantity( i, mitm[*obj].quantity );
                destroy_item( *obj );
                *obj = i;
                return (true);
            }
        }
    }
    // Non-stackable item that's been fudge-stacked (monster throwing weapons).
    // Explode the stack when dropped. We have to special case chunks, ew.
    else if (mitm[*obj].quantity > 1
             && (mitm[*obj].base_type != OBJ_FOOD
                 || mitm[*obj].sub_type != FOOD_CHUNK))
    {
        while (mitm[*obj].quantity > 1)
        {
            // If we can't copy the items out, we lose the surplus.
            if (copy_item_to_grid(mitm[*obj], x, y, 1, false))
                --mitm[*obj].quantity;
            else
                mitm[*obj].quantity = 1;
        }
    }

    ASSERT( *obj != NON_ITEM );

    // Need to actually move object, so first unlink from old position. 
    unlink_item( *obj );

    // move item to coord:
    mitm[*obj].x = x;
    mitm[*obj].y = y;

    // link item to top of list.
    mitm[*obj].link = igrd[x][y];
    igrd[x][y] = *obj;

    return (true);
}

void move_item_stack_to_grid( int x, int y, int targ_x, int targ_y )
{
    // Tell all items in stack what the new coordinate is. 
    for (int o = igrd[x][y]; o != NON_ITEM; o = mitm[o].link)
    {
        mitm[o].x = targ_x;
        mitm[o].y = targ_y;
    }

    igrd[targ_x][targ_y] = igrd[x][y];
    igrd[x][y] = NON_ITEM;
}


// returns quantity dropped
bool copy_item_to_grid( const item_def &item, int x_plos, int y_plos, 
                        int quant_drop, bool mark_dropped )
{
    if (quant_drop == 0)
        return (false);

    // default quant_drop == -1 => drop all
    if (quant_drop < 0)
        quant_drop = item.quantity;

    // loop through items at current location
    if (is_stackable_item( item ))
    {
        for (int i = igrd[x_plos][y_plos]; i != NON_ITEM; i = mitm[i].link)
        {
            if (items_stack( item, mitm[i] ))
            {
                inc_mitm_item_quantity( i, quant_drop );
                
                // If the items on the floor already have a nonzero slot,
                // leave it as such, otherwise set the slot.
                if (mark_dropped && !mitm[i].slot)
                    mitm[i].slot = index_to_letter(item.link);
                return (true);
            }
        }
    }

    // item not found in current stack, add new item to top.
    int new_item = get_item_slot(10);
    if (new_item == NON_ITEM)
        return (false);

    // copy item
    mitm[new_item] = item;

    // set quantity, and set the item as unlinked
    mitm[new_item].quantity = quant_drop;
    mitm[new_item].x = 0;
    mitm[new_item].y = 0;
    mitm[new_item].link = NON_ITEM;

    if (mark_dropped)
    {
        mitm[new_item].slot = index_to_letter(item.link);
        mitm[new_item].flags |= ISFLAG_DROPPED;
        mitm[new_item].flags &= ~ISFLAG_THROWN;
        origin_set_unknown(mitm[new_item]);
    }

    move_item_to_grid( &new_item, x_plos, y_plos );

    return (true);
}                               // end copy_item_to_grid()


//---------------------------------------------------------------
//
// move_top_item -- moves the top item of a stack to a new
// location.
//
//---------------------------------------------------------------
bool move_top_item( int src_x, int src_y, int dest_x, int dest_y )
{
    int item = igrd[ src_x ][ src_y ];
    if (item == NON_ITEM)
        return (false);

    // Now move the item to its new possition...
    move_item_to_grid( &item, dest_x, dest_y );

    return (true);
}

bool drop_item( int item_dropped, int quant_drop, bool try_offer )
{
    if (quant_drop < 0 || quant_drop > you.inv[item_dropped].quantity)
        quant_drop = you.inv[item_dropped].quantity;

    if (item_dropped == you.equip[EQ_LEFT_RING]
        || item_dropped == you.equip[EQ_RIGHT_RING]
        || item_dropped == you.equip[EQ_AMULET])
    {
        if (!Options.easy_unequip)
        {
            mpr("You will have to take that off first.");
            return (false);
        }

        if (remove_ring( item_dropped, true ))
            start_delay( DELAY_DROP_ITEM, 1, item_dropped, 1 );

        return (false);
    }

    if (item_dropped == you.equip[EQ_WEAPON] 
        && you.inv[item_dropped].base_type == OBJ_WEAPONS
        && item_cursed( you.inv[item_dropped] ))
    {
        mpr("That object is stuck to you!");
        return (false);
    }

    for (int i = EQ_CLOAK; i <= EQ_BODY_ARMOUR; i++)
    {
        if (item_dropped == you.equip[i] && you.equip[i] != -1)
        {
            if (!Options.easy_unequip)
            {
                mpr("You will have to take that off first.");
            }
            else 
            {
                // If we take off the item, cue up the item being dropped
                if (takeoff_armour( item_dropped ))
                {
                    start_delay( DELAY_DROP_ITEM, 1, item_dropped, 1 );
                    you.turn_is_over = false; // turn happens later
                }
            }

            // Regardless, we want to return here because either we're
            // aborting the drop, or the drop is delayed until after 
            // the armour is removed. -- bwr
            return (false);
        }
    }

    // [ds] easy_unequip does not apply to weapons.
    //
    // Unwield needs to be done before copy in order to clear things
    // like temporary brands. -- bwr
    if (item_dropped == you.equip[EQ_WEAPON]
        && quant_drop >= you.inv[item_dropped].quantity)
    {
        unwield_item();
        canned_msg( MSG_EMPTY_HANDED );
    }

    const dungeon_feature_type my_grid = grd[you.x_pos][you.y_pos];

    if ( !grid_destroys_items(my_grid)
         && !copy_item_to_grid( you.inv[item_dropped], 
                                you.x_pos, you.y_pos, quant_drop, true ))
    {
        mpr( "Too many items on this level, not dropping the item." );
        return (false);
    }

    mprf("You drop %s.",
         quant_name(you.inv[item_dropped], quant_drop, DESC_NOCAP_A).c_str());
    
    if ( grid_destroys_items(my_grid) )
    {
        if( !silenced(you.pos()) )
            mprf(MSGCH_SOUND, grid_item_destruction_message(my_grid));
    }
    else if (strstr(you.inv[item_dropped].inscription.c_str(), "=s") != 0)
        stashes.add_stash();
   
    dec_inv_item_quantity( item_dropped, quant_drop );
    you.turn_is_over = true;

    if (try_offer
        && you.religion != GOD_NO_GOD
        && you.duration[DUR_PRAYER]
        && grid_altar_god(grd[you.x_pos][you.y_pos]) == you.religion)
    {
        offer_items();
    }

    return (true);
}

static std::string drop_menu_invstatus(const Menu *menu)
{
    char buf[100];
    const int cap = carrying_capacity(BS_UNENCUMBERED);

    std::string s_newweight;
    std::vector<MenuEntry*> se = menu->selected_entries();
    if (!se.empty())
    {
        int newweight = you.burden;
        for (int i = 0, size = se.size(); i < size; ++i)
        {
            const item_def *item = static_cast<item_def *>( se[i]->data );
            newweight -= item_mass(*item) * se[i]->selected_qty;
        }

        snprintf(buf, sizeof buf, ">%d.%d", newweight / 10, newweight % 10);
        s_newweight = buf;
    }

    snprintf(buf, sizeof buf, "(Inv: %d.%d%s/%d.%d aum)",
            you.burden / 10, you.burden % 10,
            s_newweight.c_str(),
            cap / 10, cap % 10);
    return (buf);
}

static std::string drop_menu_title(const Menu *menu, const std::string &oldt)
{
    std::string res = drop_menu_invstatus(menu) + " " + oldt;
    if (menu->is_set( MF_MULTISELECT ))
        res = "[Multidrop] " + res;

    return (res);
}

int get_equip_slot(const item_def *item)
{
    int worn = -1;
    if (item && in_inventory(*item))
    {
        for (int i = 0; i < NUM_EQUIP; ++i)
        {
            if (you.equip[i] == item->link)
            {
                worn = i;
                break;
            }
        }
    }
    return worn;
}

static std::string drop_selitem_text( const std::vector<MenuEntry*> *s )
{
    char buf[130];
    bool extraturns = false;

    if (s->empty())
        return "";

    for (int i = 0, size = s->size(); i < size; ++i)
    {
        const item_def *item = static_cast<item_def *>( (*s)[i]->data );
        const int eq = get_equip_slot(item);
        if (eq > EQ_WEAPON && eq < NUM_EQUIP)
        {
            extraturns = true;
            break;
        }
    }
    
    snprintf( buf, sizeof buf, " (%lu%s turn%s)", 
                (unsigned long) (s->size()),
                extraturns? "+" : "",
                s->size() > 1? "s" : "" );
    return buf;
}

std::vector<SelItem> items_for_multidrop;

// Arrange items that have been selected for multidrop so that
// equipped items are dropped after other items, and equipped items
// are dropped in the same order as their EQ_ slots are numbered.
static bool drop_item_order(const SelItem &first, const SelItem &second)
{
    const item_def &i1 = you.inv[first.slot];
    const item_def &i2 = you.inv[second.slot];

    const int slot1 = get_equip_slot(&i1),
              slot2 = get_equip_slot(&i2);

    if (slot1 != -1 && slot2 != -1)
        return (slot1 < slot2);
    else if (slot1 != -1 && slot2 == -1)
        return (false);
    else if (slot2 != -1 && slot1 == -1)
        return (true);

    return (first.slot < second.slot);
}

//---------------------------------------------------------------
//
// drop
//
// Prompts the user for an item to drop
//
//---------------------------------------------------------------
void drop(void)
{
    if (inv_count() < 1 && you.gold == 0)
    {
        canned_msg(MSG_NOTHING_CARRIED);
        return;
    }

    std::vector<SelItem> tmp_items;
    tmp_items = prompt_invent_items( "Drop what?", MT_DROP, -1, 
                                     drop_menu_title, true, true, 0,
                                     &Options.drop_filter, drop_selitem_text,
                                     &items_for_multidrop );

    if (tmp_items.empty())
    {
        canned_msg( MSG_OK );
        return;
    }

    // Sort the dropped items so we don't see weird behaviour when
    // dropping a worn robe before a cloak (old behaviour: remove
    // cloak, remove robe, wear cloak, drop robe, remove cloak, drop
    // cloak).
    std::sort( tmp_items.begin(), tmp_items.end(), drop_item_order );

    // If the user answers "no" to an item an with a warning inscription,
    // then remove it from the list of items to drop by not copying it
    // over to items_for_multidrop
    items_for_multidrop.clear();
    for ( unsigned int i = 0; i < tmp_items.size(); ++i )
        if ( check_warning_inscriptions( *(tmp_items[i].item), OPER_DROP))
            items_for_multidrop.push_back(tmp_items[i]);

    if ( items_for_multidrop.empty() ) // only one item
    {
        canned_msg( MSG_OK );
        return;
    }

    if ( items_for_multidrop.size() == 1 ) // only one item
    {
        drop_item( items_for_multidrop[0].slot,
                   items_for_multidrop[0].quantity,
                   true );
        items_for_multidrop.clear();
        you.turn_is_over = true;
    }
    else
        start_delay( DELAY_MULTIDROP, items_for_multidrop.size() );
}

//---------------------------------------------------------------
//
// update_corpses
//
// Update all of the corpses and food chunks on the floor. (The
// elapsed time is a double because this is called when we re-
// enter a level and a *long* time may have elapsed).
//
//---------------------------------------------------------------
void update_corpses(double elapsedTime)
{
    int cx, cy;

    if (elapsedTime <= 0.0)
        return;

    const long rot_time = static_cast<long>(elapsedTime / 20.0);

    for (int c = 0; c < MAX_ITEMS; c++)
    {
        item_def &it = mitm[c];
        
        if (!is_valid_item(it))
            continue;

        if (it.base_type != OBJ_CORPSES && it.base_type != OBJ_FOOD)
        {
            continue;
        }

        if (it.base_type == OBJ_CORPSES
            && it.sub_type > CORPSE_SKELETON)
        {
            continue;
        }

        if (it.base_type == OBJ_FOOD && it.sub_type != FOOD_CHUNK)
        {
            continue;
        }

        if (rot_time >= it.special && !is_being_butchered(it))
        {
            if (it.base_type == OBJ_FOOD)
            {
                destroy_item(c);
            }
            else
            {
                if (it.sub_type == CORPSE_SKELETON
                    || !mons_skeleton( it.plus ))
                {
                    destroy_item(c);
                }
                else
                {
                    it.sub_type = CORPSE_SKELETON;
                    it.special = 200;
                    it.colour = LIGHTGREY;
                }
            }
        }
        else
        {
            ASSERT(rot_time < 256);
            it.special -= rot_time;
        }
    }

    int fountain_checks = static_cast<int>(elapsedTime / 1000.0);
    if (random2(1000) < static_cast<int>(elapsedTime) % 1000)
        fountain_checks += 1;

    // dry fountains may start flowing again
    if (fountain_checks > 0)
    {
        for (cx=0; cx<GXM; cx++)
        {
            for (cy=0; cy<GYM; cy++)
            {
                if (grd[cx][cy] > DNGN_SPARKLING_FOUNTAIN
                    && grd[cx][cy] < DNGN_PERMADRY_FOUNTAIN)
                {
                    for (int i=0; i<fountain_checks; i++)
                    {
                        if (one_chance_in(100))
                        {
                            if (grd[cx][cy] > DNGN_SPARKLING_FOUNTAIN)
                                grd[cx][cy] =
                                    static_cast<dungeon_feature_type>(
                                        grd[cx][cy] - 1);
                        }
                    }
                }
            }
        }
    }
}

//---------------------------------------------------------------
//
// update_level
//
// Update the level when the player returns to it.
//
//---------------------------------------------------------------
void update_level( double elapsedTime )
{
    int m, i;
    const int turns = static_cast<int>(elapsedTime / 10.0);

#if DEBUG_DIAGNOSTICS
    int mons_total = 0;

    mprf(MSGCH_DIAGNOSTICS, "turns: %d", turns );
#endif

    update_corpses( elapsedTime );

    dungeon_events.fire_event(
        dgn_event(DET_TURN_ELAPSED, coord_def(0, 0), turns * 10));

    for (m = 0; m < MAX_MONSTERS; m++)
    {
        struct monsters *mon = &menv[m];

        if (mon->type == -1)
            continue;

#if DEBUG_DIAGNOSTICS
        mons_total++;
#endif

        // following monsters don't get movement
        if (mon->flags & MF_JUST_SUMMONED)
            continue;

        // XXX: Allow some spellcasting (like Healing and Teleport)? -- bwr
        // const bool healthy = (mon->hit_points * 2 > mon->max_hit_points);

        // This is the monster healing code, moved here from tag.cc:
        if (monster_descriptor( mon->type, MDSC_REGENERATES )
            || mon->type == MONS_PLAYER_GHOST)
        {   
            heal_monster( mon, turns, false );
        }
        else
        {   
            heal_monster( mon, (turns / 10), false );
        }

        if (turns >= 10)
            mon->timeout_enchantments( turns / 10 );

        // Don't move water, lava, or stationary monsters around
        if (monster_habitat( mon->type ) != DNGN_FLOOR
            || mons_is_stationary( mon ))
        {
            continue;
        }

        // Let sleeping monsters lie
        if (mon->behaviour == BEH_SLEEP || mons_is_paralysed(mon))
            continue;

        const int range = (turns * mon->speed) / 10;
        const int moves = (range > 50) ? 50 : range;

        // const bool short_time = (range >= 5 + random2(10));
        const bool long_time  = (range >= (500 + roll_dice( 2, 500 )));

        const bool ranged_attack = (mons_has_ranged_spell( mon ) 
                                    || mons_has_ranged_attack( mon )); 

#if DEBUG_DIAGNOSTICS
        // probably too annoying even for DEBUG_DIAGNOSTICS
        mprf(MSGCH_DIAGNOSTICS,
             "mon #%d: range %d; long %d; "
             "pos (%d,%d); targ %d(%d,%d); flags %ld", 
             m, range, long_time, mon->x, mon->y, 
             mon->foe, mon->target_x, mon->target_y, mon->flags );
#endif 

        if (range <= 0)
            continue;

        if (long_time 
            && (mon->behaviour == BEH_FLEE 
                || mon->behaviour == BEH_CORNERED
                || testbits( mon->flags, MF_BATTY )
                || ranged_attack
                || coinflip()))
        {
            if (mon->behaviour != BEH_WANDER)
            {
                mon->behaviour = BEH_WANDER;
                mon->foe = MHITNOT;
                mon->target_x = 10 + random2( GXM - 10 ); 
                mon->target_y = 10 + random2( GYM - 10 ); 
            }
            else 
            {
                // monster will be sleeping after we move it
                mon->behaviour = BEH_SLEEP; 
            }
        }
        else if (ranged_attack)
        {
            // if we're doing short time movement and the monster has a 
            // ranged attack (missile or spell), then the monster will
            // flee to gain distance if its "too close", else it will 
            // just shift its position rather than charge the player. -- bwr
            if (grid_distance(mon->x, mon->y, mon->target_x, mon->target_y) < 3)
            {
                mon->behaviour = BEH_FLEE;

                // if the monster is on the target square, fleeing won't work
                if (mon->x == mon->target_x && mon->y == mon->target_y)
                {
                    if (you.x_pos != mon->x || you.y_pos != mon->y)
                    {
                        // flee from player's position if different
                        mon->target_x = you.x_pos;
                        mon->target_y = you.y_pos;
                    }
                    else
                    {
                        // randomize the target so we have a direction to flee
                        mon->target_x += (random2(3) - 1);
                        mon->target_y += (random2(3) - 1);
                    }
                }

#if DEBUG_DIAGNOSTICS
                mpr( "backing off...", MSGCH_DIAGNOSTICS );
#endif
            }
            else
            {
                shift_monster( mon, mon->x, mon->y );

#if DEBUG_DIAGNOSTICS
                mprf(MSGCH_DIAGNOSTICS, "shifted to (%d,%d)", mon->x, mon->y);
#endif
                continue;
            }
        }

	const hexcoord target(mon->target_x, mon->target_y);
	hexdir dir;
	int rot;
	hexcoord pos = mon->pos();
        // dirt simple movement:
        for (i = 0; i < moves; i++)
        {
	    dir = hex_dir_towards(target - pos);

            if (dir == hexdir::zero)
		break;

            if (mon->behaviour == BEH_FLEE)
            {
		dir *= -1;
            }

	    for (rot = 0; rot < 3; rot++)
		if (in_G_bounds(pos + dir.rotated(rot==2?-1:rot))
		    && grd(pos + dir.rotated(rot==2?-1:rot)) < DNGN_FLOOR)
                break;
	    if (rot == 3)
		break;
	    else
		dir.rotate(rot==2?-1:rot);

	    pos += dir;
        }

        if (!shift_monster( mon, pos.x, pos.y ))
            shift_monster( mon, mon->x, mon->y );

#if DEBUG_DIAGNOSTICS
        mprf(MSGCH_DIAGNOSTICS, "moved to (%d,%d)", mon->x, mon->y );
#endif
    }

#if DEBUG_DIAGNOSTICS
    mprf(MSGCH_DIAGNOSTICS, "total monsters on level = %d", mons_total );
#endif

    for (i = 0; i < MAX_CLOUDS; i++)
        delete_cloud( i );
}


//---------------------------------------------------------------
//
// handle_time
//
// Do various time related actions... 
// This function is called about every 20 turns.
//
//---------------------------------------------------------------
void handle_time( long time_delta )
{
    int temp_rand;              // probability determination {dlb}

    // so as not to reduplicate f(x) calls {dlb}
    unsigned int which_miscast = SPTYP_RANDOM;

    bool summon_instead;        // for branching within a single switch {dlb}
    int which_beastie = MONS_PROGRAM_BUG;       // error trapping {dlb}
    unsigned char i;            // loop variable {dlb}
    bool new_rotting_item = false; //mv: becomes true when some new item becomes rotting

    // BEGIN - Nasty things happen to people who spend too long in Hell:
    if (player_in_hell() && coinflip())
    {
        temp_rand = random2(17);

        mpr((temp_rand == 0)  ? "\"You will not leave this place.\"" :
            (temp_rand == 1)  ? "\"Die, mortal!\"" :
            (temp_rand == 2)  ? "\"We do not forgive those who trespass against us!\"" :
            (temp_rand == 3)  ? "\"Trespassers are not welcome here!\"" :
            (temp_rand == 4)  ? "\"You do not belong in this place!\"" :
            (temp_rand == 5)  ? "\"Leave now, before it is too late!\"" :
            (temp_rand == 6)  ? "\"We have you now!\"" :
            // plain messages
            (temp_rand == 7)  ? (player_can_smell()) ? "You smell brimstone." :
                                "Brimstone rains from above." :
            (temp_rand == 8)  ? "You feel lost and a long, long way from home..." :
            (temp_rand == 9)  ? "You shiver with fear." :
            // warning
            (temp_rand == 10) ? "You feel a terrible foreboding..." :
            (temp_rand == 11) ? "Something frightening happens." :
            (temp_rand == 12) ? "You sense an ancient evil watching you..." :
            (temp_rand == 13) ? "You suddenly feel all small and vulnerable." :
            (temp_rand == 14) ? "You sense a hostile presence." :
            // sounds
            (temp_rand == 15) ? "A gut-wrenching scream fills the air!" :
            (temp_rand == 16) ? "You hear words spoken in a strange and terrible language..."
                              : "You hear diabolical laughter!",
                                (temp_rand < 7  ? MSGCH_TALK :
                                 temp_rand < 10 ? MSGCH_PLAIN :
                                 temp_rand < 15 ? MSGCH_WARN
                                                : MSGCH_SOUND) );

        temp_rand = random2(27);

        if (temp_rand > 17)     // 9 in 27 odds {dlb}
        {
            temp_rand = random2(8);

            if (temp_rand > 3)  // 4 in 8 odds {dlb}
                which_miscast = SPTYP_NECROMANCY;
            else if (temp_rand > 1)     // 2 in 8 odds {dlb}
                which_miscast = SPTYP_SUMMONING;
            else if (temp_rand > 0)     // 1 in 8 odds {dlb}
                which_miscast = SPTYP_CONJURATION;
            else                // 1 in 8 odds {dlb}
                which_miscast = SPTYP_ENCHANTMENT;

            miscast_effect( which_miscast, 4 + random2(6), random2avg(97, 3),
                            100, "the effects of Hell" );
        }
        else if (temp_rand > 7) // 10 in 27 odds {dlb}
        {
            // 60:40 miscast:summon split {dlb}
            summon_instead = (random2(5) > 2);

            switch (you.where_are_you)
            {
            case BRANCH_DIS:
                if (summon_instead)
                    which_beastie = summon_any_demon(DEMON_GREATER);
                else
                    which_miscast = SPTYP_EARTH;
                break;
            case BRANCH_GEHENNA:
                if (summon_instead)
                    which_beastie = MONS_FIEND;
                else
                    which_miscast = SPTYP_FIRE;
                break;
            case BRANCH_COCYTUS:
                if (summon_instead)
                    which_beastie = MONS_ICE_FIEND;
                else
                    which_miscast = SPTYP_ICE;
                break;
            case BRANCH_TARTARUS:
                if (summon_instead)
                    which_beastie = MONS_SHADOW_FIEND;
                else
                    which_miscast = SPTYP_NECROMANCY;
                break;
            default:        // this is to silence gcc compiler warnings {dlb}
                if (summon_instead)
                    which_beastie = MONS_FIEND;
                else
                    which_miscast = SPTYP_NECROMANCY;
                break;
            }

            if (summon_instead)
            {
                create_monster( which_beastie, 0, BEH_HOSTILE, you.x_pos,
                                you.y_pos, MHITYOU, 250 );
            }
            else
            {
                miscast_effect( which_miscast, 4 + random2(6),
                                random2avg(97, 3), 100, "the effects of Hell" );
            }
        }

        // NB: no "else" - 8 in 27 odds that nothing happens through
        // first chain {dlb}
        // also note that the following is distinct from and in
        // addition to the above chain:

        // try to summon at least one and up to five random monsters {dlb}
        if (one_chance_in(3))
        {
            create_monster( RANDOM_MONSTER, 0, BEH_HOSTILE, 
                            you.x_pos, you.y_pos, MHITYOU, 250 );

            for (i = 0; i < 4; i++)
            {
                if (one_chance_in(3))
                {
                    create_monster( RANDOM_MONSTER, 0, BEH_HOSTILE,
                                    you.x_pos, you.y_pos, MHITYOU, 250 );
                }
            }
        }
    }
    // END - special Hellish things...

    // Adjust the player's stats if s/he's diseased (or recovering).
    if (!you.disease)
    {
        if (you.strength < you.max_strength && one_chance_in(100))
        {
            mpr("You feel your strength returning.", MSGCH_RECOVERY);
            you.strength++;
            you.redraw_strength = 1;
        }

        if (you.dex < you.max_dex && one_chance_in(100))
        {
            mpr("You feel your dexterity returning.", MSGCH_RECOVERY);
            you.dex++;
            you.redraw_dexterity = 1;
        }

        if (you.intel < you.max_intel && one_chance_in(100))
        {
            mpr("You feel your intelligence returning.", MSGCH_RECOVERY);
            you.intel++;
            you.redraw_intelligence = 1;
        }
    }
    else
    {
        if (one_chance_in(30))
        {
            mpr("Your disease is taking its toll.", MSGCH_WARN);
            lose_stat(STAT_RANDOM, 1);
        }
    }

    // Adjust the player's stats if s/he has the deterioration mutation
    if (you.mutation[MUT_DETERIORATION]
        && random2(200) <= you.mutation[MUT_DETERIORATION] * 5 - 2)
    {
        lose_stat(STAT_RANDOM, 1);
    }

    int added_contamination = 0;

    // Account for mutagenic radiation.  Invis and haste will give the
    // player about .1 points per turn, mutagenic randarts will give
    // about 1.5 points on average, so they can corrupt the player
    // quite quickly.  Wielding one for a short battle is OK, which is
    // as things should be.   -- GDL
    if (you.duration[DUR_INVIS] && random2(10) < 6)
        added_contamination++;

    if (you.duration[DUR_HASTE] && !you.duration[DUR_BERSERKER]
        && random2(10) < 6)
    {
        added_contamination++;
    }

    if (const int randart_glow = scan_randarts(RAP_MUTAGENIC))
    {
        // Reduced randart glow. Note that one randart will contribute
        // 2 - 5 units of glow to randart_glow. A randart with a mutagen
        // index of 2 does about 0.58 points of contamination per turn.
        // A randart with a mutagen index of 5 does about 0.7 points of
        // contamination per turn.
        
        const int mean_glow   = 500 + randart_glow * 40;
        const int actual_glow = mean_glow / 2 + random2(mean_glow);
        added_contamination += div_rand_round(actual_glow, 1000);
    }

    // we take off about .5 points per turn
    if (!you.duration[DUR_INVIS] && !you.duration[DUR_HASTE] && coinflip())
        added_contamination -= 1;

    contaminate_player( added_contamination );

    // only check for badness once every other turn
    if (coinflip())
    {
        // [ds] Be less harsh with glow mutation; Brent and Mark Mackey note
        // that the commented out random2(X) <= MC check was a bug. I've
        // uncommented it but dropped the roll sharply from 150. (Brent used
        // the original roll of 150 for 4.1.2, but I think players are
        // sufficiently used to beta 26's unkindness that we can use a lower
        // roll.)
        if (you.magic_contamination >= 5
            && random2(50) <= you.magic_contamination)
        {
            mpr("Your body shudders with the violent release of wild energies!", MSGCH_WARN);

            // for particularly violent releases, make a little boom
            if (you.magic_contamination > 25 && one_chance_in(3))
            {
                struct bolt boom;
                boom.type = SYM_BURST;
                boom.colour = BLACK;
                boom.flavour = BEAM_RANDOM;
                boom.target_x = you.x_pos;
                boom.target_y = you.y_pos;
                boom.damage = dice_def( 3, (you.magic_contamination / 2) );
                boom.thrower = KILL_MISC;
                boom.aux_source = "a magical explosion";
                boom.beam_source = NON_MONSTER;
                boom.is_beam = false;
                boom.is_tracer = false;
                boom.is_explosion = true;
                boom.name = "magical storm";

                boom.ench_power = (you.magic_contamination * 5);
                boom.ex_size = (you.magic_contamination / 15);
                if (boom.ex_size > 9)
                    boom.ex_size = 9;

                explosion(boom);
            }

            // we want to warp the player, not do good stuff!
            if (one_chance_in(5))
                mutate(RANDOM_MUTATION);
            else
                give_bad_mutation(coinflip());

            // we're meaner now, what with explosions and whatnot, but
            // we dial down the contamination a little faster if its actually
            // mutating you.  -- GDL
            contaminate_player( -(random2(you.magic_contamination / 4) + 1) );
        }
    }

    // Random chance to identify staff in hand based off of Spellcasting
    // and an appropriate other spell skill... is 1/20 too fast?
    if (you.equip[EQ_WEAPON] != -1
        && you.inv[you.equip[EQ_WEAPON]].base_type == OBJ_STAVES
        && !item_type_known( you.inv[you.equip[EQ_WEAPON]] )
        && one_chance_in(20))
    {
        int total_skill = you.skills[SK_SPELLCASTING];

        switch (you.inv[you.equip[EQ_WEAPON]].sub_type)
        {
        case STAFF_WIZARDRY:
        case STAFF_ENERGY:
            total_skill += you.skills[SK_SPELLCASTING];
            break;
        case STAFF_FIRE:
            if (you.skills[SK_FIRE_MAGIC] > you.skills[SK_ICE_MAGIC])
                total_skill += you.skills[SK_FIRE_MAGIC];
            else
                total_skill += you.skills[SK_ICE_MAGIC];
            break;
        case STAFF_COLD:
            if (you.skills[SK_ICE_MAGIC] > you.skills[SK_FIRE_MAGIC])
                total_skill += you.skills[SK_ICE_MAGIC];
            else
                total_skill += you.skills[SK_FIRE_MAGIC];
            break;
        case STAFF_AIR:
            if (you.skills[SK_AIR_MAGIC] > you.skills[SK_EARTH_MAGIC])
                total_skill += you.skills[SK_AIR_MAGIC];
            else
                total_skill += you.skills[SK_EARTH_MAGIC];
            break;
        case STAFF_EARTH:
            if (you.skills[SK_EARTH_MAGIC] > you.skills[SK_AIR_MAGIC])
                total_skill += you.skills[SK_EARTH_MAGIC];
            else
                total_skill += you.skills[SK_AIR_MAGIC];
            break;
        case STAFF_POISON:
            total_skill += you.skills[SK_POISON_MAGIC];
            break;
        case STAFF_DEATH:
            total_skill += you.skills[SK_NECROMANCY];
            break;
        case STAFF_CONJURATION:
            total_skill += you.skills[SK_CONJURATIONS];
            break;
        case STAFF_ENCHANTMENT:
            total_skill += you.skills[SK_ENCHANTMENTS];
            break;
        case STAFF_SUMMONING:
            total_skill += you.skills[SK_SUMMONINGS];
            break;
        }

        if (random2(100) < total_skill)
        {
            item_def& item = you.inv[you.equip[EQ_WEAPON]];
            set_ident_flags( item, ISFLAG_IDENT_MASK );

            mprf("You are wielding %s.", item.name(DESC_NOCAP_A).c_str());
            more();

            you.wield_change = true;
        }
    }

    // Check to see if an upset god wants to do something to the player
    // jmf: moved huge thing to religion.cc
    handle_god_time();

    // If the player has the lost mutation forget portions of the map
    if (you.mutation[MUT_LOST] && !wearing_amulet(AMU_CLARITY) &&
        (random2(100) <= you.mutation[MUT_LOST] * 5) )
        forget_map(5 + random2(you.mutation[MUT_LOST] * 10));

    // Update all of the corpses and food chunks on the floor
    update_corpses(time_delta);

    // Update all of the corpses and food chunks in the player's
    // inventory {should be moved elsewhere - dlb}


    for (i = 0; i < ENDOFPACK; i++)
    {
        if (you.inv[i].quantity < 1)
            continue;

        if (you.inv[i].base_type != OBJ_CORPSES && you.inv[i].base_type != OBJ_FOOD)
            continue;

        if (you.inv[i].base_type == OBJ_CORPSES
            && you.inv[i].sub_type > CORPSE_SKELETON)
        {
            continue;
        }

        if (you.inv[i].base_type == OBJ_FOOD && you.inv[i].sub_type != FOOD_CHUNK)
            continue;

        if ((time_delta / 20) >= you.inv[i].special)
        {
            if (you.inv[i].base_type == OBJ_FOOD)
            {
                if (you.equip[EQ_WEAPON] == i)
                    unwield_item();

                mpr("Your equipment suddenly weighs less.", MSGCH_ROTTEN_MEAT);
                // FIXME should replace with a destroy_item call
                you.inv[i].quantity = 0;
                burden_change();
                continue;
            }

            if (you.inv[i].sub_type == CORPSE_SKELETON)
                continue;       // carried skeletons are not destroyed

            if (!mons_skeleton( you.inv[i].plus ))
            {
                if (you.equip[EQ_WEAPON] == i)
                    unwield_item();

                // FIXME should replace with a destroy_item call
                you.inv[i].quantity = 0;
                burden_change();
                continue;
            }

            you.inv[i].sub_type = 1;
            you.inv[i].special = 0;
            you.inv[i].colour = LIGHTGREY;
            you.wield_change = true;
            continue;
        }

        you.inv[i].special -= (time_delta / 20);

        if (you.inv[i].special < 100 && (you.inv[i].special + (time_delta / 20)>=100))
        {
            new_rotting_item = true; 
        }
    }

    //mv: messages when chunks/corpses become rotten
    if (new_rotting_item)
    {
        switch (you.species)
        {
        // XXX: should probably still notice?
        case SP_MUMMY: // no smell 
        case SP_TROLL: // stupid, living in mess - doesn't care about it
            break;

        case SP_GHOUL: //likes it
            temp_rand = random2(8);
            mpr( ((temp_rand  < 5) ? "You smell something rotten." :
                  (temp_rand == 5) ? "The smell of rotting flesh makes you hungry." :
                  (temp_rand == 6) ? "You smell decay. Yum-yum."
                                   : "Wow! There is something tasty in your inventory."),
                MSGCH_ROTTEN_MEAT );
            break;

        case SP_KOBOLD: //mv: IMO these race aren't so "touchy"
        case SP_OGRE:
        case SP_MINOTAUR:
        case SP_HILL_ORC:
            temp_rand = random2(8);
            mpr( ((temp_rand  < 5) ? "You smell something rotten." :
                  (temp_rand == 5) ? "You smell rotting flesh." :
                  (temp_rand == 6) ? "You smell decay."
                                   : "There is something rotten in your inventory."),
                MSGCH_ROTTEN_MEAT );
            break;

        default:
            temp_rand = random2(8);
            mpr( ((temp_rand  < 5) ? "You smell something rotten." :
                  (temp_rand == 5) ? "The smell of rotting flesh makes you sick." :
                  (temp_rand == 6) ? "You smell decay. Yuck!"
                                   : "Ugh! There is something really disgusting in your inventory."), 
                MSGCH_ROTTEN_MEAT );
            break;
        }
        learned_something_new(TUT_ROTTEN_FOOD);
    }

    // exercise armour *xor* stealth skill: {dlb}
    if (!player_light_armour(true))
    {
        if (random2(1000) <= item_mass( you.inv[you.equip[EQ_BODY_ARMOUR]] ))
            return;

        if (one_chance_in(6))   // lowered random roll from 7 to 6 -- bwross
            exercise(SK_ARMOUR, 1);
    }
    else                        // exercise stealth skill:
    {
        if (you.burden_state != BS_UNENCUMBERED || you.duration[DUR_BERSERKER])
            return;

        if (you.special_wield == SPWLD_SHADOW)
            return;

        if (you.equip[EQ_BODY_ARMOUR] != -1
            && random2( item_mass( you.inv[you.equip[EQ_BODY_ARMOUR]] )) >= 100)
        {
            return;
        }

        if (one_chance_in(18))
	{
	    // At higher SK_STEALTH levels, it stops being exercised so much
	    // here, as exercise instead comes from active use of stealth
	    // subskills.
	    if (!(you.skills[SK_STEALTH] >= 4 && one_chance_in(5)
		    || you.skills[SK_STEALTH] >= 6 && one_chance_in(5)
		    || you.skills[SK_STEALTH] >= 8 && one_chance_in(5)))
		exercise(SK_STEALTH, 1);
	}
    }

    return;
}                               // end handle_time()

static void autoinscribe_item( item_def& item )
{
    std::string iname = item.name(DESC_INVENTORY);

    /* if there's an inscription already, do nothing */
    if ( item.inscription.size() > 0 )
        return;

    for ( unsigned i = 0; i < Options.autoinscriptions.size(); ++i )
    {
        if ( Options.autoinscriptions[i].first.matches(iname) )
        {
            // Don't autoinscribe dropped items on ground with
            // "=g".  If the item matches a rule which adds "=g",
            // "=g" got added to it before it was dropped, and
            // then the user explictly removed it because they
            // don't want to autopickup it again.
            std::string str = Options.autoinscriptions[i].second;
            if ((item.flags & ISFLAG_DROPPED)
                && (item.x != -1 || item.y != -1))
            {
                str = replace_all(str, "=g", "");
            }

            // Note that this might cause the item inscription to
            // pass 80 characters.
            item.inscription += str;
        }
    }
}

static void autoinscribe_floor_items()
{
    int o, next;
    o = igrd[you.x_pos][you.y_pos];

    while (o != NON_ITEM)
    {
        next = mitm[o].link;
        autoinscribe_item( mitm[o] );
        o = next;
    }
}

static void autoinscribe_inventory()
{
    for (int i = 0; i < ENDOFPACK; i++)
    {
        if (is_valid_item(you.inv[i]))
            autoinscribe_item( you.inv[i] );
    }
}

bool need_to_autoinscribe()
{
    return will_autoinscribe;
}

void request_autoinscribe(bool do_inscribe)
{
    will_autoinscribe = do_inscribe;
}

void autoinscribe()
{
    autoinscribe_floor_items();
    autoinscribe_inventory();

    will_autoinscribe = false;
}

static inline std::string autopickup_item_name(const item_def &item)
{
    return userdef_annotate_item(STASH_LUA_SEARCH_ANNOTATE, &item, true)
        + item.name(DESC_PLAIN);    
}

static bool is_denied_autopickup(const item_def &item, std::string &iname)
{
    if (iname.empty())
        iname = autopickup_item_name(item);
    for (unsigned i = 0, size = Options.never_pickup.size(); i < size; ++i)
    {
        if (Options.never_pickup[i].matches(iname))
            return (true);
    }
    return false;
}

static bool is_forced_autopickup(const item_def &item, std::string &iname)
{
    if (iname.empty())
        iname = autopickup_item_name(item);
    for (unsigned i = 0, size = Options.always_pickup.size(); i < size; ++i)
    {
        if (Options.always_pickup[i].matches(iname))
            return (true);
    }
    return false;
}

bool item_needs_autopickup(const item_def &item)
{
    if (item_is_stationary(item))
        return (false);
    
    if (strstr(item.inscription.c_str(), "=g") != 0)
        return (true);

    if ((item.flags & ISFLAG_THROWN) && Options.pickup_thrown)
        return (true);

    std::string itemname;
    return (((Options.autopickups & (1L << item.base_type))
#ifdef CLUA_BINDINGS
             && clua.callbooleanfn(true, "ch_autopickup", "u", &item)
#endif
             || is_forced_autopickup(item, itemname))
            && (Options.pickup_dropped || !(item.flags & ISFLAG_DROPPED))
            && !is_denied_autopickup(item, itemname));
}
              
bool can_autopickup()
{
    // [ds] Checking for autopickups == 0 is a bad idea because
    // autopickup is still possible with inscriptions and
    // pickup_thrown.
    if (!Options.autopickup_on)
        return (false);

    if (you.attribute[ATTR_TRANSFORMATION] == TRAN_AIR 
            && you.duration[DUR_TRANSFORMATION] > 0)
        return (false);

    if (you.flight_mode() == FL_LEVITATE)
        return (false);

    if ( Options.safe_autopickup && !i_feel_safe() )
        return (false);

    return (true);
}

static void do_autopickup()
{
    //David Loewenstern 6/99
    int result, o, next;
    int n_did_pickup = 0;
    int n_tried_pickup = 0;

    will_autopickup = false;
    
    if (!can_autopickup())
    {
        item_check(false);
        return;
    }
    
    o = igrd[you.x_pos][you.y_pos];

    while (o != NON_ITEM)
    {
        next = mitm[o].link;

        if (item_needs_autopickup(mitm[o]))
        {
            int num_to_take = mitm[o].quantity;
            if ( Options.autopickup_no_burden && item_mass(mitm[o]) != 0)
            {
                int num_can_take =
                    (carrying_capacity(you.burden_state) - you.burden) /
                    item_mass(mitm[o]);

                if ( num_can_take < num_to_take )
                {
                    if (!n_tried_pickup)
                        mpr("You can't pick everything up without burdening "
                            "yourself.");
                    n_tried_pickup++;
                    num_to_take = num_can_take;
                }

                if ( num_can_take == 0 )
                {
                    o = next;
                    continue;
                }
            }

            const unsigned long iflags(mitm[o].flags);
            mitm[o].flags &= ~(ISFLAG_THROWN | ISFLAG_DROPPED);

            result = move_item_to_player(o, num_to_take);

            if (result == 0 || result == -1)
            {
                n_tried_pickup++;
                if (result == 0)
                    mpr("You can't carry any more.");
                else
                    mpr("Your pack is full.");
                mitm[o].flags = iflags;
                break;
            }

            n_did_pickup++;
        }

        o = next;
    }

    if (n_did_pickup)
        you.turn_is_over = true;

    item_check(false);
    
    explore_pickup_event(n_did_pickup, n_tried_pickup);
}

void autopickup()
{
    autoinscribe_floor_items();
    do_autopickup();
}

int inv_count(void)
{
    int count=0;

    for(int i=0; i< ENDOFPACK; i++)
    {
        if (is_valid_item( you.inv[i] ))
            count++;
    }

    return count;
}

static bool find_subtype_by_name(item_def &item,
                                 object_class_type base_type, int ntypes,
                                 const std::string &name)
{
    // In order to get the sub-type, we'll fill out the base type... 
    // then we're going to iterate over all possible subtype values 
    // and see if we get a winner. -- bwr

    item.base_type = base_type;
    item.sub_type  = OBJ_RANDOM;
    item.plus      = 0;
    item.plus2     = 0;
    item.special   = 0;
    item.flags     = 0;
    item.quantity  = 1;
    set_ident_flags( item, ISFLAG_KNOW_TYPE | ISFLAG_KNOW_PROPERTIES );

    if (base_type == OBJ_ARMOUR) 
    {
        if (name.find("wizard's hat") != std::string::npos)
        {
            item.sub_type = ARM_HELMET;
            item.plus2 = THELM_WIZARD_HAT;
        }
        else if (name == "cap") // Don't search - too many possible collisions
        {
            item.sub_type = ARM_HELMET;
            item.plus2 = THELM_CAP;
        }
        else if (name == "helm")  // Don't search.
        {
            item.sub_type = ARM_HELMET;
            item.plus2 = THELM_HELM;
        }
    }

    if (item.sub_type != OBJ_RANDOM)
        return (true);

    int type_wanted = -1;

    for (int i = 0; i < ntypes; i++)
    {
        item.sub_type = i;
        if (name == lowercase_string(item.name(DESC_PLAIN)))
        {
            type_wanted = i;
            break;
        }
    }

    if (type_wanted != -1)
        item.sub_type = type_wanted;
    else
        item.sub_type = OBJ_RANDOM;

    return (item.sub_type != OBJ_RANDOM);
}

// Returns an incomplete item_def with base_type and sub_type set correctly
// for the given item name. If the name is not found, sets sub_type to
// OBJ_RANDOM.
item_def find_item_type(object_class_type base_type, std::string name)
{
    item_def item;
    item.base_type = OBJ_RANDOM;
    item.sub_type  = OBJ_RANDOM;
    lowercase(name);

    if (base_type == OBJ_RANDOM || base_type == OBJ_UNASSIGNED)
        base_type = OBJ_UNASSIGNED;
    
    static int max_subtype[] = 
    {
        NUM_WEAPONS,
        NUM_MISSILES,
        NUM_ARMOURS,
        NUM_WANDS,
        NUM_FOODS,
        0,              // unknown I
        NUM_SCROLLS,
        NUM_JEWELLERY,
        NUM_POTIONS,
        0,              // unknown II
        NUM_BOOKS,
        NUM_STAVES,
        0,              // Orbs         -- only one, handled specially
        NUM_MISCELLANY,
        0,              // corpses      -- handled specially
        0,              // gold         -- handled specially
        0,              // "gemstones"  -- no items of type
    };

    if (base_type == OBJ_UNASSIGNED)
    {
        for (unsigned i = 0; i < sizeof(max_subtype) / sizeof(*max_subtype);
             ++i)
        {
            if (!max_subtype[i])
                continue;
            if (find_subtype_by_name(item, static_cast<object_class_type>(i),
                                     max_subtype[i], name))
                break;
        }
    }
    else
    {
        find_subtype_by_name(item, base_type, max_subtype[base_type], name);
    }

    return (item);
}

////////////////////////////////////////////////////////////////////////
// item_def functions.

bool item_def::has_spells() const
{
    return ((base_type == OBJ_BOOKS && item_type_known(*this)
            && sub_type != BOOK_DESTRUCTION
            && sub_type != BOOK_MANUAL)
            || count_staff_spells(*this, true) > 1);
}

int item_def::book_number() const
{
    return (base_type == OBJ_BOOKS?   sub_type                      :
            base_type == OBJ_STAVES?  sub_type + 50 - STAFF_SMITING :
            -1);
}

bool item_def::cursed() const
{
    return (item_cursed(*this));
}

bool item_def::launched_by(const item_def &launcher) const
{
    if (base_type != OBJ_MISSILES)
        return (false);
    const missile_type mt = fires_ammo_type(launcher);
    return (sub_type == mt || (mt == MI_STONE && sub_type == MI_SLING_BULLET));
}

int item_def::index() const
{
    return (this - mitm.buffer());
}

int item_def::armour_rating() const
{
    if (!is_valid_item(*this) || base_type != OBJ_ARMOUR)
        return (0);

    return (property(*this, PARM_AC) + plus);
}
