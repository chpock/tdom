/*----------------------------------------------------------------------------
|   Copyright (c) 2017  Rolf Ade (rolf@pointsman.de)
|-----------------------------------------------------------------------------
|
|
|   The contents of this file are subject to the Mozilla Public License
|   Version 1.1 (the "License"); you may not use this file except in
|   compliance with the License. You may obtain a copy of the License at
|   http://www.mozilla.org/MPL/
|
|   Software distributed under the License is distributed on an "AS IS"
|   basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
|   License for the specific language governing rights and limitations
|   under the License.
|
|   Contributor(s):
|
|
|   written by Rolf Ade
|   April 2017
|
\---------------------------------------------------------------------------*/

/* Some parts of the following are inspired, derivated or, for a few
 * smaller pieces, even verbatim copied from the (public domain)
 * sqlite JSON parser
 * (https://www.sqlite.org/src/artifact/312b4ddf4c7399dc) */

#include <dom.h>
#include <ctype.h>

static const char jsonIsSpace[] = {
  0, 0, 0, 0, 0, 0, 0, 0,     0, 1, 1, 0, 0, 1, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  1, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
};
#define skipspace(x)  while (jsonIsSpace[(unsigned char)json[(x)]]) { (x)++; }

#define rc(i) if (jparse->status != JSON_OK && jparse->status != JSON_NEED_JSON_NS) return (i);

static const char *tdomns = "http://tdom.org/json";

/* The meaning of parse state values */
typedef enum {
    JSON_OK,
    JSON_NEED_JSON_NS,
    JSON_NO_ARRAY,
    JSON_MAX_NESTING_REACHED,
    JSON_SYNTAX_ERR,
    JSON_INVALID_XML_NAME,
    JSON_INVALID_XML_CHAR
} JSONParseState;

// Error sting constants, indexed by JSONParseState.
static const char *JSONParseStateStr[] = {
    "OK",
    "Internal: result tree needs tdom json XML namespace",
    "A JSON string with an array at top level cannot be parsed without a -jsonroot",
    "Maximum JSON object/array nesting depth exceeded",
    "JSON syntax error",
    "JSON object name that cannot be an XML element name",
    "JSON input includes characters not possible in an XML document."
};

typedef struct 
{
    JSONParseState status;
    int  nestingDepth;
    int  maxnesting;
    char *buf;
    int len;
} JSONParse;


#define errReturn(i,j) {jparse->status = j; return (i);}
    

/* #define DEBUG */
#ifdef DEBUG
# define DBG(x) x
#else
# define DBG(x) 
#endif

/*
** Return true if z[] begins with 4 (or more) hexadecimal digits
*/
static int jsonIs4Hex(const char *z){
  int i;
  for (i=0; i<4; i++) if (!isxdigit(z[i])) return 0;
  return 1;
}

/* Parse a single JSON string which begins (with the starting '"') at
 * json[i]. Return the index of the closing '"' of the string
 * parsed. */

static int jsonParseString (
    char *json,
    int   i,
    int   needName,
    JSONParse *jparse
    )
{
    unsigned char c;
    int clen, j, k, savedStart, firstchar = 1;
    unsigned int u;
    
    DBG(fprintf(stderr, "jsonParseString start: '%s'\n", &json[i]););
    if (jparse->len) jparse->buf[0] = 0;
    savedStart = i;

    if (json[i] != '"') {
        errReturn(i,JSON_SYNTAX_ERR);
    }
    i++;
    if (json[i] == '"') {
        if (needName) {
            /* The empty string isn't a valid XML element name */
            errReturn(i,JSON_INVALID_XML_NAME);
        }
        return i;
    }
    for(;;) {
        c = json[i];
        /* Unescaped control characters are not allowed in JSON
         * strings. */
        if (c <= 0x1f) {
            errReturn(i,JSON_SYNTAX_ERR);
        }
        if (c == '\\') {
            goto unescape;
        }
        if (c == '"') {
            return i;
        }
        if (needName) {
            if (firstchar) {
                if (!isNameStart(&json[i]))
                    errReturn(i,JSON_INVALID_XML_NAME);
            } else {
                if (!isNameChar(&json[i]))
                    errReturn(i,JSON_INVALID_XML_NAME);
            }
        }
        if ((clen = UTF8_CHAR_LEN(c)) == 0)
            errReturn(i,JSON_SYNTAX_ERR);
        if (!UTF8_XMLCHAR(&json[i],clen))
            errReturn(i,JSON_INVALID_XML_CHAR);
        i += clen;
        firstchar = 0;
    }
    unescape:
    /* If we here, then i points to the first backslash in the string
     * to parse */
    if (i - savedStart > jparse->len - 200) {
        jparse->buf = REALLOC(jparse->buf, i-savedStart+200);
        jparse->len = i-savedStart+200;
        memcpy (jparse->buf, &json[savedStart+1], i-savedStart);
    }
    j = i-savedStart-1;
    if (j == 0) firstchar = 1;
    for(;;) {
        c = json[i];
        /* Unescaped control characters are not allowed in JSON
         * strings. */
        if (c <= 0x1f) errReturn(i,JSON_SYNTAX_ERR);
        if (jparse->len - j < 4) {
            jparse->buf = REALLOC (jparse->buf, jparse->len * 2);
            jparse->len *= 2;
        }
        if (c == '\\') {
            c = json[i+1];
            if (c == 'u' && jsonIs4Hex(&json[i+2])) {
                u = 0;
                for (k = 2; k < 6; k++) {
                    c = json[i+k];
                    if (c <= '9') u = u*16 + c - '0';
                    else if (c <= 'F') u = u*16 + c - 'A' + 10;
                    else u = u*16 + c - 'a' + 10;
                }
                if (u <= 0x7f) {
                    
                    jparse->buf[j++] = (char)u;
                    clen = 1;
                } else if (u <= 0x7ff) {
                    jparse->buf[j++] = (char)(0xc0 | (u>>6));
                    jparse->buf[j++] = 0x80 | (u&0x3f);
                    clen = 2;
                } else {
                    jparse->buf[j++] = (char)(0xe0 | (u>>12));
                    jparse->buf[j++] = 0x80 | ((u>>6)&0x3f);
                    jparse->buf[j++] = 0x80 | (u&0x3f);
                    clen = 3;
                }
                i += 6;
            } else {
                if (c == '\\') {
                    c = '\\';
                } else if (c == '"') {
                    c = '"';
                } else if (c == '/') {
                    c = '/';
                } else if (c == 'b') {
                    c = '\b';
                } else if (c == 'f') {
                    c = '\f';
                } else if (c == 'n') {
                    c = '\n';
                } else if (c == 'r') {
                    c = '\r';
                } else if (c == 't') {
                    c = '\t';
                } else {
                    errReturn(i+1,JSON_SYNTAX_ERR);
                }
                jparse->buf[j++] = c;
                clen = 1;
                i += 2;
            }
            if (needName) {
                if (firstchar) {
                    if (needName && !isNameStart(jparse->buf+j-clen))
                        errReturn(i-1,JSON_INVALID_XML_NAME);
                } else {
                    if (!isNameChar(jparse->buf+j-clen))
                        errReturn(i-1,JSON_INVALID_XML_NAME);
                }
            } else {
                if (!UTF8_XMLCHAR(jparse->buf+j-clen,clen))
                    errReturn(i-1,JSON_INVALID_XML_CHAR);
            }
            firstchar = 0;
            continue;
        }
        if (c == '"') {
            jparse->buf[j] = '\0';
            return i;
        }
        if (needName) {
            if (!isNameChar(&json[i]))
                errReturn(i,JSON_INVALID_XML_NAME);
        }
        if ((clen = UTF8_CHAR_LEN(json[i])) == 0)
            errReturn(i,JSON_SYNTAX_ERR);
        if (!needName && !UTF8_XMLCHAR(&json[i],clen))
            errReturn(i,JSON_INVALID_XML_CHAR);
        for (k = 0; k < clen; k++) {
            jparse->buf[j++] = json[i+k];
        }
        firstchar = 0;
        i += clen;
    }
}

/* Parse a single JSON value which begins at json[i]. Return the index
 * of the first character past the end of the value parsed. */

static int jsonParseValue(
    domNode   *parent,
    char      *json,
    int        i,
    JSONParse *jparse
    )
{
    char c, save;
    int j;
    int first = 1;
    domNode *node;

    DBG(fprintf(stderr, "jsonParseValue start: '%s'\n", &json[i]););
    if (jparse->len) jparse->buf[0] = 0;
    skipspace(i);
    if ((c = json[i]) == '{' ) {
        /* Parse object */
        if (++jparse->nestingDepth > jparse->maxnesting)
            errReturn(i,JSON_MAX_NESTING_REACHED);
        i++;
        skipspace(i);
        if (json[i] == '}') {
            /* Empty object. */
            /* Needed is only one type attribute for empty elements,
             * either for object or array, the not typed must be the
             * other one. */
            /* domSetAttributeNS (parent, "json:type", "object", tdomns, 0); */
            jparse->nestingDepth--;
            return i+1;
        }
        for (;;) {
            j = jsonParseString (json, i, 1, jparse);
            rc(j);
            if (jparse->len && jparse->buf[0]) {
                DBG(fprintf(stderr, "New object member '%s'\n", jparse->buf););
                node = domNewElementNode (parent->ownerDocument,
                                          jparse->buf, ELEMENT_NODE);
                domAppendChild (parent, node);
                jparse->buf[0] = 0;
            } else {
                save = json[j];
                json[j] = '\0';
                DBG(fprintf(stderr, "New object member '%s'\n", &json[i+1]););
                node = domNewElementNode (parent->ownerDocument, &json[i+1],
                                          ELEMENT_NODE);
                domAppendChild (parent, node);
                json[j] = save;
            }
            i = j+1;
            skipspace(i);
            if (json[i] != ':') errReturn(i,JSON_SYNTAX_ERR);
            i++;
            skipspace(i);
            j = jsonParseValue (node, json, i, jparse);
            rc(j);
            i = j;
            skipspace(i);
            if (json[i] == '}') {
                jparse->nestingDepth--;
                return i+1;
            }
            if (json[i] == ',') {
                i++; skipspace(i);
                continue;
            }
            errReturn(i,JSON_SYNTAX_ERR);
        }
    } else if (c == '[') {
        /* Parse array */
        if (++jparse->nestingDepth > jparse->maxnesting)
            errReturn(i,JSON_MAX_NESTING_REACHED);
        i++;
        node = parent;
        for (;;) {
            DBG(fprintf(stderr, "Next array value node '%s'\n", &json[i]););
            skipspace(i);
            if (first && json[i] == ']') {
                /* empty array */
                DBG(fprintf(stderr,"Empty JSON array.\n"););
                domSetAttributeNS (parent, "json:typehint", "array", tdomns, 1);
                jparse->status = JSON_NEED_JSON_NS;
                jparse->nestingDepth--;
                return i+1;
            }
            i = jsonParseValue (node, json, i, jparse);
            rc(i);
            skipspace(i);
            if (json[i] == ']') {
                if (first) {
                    domSetAttributeNS (parent, "json:typehint", "array", tdomns, 1);
                    jparse->status = JSON_NEED_JSON_NS;
                }
                DBG(fprintf(stderr,"JSON array end\n"););
                jparse->nestingDepth--;
                return i+1;
            }
            if (json[i] == ',') {
                first = 0;
                node = domNewElementNode (parent->ownerDocument,
                                          parent->nodeName,
                                          ELEMENT_NODE);
                if (parent->parentNode) {
                    domAppendChild (parent->parentNode, node);
                } else {
                    domAppendChild (parent->ownerDocument->rootNode,
                                    node);
                }
                i++;
                continue;
            }
            errReturn(i,JSON_SYNTAX_ERR);
        }
    } else if (c =='"') {
        /* Parse string */
        j = jsonParseString (json, i, 0, jparse);
        DBG(fprintf(stderr, "String parsing result: %s\n", JSONParseState[jparse->status]));
        rc(j);
        if (jparse->len && jparse->buf[0]) {
            DBG(fprintf(stderr, "New unescaped text node '%s'\n", jparse->buf));
            domAppendChild (parent,
                            (domNode *) domNewTextNode (
                                parent->ownerDocument,
                                jparse->buf, strlen(jparse->buf),
                                TEXT_NODE));
        } else {
            DBG(save = json[j];json[j] = '\0';fprintf(stderr, "New text node '%s'\n", &json[i+1]);json[j] = save;);
            domAppendChild (parent,
                            (domNode *) domNewTextNode (
                                parent->ownerDocument,
                                &json[i+1], j-i-1,
                                TEXT_NODE));
        }
        return j+1;
    } else if (c == 'n'
               && strncmp (json+i, "null", 4) == 0
               && !isalnum(json[i+4])) {
        domSetAttributeNS (parent, "json:typehint", "null", tdomns, 1);
        jparse->status = JSON_NEED_JSON_NS;
        return i+4;
    } else if (c == 't'
               && strncmp (json+i, "true", 4) == 0
               && !isalnum(json[i+4])) {
        domSetAttributeNS (parent, "json:typehint", "boolean", tdomns, 1);
        jparse->status = JSON_NEED_JSON_NS;
        domAppendChild (parent,
                        (domNode *) domNewTextNode (
                            parent->ownerDocument,
                            "true", 4, TEXT_NODE));
        return i+4;
    } else if (c == 'f'
               && strncmp (json+i, "false", 5) == 0
               && !isalnum(json[i+5])) {
        domSetAttributeNS (parent, "json:typehint", "boolean", tdomns, 1);
        jparse->status = JSON_NEED_JSON_NS;
        domAppendChild (parent,
                        (domNode *) domNewTextNode (
                            parent->ownerDocument,
                            "false", 5, TEXT_NODE));
        return i+5;
    } else if (c == '-' || (c>='0' && c<='9')) {
        /* Parse number */
        int seenDP = 0;
        int seenE = 0;
        if( c<='0' ){
            j = (c == '-' ? i+1 : i);
            if (json[j] == '0' && json[j+1] >= '0' && json[j+1] <= '9' )
                errReturn(j+1,JSON_SYNTAX_ERR);
        }
        j = i+1;
        for(;; j++){
            c = json[j];
            if (c >= '0' && c <= '9') continue;
            if (c == '.') {
                if (json[j-1] == '-') errReturn(j,JSON_SYNTAX_ERR);
                if (seenDP) errReturn(j,JSON_SYNTAX_ERR);
                seenDP = 1;
                continue;
            }
            if (c == 'e' || c == 'E') {
                if (json[j-1] < '0') errReturn(j,JSON_SYNTAX_ERR);
                if (seenE) errReturn(j,JSON_SYNTAX_ERR);
                seenDP = seenE = 1;
                c = json[j+1];
                if (c == '+' || c == '-') {
                    j++;
                    c = json[j+1];
                }
                if (c < '0' || c > '9') errReturn(j,JSON_SYNTAX_ERR);
                continue;
            }
            break;
        }
        /* Catches a plain '-' without following digits */
        if( json[j-1]<'0' ) errReturn(j-1,JSON_SYNTAX_ERR);
        DBG(save = json[j];json[j] = '\0';fprintf(stderr, "New text node '%s'\n", &json[i]);json[j] = save;);
        domAppendChild(parent,
                       (domNode *) domNewTextNode (
                           parent->ownerDocument,
                           &json[i], j-i,
                           TEXT_NODE));
        return j;
    } else if (c == '\0') {
        return 0;   /* End of input */
    } else {
        errReturn(i,JSON_SYNTAX_ERR);
    }
}


domDocument *
JSON_Parse (
    char *json,    /* Complete text of the json string being parsed */
    char *documentElement, /* name of the root element, may be NULL */
    int   maxnesting,
    char **errStr,
    int  *byteIndex
    )
{
    domDocument *doc = domCreateDoc (NULL, 0);
    domNode *root;
    JSONParse jparse;
    int pos = 0;
    
    jparse.status = JSON_OK;
    jparse.nestingDepth = 0;
    jparse.maxnesting = maxnesting;
    jparse.buf = NULL;
    jparse.len = 0;

    skipspace(pos);
    if (json[pos] == '\0') {
        *byteIndex = pos;
        jparse.status = JSON_SYNTAX_ERR;
        goto reportError;
    }
    if (documentElement) {
        root = domNewElementNode(doc, documentElement, ELEMENT_NODE);
        domAppendChild(doc->rootNode, root);
        domSetAttributeNS (root, "xmlns:json", tdomns, NULL, 0);
    } else {
        if (json[pos] == '[') {
            *byteIndex = pos;
            if (maxnesting == 0) {
                jparse.status = JSON_MAX_NESTING_REACHED;
            } else {
                jparse.status = JSON_NO_ARRAY;
            }
            goto reportError;
        }
        root = doc->rootNode;
    }
    *byteIndex = jsonParseValue (root, json, pos, &jparse );
    if (jparse.status != JSON_OK && jparse.status != JSON_NEED_JSON_NS)
        goto reportError;
    if (*byteIndex > 0) {
        pos = *byteIndex;
        skipspace(pos);
    }
    if (json[pos] != '\0') {
        *byteIndex = pos;
        goto reportError;
    }
    if (jparse.status != JSON_NEED_JSON_NS) {
        domRemoveAttribute(root, "xmlns:json");
    }
    if (jparse.len > 0) {
        FREE (jparse.buf);
    }
    return doc;
reportError:
    if (jparse.len > 0) {
        FREE (jparse.buf);
    }
    domFreeDocument (doc, NULL, NULL);
    doc = NULL;
    *errStr = (char *)JSONParseStateStr[jparse.status];
    return doc;
}
