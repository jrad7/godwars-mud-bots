#ifndef _AREA_LEVELS_H
#define _AREA_LEVELS_H

struct area_level_type
{
    char *filename;
    int level;
};

static const struct area_level_type area_level_table[] =
{
    { "hell.are",   125 },
    { "heaven.are", 125 },
    { "weed.are",    75 },
    { "plains.are",  75 },
    { "canyon.are",  45 },
    { "moria.are",   45 },
    { "thalos.are",  45 },
    { "sewer.are",   25 },
    { "smurf.are",   25 },
    { "shire.are",   25 },
    { "school.are",  10 },
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

static const struct grind_zone_type grind_zone_table[] =
{
    { 3700,  3760,  100, "School"  },
    { 7000,  7445,  125, "Sewer"   },
    {  100,   129,  125, "Smurf"   },
    { 3900,  4172,  150, "Moria"   },
    { 9201,  9260,  150, "Canyon"  },
    {  300,   350,  200, "Plains"  },
    {30232, 30261,  200, "Weed"    },
    { 5200,  5280,  300, "Thalos"  },
    { 1100,  1157,  300, "Shire"   },
    {30100, 30200,  500, "Hell"    },
    {99000, 99100,  600, "Heaven"  },
    {    0,     0,    0, NULL      }   /* end */
};

#endif
