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

#include <dom.h>
#include <ctype.h>

typedef unsigned int u32;

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

static const char *tdomns = "http://tdom.org/json";

/*
** Return true if z[] begins with 4 (or more) hexadecimal digits
*/
static int jsonIs4Hex(const char *z){
  int i;
  for (i=0; i<4; i++) if (!isxdigit(z[i])) return 0;
  return 1;
}

/* The meaning of parsing return values */
#define JSON_SYNTAX_ERR -1
#define JSON_NEED_UNESCAPING -2
#define JSON_INVALID_XML_NAME -3
#define JSON_INVALID_XML_CHAR -4

/* #define DEBUG */
#ifdef DEBUG
# define DBG(x) x
#else
# define DBG(x) 
#endif

static int jsonParseString (
    char *json,
    int   i,
    int   needName
    )
{
    char c;
    int clen;
    
    DBG(fprintf(stderr, "jsonParseString start: '%s'\n", &json[i]););
    if (json[i] == '"') {
        i++;
        if (json[i] == '"') {
            return i;
        }
        if (needName) {
            if (!isNameStart(&json[i])) return JSON_INVALID_XML_NAME;
        } else {
            if ((clen = UTF8_CHAR_LEN(json[i])) == 0)
                return JSON_SYNTAX_ERR;
            if (!UTF8_XMLCHAR(&json[i],clen)) {
                return JSON_INVALID_XML_CHAR;
            }
        }
        i++;
        for(;;) {
            c = json[i];
            if (c == '\0') return JSON_SYNTAX_ERR;
            if (c == '\\') {
                c = json[i+1];
                if (needName) {
                    if (c == 'u' && jsonIs4Hex(json+i+2)) {
                        /* Need to unescape. For now, we don't support
                         * unescaping JSON strings and return an
                         * approriate error code */
                        return JSON_NEED_UNESCAPING;
                    } else {
                        return JSON_INVALID_XML_NAME;
                    }
                } else {
                    if (c == '"' || c == '\\' || c == '/' || c == 'b'
                        || c == 'f' || c == 'n' || c == 'r' || c == 't'
                        || (c == 'u' && jsonIs4Hex(json+i+2))) {
                        return JSON_NEED_UNESCAPING;
                    } else {
                        return JSON_SYNTAX_ERR;
                    } 
                }
            }
            if (c == '"') {
                return i;
            }
            if (needName) {
                if (!isNameChar(&json[i])) return JSON_INVALID_XML_NAME;
            }
            if ((clen = UTF8_CHAR_LEN(json[i])) == 0)
                return JSON_SYNTAX_ERR;
            if (!needName && !UTF8_XMLCHAR(&json[i],clen))
                return JSON_INVALID_XML_CHAR;
            i += clen;
        }
    } else {
        return JSON_SYNTAX_ERR;
    }
}

/* Parse a single JSON value which begins at json[i]. Return the index
 * of the first character past the end of the value parsed. */

static int jsonParseValue(
    domNode *parent,
    char    *json,
    int      i
    )
{
    char c, save;
    int j;
    int first = 1;
    domNode *node;

    DBG(fprintf(stderr, "jsonParseValue start: '%s'\n", &json[i]););
    skipspace(i);
    if ((c = json[i]) == '{' ) {
        /* Parse object */
        i++;
        for (;;) {
            skipspace(i);
            if (first && json[i] == '}') {
                /* Needed is only one type attribute for empty elements,
                 * either for object or array, the not typed must be the
                 * other one. */
                /* domSetAttributeNS (parent, "json:type", "object", tdomns, 0); */
                return i+1;
            }
            j = jsonParseString (json, i, 1);
            if (j < 0) {
                return j;
            }
            save = json[j];
            json[j] = '\0';
            DBG(fprintf(stderr, "New object member '%s'\n", &json[i+1]););
            node = domNewElementNode (parent->ownerDocument, &json[i+1],
                                      ELEMENT_NODE);
            domAppendChild (parent, node);
            json[j] = save;
            i = j+1;
            skipspace(i);
            if (json[i] != ':') return JSON_SYNTAX_ERR;
            i++;
            skipspace(i);
            j = jsonParseValue (node, json, i);
            if (j < 0) {
                return j;
            };
            i = j;
            skipspace(i);
            if (json[i] == '}') return i+1;
            if (json[i] == ',') {first = 0; i++; continue;}
            return JSON_SYNTAX_ERR;
        }
    } else if (c == '[') {
        /* Parse array */
        i++;
        node = parent;
        for (;;) {
            DBG(fprintf(stderr, "Next array value node '%s'\n", &json[i]););
            skipspace(i);
            if (first && json[i] == ']') {
                /* empty array */
                DBG(fprintf(stderr,"Empty JSON array.\n"););
                domSetAttributeNS (parent, "json:typehint", "array", tdomns, 1);
                return i+1;
            }
            j = jsonParseValue (node, json, i);
            if (j < 0) {
                return j;
            }
            i = j;
            skipspace(i);
            if (json[i] == ']') {
                if (first) {
                    domSetAttributeNS (parent, "json:typehint", "array", tdomns, 1);
                }
                DBG(fprintf(stderr,"JSON array end\n"););
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
            return JSON_SYNTAX_ERR;
        }
    } else if (c =='"') {
        /* Parse string */
        j = jsonParseString (json, i, 0);
        if (j < 0) {
            return j;
        }
        DBG(save = json[j];json[j] = '\0';fprintf(stderr, "New text node '%s'\n", &json[i+1]);json[j] = save;);
        domAppendChild (parent,
                        (domNode *) domNewTextNode (
                            parent->ownerDocument,
                            &json[i+1], j-i-1,
                            TEXT_NODE));
        return j+1;
    } else if (c == 'n'
               && strncmp (json+i, "null", 4) == 0
               && !isalnum(json[i+4])) {
        domSetAttributeNS (parent, "json:typehint", "null", tdomns, 1);
        return i+4;
    } else if (c == 't'
               && strncmp (json+i, "true", 4) == 0
               && !isalnum(json[i+4])) {
        domSetAttributeNS (parent, "json:typehint", "boolean", tdomns, 1);
        domAppendChild (parent,
                        (domNode *) domNewTextNode (
                            parent->ownerDocument,
                            "true", 4, TEXT_NODE));
        return i+4;
    } else if (c == 'f'
               && strncmp (json+i, "false", 5) == 0
               && !isalnum(json[i+5])) {
        domSetAttributeNS (parent, "json:typehint", "boolean", tdomns, 1);
        domAppendChild (parent,
                        (domNode *) domNewTextNode (
                            parent->ownerDocument,
                            "false", 5, TEXT_NODE));
        return i+5;
    } else if (c == '-' || (c>='0' && c<='9')) {
        /* Parse number */
        int seenDP = 0;
        int seenE = 0;
        if (c == '0' && json[i+1] == '0') return JSON_SYNTAX_ERR;
        j = i+1;
        for(;; j++){
            c = json[j];
            if (c >= '0' && c <= '9') continue;
            if (c == '.') {
                if (json[j-1] == '-') return JSON_SYNTAX_ERR;
                if (seenDP) return JSON_SYNTAX_ERR;
                seenDP = 1;
                continue;
            }
            if (c == 'e' || c == 'E') {
                if (json[j-1] < '0') return JSON_SYNTAX_ERR;
                if (seenE) return JSON_SYNTAX_ERR;
                seenDP = seenE = 1;
                c = json[j+1];
                if (c == '+' || c == '-') {
                    j++;
                    c = json[j+1];
                }
                if (c < '0' || c > '9') return JSON_SYNTAX_ERR;
                continue;
            }
            break;
        }
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
        return JSON_SYNTAX_ERR;
    }
}


domDocument *
JSON_Parse (
    char *json,    /* Complete text of the json string being parsed */
    char *documentElement, /* name of the root element, may be NULL */
    int  *errNr 
    )
{
    domDocument *doc = domCreateDoc (NULL, 0);
    domNode *root;
    u32 pos = 0;
    
    if (documentElement) {
        root = domNewElementNode(doc, documentElement, ELEMENT_NODE);
        domAppendChild(doc->rootNode, root);
        domSetAttributeNS (root, "xmlns:json", tdomns, NULL, 0);
    } else {
        root = doc->rootNode;
    }
    skipspace(pos);
    *errNr = jsonParseValue (root, json, pos);
    if (*errNr > 0) {
        pos = *errNr;
        skipspace(pos);
    }
    if (json[pos] != '\0' || *errNr < 0) {
        domFreeDocument (doc, NULL, NULL);
        doc = NULL;
    }
    return doc;
}
