/*
 *  File:       stuff.cc
 *  Summary:    Misc stuff.
 *  Written by: Linley Henzell
 *
 *  Modified for Crawl Reference by $Author: dshaligram $ on $Date: 2007-11-15 18:51:59 +0100 (Thu, 15 Nov 2007) $
 *
 *  Modified for Hexcrawl by Martin Bays, 2007
 *
 *  Change History (most recent first):
 *
 *   <3>    11/14/99     cdl    added random40
 *   <2>    11/06/99     cdl    added random22
 *   <1>     -/--/--     LRH    Created
 */


#ifndef STUFF_H
#define STUFF_H

#include "externs.h"

std::string make_time_string(time_t abs_time, bool terse = false);
void set_redraw_status( unsigned long flags );
void tag_followers();
void untag_followers();

void seed_rng();
void seed_rng(long seed);
void push_rng_state();
void pop_rng_state();

void cf_setseed();
bool coinflip();
int div_rand_round( int num, int den );
bool one_chance_in(int a_million);
int random2(int randmax);
int random_range(int low, int high);
int random_range(int low, int high, int nrolls);
int random_choose(int first, ...);
int random_choose_weighted(int weight, int first, ...);
unsigned long random_int();
int random2avg( int max, int rolls );
int bestroll(int max, int rolls);

int roll_dice( int num, int size );
int roll_dice( const struct dice_def &dice );
void scale_dice( dice_def &dice, int threshold = 24 );


int random2limit(int max, int limit);
int stepdown_value(int base_value, int stepping, int first_step,
                   int last_step, int ceiling_value);
int stat_mult( int stat_level, int value, int div = 20, int shift = 3 );
int stat_div( int stat_level, int value, int div = 20, int shift = 3 );
int skill_bump( int skill );
unsigned char get_ch();

int fuzz_value(int val, int lowfuzz, int highfuzz, int naverage = 2);

void cio_init();
void cio_cleanup();
void end(int exit_code, bool print_err = false,
         const char *message = NULL, ...);

void modify_all_stats(int STmod, int IQmod, int DXmod);

void redraw_screen();

void canned_msg(canned_message_type which_message);

bool yesno( const char * str, bool safe = true, int safeanswer = 0, 
            bool clear_after = true, bool interrupt_delays = true,
            bool noprompt = false );

int yesnoquit( const char* str, bool safe = true,
               int safeanswer = 0, bool clear_after = true );


bool in_hex_G_bounds( const hexcoord &pos, int boundary = 0, int square_boundary = 0 );
bool in_G_bounds( const hexcoord &pos, int boundary = 0 );
bool in_bounds( const hexcoord &pos );
bool map_bounds( const hexcoord &pos );

inline bool in_G_bounds(int x, int y, int boundary = 0)
{
    return in_G_bounds(hexcoord(x,y), boundary);
}

inline bool in_bounds(int x, int y)
{
    return in_bounds(hexcoord(x,y));
}

inline bool map_bounds(int x, int y)
{
    return map_bounds(hexcoord(x,y));
}

inline bool in_G_bounds(const coord_def &pos, int boundary = 0)
{
    return in_G_bounds(hexcoord(pos), boundary);
}

inline bool in_bounds(const coord_def &pos)
{
    return in_bounds(hexcoord(pos));
}

inline bool map_bounds(const coord_def &pos)
{
    return map_bounds(hexcoord(pos));
}

int grid_distance( int x, int y, int x2, int y2 );
int distance( int x, int y, int x2, int y2);
int distance( hexcoord c1, hexcoord c2 );
bool adjacent( int x, int y, int x2, int y2 );

bool silenced(int x, int y);
inline bool silenced(const coord_def &p) { return silenced(p.x, p.y); }

bool player_can_hear(int x, int y);
inline bool player_can_hear(const coord_def &p)
{
    return player_can_hear(p.x, p.y);
}

unsigned char random_colour();
unsigned char random_uncommon_colour();
bool is_element_colour( int col );
int  element_colour( int element, bool no_random = false );

char index_to_letter (int the_index);

int letter_to_index(int the_letter);

int near_stairs(const hexcoord &p, int max_dist,
                dungeon_char_type &stair_type,
                branch_type &branch);

inline bool testbits(unsigned long flags, unsigned long test)
{
    return ((flags & test) == test);
}

template <typename Z> inline Z sgn(Z x)
{
    return (x < 0? -1 : (x > 0? 1 : 0));
}

bool is_trap_square(int x, int y);
void zap_los_monsters();

class rng_save_excursion
{
public:
    rng_save_excursion()  { push_rng_state(); }
    ~rng_save_excursion() { pop_rng_state(); }
};

template<typename Iterator>
int choose_random_weighted(Iterator beg, const Iterator end)
{
    int totalweight = 0;
    int count = 0, result = 0;
    while ( beg != end )
    {
        totalweight += *beg;
        if ( random2(totalweight) < *beg )
            result = count;
        ++count;
        ++beg;
    }
    return result;
}

hexdir random_hex(int radius);
hexdir random_direction();
hexdir hex_dir_towards(const hexdir &hex);
bool hexdir_is_straight(const hexdir &d);

int random_rod_subtype();

#endif
