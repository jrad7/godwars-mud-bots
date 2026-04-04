#ifndef _AREA_LEVELS_H
#define _AREA_LEVELS_H

struct area_level_type
{
    char *filename;
    int level;
};

const struct area_level_type area_level_table[] =
{
    { "hell.are", 200 },
    { "heaven.are", 150 },
    { "shire.are", 100 },
    { "weed.are", 50 },
    { "canyon.are", 25 },    
    { "smurf.are", 15 },    
    { "school.are", 5 },
    { NULL, 0 }
};

#endif
