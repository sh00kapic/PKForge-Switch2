#pragma once
#ifdef __SWITCH2__
#define _PKSMCORE_LANG_FOLDER "romfs:/strings/"
#define _PKSMCORE_PERSONAL_FOLDER "romfs:/personals/"
#else
#define _PKSMCORE_LANG_FOLDER "romfs/strings/"
#define _PKSMCORE_PERSONAL_FOLDER "romfs/personals/"
#endif
#define _PKSMCORE_DISABLE_THREAD_SAFETY

#ifdef __SWITCH2__
#include <cstdio>
#include <cstdlib>
#include <sys/types.h>

inline ssize_t pkforgeGetline(char** buffer, size_t* capacity, FILE* file)
{
    size_t length = 0;
    for (int ch; (ch = std::getc(file)) != EOF; )
    {
        if (!*buffer || length + 1 >= *capacity)
        {
            const size_t next = *buffer ? *capacity * 2 : 128;
            auto* resized = static_cast<char*>(std::realloc(*buffer, next));
            if (!resized)
                return -1;
            *buffer = resized;
            *capacity = next;
        }
        (*buffer)[length++] = static_cast<char>(ch);
        if (ch == '\n')
            break;
    }
    if (!length)
        return -1;
    (*buffer)[length] = '\0';
    return static_cast<ssize_t>(length);
}
#define _PKSMCORE_GETLINE_FUNC pkforgeGetline
#endif
