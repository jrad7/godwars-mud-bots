#ifndef _AREA_LEVELS_H
#define _AREA_LEVELS_H

struct area_level_type
{
    char *filename;
    int level;
};

static const struct area_level_type area_level_table[] =
{
    { "hell.are",   300 },
    { "heaven.are", 200 },
    { "shire.are",  125 },
    { "thalos.are", 100 },
    { "weed.are",    75 },
    { "plains.are",  60 },
    { "canyon.are",  50 },
    { "moria.are",   40 },
    { "sewer.are",   20 },
    { "smurf.are",   30 },
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
    { 7000,  7445,  110, "Sewer"   },
    {  100,   129,  125, "Smurf"   },
    { 3900,  4172,  135, "Moria"   },
    { 9201,  9260,  150, "Canyon"  },
    {  300,   350,  175, "Plains"  },
    {30232, 30261,  200, "Weed"    },
    { 5200,  5280,  250, "Thalos"  },
    { 1100,  1157,  300, "Shire"   },
    {30100, 30200,  400, "Hell"    },
    {99000, 99100,  500, "Heaven"  },
    {    0,     0,    0, NULL      }   /* end */
};

#endif
