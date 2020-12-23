/*
 * Mini65 - Small 6502 simulator with Atari 8bit bios.
 * Copyright (C) 2017-2019 Daniel Serpell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include "dosfname.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Transforms a string to lowercase, not using locales
static void str_lcase(char *str)
{
    for(;*str;str++)
        if(*str >= 'A' && *str <= 'Z')
            *str = *str + ('a' - 'A');
}

static int get_case(const char *str)
{
    for(;*str;str++)
        if(*str >= 'a' && *str <= 'z')
            return 0;
    return 1;
}

FILE *dosfopen(const char *name, const char *mode)
{
    // Easy, check if file already exists
    struct stat st;
    if(0 == stat(name, &st))
        return fopen(name, mode);

    // Not, check if file with lower case exists:
    char *lname = strdup(name);
    FILE *ret;
    str_lcase(lname);
    if(0 == stat(lname, &st))
        ret = fopen(lname, mode);
    // No, check if there are lower-case letters in the file name
    else if( get_case(name) )
        // Yes, use filename as is
        ret = fopen(name, mode);
    else
        // No, convert to lowercase
        ret = fopen(lname, mode);
    free(lname);
    return ret;
}
