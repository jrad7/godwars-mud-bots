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
    { "weed.are", 75 },
    { "canyon.are", 50 },    
    { "smurf.are", 30 },    
    { "school.are", 10 },
    { NULL, 0 }
};

#endif
