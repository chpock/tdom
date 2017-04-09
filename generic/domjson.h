
domDocument *
JSON_Parse (
    char *json,    /* Complete text of the json string being parsed */
    char *documentElement, /* name of the root element, may be NULL */
    char **errStr,
    int  *byteIndex
    );

