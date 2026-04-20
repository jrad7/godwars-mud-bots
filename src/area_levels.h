#ifndef _AREA_LEVELS_H
#define _AREA_LEVELS_H

struct area_level_type
{
    char *filename;
    int level;
};

static const struct area_level_type area_level_table[] =
{
    { "hell.are",   150 },
    { "heaven.are", 150 },
    { "weed.are",   125 },
    { "drow.are",   125 },
    { "disney.are", 125 },
    { "plains.are",  75 },
    { "canyon.are",  75 },
    { "air.are",     75 },
    { "moria.are",   45 },
    { "thalos.are",  45 },
    { "galaxy.are",  45 },
    { "sewer.are",   25 },
    { "shire.are",   25 },
    { "mega1.are",   25 },
    { "school.are",  10 },
    { "smurf.are",   10 },
    { "daycare.are", 10 },
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
    {  100,   129,  100, "Smurf"   },
    { 6600,  6647,  100, "Daycare" },
    { 7000,  7445,  150, "Sewer"   },
    { 1100,  1157,  150, "Shire"   },
    { 8000,  8028,  150, "Mega"    },
    { 3900,  4172,  200, "Moria"   },
    { 5200,  5280,  200, "Thalos"  },
    { 9301,  9371,  200, "Galaxy"  },
    { 9201,  9260,  300, "Canyon"  },
    {  300,   350,  300, "Plains"  },
    { 1000,  1040,  300, "Air"     },
    {30232, 30261,  700, "Weed"    },
    { 5100,  5150,  700, "Drow"    },
    {50000, 50100,  700, "Disney"  },
    {30100, 30200,  1400, "Hell"    },
    {99000, 99100,  1400, "Heaven"  },
    {    0,     0,    0, NULL      }   /* end */
};

#endif
