#ifndef _AREA_LEVELS_H
#define _AREA_LEVELS_H

struct area_level_type
{
    char *filename;
    int level;
};

const struct area_level_type area_level_table[] =
{
    { "weed.are", 50 },
    { "canyon.are", 25 },
    { "shire.are", 25 },
    { "hell.are", 15 },
    { "smurf.are", 15 },
    { "heaven.are", 5 },
    { "school.are", 5 },
    { NULL, 0 }
};

#endif
