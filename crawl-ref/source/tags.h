/*
 *  File:       tags.h
 *  Summary:    Auxilary functions to make savefile versioning simpler.
 *  Written by: Gordon Lipford
 *
 *  Modified for Crawl Reference by $Author: zelgadis $ on $Date: 2007-09-16 00:33:50 +0100 (Sun, 16 Sep 2007) $
 *
 *  Modified for Hexcrawl by Martin Bays, 2007
 *
 *  Change History (most recent first):
 *
 *   <1>   27 Jan 2001      GDL    Created
 */

#ifndef TAGS_H
#define TAGS_H

#include <stdio.h>
#include "externs.h"

enum tag_type   // used during save/load process to identify data blocks
{
    TAG_VERSION = 0,                    // should NEVER be read in!
    TAG_YOU = 1,                        // 'you' structure
    TAG_YOU_ITEMS,                      // your items
    TAG_YOU_DUNGEON,                    // dungeon specs (stairs, branches, features)
    TAG_LEVEL,                          // various grids & clouds
    TAG_LEVEL_ITEMS,                    // items/traps
    TAG_LEVEL_MONSTERS,                 // monsters
    TAG_GHOST,                          // ghost
    TAG_LEVEL_ATTITUDE,                 // monster attitudes
    TAG_LOST_MONSTERS,                  // monsters in transit
    NUM_TAGS
};

enum tag_file_type   // file types supported by tag system
{
    TAGTYPE_PLAYER=0,           // Foo.sav
    TAGTYPE_LEVEL,              // Foo.00a, .01a, etc.
    TAGTYPE_GHOST,              // bones.xxx

    TAGTYPE_PLAYER_NAME         // Used only to read the player name
};

struct tagHeader 
{
    short tagID;
    long offset;
};

// last updated 22jan2001 {gdl}
/* ***********************************************************************
 * called from: files tags
 * *********************************************************************** */
int write2(FILE * file, const char *buffer, unsigned int count);


// last updated 22jan2001 {gdl}
/* ***********************************************************************
 * called from: files tags
 * *********************************************************************** */
int read2(FILE * file, char *buffer, unsigned int count);


// last updated 22jan2001 {gdl}
/* ***********************************************************************
 * called from: files tags
 * *********************************************************************** */
void marshallByte(struct tagHeader &th, char data);
void marshallShort(struct tagHeader &th, short data);
void marshallLong(struct tagHeader &th, long data);
void marshallFloat(struct tagHeader &th, float data);
void marshallBoolean(struct tagHeader &th, bool data);
void marshallString(struct tagHeader &th, const std::string &data,
                    int maxSize = 0);
void marshallCoord(tagHeader &th, const coord_def &c);

// last updated 22jan2001 {gdl}
/* ***********************************************************************
 * called from: tags files
 * *********************************************************************** */
char unmarshallByte(struct tagHeader &th);
short unmarshallShort(struct tagHeader &th);
long unmarshallLong(struct tagHeader &th);
float unmarshallFloat(struct tagHeader &th);
bool unmarshallBoolean(struct tagHeader &th);
int unmarshallCString(struct tagHeader &th, char *data, int maxSize);
std::string unmarshallString(tagHeader &th, int maxSize = 1000);
void unmarshallCoord(tagHeader &th, coord_def &c);

std::string make_date_string( time_t in_date );
time_t parse_date_string( char[20] );

// last updated 22jan2001 {gdl}
/* ***********************************************************************
 * called from: acr
 * *********************************************************************** */
void tag_init(long largest_tag = 200000);


// last updated 22jan2001 {gdl}
/* ***********************************************************************
 * called from: files
 * *********************************************************************** */
void tag_construct(struct tagHeader &th, int i);


// last updated 22jan2001 {gdl}
/* ***********************************************************************
 * called from: files
 * *********************************************************************** */
void tag_write(struct tagHeader &th, FILE *saveFile);


// last updated 22jan2001 {gdl}
/* ***********************************************************************
 * called from: files
 * *********************************************************************** */
void tag_set_expected(char tags[], int fileType);


// last updated 22jan2001 {gdl}
/* ***********************************************************************
 * called from: files
 * *********************************************************************** */
void tag_missing(int tag, char minorVersion);


// last updated 22jan2001 {gdl}
/* ***********************************************************************
 * called from: files
 * *********************************************************************** */
int tag_read(FILE *fp, char minorVersion);

#endif // TAGS_H
