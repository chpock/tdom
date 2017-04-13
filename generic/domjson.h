
#ifndef JSON_MAX_NESTING
# define JSON_MAX_NESTING 2000
#endif

domDocument *
JSON_Parse (
    char *json,    /* Complete text of the json string being parsed */
    char *documentElement, /* name of the root element, may be NULL */
    int   maxnesting,
    char **errStr,
    int  *byteIndex
    );

