#ifndef _AREA_LEVELS_H
#define _AREA_LEVELS_H

struct area_level_type
{
    char *filename;
    int level;
};

const struct area_level_type area_level_table[] =
{
    { "hell.are", 300 },
    { "heaven.are", 200 },
    { "shire.are", 125 },
    { "weed.are", 80 },
    { "canyon.are", 60 },
    { "smurf.are", 40 },
    { "school.are", 10 },
    { NULL, 0 }
};

/*
 * Grind Zone XP/CP Multipliers
 * Add a row to register a zone. mult is a percentage: 100 = 1x, 200 = 2x, etc.
 * Zones not listed here give normal (unmodified) XP.
 */
struct grind_zone_type
{
    int  vnum_lo;
    int  vnum_hi;
    int  mult;
    const char *name;
};

const struct grind_zone_type grind_zone_table[] =
{
    { 3700,  3760,  100, "School"  },  /* 1.5x */
    {  100,   129,  125, "Smurf"   },  /* 2x   */
    { 9201,  9260,  150, "Canyon"  },  /* 3x   */
    {30232, 30261,  200, "Weed"    },  /* 4x   */
    { 1100,  1157,  300, "Shire"   },  /* 5x   */
    {30100, 30200,  400, "Hell"    },  /* 6x   */
    {99000, 99100,  500, "Heaven"  },  /* 8x   */
    {    0,     0,    0, NULL      }   /* end  */
};

#endif
