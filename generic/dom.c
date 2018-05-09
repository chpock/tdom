/*---------------------------------------------------------------------------
|   Copyright (C) 1999  Jochen C. Loewer (loewerj@hotmail.com)
+----------------------------------------------------------------------------
|
|   $Id$
|
|
|   A DOM interface upon the expat XML parser for the C language
|   according to the W3C recommendation REC-DOM-Level-1-19981001
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
|   The Original Code is tDOM.
|
|   The Initial Developer of the Original Code is Jochen Loewer
|   Portions created by Jochen Loewer are Copyright (C) 1998, 1999
|   Jochen Loewer. All Rights Reserved.
|
|   Contributor(s):
|       Sept99  Carsten Zerbst    Added comment and processing instructions
|                                 nodes.
|
|       June00  Zoran Vasiljevic  Made thread-safe.
|
|           01  Rolf Ade          baseURI stuff, ID support, external
|                                 entities, tdom command
|
|
|   written by Jochen Loewer
|   April 5, 1999
|
\--------------------------------------------------------------------------*/



/*---------------------------------------------------------------------------
|   Includes
|
\--------------------------------------------------------------------------*/
#include <tcl.h>
#include <dom.h>
#include <domxpath.h>
#include <tclexpat.h>


/* #define DEBUG */
/*----------------------------------------------------------------------------
|   Debug Macros
|
\---------------------------------------------------------------------------*/
#ifdef DEBUG
# define DBG(x) x
#else
# define DBG(x) 
#endif

#define MutationEvent()
#define MutationEvent2(type,node)
#define MutationEvent3(type,node,relatioNode)

#define MCHK(a)  if ((a)==NULL) { \
                     fprintf(stderr, \
                            "Memory alloc error line: %d",__LINE__); \
                     exit(1); \
                 }

#define INITIAL_BASEURISTACK_SIZE 4;

/*---------------------------------------------------------------------------
|   Globals
|   In threading environment, some are located in domDocument structure
|   and some are handled differently (domUniqueNodeNr, domUniqueDocNr)
|
\--------------------------------------------------------------------------*/

#ifndef TCL_THREADS
  unsigned long domUniqueNodeNr = 0;
  unsigned long domUniqueDocNr  = 0;
  Tcl_HashTable tdom_tagNames;
  Tcl_HashTable tdom_attrNames;
#endif

static int domModuleIsInitialized = 0;
TDomThreaded(static Tcl_Mutex initMutex;)

static char *domException2StringTable [] = {

    "OK - no exception",
    "INDEX_SIZE_ERR",
    "DOMSTRING_SIZE_ERR",
    "HIERARCHY_REQUEST_ERR",
    "WRONG_DOCUMENT_ERR",
    "INVALID_CHARACTER_ERR",
    "NO_DATA_ALLOWED_ERR",
    "NO_MODIFICATION_ALLOWED_ERR",
    "NOT_FOUND_ERR",
    "NOT_SUPPORTED_ERR",
    "INUSE_ATTRIBUTE_ERR"
};

static char tdom_usage[] =
                "Usage tdom <expat parser obj> <subCommand>, where subCommand can be:\n"
                "           enable             \n"
                "           getdoc             \n"
                "           setStoreLineColumn \n"
                ;


/*---------------------------------------------------------------------------
|   type domActiveNS
|
\--------------------------------------------------------------------------*/
typedef struct _domActiveNS {

    int    depth;
    domNS *namespace;

} domActiveNS;

/*---------------------------------------------------------------------------
|   type domBaseURIstackElem
|
\--------------------------------------------------------------------------*/
typedef struct _domActiveBaseURI {

    int   depth;
    const char *baseURI;

} domActiveBaseURI;


/* The elements of TNC_Content carry exactly the same information
   as expats XML_Content. But the element is identified by his
   Tcl_HashEntry entry within the "tagNames" Hashtable (see TNC_Data)
   and not the element name. This should be much more efficient. */
typedef struct TNC_cp TNC_Content;
typedef struct TNC_elemAttInfo TNC_ElemAttInfo;

struct TNC_cp
{
    enum XML_Content_Type   type;
    enum XML_Content_Quant  quant;
    Tcl_HashEntry          *nameId;
    unsigned int            numchildren;
    TNC_Content            *children;
    TNC_ElemAttInfo        *attInfo;
};

typedef struct TNC_contentStack
{
    TNC_Content  *model;
    int           activeChild;
    int           deep;
    int           alreadymatched;
} TNC_ContentStack;

typedef enum TNC_attType {
    TNC_ATTTYPE_CDATA,
    TNC_ATTTYPE_ID,
    TNC_ATTTYPE_IDREF,
    TNC_ATTTYPE_IDREFS,
    TNC_ATTTYPE_ENTITY,
    TNC_ATTTYPE_ENTITIES,
    TNC_ATTTYPE_NMTOKEN,
    TNC_ATTTYPE_NMTOKENS,
    TNC_ATTTYPE_NOTATION,
    TNC_ATTTYPE_ENUMERATION,
} TNC_AttType;

struct TNC_elemAttInfo
{
    Tcl_HashTable *attributes;
    int            nrOfreq;
    int            nrOfIdAtts;
};

typedef struct TNC_attDecl
{
    TNC_AttType    att_type;
    char          *dflt;
    int            isrequired;
    Tcl_HashTable *lookupTable;   /* either NotationTypes or enum values */
} TNC_AttDecl;

typedef struct TNC_entityInfo
{
    int    is_notation;
    char  *notationName;
} TNC_EntityInfo;

typedef Tcl_HashEntry TNC_NameId;

#define TNC_INITCONTENTSTACKSIZE 512

enum TNC_Error {
    TNC_ERROR_NONE,
    TNC_ERROR_DUPLICATE_ELEMENT_DECL,
    TNC_ERROR_DUPLICATE_MIXED_ELEMENT,
    TNC_ERROR_UNKNOWN_ELEMENT,
    TNC_ERROR_EMPTY_ELEMENT,
    TNC_ERROR_DISALLOWED_PCDATA,
    TNC_ERROR_DISALLOWED_CDATA,
    TNC_ERROR_NO_DOCTYPE_DECL,
    TNC_ERROR_WRONG_ROOT_ELEMENT,
    TNC_ERROR_NO_ATTRIBUTES,
    TNC_ERROR_UNKNOWN_ATTRIBUTE,
    TNC_ERROR_WRONG_FIXED_ATTVALUE,
    TNC_ERROR_MISSING_REQUIRED_ATTRIBUTE,
    TNC_ERROR_MORE_THAN_ONE_ID_ATT,
    TNC_ERROR_ID_ATT_DEFAULT,
    TNC_ERROR_DUPLICATE_ID_VALUE,
    TNC_ERROR_UNKNOWN_ID_REFERRED,
    TNC_ERROR_ENTITY_ATTRIBUTE,
    TNC_ERROR_ENTITIES_ATTRIBUTE,
    TNC_ERROR_ATT_ENTITY_DEFAULT_MUST_BE_DECLARED,
    TNC_ERROR_NOTATION_REQUIRED,
    TNC_ERROR_NOTATION_MUST_BE_DECLARED,
    TNC_ERROR_IMPOSSIBLE_DEFAULT,
    TNC_ERROR_ENUM_ATT_WRONG_VALUE,
    TNC_ERROR_NMTOKEN_REQUIRED,
    TNC_ERROR_NAME_REQUIRED,
    TNC_ERROR_NAMES_REQUIRED,
    TNC_ERROR_ELEMENT_NOT_ALLOWED_HERE,
    TNC_ERROR_ELEMENT_CAN_NOT_END_HERE,
    TNC_ERROR_ONLY_THREE_BYTE_UTF8,
    TNC_ERROR_UNKNOWN_NODE_TYPE
};

const char *
TNC_ErrorString (int code)
{
    static const char *message[] = {
        "No error.",
        "Element declared more than once.",
        "The same name must not appear more than once in \n\tone mixed-content declaration.",
        "No declaration for this element.",
        "Element is declared to be empty, but isn't.",
        "PCDATA not allowed here.",
        "CDATA section not allowed here.",
        "No DOCTYPE declaration.",
        "Root element doesn't match DOCTYPE name.",
        "No attributes defined for this element.",
        "Unknown attribute for this element.",
        "Attribute value must match the FIXED default.",
        "Required attribute missing.",
        "Only one attribute with type ID allowed.",
        "No default value allowed for attribute type ID.",
        "ID attribute values must be unique within the document.",
        "Unknown ID referred.",
        "Attribute value has to be a unparsed entity.",
        "Attribute value has to be a sequence of unparsed entities.",
        "The defaults of attributes with type ENTITY or ENTITIES\nhas to be unparsed entities.",
        "Attribute value has to be one of the allowed notations.",
        "Every used NOTATION must be declared.",
        "Attribute default is not one of the allowed values",
        "Attribute hasn't one of the allowed values.",
        "Attribute value has to be a NMTOKEN.",
        "Attribute value has to be a Name.",
        "Attribute value has to match production Names.",
        "Element is not allowed here.",
        "Element can not end here (required element(s) missing).",
        "Can only handle UTF8 chars up to 3 bytes length."
        "Unknown or unexpected dom node type."
    };
/*      if (code > 0 && code < sizeof(message)/sizeof(message[0])) */
        return message[code];
    return 0;
}

/*---------------------------------------------------------------------------
|   type domReadInfo
|
\--------------------------------------------------------------------------*/
typedef struct _domReadInfo {

    XML_Parser        parser;
    domDocument      *document;
    domNode          *currentNode;
    int               depth;
    int               ignoreWhiteSpaces;
    int               cdataSection;
    Tcl_DString      *cdata;
    int               storeLineColumn;
    int               ignorexmlns;
    int               feedbackAfter;
    Tcl_Obj          *feedbackCmd;
    XML_Index         nextFeedbackPosition;
    Tcl_Interp       *interp;
    int               activeNSsize;
    int               activeNSpos;
    domActiveNS      *activeNS;
    int               baseURIstackSize;
    int               baseURIstackPos;
    domActiveBaseURI *baseURIstack;
    int               insideDTD;
    int               dtdvalidation;
    int               status;
    /* DTD validation releated struct members follow */
    char             *doctypeName;            /* From DOCTYPE declaration */
    int               skipWhiteCDATAs;        /* Flag: white space allowed in 
                                                 current content model? */
    int               ignorePCDATA;           /* Flag: currently mixed content
                                                 model? */
    Tcl_HashTable    *tagNames;               /* Hash table of all ELEMENT
                                                 declarations of the DTD.
                                                 Element name is the key.
                                                 While parsing, entry points
                                                 to the XML_Content of that
                                                 Element, after finishing of
                                                 DTD parsing, entry holds a
                                                 pointer to the TNC_Content
                                                 of that element. */
    TNC_ElemAttInfo  *elemAttInfo;            /* TncElementStartCommand stores
                                                 the elemAttInfo pointer of
                                                 the current element here for
                                                 DOM validation, to avoid two
                                                 element name lookups. */
    int               elemContentsRewriten;   /* Signals, if the tagNames
                                                 entries point to
                                                 TNC_Contents */
    int               dtdstatus;              /* While used with expat obj:
                                                 1 after successful parsed
                                                 DTD, 0 otherwise.
                                                 For validateCmd used for
                                                 error report during
                                                 validation: 0 OK, 1 validation
                                                 error. */
    int               idCheck;                /* Flag: check IDREF resolution*/
    Tcl_HashTable    *attDefsTables;          /* Used to store ATTLIST 
                                                 declarations while parsing.
                                                 Keys are the element names. */
    Tcl_HashTable    *entityDecls;            /* Used to store ENTITY
                                                 declarations. */
    Tcl_HashTable    *notationDecls;          /* Used to store NOTATION
                                                 declarations. */
    Tcl_HashTable    *ids;                    /* Used to track IDs */
    int               contentStackSize;       /* Current size of the content
                                                 stack */
    int               contentStackPtr;        /* Points to the currently active
                                                 content model on the stack */
    TNC_ContentStack *contentStack;           /* Stack for the currently
                                                 nested open content models. */
} domReadInfo;

/*----------------------------------------------------------------------------
|   Prototypes
|
\---------------------------------------------------------------------------*/
static void DispatchPCDATA (domReadInfo *info);

#define CHECK_UTF_CHARLEN(d) if (!(d)) { \
                                signalNotValid (userData, TNC_ERROR_ONLY_THREE_BYTE_UTF8);\
                                return;\
                             }

#define CHECK_UTF_CHARLENR(d) if (!(d)) { \
                                signalNotValid (userData, TNC_ERROR_ONLY_THREE_BYTE_UTF8);\
                                return 0;\
                             }

#define CHECK_UTF_CHARLEN_COPY(d) if (!(d)) { \
                                signalNotValid (userData, TNC_ERROR_ONLY_THREE_BYTE_UTF8);\
                                FREE (copy);\
                                return;\
                                }

#ifndef TCL_THREADS

/*---------------------------------------------------------------------------
|   domModuleFinalize
|
\--------------------------------------------------------------------------*/
static void
domModuleFinalize(ClientData unused)
{
    Tcl_HashEntry *entryPtr;
    Tcl_HashSearch search;

    entryPtr = Tcl_FirstHashEntry(&tdom_tagNames, &search);
    while (entryPtr) {
        Tcl_DeleteHashEntry(entryPtr);
        entryPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&tdom_tagNames);

    entryPtr = Tcl_FirstHashEntry(&tdom_attrNames, &search);
    while (entryPtr) {
        Tcl_DeleteHashEntry(entryPtr);
        entryPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&tdom_attrNames);

    return;
}
#endif /* TCL_THREADS */

/*---------------------------------------------------------------------------
|   domModuleInitialize
|
\--------------------------------------------------------------------------*/
void
domModuleInitialize (
)
{
    if (domModuleIsInitialized == 0) {
        TDomThreaded(Tcl_MutexLock(&initMutex);)
        if (domModuleIsInitialized == 0) {
            domAllocInit();
            TDomNotThreaded (
                Tcl_InitHashTable(&tdom_tagNames, TCL_STRING_KEYS);
                Tcl_InitHashTable(&tdom_attrNames, TCL_STRING_KEYS);
                Tcl_CreateExitHandler(domModuleFinalize, NULL);
            )
            TDomThreaded(
                Tcl_CreateExitHandler(domLocksFinalize, NULL);
            )
            domModuleIsInitialized = 1;
        }
        TDomThreaded(Tcl_MutexUnlock(&initMutex);)
    }
}

/*---------------------------------------------------------------------------
|   coercion routines for calling from C++
|
\--------------------------------------------------------------------------*/
domAttrNode * coerceToAttrNode( domNode *n )  {
    return (domAttrNode *)n;
}

domTextNode * coerceToTextNode( domNode *n ) {
    return (domTextNode *)n;
}

domProcessingInstructionNode * coerceToProcessingInstructionNode( domNode *n ) {
    return (domProcessingInstructionNode *)n;
}


static void
signalNotValid (userData, code)
    void        *userData;
    int          code;
{
    domReadInfo *tncdata = (domReadInfo *) userData;
    char linenr[50], colnr[50];
    
    
    tncdata->status = TCL_ERROR;
    sprintf(linenr, "%ld", XML_GetCurrentLineNumber(tncdata->parser));
    sprintf(colnr, "%ld", XML_GetCurrentColumnNumber(tncdata->parser));
    Tcl_ResetResult (tncdata->interp);
    Tcl_AppendResult (tncdata->interp, "At line ", linenr, ", column ",
                      colnr, ": ", (char *)TNC_ErrorString (code),
                      NULL);
    XML_StopParser(tncdata->parser, 1);
}

/*---------------------------------------------------------------------------
|   domIsNAME
|
\--------------------------------------------------------------------------*/
int
domIsNAME (
    const char *name
    )
{
    const char *p;

    p = name;
    if (!isNameStart(p)) return 0;
    p += UTF8_CHAR_LEN(*p);
    while (*p) {
        if (isNameChar(p))
            p += UTF8_CHAR_LEN(*p);
        else return 0;
    }
    return 1;
}

/*---------------------------------------------------------------------------
|   domIsPINAME
|
\--------------------------------------------------------------------------*/
int
domIsPINAME (
    const char *name
    )
{
    if (strlen (name) == 3
        && ((name[0] == 'x') || (name[0] == 'X'))
        && ((name[1] == 'm') || (name[1] == 'M'))
        && ((name[2] == 'l') || (name[2] == 'L')) ) {
        return 0;
    }
    return domIsNAME (name);
}

/*---------------------------------------------------------------------------
|   domIsQNAME
|
\--------------------------------------------------------------------------*/
int
domIsQNAME (
    const char *name
    )
{
    const char *p;
    
    p = name;
    if (!isNCNameStart(p)) return 0;
    p += UTF8_CHAR_LEN(*p);
    while (*p) {
        if (isNCNameChar(p))
            p += UTF8_CHAR_LEN(*p);
        else {
            if (*p == ':') {
                p += 1;
                if (!isNCNameStart(p)) return 0;
                p += UTF8_CHAR_LEN(*p);
                break;
            }
            else return 0;
        }
    }
    while (*p) {
        if (isNCNameChar(p))
            p += UTF8_CHAR_LEN(*p);
        else return 0;
    }
    return 1;
}

/*---------------------------------------------------------------------------
|   domIsNCNAME
|
\--------------------------------------------------------------------------*/
int
domIsNCNAME (
    const char *name
    )
{
    const char *p;

    p = name;
    if (!isNCNameStart(p)) return 0;
    p += UTF8_CHAR_LEN(*p);
    while (*p) {
        if (isNCNameChar(p))
            p += UTF8_CHAR_LEN(*p);
        else return 0;
    }
    return 1;
}

/*---------------------------------------------------------------------------
|   domIsChar 
|
\--------------------------------------------------------------------------*/
int
domIsChar (
    const char *str
    )
{
    const char *p;
    int   clen;
    
    p = str;
    while (*p) {
        clen = UTF8_CHAR_LEN(*p);
        if (clen > 4) return 0;
        if (UTF8_XMLCHAR((unsigned const char *)p,clen))
            p += clen;
        else return 0;
    }
    return 1;
}

/*---------------------------------------------------------------------------
|   domIsBMPChar 
|
\--------------------------------------------------------------------------*/
int
domIsBMPChar (
    const char *str
    )
{
    const char *p;
    int   clen;
    
    p = str;
    while (*p) {
        clen = UTF8_CHAR_LEN(*p);
        if (clen > 3 || clen == 0) return 0;
        p += clen;
    }
    return 1;
}

/*---------------------------------------------------------------------------
|   domIsComment
|
\--------------------------------------------------------------------------*/
int
domIsComment (
    const char *str
    )
{
    const char *p;
    int   len, i = 0;
    
    p = str;
    len = strlen (str);
    while (i < len) {
        if (*p == '-') {
            if (i == len - 1) return 0;
            p++; i++;
            if (*p == '-') return 0;
        }
        p++; i++;
    }
    return domIsChar (str);
}

/*---------------------------------------------------------------------------
|   domIsCDATA
|
\--------------------------------------------------------------------------*/
int
domIsCDATA (
    const char *str
    )
{
    const char *p;
    int   len, i = 0;

    p = str;
    len = strlen (str);
    while (i < len - 2) {
        if (  *p == ']'
            && p[1] == ']'
            && p[2] == '>') return 0;
        p++; i++;
    }
    return domIsChar (str);
}

/*---------------------------------------------------------------------------
|   domIsPIValue
|
\--------------------------------------------------------------------------*/
int
domIsPIValue (
    const char *str
    )
{
    const char *p;
    int   len, i = 0;

    p = str;
    len = strlen (str);
    while (i < len - 1) {
        if (*p == '?' && p[1] == '>') return 0;
        p++; i++;
    }
    return domIsChar (str);
}

/*---------------------------------------------------------------------------
|   domLookupNamespace
|
\--------------------------------------------------------------------------*/
domNS *
domLookupNamespace (
    domDocument *doc,
    const char  *prefix,
    const char  *namespaceURI
)
{
    domNS *ns;
    int i;

    if (prefix==NULL) return NULL;
    for (i = 0; i <= doc->nsptr; i++) {
        ns = doc->namespaces[i];
        if (   (ns->prefix != NULL)
            && (strcmp(prefix,ns->prefix)==0)
            && (strcmp(namespaceURI, ns->uri)==0)
        ) {
            return ns;
        }
    }
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * domPrecedes --
 *
 *	This helper procedure returns if node precedes other with regard
 *      to their position in the document and according to the document
 *      order. The two nodes could be out of the two documents. Both
 *      nodes must not be out of the fragments list.
 *
 * Results:
 *	1 if node precedes other in document order, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
domPrecedes (
    domNode *node,
    domNode *other
    )
{
    domNode *nodeAncestor, *otherAncestor;
    domAttrNode *attrN, *attrO;
    
    if (node == other) {
        return 0;
    }
    
    if (node->nodeType == ATTRIBUTE_NODE) {
        attrN = (domAttrNode*)node;
        if (other->nodeType == ATTRIBUTE_NODE) {
            attrO = (domAttrNode*)other;
            if (attrN->parentNode == attrO->parentNode) {
                attrN = attrN->nextSibling;
                while (attrN) {
                    if (attrN == attrO) {
                        return 1;
                    }
                    attrN = attrN->nextSibling;
                }
                return 0;
            } else {
                node = attrN->parentNode;
                other = attrO->parentNode;
            }
        } else {
            if (attrN->parentNode == other) {
                return 0;
            } else {
                node = attrN->parentNode;                
            }
        }
    }
    if (other->nodeType == ATTRIBUTE_NODE) {
        attrO = (domAttrNode*)other;
        if (node == attrO->parentNode) {
            return 1;
        } else {
            other = attrO->parentNode;
        }
    }
    
    if (node->ownerDocument != other->ownerDocument) {
        /* For mt tdom, this does not, what it should:
           whatever relative order two nodes out of different
           documents ever have (that is not determined by the rec) it
           must return always the same order (that is required by the 
           rec). */
        return (node->ownerDocument->documentNumber < 
                other->ownerDocument->documentNumber);
    }

#ifndef TCL_THREADS
    if (node->ownerDocument->nodeFlags & NEEDS_RENUMBERING) {
        domRenumberTree (node->ownerDocument->rootNode);
        node->ownerDocument->nodeFlags &= ~NEEDS_RENUMBERING;
    }
    return (node->nodeNumber < other->nodeNumber);
# else 
    if (node->ownerDocument->nodeFlags & NEEDS_RENUMBERING
        && node->ownerDocument->refCount <= 1) {
        domRenumberTree (node->ownerDocument->rootNode);
        node->ownerDocument->nodeFlags &= ~NEEDS_RENUMBERING;
    }
    if (!(node->ownerDocument->nodeFlags & NEEDS_RENUMBERING)) {
        return (node->nodeNumber < other->nodeNumber);
    }
#endif
    
    otherAncestor = other;
    while (otherAncestor->parentNode) {
        otherAncestor = otherAncestor->parentNode;
        if (otherAncestor == node) {
            return 1;
        }
    }
    
    nodeAncestor = node;
    while (nodeAncestor->parentNode) {
        otherAncestor = other;
        while (otherAncestor->parentNode) {
            if (nodeAncestor->parentNode == otherAncestor->parentNode) {
                nodeAncestor = nodeAncestor->nextSibling;
                while (nodeAncestor) {
                    if (nodeAncestor == otherAncestor) {
                        return 1;
                    }
                    nodeAncestor = nodeAncestor->nextSibling;
                }
                return 0;
            }
            otherAncestor = otherAncestor->parentNode;
        }
        nodeAncestor = nodeAncestor->parentNode;
        if (nodeAncestor == other) {
            return 0;
        }
    }
    nodeAncestor = nodeAncestor->nextSibling;
    while (nodeAncestor) {
        if (nodeAncestor == otherAncestor) {
            return 1;
        }
        nodeAncestor = nodeAncestor->nextSibling;
    }
    if (node == node->ownerDocument->rootNode) {
        return 1;
    }
    return 0;
}

/*---------------------------------------------------------------------------
|   domRenumberTree
|
\--------------------------------------------------------------------------*/
void
domRenumberTree (
    domNode *node
)
{
    while (node) {
        node->nodeNumber = NODE_NO(node->ownerDocument);
        if (node->nodeType == ELEMENT_NODE) {
            domRenumberTree (node->firstChild);
        }
        node = node->nextSibling;
    }
}

/*---------------------------------------------------------------------------
|   domLookupPrefixWithMappings
|
\--------------------------------------------------------------------------*/
const char *
domLookupPrefixWithMappings (
    domNode    *node,
    const char *prefix,
    char      **prefixMappings 
    )
{
    int    i;
    domNS *ns;
    
    if (prefixMappings) {
        i = 0;
        while (prefixMappings[i]) {
            if (strcmp (prefix, prefixMappings[i]) == 0) {
                return prefixMappings[i+1];
            }
            i += 2;
        }
    }
    ns = domLookupPrefix (node, prefix);
    if (ns) return ns->uri;
    else    return NULL;
}

/*---------------------------------------------------------------------------
|   domLookupPrefix
|
\--------------------------------------------------------------------------*/
domNS *
domLookupPrefix (
    domNode *node,
    const char    *prefix
    )
{
    domAttrNode   *NSattr;
    domNode       *orgNode = node;
    int            found;

    found = 0;
    while (node) {
        if (node->firstAttr && !(node->firstAttr->nodeFlags & IS_NS_NODE)) {
            node = node->parentNode;
            continue;
        }
        NSattr = node->firstAttr;
        while (NSattr && (NSattr->nodeFlags & IS_NS_NODE)) {
            if (prefix[0] == '\0') {
                if (NSattr->nodeName[5] == '\0') {
                    found = 1;
                    break;
                }
            } else {
                if (NSattr->nodeName[5] != '\0'
                    && strcmp (&NSattr->nodeName[6], prefix)==0) {
                    found = 1;
                    break;
                }
            }
            NSattr = NSattr->nextSibling;
        }
        if (found) {
            return domGetNamespaceByIndex (node->ownerDocument,
                                           NSattr->namespace);
        }
        node = node->parentNode;
    }
    if (prefix && (strcmp (prefix, "xml")==0)) {
        NSattr = orgNode->ownerDocument->rootNode->firstAttr;
        return domGetNamespaceByIndex (orgNode->ownerDocument,
                                       NSattr->namespace);
    }
    return NULL;
}

/*---------------------------------------------------------------------------
|   domIsNamespaceInScope
|
\--------------------------------------------------------------------------*/
static int
domIsNamespaceInScope (
    domActiveNS *NSstack,
    int          NSstackPos,
    const char  *prefix,
    const char  *namespaceURI
)
{
    int    i;

    for (i = NSstackPos; i >= 0; i--) {
        if (NSstack[i].namespace->prefix[0] &&
            (strcmp(NSstack[i].namespace->prefix, prefix)==0)) {
            if (strcmp(NSstack[i].namespace->uri, namespaceURI)==0) {
                /* OK, exactly the same namespace declaration is in scope */
                return 1;
            } else {
                /* This prefix is currently assigned to another uri,
                   we need a new NS declaration, to override this one */
                return 0;
            }
        }
    }
    return 0;
}

/*---------------------------------------------------------------------------
|   domLookupURI
|
\--------------------------------------------------------------------------*/
domNS *
domLookupURI (
    domNode *node,
    char        *uri
    )
{
    domAttrNode   *NSattr;
    int            found, alreadyHaveDefault;

    found = 0;
    alreadyHaveDefault = 0;
    while (node) {
        if (node->firstAttr && !(node->firstAttr->nodeFlags & IS_NS_NODE)) {
            node = node->parentNode;
            continue;
        }
        NSattr = node->firstAttr;
        while (NSattr && (NSattr->nodeFlags & IS_NS_NODE)) {
            if (NSattr->nodeName[5] == '\0') {
                if (!alreadyHaveDefault) {
                    if (strcmp (NSattr->nodeValue, uri)==0) {
                        found = 1;
                        break;
                    } else {
                        alreadyHaveDefault = 1;
                    }
                }
            } else {
                if (strcmp (NSattr->nodeValue, uri)==0) {
                    found = 1;
                    break;
                }
            }
            NSattr = NSattr->nextSibling;
        }
        if (found) {
            return domGetNamespaceByIndex (node->ownerDocument,
                                           NSattr->namespace);
        }
        node = node->parentNode;
    }
    return NULL;
}


/*---------------------------------------------------------------------------
|   domGetNamespaceByIndex
|
\--------------------------------------------------------------------------*/
domNS *
domGetNamespaceByIndex (
    domDocument *doc,
    int          nsIndex
)
{
    if (!nsIndex) return NULL;
    return doc->namespaces[nsIndex-1];
}


/*---------------------------------------------------------------------------
|   domNewNamespace
|
\--------------------------------------------------------------------------*/
domNS* domNewNamespace (
    domDocument *doc,
    const char  *prefix,
    const char  *namespaceURI
)
{
    domNS *ns = NULL;

    DBG(fprintf(stderr, "domNewNamespace '%s' --> '%s' \n", prefix, namespaceURI);)

    ns = domLookupNamespace (doc, prefix, namespaceURI);
    if (ns != NULL) return ns;
    doc->nsptr++;
#ifdef TDOM_LESS_NS
    if (doc->nsptr > 254) {
        DBG(fprintf (stderr, "maximum number of namespaces exceeded!!!\n");)
        domPanic("domNewNamespace: maximum number of namespaces exceeded!");
    }
#endif
    if (doc->nsptr >= doc->nslen) {
        doc->namespaces = (domNS**) REALLOC ((char*) doc->namespaces,
                                             sizeof (domNS*) * 2 * doc->nslen);
        doc->nslen *= 2;
    }
    doc->namespaces[doc->nsptr] = (domNS*)MALLOC (sizeof (domNS));
    ns = doc->namespaces[doc->nsptr];


    if (prefix == NULL) {
        ns->prefix = tdomstrdup("");
    } else {
        ns->prefix = tdomstrdup(prefix);
    }
    if (namespaceURI == NULL) {
        ns->uri = tdomstrdup("");
    } else {
        ns->uri   = tdomstrdup(namespaceURI);
    }
    ns->index = doc->nsptr + 1;

    return ns;
}


/*---------------------------------------------------------------------------
|   domSplitQName  -  extract namespace prefix (if any)
|
\--------------------------------------------------------------------------*/
int
domSplitQName (
    const char  *name,
    char        *prefix,
    const char **localName
)
{
    const char  *s;
    char        *p, *prefixEnd;

    s = name;
    p = prefix;
    prefixEnd = &prefix[MAX_PREFIX_LEN-1];
    while (*s && (*s != ':'))  {
        if (p < prefixEnd) *p++ = *s;
        s++;
    }
    if (*s != ':') {
        *prefix    = '\0';
        *localName = name;
        return 0;
    }
    *p++ = '\0';
    *localName = ++s;
    DBG(fprintf(stderr, "domSplitName %s -> '%s' '%s'\n",
                         name, prefix, *localName);
    )
    return 1;
}


/*---------------------------------------------------------------------------
|   domNamespaceURI
|
\--------------------------------------------------------------------------*/
const char *
domNamespaceURI (
    domNode *node
)
{
    domAttrNode *attr;
    domNS       *ns;

    if (node->nodeType == ATTRIBUTE_NODE) {
        attr = (domAttrNode*)node;
        if (!attr->namespace) return NULL;
        if (attr->nodeFlags & IS_NS_NODE) return NULL;
        ns = attr->parentNode->ownerDocument->namespaces[attr->namespace-1];
    } else
    if (node->nodeType == ELEMENT_NODE) {
        if (!node->namespace) return NULL;
        ns = node->ownerDocument->namespaces[node->namespace-1];
    } else {
        return NULL;
    }
    return ns->uri;
}


/*---------------------------------------------------------------------------
|   domNamespacePrefix
|
\--------------------------------------------------------------------------*/
const char *
domNamespacePrefix (
    domNode *node
)
{
    domAttrNode *attr;
    domNS *ns;

    if (node->nodeType == ATTRIBUTE_NODE) {
        attr = (domAttrNode*)node;
        if (!attr->namespace) return NULL;
        ns = attr->parentNode->ownerDocument->namespaces[attr->namespace-1];
    } else
    if (node->nodeType == ELEMENT_NODE) {
        if (!node->namespace) return NULL;
        ns = node->ownerDocument->namespaces[node->namespace-1];
    } else {
        return NULL;
    }
    if (ns) return ns->prefix;
    return NULL;
}


/*---------------------------------------------------------------------------
|   domGetLocalName
|
\--------------------------------------------------------------------------*/
const char *
domGetLocalName (
    const char *nodeName
)
{
    char prefix[MAX_PREFIX_LEN];
    const char *localName;

    domSplitQName (nodeName, prefix, &localName);
    return localName;
}

/*
 *----------------------------------------------------------------------
 *
 * domGetAttributeNodeNS --
 *
 *      Search a given node for an attribute with namespace "uri" and
 *      localname "localname".
 *
 * Results:
 *      Returns a pointer to the attribute, if there is one with the
 *      given namespace and localname. Otherwise returns NULL.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

domAttrNode *
domGetAttributeNodeNS (
    domNode    *node,         /* The attributes of this node are searched for a
                                 matching attribute; the node must exist */
    const char *uri,          /* The namespace of the demanded attribute */
    const char *localname     /* The localname of the demanded attribute */
    )
{
    domAttrNode *attr;
    domNS       *ns;
    int          noNS;
    char         prefix[MAX_PREFIX_LEN];
    const char  *attrLocalName;

    if (uri[0] == '\0') noNS = 1;
    else                noNS = 0;

    attr = node->firstAttr;
    while (attr) {
        if (noNS) {
            if (!attr->namespace 
                && strcmp (attr->nodeName, localname) == 0) {
                return attr;
                
            }
        } else {
            if (attr->namespace) {
                domSplitQName (attr->nodeName, prefix, &attrLocalName);
                if (strcmp (localname, attrLocalName) == 0) {
                    ns = domGetNamespaceByIndex (node->ownerDocument,
                                                 attr->namespace);
                    if (strcmp (ns->uri, uri) == 0) {
                        return attr;
                    }
                }
            }
        }
        attr = attr->nextSibling;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * domPreviousSibling --
 *
 *      Returns the previous node to the given node or NULL, if there
 *      is no previous node. This function is needed in situations,
 *      where the given node may also be an domAttrNode. Namespace
 *      declaring attributes are treated as any other
 *      attributes. Since the domAttrNode struct doesn't has an
 *      element for the previous attribute, we need a function for the
 *      relatively rare cases, the 'previous attribute' is
 *      needed. Remember, that the XML rec say, that there is no
 *      specific order of the attributes of a node.
 *
 * Results: 
 *      A pointer to the previous node of the given one
 *      or NULL, if there isn't a previous node.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

domNode *
domPreviousSibling (
    domNode *node      /* The reference attribute */
    )
{
    domAttrNode *attr, *attr1;
    
    if (node->nodeType != ATTRIBUTE_NODE) {
        return node->previousSibling;
    }

    attr = (domAttrNode*) node;
    if (attr->parentNode->firstAttr == attr) {
        return NULL;
    }
    attr1 = attr->parentNode->firstAttr;
    while (attr1) {
        if (attr1->nextSibling == attr) {
            return (domNode*)attr1;
        }
        attr1 = attr1->nextSibling;
    }
    /* Not reached */
    return NULL;
}

#ifndef  TDOM_NO_EXPAT

/*
 *----------------------------------------------------------------------------
 *
 * TncRewriteModel --
 *
 *	This helper procedure creates recursively a TNC_Content from
 *      a XML_Content.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates memory for the TNC_Content models.
 *
 *----------------------------------------------------------------------------
 */

static void
TncRewriteModel (emodel, tmodel, tagNames)
    XML_Content   *emodel;
    TNC_Content   *tmodel;
    Tcl_HashTable *tagNames;
{
    Tcl_HashEntry *entryPtr;
    unsigned int i;

    tmodel->type = emodel->type;
    tmodel->quant = emodel->quant;
    tmodel->numchildren = emodel->numchildren;
    tmodel->children = NULL;
    tmodel->nameId = NULL;
    switch (emodel->type) {
    case XML_CTYPE_MIXED:
        if (emodel->quant == XML_CQUANT_REP) {
            tmodel->children = (TNC_Content *)
                MALLOC (sizeof (TNC_Content) * emodel->numchildren);
            for (i = 0; i < emodel->numchildren; i++) {
                TncRewriteModel (&emodel->children[i], &tmodel->children[i],
                                 tagNames);
            }
        }
        break;
    case XML_CTYPE_ANY:
    case XML_CTYPE_EMPTY:
        /* do nothing */
        break;
    case XML_CTYPE_SEQ:
    case XML_CTYPE_CHOICE:
        tmodel->children = (TNC_Content *)
            MALLOC (sizeof (TNC_Content) * emodel->numchildren);
        for (i = 0; i < emodel->numchildren; i++) {
            TncRewriteModel (&emodel->children[i], &tmodel->children[i],
                             tagNames);
        }
        break;
    case XML_CTYPE_NAME:
        entryPtr = Tcl_FindHashEntry (tagNames, emodel->name);
        /* Notice, that it is possible for entryPtr to be NULL.
           This means, a content model uses a not declared element.
           This is legal even in valid documents. (Of course, if the
           undeclared element actually shows up in the document
           that would make the document invalid.) See rec 3.2

           QUESTION: Should there be a flag to enable a warning,
           when a declaration contains an element type for which
           no declaration is provided, as rec 3.2 metioned?
           This would be the appropriated place to omit the
           warning. */
        tmodel->nameId = entryPtr;
    }
}

/*
 *----------------------------------------------------------------------------
 *
 * TncEndDoctypeDeclHandler --
 *
 *	This procedure is called at the end of the DOCTYPE
 *      declaration, after processing any external subset.
 *      It rewrites the XML_Content models to TNC_Content
 *      models and frees the XML_Content models.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Rewrites the XML_Content models to TNC_Content
 *      models.
 *
 *----------------------------------------------------------------------------
 */

static void
TncEndDoctypeDeclHandler (userData)
    void *userData;
{
    domReadInfo *tncdata = (domReadInfo *) userData;
    Tcl_HashEntry *entryPtr, *ePtr1;
    Tcl_HashSearch search;
    XML_Content   *emodel;
    TNC_Content   *tmodel = NULL;
    char *elementName;

    entryPtr = Tcl_FirstHashEntry (tncdata->tagNames, &search);
    while (entryPtr != NULL) {
#ifdef TNC_DEBUG
        printf ("name: %-20s   nameId: %p\n",
                Tcl_GetHashKey (tncdata->tagNames, entryPtr),
                entryPtr);
#endif
        emodel = (XML_Content*) Tcl_GetHashValue (entryPtr);
        tmodel = (TNC_Content*) MALLOC (sizeof (TNC_Content));
        TncRewriteModel (emodel, tmodel, tncdata->tagNames);
        elementName = Tcl_GetHashKey (tncdata->tagNames, entryPtr);
        ePtr1 = Tcl_FindHashEntry (tncdata->attDefsTables, elementName);
        if (ePtr1) {
            tmodel->attInfo = (TNC_ElemAttInfo *) Tcl_GetHashValue (ePtr1);
        } else {
            tmodel->attInfo = NULL;
        }
        Tcl_SetHashValue (entryPtr, tmodel);
        XML_MemFree (tncdata->parser, emodel);
        entryPtr = Tcl_NextHashEntry (&search);
    }
    tncdata->elemContentsRewriten = 1;
    /* Checks, if every used notation name is in deed declared */
    entryPtr = Tcl_FirstHashEntry (tncdata->notationDecls, &search);
    while (entryPtr != NULL) {
#ifdef TNC_DEBUG
        printf ("check notation name %s\n",
                Tcl_GetHashKey (tncdata->notationDecls, entryPtr));
        printf ("value %p\n", Tcl_GetHashValue (entryPtr));
#endif
        if (!Tcl_GetHashValue (entryPtr)) {
            signalNotValid (userData, TNC_ERROR_NOTATION_MUST_BE_DECLARED);
            return;
        }
        entryPtr = Tcl_NextHashEntry (&search);
    }
    /* Checks, if every used entity name is indeed declared */
    entryPtr = Tcl_FirstHashEntry (tncdata->entityDecls, &search);
    while (entryPtr != NULL) {
        if (!Tcl_GetHashValue (entryPtr)) {
            signalNotValid (userData,
                            TNC_ERROR_ATT_ENTITY_DEFAULT_MUST_BE_DECLARED);
            return;
        }
        entryPtr = Tcl_NextHashEntry (&search);
    }
    tncdata->dtdstatus = 1;
}

/*
 *----------------------------------------------------------------------------
 *
 * TncProbeElement --
 *
 *	This function checks, if the element match the
 *      topmost content model on the content stack.
 *
 * Results:
 *	1 if the element match,
 *      0 if not.
 *     -1 if not, but this isn't a validation error
 *
 * Side effects:
 *	Eventually pushes data to the contentStack (even in
 *      recurive calls).
 *
 *----------------------------------------------------------------------------
 */

static int
TncProbeElement (nameId, tncdata)
    TNC_NameId *nameId;
    domReadInfo   *tncdata;
{
    TNC_ContentStack *stackelm;
    TNC_Content *activeModel;
    int myStackPtr, zeroMatchPossible, result;
    unsigned int i, seqstartindex;

#ifdef TNC_DEBUG
    printf ("TncProbeElement start\n");
    printContentStack (tncdata);
#endif
    myStackPtr = tncdata->contentStackPtr - 1;
    stackelm = &(tncdata->contentStack)[myStackPtr];
    switch (stackelm->model->type) {
    case XML_CTYPE_MIXED:
#ifdef TNC_DEBUG
        printf ("TncProbeElement XML_CTYPE_MIXED\n");
#endif
        for (i = 0; i < stackelm->model->numchildren; i++) {
            if ((&stackelm->model->children[i])->nameId == nameId) {
                return 1;
            }
        }
        return 0;
    case XML_CTYPE_ANY:
#ifdef TNC_DEBUG
        printf ("TncProbeElement XML_CTYPE_ANY\n");
#endif
        return 1;
    case XML_CTYPE_EMPTY:
#ifdef TNC_DEBUG
        printf ("TncProbeElement XML_CTYPE_EMPTY\n");
#endif
        return 0;
    case XML_CTYPE_CHOICE:
#ifdef TNC_DEBUG
        printf ("TncProbeElement XML_CTYPE_CHOICE\n");
#endif
        if (stackelm->alreadymatched) {
            activeModel = &stackelm->model->children[stackelm->activeChild];
            if (activeModel->type == XML_CTYPE_NAME) {
                /* so this stackelement must be the topmost */
                if (activeModel->quant == XML_CQUANT_REP
                    || activeModel->quant == XML_CQUANT_PLUS) {
                    /* the last matched element is multiple, maybe it
                       matches again */
                    if (nameId == activeModel->nameId) {
#ifdef TNC_DEBUG
                        printf ("-->matched! child Nr. %d\n",
                                stackelm->activeChild);
#endif
                        /* stack and activeChild nr. are already OK, just
                           report success. */
                        return 1;
                    }
                }
            }
            /* The active child is a SEQ or CHOICE. */
            if (stackelm->model->quant == XML_CQUANT_NONE ||
                stackelm->model->quant == XML_CQUANT_OPT) {
                /*The child cp's type SEQ or CHOICE keep track by
                  themselve about if they are repeated. Because we are
                  here, they don't.  Since the current cp has already
                  matched and isn't multiple, the current cp as a whole
                  is done.  But no contradiction detected, so return
                  "search further" */
                return -1;
            }
        }

        /* If one of the alternatives within the CHOICE cp is quant
           REP or OPT, it isn't a contradition to the document structure,
           if the cp doesn't match, even if it is quant
           NONE or PLUS, because of the "zero time" match of this one
           alternative. We use zeroMatchPossible, to know about this.*/
        zeroMatchPossible = 0;
        for (i = 0; i < stackelm->model->numchildren; i++) {
            if ((&stackelm->model->children[i])->type == XML_CTYPE_NAME) {
#ifdef TNC_DEBUG
                printf ("child is type NAME\n");
#endif
                if ((&stackelm->model->children[i])->nameId == nameId) {
#ifdef TNC_DEBUG
                    printf ("-->matched! child Nr. %d\n",i);
#endif
                    (&tncdata->contentStack[myStackPtr])->activeChild = i;
                    (&tncdata->contentStack[myStackPtr])->alreadymatched = 1;
                    return 1;
                }
                else {
                    /* If the name child is optional, we have a
                       candidat for "zero match". */
                    if ((&stackelm->model->children[i])->quant
                        == XML_CQUANT_OPT ||
                        (&stackelm->model->children[i])->quant
                        == XML_CQUANT_REP) {
#ifdef TNC_DEBUG
                        printf ("zero match possible\n");
#endif
                        zeroMatchPossible = 1;
                    }
                }
            }
            else {
#ifdef TNC_DEBUG
                printf ("complex child type\n");
#endif
                if (tncdata->contentStackPtr == tncdata->contentStackSize) {
                    tncdata->contentStack = (TNC_ContentStack *)
                        Tcl_Realloc ((char *)tncdata->contentStack,
                                     sizeof (TNC_Content *) * 2 *
                                     tncdata->contentStackSize);
                    tncdata->contentStackSize *= 2;
                }
                (&tncdata->contentStack[tncdata->contentStackPtr])->model
                    = &stackelm->model->children[i];
                tncdata->contentStack[tncdata->contentStackPtr].activeChild
                    = 0;
                tncdata->contentStack[tncdata->contentStackPtr].deep
                    = stackelm->deep + 1;
                tncdata->contentStack[tncdata->contentStackPtr].alreadymatched
                    = 0;
                tncdata->contentStackPtr++;
                result = TncProbeElement (nameId, tncdata);
                if (result == 1) {
#ifdef TNC_DEBUG
                    printf ("-->matched! child nr. %d\n",i);
#endif
                    (&tncdata->contentStack[myStackPtr])->activeChild = i;
                    (&tncdata->contentStack[myStackPtr])->alreadymatched = 1;
                    return 1;
                }
                /* The child cp says, it doesn't has matched, but says
                   also, it's perfectly OK, if it doesn't at all. So we
                   have a candidat for "zero match". */
                if (result == -1) {
                    zeroMatchPossible = 1;
                }
                tncdata->contentStackPtr--;
            }
        }
        /* OK, nobody has claimed a match. Question is: try further or is
           this a document structure error. */
        if (zeroMatchPossible ||
            stackelm->alreadymatched ||
            stackelm->model->quant == XML_CQUANT_REP ||
            stackelm->model->quant == XML_CQUANT_OPT) {
            return -1;
        }
#ifdef TNC_DEBUG
        printf ("validation error\n");
#endif
        return 0;
    case XML_CTYPE_SEQ:
#ifdef TNC_DEBUG
        printf ("TncProbeElement XML_CTYPE_SEQ\n");
#endif
        if (stackelm->alreadymatched) {
            activeModel = &stackelm->model->children[stackelm->activeChild];
            if (activeModel->type == XML_CTYPE_NAME) {
                /* so this stackelement must be the topmost */
                if (activeModel->quant == XML_CQUANT_REP
                    || activeModel->quant == XML_CQUANT_PLUS) {
                    /* the last matched element is multiple, maybe it
                       matches again */
                    if (nameId == activeModel->nameId) {
#ifdef TNC_DEBUG
                        printf ("-->matched! child Nr. %d\n",
                                stackelm->activeChild);
#endif
                        /* stack and activeChild nr. are already OK, just
                           report success. */
                        return 1;
                    }
                }
            }
        }

        if (stackelm->alreadymatched) {
            seqstartindex = stackelm->activeChild + 1;
        }
        else {
            seqstartindex = 0;
        }
        /* This time zeroMatchPossible flags, if every of the remaining
           childs - that may every child, if !alreadymatched - doesn't
           must occur.  We assume, the (outstanding childs of, in case
           of alreadymatched) current stackelement model has only
           optional childs, and set to wrong, if we find any
           non-optional child */
        zeroMatchPossible = 1;
        for (i = seqstartindex; i < stackelm->model->numchildren; i++) {
            if ((&stackelm->model->children[i])->type == XML_CTYPE_NAME) {
                if ((&stackelm->model->children[i])->nameId == nameId) {
#ifdef TNC_DEBUG
                    printf ("-->matched! child Nr. %d\n",i);
#endif
                    (&tncdata->contentStack[myStackPtr])->activeChild = i;
                    (&tncdata->contentStack[myStackPtr])->alreadymatched = 1;
                    return 1;
                } else if ((&stackelm->model->children[i])->quant
                           == XML_CQUANT_NONE
                           || (&stackelm->model->children[i])->quant
                               == XML_CQUANT_PLUS) {
                    zeroMatchPossible = 0;
                    break;
                }
            } else {
                if (tncdata->contentStackPtr == tncdata->contentStackSize) {
                    tncdata->contentStack = (TNC_ContentStack *)
                        Tcl_Realloc ((char *)tncdata->contentStack,
                                     sizeof (TNC_Content *) * 2 *
                                     tncdata->contentStackSize);
                    tncdata->contentStackSize *= 2;
                }
                (&tncdata->contentStack[tncdata->contentStackPtr])->model =
                    &stackelm->model->children[i];
                tncdata->contentStack[tncdata->contentStackPtr].activeChild
                    = 0;
                tncdata->contentStack[tncdata->contentStackPtr].deep
                    = stackelm->deep + 1;
                tncdata->contentStack[tncdata->contentStackPtr].alreadymatched
                    = 0;
                tncdata->contentStackPtr++;
                result = TncProbeElement (nameId, tncdata);
                if (result == 1) {
                    (&tncdata->contentStack[myStackPtr])->activeChild = i;
                    (&tncdata->contentStack[myStackPtr])->alreadymatched = 1;
                    return 1;
                }
                tncdata->contentStackPtr--;
                if (result == 0) {
                    zeroMatchPossible = 0;
                    break;
                }
            }
        }
        if (!stackelm->alreadymatched) {
            if (zeroMatchPossible) {
                /* The stackelm hasn't matched, but don't have to
                   after all.  Return try further */
                return -1;
            } else {
                /* No previous match, but at least one child is
                   necessary. Return depends of the quant of the
                   entire seq */
                if (stackelm->model->quant == XML_CQUANT_NONE ||
                    stackelm->model->quant == XML_CQUANT_PLUS) {
                    /* DTD claims, the seq as to be there, but isn't */
                    return 0;
                } else {
                    /* The seq is optional */
                    return -1;
                }
            }
        }
        if (stackelm->alreadymatched) {
            if (!zeroMatchPossible) {
                /* Some child at the start of the seq has matched in
                   the past, but since zeroMatchPossible has changed
                   to zero, there must be a non-matching non-optional
                   child later. Error in document structure. */
                return 0;
            } else {
                /* OK, SEQ has matched befor. But after the last match, there
                   where no required (quant NONE or PLUS) childs. */
                if (stackelm->model->quant == XML_CQUANT_NONE ||
                    stackelm->model->quant == XML_CQUANT_OPT) {
                    /* The entire seq isn't multiple. Just look further. */
                    return -1;
                }
            }
        }
        /* The last untreated case is alreadymatched true,
           zeroMatchPossible (of the rest of the seq childs after the
           last match) true and the entire seq may be
           multiple. Therefore start again with activeChild = 0, to
           see, if the current nameId starts a repeated match of the
           seq.  By the way: zeroMatchPossible still has initial value
           1, therefor no second initialiation is needed */
        for (i = 0; i < seqstartindex; i++) {
            if ((&stackelm->model->children[i])->type == XML_CTYPE_NAME) {
                if ((&stackelm->model->children[i])->nameId == nameId) {
#ifdef TNC_DEBUG
                    printf ("-->matched! child Nr. %d\n",i);
#endif
                    (&tncdata->contentStack[myStackPtr])->activeChild = i;
                    (&tncdata->contentStack[myStackPtr])->alreadymatched = 1;
                    return 1;
                } else if ((&stackelm->model->children[i])->quant
                           == XML_CQUANT_NONE
                           || (&stackelm->model->children[i])->quant
                               == XML_CQUANT_PLUS) {
                    zeroMatchPossible = 0;
                    break;
                }
            } else {
                if (tncdata->contentStackPtr == tncdata->contentStackSize) {
                    tncdata->contentStack = (TNC_ContentStack *)
                        Tcl_Realloc ((char *)tncdata->contentStack,
                                     sizeof (TNC_Content *) * 2 *
                                     tncdata->contentStackSize);
                    tncdata->contentStackSize *= 2;
                }
                (&tncdata->contentStack[tncdata->contentStackPtr])->model =
                    &stackelm->model->children[i];
                tncdata->contentStack[tncdata->contentStackPtr].activeChild
                    = 0;
                tncdata->contentStack[tncdata->contentStackPtr].deep
                    = stackelm->deep + 1;
                tncdata->contentStack[tncdata->contentStackPtr].alreadymatched
                    = 0;
                tncdata->contentStackPtr++;
                result = TncProbeElement (nameId, tncdata);
                if (result) {
                    (&tncdata->contentStack[myStackPtr])->activeChild = i;
                    /* alreadymatched is already 1 */
                    return 1;
                }
                tncdata->contentStackPtr--;
                if (result == 0) {
                    /* OK, the seq doesn't match again. But since it have
                       already matched, this isn't return 0 but.. */
                    return -1;
                }
            }
        }
        /* seq doesn't match again and every seq child from the very first
           up to (not including) the last match aren't required. This last
           fact may be nice to know, but after all since the entire seq have
           matched already ... */
        return -1;
    case XML_CTYPE_NAME:
        /* NAME type dosen't occur at top level of a content model and is
           handled in some "shotcut" way directly in the CHOICE and SEQ cases.
           It's only here to pacify gcc -Wall. */
        printf ("error!!! - in TncProbeElement: XML_CTYPE_NAME shouldn't reached in any case.\n");
    default:
        printf ("error!!! - in TncProbeElement: unknown content type: %d\n",
                stackelm->model->type);
    }
    /* not reached */
    printf ("error!!! - in TncProbeElement: end of function reached.\n");
    return 0;
}

/*
 *----------------------------------------------------------------------------
 *
 * TncProbeAttribute --
 *
 *	This function checks, if the given attribute 
 *      and it's value are allowed for this element.
 *
 * Results:
 *	1 if the attribute name/value is OK,
 *      0 if not.
 *
 * Side effects:
 *	Eventually increments the required attributes counter.
 *
 *----------------------------------------------------------------------------
 */

static int
TncProbeAttribute (userData, elemAtts, attrName, attrValue, nrOfreq)
    void *userData;
    Tcl_HashTable *elemAtts;
    char *attrName;
    char *attrValue;
    int *nrOfreq;
{
    domReadInfo *tncdata = (domReadInfo *) userData;
    Tcl_HashEntry *entryPtr;
    TNC_AttDecl *attDecl;
    char *pc, *copy, save;
    int clen, i, start, hnew;
    TNC_EntityInfo *entityInfo;

    entryPtr = Tcl_FindHashEntry (elemAtts, attrName);
    if (!entryPtr) {
        signalNotValid (userData, TNC_ERROR_UNKNOWN_ATTRIBUTE);
        return 0;
    }
    /* NOTE: attribute uniqueness per element is a wellformed
               constrain and therefor done by expat. */
    attDecl = (TNC_AttDecl *) Tcl_GetHashValue (entryPtr);
    switch (attDecl->att_type) {
    case TNC_ATTTYPE_CDATA:
        if (attDecl->isrequired && attDecl->dflt) {
            if (strcmp (attDecl->dflt, attrValue) != 0) {
                signalNotValid (userData,
                                TNC_ERROR_WRONG_FIXED_ATTVALUE);
                return 0;
            }
        }
        break;

    case TNC_ATTTYPE_ID:
        pc = (char*)attrValue;
        clen = UTF8_CHAR_LEN (*pc);
        CHECK_UTF_CHARLENR (clen);
        if (!UTF8_GET_NAME_START (pc, clen)) {
            signalNotValid (userData, TNC_ERROR_NAME_REQUIRED);
        }
        pc += clen;
        while (1) {
            if (*pc == '\0') {
                break;
            }
            clen = UTF8_CHAR_LEN (*pc);
            CHECK_UTF_CHARLENR (clen);
            if (!UTF8_GET_NAMING_NMTOKEN (pc, clen)) {
                signalNotValid (userData, TNC_ERROR_NAME_REQUIRED);
                return 0;
            }
            pc += clen;
        }
        entryPtr = Tcl_CreateHashEntry (tncdata->ids, attrValue, &hnew);
        if (!hnew) {
            if (Tcl_GetHashValue (entryPtr)) {
                signalNotValid (userData,
                                TNC_ERROR_DUPLICATE_ID_VALUE);
                return 0;
            }
        }
        Tcl_SetHashValue (entryPtr, (char *) 1);
        break;

    case TNC_ATTTYPE_IDREF:
        /* Name type constraint "implicit" checked. If the
           referenced ID exists, the type must be OK, because the
           type of the ID's within the document are checked.
           If there isn't such an ID, it's an error anyway. */
        if (attrValue[0] == '\0') {
            signalNotValid (userData, TNC_ERROR_NAME_REQUIRED);
            return 0;
        }
        entryPtr = Tcl_CreateHashEntry (tncdata->ids, attrValue, &hnew);
        break;

    case TNC_ATTTYPE_IDREFS:
        if (attrValue[0] == '\0') {
            signalNotValid (userData, TNC_ERROR_NAMES_REQUIRED);
            return 0;
        }
        /* Due to attribute value normalization (xml rec 3.3.3) this
           is a simple list "ref ref ref ..." without leading or
           trailing spaces and exact one space between the refs. */
        start = i = 0;
        while (attrValue[i]) {
            if (attrValue[i] == ' ') {
                save = attrValue[i];
                attrValue[i] = '\0';
                entryPtr = Tcl_CreateHashEntry (tncdata->ids,
                                                &attrValue[start], &hnew);
                attrValue[i] = save;
                start = ++i;
                continue;
            }
            i++;
        }
        entryPtr = Tcl_CreateHashEntry (tncdata->ids, &attrValue[start], 
                                        &hnew);
        break;

    case TNC_ATTTYPE_ENTITY:
        /* There is a validity constraint requesting entity attributes
           values to be type Name. But if there would be an entity
           declaration that doesn't fit this constraint, expat would
           have already complained about the definition. So we go the
           easy way and just look up the att value. If it's declared,
           type must be OK, if not, it's an error anyway. */
        entryPtr = Tcl_FindHashEntry (tncdata->entityDecls, attrValue);
        if (!entryPtr) {
            signalNotValid (userData, TNC_ERROR_ENTITY_ATTRIBUTE);
            return 0;
        }
        entityInfo = (TNC_EntityInfo *) Tcl_GetHashValue (entryPtr);
        if (!entityInfo->is_notation) {
            signalNotValid (userData, TNC_ERROR_ENTITY_ATTRIBUTE);
            return 0;
        }
        break;

    case TNC_ATTTYPE_ENTITIES:
        /* Normalized by exapt; for type see comment to
           TNC_ATTTYPE_ENTITY */
        copy = tdomstrdup (attrValue);
        start = i = 0;
        while (1) {
            if (copy[i] == '\0') {
                entryPtr = Tcl_FindHashEntry (tncdata->entityDecls,
                                              &copy[start]);
                if (!entryPtr) {
                    signalNotValid (userData, TNC_ERROR_ENTITIES_ATTRIBUTE);
                    FREE (copy);
                    return 0;
                }
                entityInfo = (TNC_EntityInfo *) Tcl_GetHashValue (entryPtr);
                if (!entityInfo->is_notation) {
                    signalNotValid (userData, TNC_ERROR_ENTITIES_ATTRIBUTE);
                    FREE (copy);
                    return 0;
                }
                FREE (copy);
                break;
            }
            if (copy[i] == ' ') {
                copy[i] = '\0';
                entryPtr = Tcl_FindHashEntry (tncdata->entityDecls,
                                              &copy[start]);
                if (!entryPtr) {
                    signalNotValid (userData, TNC_ERROR_ENTITIES_ATTRIBUTE);
                    FREE (copy);
                    return 0;
                }
                entityInfo = (TNC_EntityInfo *) Tcl_GetHashValue (entryPtr);
                if (!entityInfo->is_notation) {
                    signalNotValid (userData, TNC_ERROR_ENTITIES_ATTRIBUTE);
                    FREE (copy);
                    return 0;
                }
                start = ++i;
                continue;
            }
            i++;
        }
        break;

    case TNC_ATTTYPE_NMTOKEN:
        /* We assume, that the UTF-8 representation of the value is
           valid (no partial chars, minimum encoding). This makes
           things a little more easy and faster. I guess (but
           haven't deeply checked - QUESTION -), expat would have
           already complained otherwise. */
        pc = (char*)attrValue;
        clen = 0;
        while (1) {
            if (*pc == '\0') {
                break;
            }
            clen = UTF8_CHAR_LEN (*pc);
            CHECK_UTF_CHARLENR (clen);
            if (!UTF8_GET_NAMING_NMTOKEN (pc, clen)) {
                signalNotValid (userData, TNC_ERROR_NMTOKEN_REQUIRED);
                return 0;
            }
            pc += clen;
        }
        if (!clen) 
            signalNotValid (userData, TNC_ERROR_NMTOKEN_REQUIRED);
        break;

    case TNC_ATTTYPE_NMTOKENS:
        pc = (char*)attrValue;
        clen = 0;
        while (1) {
            if (*pc == '\0') {
                break;
            }
            /* NMTOKENS are normalized by expat, so this should
               be secure. */
            if (*pc == ' ') {
                pc++;
            }
            clen = UTF8_CHAR_LEN (*pc);
            CHECK_UTF_CHARLENR (clen);
            if (!UTF8_GET_NAMING_NMTOKEN (pc, clen)) {
                signalNotValid (userData, TNC_ERROR_NMTOKEN_REQUIRED);
                return 0;
            }
            pc += clen;
        }
        if (!clen)
            signalNotValid (userData, TNC_ERROR_NMTOKEN_REQUIRED);
        break;

    case TNC_ATTTYPE_NOTATION:
        entryPtr = Tcl_FindHashEntry (attDecl->lookupTable, attrValue);
        if (!entryPtr) {
            signalNotValid (userData, TNC_ERROR_NOTATION_REQUIRED);
            return 0;
        }
        break;

    case TNC_ATTTYPE_ENUMERATION:
        if (!Tcl_FindHashEntry (attDecl->lookupTable, attrValue)) {
            signalNotValid (userData, TNC_ERROR_ENUM_ATT_WRONG_VALUE);
            return 0;
        }
        break;
    }

    if (attDecl->isrequired) {
        (*nrOfreq)++;
    }

    return 1;
}

/*
 *----------------------------------------------------------------------------
 *
 * TncProbeElementEnd --
 *
 *	This procedure checks, if the current content allows the
 *      the element to end here.
 *
 * Results:
 *	1 if element end is OK,
 *      0 if not.
 *
 * Side effects:
 *	Let the contentStackPtr point to the last current content
 *      model before the element had started.
 *
 *----------------------------------------------------------------------------
 */

static int
TncProbeElementEnd (tncdata)
    domReadInfo *tncdata;
{
    TNC_ContentStack stackelm;
    unsigned int i;
    int zeroMatchPossible, seqstartindex;

    stackelm = tncdata->contentStack[tncdata->contentStackPtr - 1];
    switch (stackelm.model->type) {
    case XML_CTYPE_MIXED:
    case XML_CTYPE_ANY:
    case XML_CTYPE_EMPTY:
        return 1;
    case XML_CTYPE_CHOICE:
        if (stackelm.alreadymatched) {
            return 1;
        }

        if (stackelm.model->quant == XML_CQUANT_REP ||
            stackelm.model->quant == XML_CQUANT_OPT) {
            return 1;
        }
        zeroMatchPossible = 0;
        for (i = 0; i < stackelm.model->numchildren; i++) {
            if ((&stackelm.model->children[i])->type == XML_CTYPE_NAME) {
                if ((&stackelm.model->children[i])->quant == XML_CQUANT_OPT ||
                    (&stackelm.model->children[i])->quant == XML_CQUANT_REP) {
                    zeroMatchPossible = 1;
                    break;
                }
            }
            else {
                if (tncdata->contentStackPtr == tncdata->contentStackSize) {
                    tncdata->contentStack = (TNC_ContentStack *)
                        Tcl_Realloc ((char *)tncdata->contentStack,
                                     sizeof (TNC_Content *) * 2 *
                                     tncdata->contentStackSize);
                    tncdata->contentStackSize *= 2;
                }
                (&tncdata->contentStack[tncdata->contentStackPtr])->model
                    = &stackelm.model->children[i];
                tncdata->contentStack[tncdata->contentStackPtr].activeChild
                    = 0;
                tncdata->contentStack[tncdata->contentStackPtr].deep
                    = stackelm.deep + 1;
                tncdata->contentStack[tncdata->contentStackPtr].alreadymatched
                    = 0;
                tncdata->contentStackPtr++;
                if (TncProbeElementEnd (tncdata)) {
                    zeroMatchPossible = 1;
                    tncdata->contentStackPtr--;
                    break;
                }
                tncdata->contentStackPtr--;
            }
        }
        if (zeroMatchPossible) {
            return 1;
        } else {
            return 0;
        }
    case XML_CTYPE_SEQ:
        if (!stackelm.alreadymatched) {
            if (stackelm.model->quant == XML_CQUANT_REP ||
                stackelm.model->quant == XML_CQUANT_OPT) {
                return 1;
            }
        }
        if (!stackelm.alreadymatched) {
            seqstartindex = 0;
        }
        else {
            seqstartindex = stackelm.activeChild + 1;
        }
        for (i = seqstartindex; i < stackelm.model->numchildren; i++) {
            if ((&stackelm.model->children[i])->type == XML_CTYPE_NAME) {
                if ((&stackelm.model->children[i])->quant == XML_CQUANT_OPT ||
                    (&stackelm.model->children[i])->quant == XML_CQUANT_REP) {
                    continue;
                } else {
                    return 0;
                }
            } else {
                if (tncdata->contentStackPtr == tncdata->contentStackSize) {
                    tncdata->contentStack = (TNC_ContentStack *)
                        Tcl_Realloc ((char *)tncdata->contentStack,
                                     sizeof (TNC_Content *) * 2 *
                                     tncdata->contentStackSize);
                    tncdata->contentStackSize *= 2;
                }
                (&tncdata->contentStack[tncdata->contentStackPtr])->model
                    = &stackelm.model->children[i];
                tncdata->contentStack[tncdata->contentStackPtr].activeChild
                    = 0;
                tncdata->contentStack[tncdata->contentStackPtr].deep
                    = stackelm.deep + 1;
                tncdata->contentStack[tncdata->contentStackPtr].alreadymatched
                    = 0;
                tncdata->contentStackPtr++;
                if (TncProbeElementEnd (tncdata)) {
                    tncdata->contentStackPtr--;
                    continue;
                }
                else {
                    tncdata->contentStackPtr--;
                    return 0;
                }
            }
        }
        return 1;
    case XML_CTYPE_NAME:
        /* NAME type dosen't occur at top level of a content model and is
           handled in some "shotcut" way directly in the CHOICE and SEQ cases.
           It's only here to pacify gcc -Wall. */
        fprintf (stderr, "error!!! - in TncProbeElementEnd: XML_CTYPE_NAME "
                 "shouldn't be reached in any case.\n");
    default:
        fprintf (stderr, "error!!! - in TncProbeElementEnd: unknown content "
                 "type: %d\n", stackelm.model->type);
        return 1;
    }
}

/*---------------------------------------------------------------------------
|   startElement
|
\--------------------------------------------------------------------------*/
static void
startElement(
    void         *userData,
    const char   *name,
    const char  **atts
)
{
    domReadInfo   *info = userData;
    domNode       *node, *parentNode;
    domLineColumn *lc;
    domAttrNode   *attrnode, *lastAttr;
    const char   **atPtr, **idAttPtr;
    Tcl_HashEntry *h;
    int            hnew, len, pos, idatt, newNS, result;
    const char    *xmlns, *localname;
    char           tagPrefix[MAX_PREFIX_LEN];
    char           prefix[MAX_PREFIX_LEN];
    domNS         *ns;
    char           feedbackCmd[24];

    if (info->feedbackAfter) {

        if (info->nextFeedbackPosition
             <= XML_GetCurrentByteIndex (info->parser)
        ) {
            if (info->feedbackCmd) {
                result = Tcl_GlobalEvalObj(info->interp, info->feedbackCmd);
            } else {
                sprintf(feedbackCmd, "%s", "::dom::domParseFeedback");
                result = Tcl_Eval(info->interp, feedbackCmd);
            }
            if (result != TCL_OK) {
                DBG(fprintf(stderr, "%s\n", 
                            Tcl_GetStringResult (info->interp)););
                info->status = result;
                XML_StopParser(info->parser, 1);
                return;
            }
            info->nextFeedbackPosition = 
                XML_GetCurrentByteIndex (info->parser) + info->feedbackAfter;
            Tcl_ResetResult (info->interp);
        }
    }

    DispatchPCDATA (info);

    if (info->dtdvalidation) {
        domReadInfo *tncdata = info;
        Tcl_HashEntry *entryPtr;
        Tcl_HashTable *elemAtts;
        const char **atPtr;
        TNC_ElemAttInfo *elemAttInfo;
        TNC_Content *model;
        int result, nrOfreq, acceptNoDoctype = 0;

#ifdef TNC_DEBUG
        printf ("TncElementStartCommand name: %s\n", name);
#endif

        /* If the document doesn't have a doctype declaration, but the
           user have used the -useForeignDTD 1 feature, the collected
           data out of the provided DTD isn't postprocessed by 
           TncElementStartCommand. We do this now.
           NOTE: Since there wasn't a doctype declaration, there is no
           information available which element is expected to be the
           document element. Eventually it would be desirable, to set
           this somehow. For now, this means, that every valid subtree
           of the given DTD information is accepted.  */
        if (!tncdata->contentStackPtr && !tncdata->elemContentsRewriten) {
            TncEndDoctypeDeclHandler (userData);
            acceptNoDoctype = 1;
        }

        entryPtr = Tcl_FindHashEntry (tncdata->tagNames, name);
        if (!entryPtr) {
            signalNotValid (userData, TNC_ERROR_UNKNOWN_ELEMENT);
            return;
        }
        model = (TNC_Content *) Tcl_GetHashValue (entryPtr);

        switch (model->type) {
        case XML_CTYPE_MIXED:
        case XML_CTYPE_ANY:
            tncdata->skipWhiteCDATAs = 1;
            tncdata->ignorePCDATA = 1;
            break;
        case XML_CTYPE_EMPTY:
            tncdata->skipWhiteCDATAs = 0;
            break;
        case XML_CTYPE_CHOICE:
        case XML_CTYPE_SEQ:
            tncdata->skipWhiteCDATAs = 1;
            tncdata->ignorePCDATA = 0;
            break;
        case XML_CTYPE_NAME:
            break;
        }

        if (tncdata->contentStackPtr) {
            /* This is the normal case, within some content,
               at least the root element content. */
            while (1) {
                result = TncProbeElement (entryPtr, tncdata);
                if (result == -1) {
                    if (tncdata->contentStack[tncdata->contentStackPtr - 1].deep
                        == 0) {
                        signalNotValid (userData,
                                        TNC_ERROR_ELEMENT_NOT_ALLOWED_HERE);
                        return;
                    }
                    tncdata->contentStackPtr--;
                    continue;
                }
                if (result) {
                    break;
                }
                if (!result) {
                    signalNotValid (userData, TNC_ERROR_ELEMENT_NOT_ALLOWED_HERE);
                    return;
                }
            }
            if (tncdata->contentStackPtr == tncdata->contentStackSize) {
                tncdata->contentStackSize *= 2;
                tncdata->contentStack = (TNC_ContentStack *)
                    Tcl_Realloc ((char *)tncdata->contentStack,
                                 sizeof (TNC_Content *)*tncdata->contentStackSize);
            }
            (&tncdata->contentStack[tncdata->contentStackPtr])->model = model;
            (&tncdata->contentStack[tncdata->contentStackPtr])->activeChild = 0;
            (&tncdata->contentStack[tncdata->contentStackPtr])->deep = 0;
            (&tncdata->contentStack[tncdata->contentStackPtr])->alreadymatched = 0;
            tncdata->contentStackPtr++;
        } else {
            /* This is only in case of the root element */
            if (atts) {
                if (!tncdata->doctypeName) {
                    if (!acceptNoDoctype) {
                        signalNotValid (userData, TNC_ERROR_NO_DOCTYPE_DECL);
                        return;
                    }
                } else {
                    if (strcmp (tncdata->doctypeName, name) != 0) {
                        signalNotValid (userData, TNC_ERROR_WRONG_ROOT_ELEMENT);
                        return;
                    }
                }
            }
            (&(tncdata->contentStack)[0])->model = model;
            (&(tncdata->contentStack)[0])->activeChild = 0;
            (&(tncdata->contentStack)[0])->deep = 0;
            (&(tncdata->contentStack)[0])->alreadymatched = 0;
            tncdata->contentStackPtr++;
        }
    
        if (atts) {
            elemAttInfo = model->attInfo;
            if (!elemAttInfo) {
                if (atts[0] != NULL) {
                    signalNotValid (userData, TNC_ERROR_NO_ATTRIBUTES);
                    return;
                }
            } else {
                elemAtts = elemAttInfo->attributes;
                nrOfreq = 0;
                for (atPtr = atts; atPtr[0]; atPtr += 2) {
                    if (!TncProbeAttribute (userData, elemAtts, (char *) atPtr[0],
                                            (char *) atPtr[1], &nrOfreq))    
                        return;
                }
                if (nrOfreq != elemAttInfo->nrOfreq) {
                    signalNotValid (userData, 
                                    TNC_ERROR_MISSING_REQUIRED_ATTRIBUTE);
                    return;
                }
            }
        } else {
            tncdata->elemAttInfo = model->attInfo;
        }
#ifdef TNC_DEBUG
        printf ("TncElementStartCommand end\n");
#endif
    }
    
    h = Tcl_CreateHashEntry(&HASHTAB(info->document,tdom_tagNames), name,
                            &hnew);
    if (info->storeLineColumn) {
        node = (domNode*) domAlloc(sizeof(domNode)
                                    + sizeof(domLineColumn));
    } else {
        node = (domNode*) domAlloc(sizeof(domNode));
    }
    memset(node, 0, sizeof(domNode));
    node->nodeType      = ELEMENT_NODE;
    node->nodeFlags     = 0;
    node->namespace     = 0;
    node->nodeName      = (char *)&(h->key);
    node->nodeNumber    = NODE_NO(info->document);
    node->ownerDocument = info->document;

    if (info->baseURIstack[info->baseURIstackPos].baseURI 
        != XML_GetBase (info->parser)) {
        h = Tcl_CreateHashEntry (info->document->baseURIs,
                                 (char*) node,
                                 &hnew);
        Tcl_SetHashValue (h, tdomstrdup (XML_GetBase (info->parser)));
        node->nodeFlags |= HAS_BASEURI;
        info->baseURIstackPos++;
        if (info->baseURIstackPos >= info->baseURIstackSize) {
            info->baseURIstack = (domActiveBaseURI*) REALLOC(
                (char*)info->baseURIstack,
                sizeof(domActiveBaseURI) * 2 * info->baseURIstackSize);
            info->baseURIstackSize = 2 * info->baseURIstackSize;
        }
        info->baseURIstack[info->baseURIstackPos].baseURI
            = XML_GetBase (info->parser);
        info->baseURIstack[info->baseURIstackPos].depth 
            = info->depth;
    }

    if (info->depth == 0) {
        if (info->document->rootNode->lastChild) {
            info->document->rootNode->lastChild->nextSibling = node;
            node->previousSibling = info->document->rootNode->lastChild;
        } else {
            info->document->rootNode->firstChild = node;
        }
        info->document->rootNode->lastChild = node;
    } else {
        parentNode = info->currentNode;
        node->parentNode = parentNode;
        if (parentNode->firstChild)  {
            parentNode->lastChild->nextSibling = node;
            node->previousSibling = parentNode->lastChild;
            parentNode->lastChild = node;
        } else {
            parentNode->firstChild = parentNode->lastChild = node;
        }
    }
    info->currentNode = node;
    if (info->storeLineColumn) {
        lc = (domLineColumn*) ( ((char*)node) + sizeof(domNode));
        node->nodeFlags |= HAS_LINE_COLUMN;
        lc->line         = XML_GetCurrentLineNumber(info->parser);
        lc->column       = XML_GetCurrentColumnNumber(info->parser);
    }


    lastAttr = NULL;
    /*--------------------------------------------------------------
    |   process namespace declarations
    |
    \-------------------------------------------------------------*/
    if (!info->ignorexmlns) {
        for (atPtr = atts; atPtr[0] && atPtr[1]; atPtr += 2) {

            if (strncmp(atPtr[0], "xmlns", 5) == 0) {
                xmlns = atPtr[0];
                newNS = 1;
                if (xmlns[5] == ':') {
                    if (atPtr[1][0] == '\0') {
                        Tcl_SetResult (info->interp, "Missing URI in Namespace "
                               "declaration", NULL);
                        XML_StopParser(info->parser, 0);
                        return;
                    }
                    if (domIsNamespaceInScope (info->activeNS, info->activeNSpos,
                                               &(xmlns[6]), atPtr[1])) {
                        ns = domLookupPrefix (info->currentNode, &(xmlns[6]));
                        newNS = 0;
                    }
                    else {
                        ns = domNewNamespace(info->document, &xmlns[6], atPtr[1]);
                    }
                } else {
                    ns = domNewNamespace(info->document, "", atPtr[1]);
                }
                if (newNS) {
                    /* push active namespace */
                    info->activeNSpos++;
                    if (info->activeNSpos >= info->activeNSsize) {
                        info->activeNS = (domActiveNS*) REALLOC(
                            (char*)info->activeNS,
                            sizeof(domActiveNS) * 2 * info->activeNSsize);
                        info->activeNSsize = 2 * info->activeNSsize;
                    }
                    info->activeNS[info->activeNSpos].depth     = info->depth;
                    info->activeNS[info->activeNSpos].namespace = ns;
                }

                h = Tcl_CreateHashEntry(&HASHTAB(info->document, tdom_attrNames),
                                        atPtr[0], &hnew);
                attrnode = (domAttrNode*) domAlloc(sizeof(domAttrNode));
                memset(attrnode, 0, sizeof(domAttrNode));
                attrnode->nodeType    = ATTRIBUTE_NODE;
                attrnode->nodeFlags   = IS_NS_NODE;
                attrnode->namespace   = ns->index;
                attrnode->nodeName    = (char *)&(h->key);
                attrnode->parentNode  = node;
                len = strlen(atPtr[1]);
                attrnode->valueLength = len;
                attrnode->nodeValue   = (char*)MALLOC(len+1);
                strcpy(attrnode->nodeValue, atPtr[1]);
                if (node->firstAttr) {
                    lastAttr->nextSibling = attrnode;
                } else {
                    node->firstAttr = attrnode;
                }
                lastAttr = attrnode;
            }

        }

        /*----------------------------------------------------------
          |   look for namespace of element
          \---------------------------------------------------------*/
        domSplitQName (name, tagPrefix, &localname);
        for (pos = info->activeNSpos; pos >= 0; pos--) {
            if (  ((tagPrefix[0] == '\0')
                   && (info->activeNS[pos].namespace->prefix[0] == '\0'))
                  || ((tagPrefix[0] != '\0') 
                      && (info->activeNS[pos].namespace->prefix[0] != '\0')
                      && (strcmp(tagPrefix, 
                                 info->activeNS[pos].namespace->prefix) == 0))
                ) {
                if (info->activeNS[pos].namespace->prefix[0] == '\0'
                    && info->activeNS[pos].namespace->uri[0] == '\0'
                    && tagPrefix[0] == '\0') {
                    /* xml-names rec. 5.2: "The default namespace can be
                       set to the empty string. This has the same effect,
                       within the scope of the declaration, of there being
                       no default namespace." */
                    goto elemNSfound;
                }
                node->namespace = info->activeNS[pos].namespace->index;
                DBG(fprintf(stderr, "tag='%s' uri='%s' \n",
                            node->nodeName,
                            info->activeNS[pos].namespace->uri);
                    )
                    goto elemNSfound;
            }
        }
        if (tagPrefix[0] != '\0') {
            if (strcmp (tagPrefix, "xml")==0) {
                node->namespace = info->document->rootNode->firstAttr->namespace;
            } else {
                /* Since where here, this means, the element has a
                   up to now not declared namespace prefix. */
                Tcl_SetResult (info->interp, "Namespace prefix is not "
                               "defined", NULL);
                XML_StopParser(info->parser, 0);
                return;
            }
        }
    }
elemNSfound:

    /*--------------------------------------------------------------
    |   add the attribute nodes
    |
    \-------------------------------------------------------------*/
    if ((idatt = XML_GetIdAttributeIndex (info->parser)) != -1) {
        if (!info->document->ids) {
            info->document->ids = MALLOC (sizeof (Tcl_HashTable));
            Tcl_InitHashTable (info->document->ids, TCL_STRING_KEYS);
        }
        h = Tcl_CreateHashEntry (info->document->ids,
                                 atts[idatt+1],
                                 &hnew);
        /* if hnew isn't 1 this is a validation error. Hm, no clear way
           to report this. And more, XSLT and XPath can process not
           valid XML, the spec mentioned this even within the context
           of id(). If some elements share the same ID, the first in
           document order should be used. Doing it this way, this is
           guaranteed for unchanged DOM trees. There are problems, if
           the DOM tree is changed, befor using id() */
        if (hnew) {
            Tcl_SetHashValue (h, node);
        }
        idAttPtr = atts + idatt;
    } else {
        idAttPtr = NULL;
    }
    /* lastAttr already set right, either to NULL above, or to the last
       NS attribute */
    for (atPtr = atts; atPtr[0] && atPtr[1]; atPtr += 2) {
        if (!info->ignorexmlns) {
            if (strncmp(atPtr[0], "xmlns", 5) == 0) {
                continue;
            }
        }
        h = Tcl_CreateHashEntry(&HASHTAB(info->document, tdom_attrNames),
                                atPtr[0], &hnew);
        attrnode = (domAttrNode*) domAlloc(sizeof(domAttrNode));
        memset(attrnode, 0, sizeof(domAttrNode));
        attrnode->nodeType = ATTRIBUTE_NODE;
        if (atPtr == idAttPtr) {
            attrnode->nodeFlags |= IS_ID_ATTRIBUTE;
        } else {
            attrnode->nodeFlags = 0;
        }
        attrnode->namespace   = 0;
        attrnode->nodeName    = (char *)&(h->key);
        attrnode->parentNode  = node;
        len = strlen(atPtr[1]);
        attrnode->valueLength = len;
        attrnode->nodeValue   = (char*)MALLOC(len+1);
        strcpy(attrnode->nodeValue, (char *)atPtr[1]);

        if (node->firstAttr) {
            lastAttr->nextSibling = attrnode;
        } else {
            node->firstAttr = attrnode;
        }
        lastAttr = attrnode;

        if (!info->ignorexmlns) {
            /*----------------------------------------------------------
              |   look for attribute namespace
              \---------------------------------------------------------*/
            domSplitQName (attrnode->nodeName, prefix, &localname);
            if (prefix[0] != '\0') {
                for (pos = info->activeNSpos; pos >= 0; pos--) {
                    if (  ((prefix[0] == '\0') 
                           && (info->activeNS[pos].namespace->prefix[0] == '\0'))
                          || ((prefix[0] != '\0') 
                              && (info->activeNS[pos].namespace->prefix[0] != '\0')
                              && (strcmp(prefix, info->activeNS[pos].namespace->prefix) == 0))
                        ) {
                        attrnode->namespace = info->activeNS[pos].namespace->index;
                        DBG(fprintf(stderr, "attr='%s' uri='%s' \n",
                                    attrnode->nodeName,
                                    info->activeNS[pos].namespace->uri);
                            )
                            goto attrNSfound;
                    }
                }
                if (strcmp (prefix, "xml")==0) {
                    attrnode->namespace = 
                        info->document->rootNode->firstAttr->namespace;
                } else {
                    /* Since where here, this means, the attribute has a
                       up to now not declared namespace prefix. We probably
                       should return this as an error, shouldn't we?*/
                }
            attrNSfound:
                ;
            }
        }
    }

    info->depth++;
}

/*---------------------------------------------------------------------------
|   endElement
|
\--------------------------------------------------------------------------*/
static void
endElement (
    void        *userData,
    const char  *name
)
{
    domReadInfo  *info = userData;

    DispatchPCDATA (info);

    if (info->dtdvalidation) {
        domReadInfo *tncdata = info;
        Tcl_HashEntry *entryPtr;
        Tcl_HashSearch search;

#ifdef TNC_DEBUG
        printf ("TncElementEndCommand start\n");
        printContentStack (tncdata);
#endif
        while (1) {
            if (!TncProbeElementEnd (tncdata)) {
                signalNotValid (userData, TNC_ERROR_ELEMENT_CAN_NOT_END_HERE);
                return;
            }
            if (tncdata->contentStack[tncdata->contentStackPtr - 1].deep == 0) {
                break;
            }
            tncdata->contentStackPtr--;
        }
        /* Remove the content model of the closed element from the stack */
        tncdata->contentStackPtr--;
#ifdef TNC_DEBUG
        printf ("after removing ended element from the stack\n");
        printContentStack (tncdata);
#endif
        if (tncdata->contentStackPtr) {
            switch ((&tncdata->contentStack[tncdata->contentStackPtr - 1])->model->type) {
            case XML_CTYPE_MIXED:
            case XML_CTYPE_ANY:
                tncdata->skipWhiteCDATAs = 1;
                tncdata->ignorePCDATA = 1;
                break;
            case XML_CTYPE_EMPTY:
                tncdata->skipWhiteCDATAs = 0;
                break;
            case XML_CTYPE_CHOICE:
            case XML_CTYPE_SEQ:
            case XML_CTYPE_NAME:
                tncdata->skipWhiteCDATAs = 1;
                tncdata->ignorePCDATA = 0;
                break;
            }
        } else {
            /* This means, the root element is closed,
               therefor the place to check, if every IDREF points
               to a ID. */
            if (tncdata->idCheck) {
                for (entryPtr = Tcl_FirstHashEntry (tncdata->ids, &search);
                     entryPtr != NULL;
                     entryPtr = Tcl_NextHashEntry (&search)) {
#ifdef TNC_DEBUG
                    printf ("check id value %s\n",
                            Tcl_GetHashKey (tncdata->ids, entryPtr));
                    printf ("value %p\n", Tcl_GetHashValue (entryPtr));
#endif
                    if (!Tcl_GetHashValue (entryPtr)) {
                        signalNotValid (userData, TNC_ERROR_UNKNOWN_ID_REFERRED);
                        return;
                    }
                }
            }
        }
    }
    
    
    info->depth--;
    if (!info->ignorexmlns) {
        /* pop active namespaces */
        while ( (info->activeNSpos >= 0) &&
                (info->activeNS[info->activeNSpos].depth == info->depth) )
        {
            info->activeNSpos--;
        }
    }

    if (info->depth != -1) {
        info->currentNode = info->currentNode->parentNode;
    } else {
        info->currentNode = NULL;
    }

    if (info->depth) {
        if (info->baseURIstack[info->baseURIstackPos].depth == info->depth) {
            info->baseURIstackPos--;
        }
    }
}

/*---------------------------------------------------------------------------
|   characterDataHandler
|
\--------------------------------------------------------------------------*/
static void
characterDataHandler (
    void        *userData,
    const char  *data,
    int          len
)
{
    domReadInfo   *info = userData;

    Tcl_DStringAppend (info->cdata, data, len);
    if (info->dtdvalidation) {
        domReadInfo *tncdata = info;
        int i;
        char *pc;

        if (!tncdata->skipWhiteCDATAs && len > 0) {
            signalNotValid (userData, TNC_ERROR_EMPTY_ELEMENT);
            return;
        }
        if (!tncdata->ignorePCDATA) {
            for (i = 0, pc = (char*)data; i < len; i++, pc++) {
                if ( (*pc == ' ')  ||
                     (*pc == '\n') ||
                     (*pc == '\r') ||
                     (*pc == '\t') ) {
                    continue;
                }
                signalNotValid (userData, TNC_ERROR_DISALLOWED_PCDATA);
                return;
            }
        }
    }
    
    return;
    
}

/*---------------------------------------------------------------------------
|   startCDATA
|
\--------------------------------------------------------------------------*/
static void
startCDATA (
    void        *userData
    )
{
    domReadInfo   *info = userData;

    DispatchPCDATA (info);
    info->cdataSection = 1;
}

/*---------------------------------------------------------------------------
|   endCDATA
|
\--------------------------------------------------------------------------*/
static void
endCDATA (
    void        *userData
    )
{
    domReadInfo   *info = userData;
    
    DispatchPCDATA (info);
    info->cdataSection = 0;
}

/*---------------------------------------------------------------------------
|   DispatchPCDATA
|
\--------------------------------------------------------------------------*/
static void
DispatchPCDATA (
    domReadInfo *info
    )
{
    domTextNode   *node;
    domNode       *parentNode;
    domLineColumn *lc;
    Tcl_HashEntry *h;
    char          *s;
    int            len, hnew;
    
    len = Tcl_DStringLength (info->cdata);
    if (!len && !info->cdataSection) return;
    s = Tcl_DStringValue (info->cdata);
    
    parentNode = info->currentNode;
    if (!parentNode) return;

    if (   parentNode->lastChild 
        && parentNode->lastChild->nodeType == TEXT_NODE
        && !info->cdataSection) {

        /* normalize text node, i.e. there are no adjacent text nodes */
        node = (domTextNode*)parentNode->lastChild;
        node->nodeValue = REALLOC(node->nodeValue, node->valueLength + len);
        memmove(node->nodeValue + node->valueLength, s, len);
        node->valueLength += len;

    } else {

        if (info->ignoreWhiteSpaces) {
            char *pc;
            int   i, only_whites;

            only_whites = 1;
            for (i=0, pc = s; i < len; i++, pc++) {
                if ( (*pc != ' ')  &&
                     (*pc != '\t') &&
                     (*pc != '\n') &&
                     (*pc != '\r') ) {
                    only_whites = 0;
                    break;
                }
            }
            if (only_whites) {
                Tcl_DStringSetLength (info->cdata, 0);
                return;
            }
        }

        if (info->storeLineColumn) {
            node = (domTextNode*) domAlloc(sizeof(domTextNode)
                                            + sizeof(domLineColumn));
        } else {
            node = (domTextNode*) domAlloc(sizeof(domTextNode));
        }
        memset(node, 0, sizeof(domTextNode));
        if (info->cdataSection)
            node->nodeType    = CDATA_SECTION_NODE;
        else 
            node->nodeType    = TEXT_NODE;
        node->nodeFlags   = 0;
        node->nodeNumber  = NODE_NO(info->document);
        node->valueLength = len;
        node->nodeValue   = (char*)MALLOC(len);
        memmove(node->nodeValue, s, len);

        node->ownerDocument = info->document;
        node->parentNode = parentNode;
        if (parentNode->nodeType == ELEMENT_NODE) {
            if (parentNode->firstChild)  {
                parentNode->lastChild->nextSibling = (domNode*)node;
                node->previousSibling = parentNode->lastChild;
            } else {
                parentNode->firstChild = (domNode*)node;
            }
            parentNode->lastChild = (domNode*)node;
        }

        if (info->baseURIstack[info->baseURIstackPos].baseURI 
            != XML_GetBase (info->parser)) {
            h = Tcl_CreateHashEntry (info->document->baseURIs,
                                     (char*) node,
                                     &hnew);
            Tcl_SetHashValue (h, tdomstrdup (XML_GetBase (info->parser)));
            node->nodeFlags |= HAS_BASEURI;
        }

        if (info->storeLineColumn) {
            lc = (domLineColumn*) ( ((char*)node) + sizeof(domTextNode) );
            node->nodeFlags |= HAS_LINE_COLUMN;
            lc->line         = XML_GetCurrentLineNumber(info->parser);
            lc->column       = XML_GetCurrentColumnNumber(info->parser);
        }
    }
    Tcl_DStringSetLength (info->cdata, 0);
}


/*---------------------------------------------------------------------------
|   commentHandler
|
\--------------------------------------------------------------------------*/
static void
commentHandler (
    void        *userData,
    const char  *s
)
{
    domReadInfo   *info = userData;
    domTextNode   *node;
    domNode       *parentNode;
    domLineColumn *lc;
    int            len, hnew;
    Tcl_HashEntry *h;

    if (info->insideDTD) {
        DBG(fprintf (stderr, "commentHandler: insideDTD, skipping\n");)
        return;
    }

    DispatchPCDATA (info);

    len = strlen(s);
    parentNode = info->currentNode;

    if (info->storeLineColumn) {
        node = (domTextNode*) domAlloc(sizeof(domTextNode)
                                        + sizeof(domLineColumn));
    } else {
        node = (domTextNode*) domAlloc(sizeof(domTextNode));
    }
    memset(node, 0, sizeof(domTextNode));
    node->nodeType    = COMMENT_NODE;
    node->nodeFlags   = 0;
    node->nodeNumber  = NODE_NO(info->document);
    node->valueLength = len;
    node->nodeValue   = (char*)MALLOC(len);
    memmove(node->nodeValue, s, len);

    node->ownerDocument = info->document;
    node->parentNode = parentNode;
    if (parentNode == NULL) {
        if (info->document->rootNode->lastChild) {
            info->document->rootNode->lastChild->nextSibling = (domNode*)node;
            node->previousSibling = info->document->rootNode->lastChild;
        } else {
            info->document->rootNode->firstChild = (domNode*)node;
        }
        info->document->rootNode->lastChild = (domNode*)node;
    } else if(parentNode->nodeType == ELEMENT_NODE) {
        if (parentNode->firstChild)  {
            parentNode->lastChild->nextSibling = (domNode*)node;
            node->previousSibling = parentNode->lastChild;
            parentNode->lastChild = (domNode*)node;
        } else {
            parentNode->firstChild = parentNode->lastChild = (domNode*)node;
        }
    }

    if (info->baseURIstack[info->baseURIstackPos].baseURI 
        != XML_GetBase (info->parser)) {
        h = Tcl_CreateHashEntry (info->document->baseURIs,
                                 (char*) node,
                                 &hnew);
        Tcl_SetHashValue (h, tdomstrdup (XML_GetBase (info->parser)));
        node->nodeFlags |= HAS_BASEURI;
    }

    if (info->storeLineColumn) {
        lc = (domLineColumn*) ( ((char*)node) + sizeof(domTextNode) );
        node->nodeFlags |= HAS_LINE_COLUMN;
        lc->line         = XML_GetCurrentLineNumber(info->parser);
        lc->column       = XML_GetCurrentColumnNumber(info->parser);
    }
}


/*---------------------------------------------------------------------------
|   processingInstructionHandler
|
\--------------------------------------------------------------------------*/
static void
processingInstructionHandler(
    void       *userData,
    const char *target,
    const char *data
)
{
    domProcessingInstructionNode *node;
    domReadInfo                  *info = userData;
    domNode                      *parentNode;
    domLineColumn                *lc;
    int                           len,hnew;
    Tcl_HashEntry                *h;

    if (info->insideDTD) {
        DBG(fprintf (stderr, 
                     "processingInstructionHandler: insideDTD, skipping\n");)
        return;
    }

    DispatchPCDATA (info);
    
    parentNode = info->currentNode;

    if (info->storeLineColumn) {
        node = (domProcessingInstructionNode*)
               domAlloc(sizeof(domProcessingInstructionNode)
                         + sizeof(domLineColumn));
    } else {
        node = (domProcessingInstructionNode*)
               domAlloc(sizeof(domProcessingInstructionNode));
    }
    memset(node, 0, sizeof(domProcessingInstructionNode));
    node->nodeType    = PROCESSING_INSTRUCTION_NODE;
    node->nodeFlags   = 0;
    node->namespace   = 0;
    node->nodeNumber  = NODE_NO(info->document);

    if (info->baseURIstack[info->baseURIstackPos].baseURI 
        != XML_GetBase (info->parser)) {
        h = Tcl_CreateHashEntry (info->document->baseURIs,
                                 (char*) node,
                                 &hnew);
        Tcl_SetHashValue (h, tdomstrdup (XML_GetBase (info->parser)));
        node->nodeFlags |= HAS_BASEURI;
    }

    len = strlen(target);
    node->targetLength = len;
    node->targetValue  = (char*)MALLOC(len);
    memmove(node->targetValue, target, len);

    len = strlen(data);
    node->dataLength = len;
    node->dataValue  = (char*)MALLOC(len);
    memmove(node->dataValue, data, len);

    node->ownerDocument = info->document;
    node->parentNode = parentNode;
    if (parentNode == NULL) {
        if (info->document->rootNode->lastChild) {
            info->document->rootNode->lastChild->nextSibling = (domNode*)node;
            node->previousSibling = info->document->rootNode->lastChild;
        } else {
            info->document->rootNode->firstChild = (domNode*)node;
        }
        info->document->rootNode->lastChild = (domNode*)node;
    } else if(parentNode->nodeType == ELEMENT_NODE) {
        if (parentNode->firstChild)  {
            parentNode->lastChild->nextSibling = (domNode*)node;
            node->previousSibling = parentNode->lastChild;
            parentNode->lastChild = (domNode*)node;
        } else {
            parentNode->firstChild = parentNode->lastChild = (domNode*)node;
        }
    }
    if (info->storeLineColumn) {
        lc = (domLineColumn*)(((char*)node)+sizeof(domProcessingInstructionNode));
        node->nodeFlags |= HAS_LINE_COLUMN;
        lc->line         = XML_GetCurrentLineNumber(info->parser);
        lc->column       = XML_GetCurrentColumnNumber(info->parser);
    }
}

/*---------------------------------------------------------------------------
|  entityDeclHandler
|
\--------------------------------------------------------------------------*/
static void
entityDeclHandler (
    void       *userData,
    const char *entityName,
    int         is_parameter_entity,
    const char *value,
    int         value_length,
    const char *base,
    const char *systemId,
    const char *publicId,
    const char *notationName
)
{
    domReadInfo                  *info = (domReadInfo *) userData;
    Tcl_HashEntry                *entryPtr;
    int                           hnew;

    if (notationName) {
        if (!info->document->unparsedEntities) {
            info->document->unparsedEntities = MALLOC (sizeof (Tcl_HashTable));
            Tcl_InitHashTable (info->document->unparsedEntities, 
                               TCL_STRING_KEYS);
        }
        entryPtr = Tcl_CreateHashEntry (info->document->unparsedEntities,
                                        entityName, &hnew);
        if (hnew) {
            Tcl_SetHashValue (entryPtr, tdomstrdup (systemId));
        }
    }
}

/*---------------------------------------------------------------------------
|  externalEntityRefHandler
|
\--------------------------------------------------------------------------*/
static int
externalEntityRefHandler (
    XML_Parser  parser,
    const char *openEntityNames,
    const char *base,
    const char *systemId,
    const char *publicId
)
{
    domReadInfo   *info = (domReadInfo *) XML_GetUserData (parser);

    Tcl_Obj *cmdPtr, *resultObj, *resultTypeObj, *extbaseObj, *xmlstringObj;
    Tcl_Obj *channelIdObj;
    int result, mode, done, byteIndex, i;
    int keepresult = 0;
    size_t len;
    int tclLen;
    XML_Parser extparser, oldparser = NULL;
    char buf[4096], *resultType, *extbase, *xmlstring, *channelId, s[50];
    Tcl_Channel chan = (Tcl_Channel) NULL;
    enum XML_Status status;
    XML_Index storedNextFeedbackPosition;
    const char *interpResult;

    if (info->document->extResolver == NULL) {
        Tcl_AppendResult (info->interp, "Can't read external entity \"",
                          systemId, "\": No -externalentitycommand given",
                          NULL);
        return 0;
    }

    DispatchPCDATA (info);

    /*
     * Take a copy of the callback script so that arguments may be appended.
     */
    cmdPtr = Tcl_NewStringObj(info->document->extResolver, -1);
    Tcl_IncrRefCount(cmdPtr);

    if (base) {
        Tcl_ListObjAppendElement(info->interp, cmdPtr,
                                 Tcl_NewStringObj(base, strlen(base)));
    } else {
        Tcl_ListObjAppendElement(info->interp, cmdPtr,
                                 Tcl_NewObj());
    }

    /* For a document with doctype declaration, the systemId is always
       != NULL. But if the document doesn't have a doctype declaration
       and the user uses -useForeignDTD 1, the externalEntityRefHandler
       will be called with a systemId (and publicId and openEntityNames)
       == NULL. */
    if (systemId) {
        Tcl_ListObjAppendElement(info->interp, cmdPtr,
                                 Tcl_NewStringObj(systemId, strlen(systemId)));
    } else {
        Tcl_ListObjAppendElement(info->interp, cmdPtr,
                                 Tcl_NewObj());
    }

    if (publicId) {
        Tcl_ListObjAppendElement(info->interp, cmdPtr,
                                 Tcl_NewStringObj(publicId, strlen(publicId)));
    } else {
        Tcl_ListObjAppendElement(info->interp, cmdPtr,
                                 Tcl_NewObj());
    }

 
    result = Tcl_EvalObjEx (info->interp, cmdPtr, 
                            TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL);

    Tcl_DecrRefCount(cmdPtr);

    if (result != TCL_OK) {
        info->status = result;
        return 0;
    }

    extparser = XML_ExternalEntityParserCreate (parser, openEntityNames, 0);

    resultObj = Tcl_GetObjResult (info->interp);
    Tcl_IncrRefCount (resultObj);

    result = Tcl_ListObjLength (info->interp, resultObj, &tclLen);
    if ((result != TCL_OK) || (tclLen != 3)) {
        goto wrongScriptResult;
    }
    result = Tcl_ListObjIndex (info->interp, resultObj, 0, &resultTypeObj);
    if (result != TCL_OK) {
        goto wrongScriptResult;
    }
    resultType = Tcl_GetString(resultTypeObj);

    if (strcmp (resultType, "string") == 0) {
        result = Tcl_ListObjIndex (info->interp, resultObj, 2, &xmlstringObj);
        xmlstring = Tcl_GetString(xmlstringObj);
        len = strlen (xmlstring);
        chan = NULL;
    } else if (strcmp (resultType, "channel") == 0) {
        xmlstring = NULL;
        len = 0;
        result = Tcl_ListObjIndex (info->interp, resultObj, 2, &channelIdObj);
        channelId = Tcl_GetString(channelIdObj);
        chan = Tcl_GetChannel (info->interp, channelId, &mode);
        if (chan == (Tcl_Channel) NULL) {
            goto wrongScriptResult;
        }
        if ((mode & TCL_READABLE) == 0) {
            return 0;
        }
    } else if (strcmp (resultType, "filename") == 0) {
        /* result type "filename" not yet implemented */
        return 0;
    } else {
        goto wrongScriptResult;
    }

    result = Tcl_ListObjIndex (info->interp, resultObj, 1, &extbaseObj);
    if (result != TCL_OK) {
        goto wrongScriptResult;
    }
    extbase = Tcl_GetString(extbaseObj);

    /* TODO: what to do, if this document was already parsed before ? */

    if (!extparser) {
        Tcl_DecrRefCount (resultObj);
        Tcl_SetResult (info->interp,
                       "unable to create expat external entity parser",
                       NULL);
        return 0;
    }

    oldparser = info->parser;
    info->parser = extparser;
    XML_SetBase (extparser, extbase);
    storedNextFeedbackPosition = info->nextFeedbackPosition;
    info->nextFeedbackPosition = info->feedbackAfter;

    Tcl_ResetResult (info->interp);
    result = 1;
    if (chan == NULL) {
        status = XML_Parse(extparser, xmlstring, strlen (xmlstring), 1);
        switch (status) {
        case XML_STATUS_ERROR:
            interpResult = Tcl_GetStringResult(info->interp);
            sprintf(s, "%ld", XML_GetCurrentLineNumber(extparser));
            if (interpResult[0] == '\0') {
                Tcl_ResetResult (info->interp);
                Tcl_AppendResult(info->interp, "error \"",
                                 XML_ErrorString(XML_GetErrorCode(extparser)),
                                 "\" in entity \"", systemId,
                                 "\" at line ", s, " character ", NULL);
                sprintf(s, "%ld", XML_GetCurrentColumnNumber(extparser));
                Tcl_AppendResult(info->interp, s, NULL);
                byteIndex = XML_GetCurrentByteIndex(extparser);
                if (byteIndex != -1) {
                    Tcl_AppendResult(info->interp, "\n\"", NULL);
                    s[1] = '\0';
                    for (i=-20; i < 40; i++) {
                        if ((byteIndex+i)>=0) {
                            if (xmlstring[byteIndex+i]) {
                                s[0] = xmlstring[byteIndex+i];
                                Tcl_AppendResult(info->interp, s, NULL);
                                if (i==0) {
                                    Tcl_AppendResult(info->interp,
                                                     " <--Error-- ", NULL);
                                }
                            } else {
                                break;
                            }
                        }
                    }
                    Tcl_AppendResult(info->interp, "\"",NULL);
                }
            } else {
                Tcl_AppendResult(info->interp, ", referenced in entity \"",
                                 systemId, 
                                 "\" at line ", s, " character ", NULL);
                sprintf(s, "%ld", XML_GetCurrentColumnNumber(extparser));
                Tcl_AppendResult(info->interp, s, NULL);
            }
            keepresult = 1;
            result = 0;
            break;
        case XML_STATUS_SUSPENDED:
            XML_StopParser (oldparser, 1);
            keepresult = 1;
            break;
        default:
            break;
        }
    } else {
        do {
            len = Tcl_Read (chan, buf, sizeof(buf));
            done = len < sizeof(buf);
            status = XML_Parse (extparser, buf, len, done);
            switch (status) {
            case XML_STATUS_ERROR:
                interpResult = Tcl_GetStringResult(info->interp);
                sprintf(s, "%ld", XML_GetCurrentLineNumber(extparser));
                if (interpResult[0] == '\0') {
                    Tcl_ResetResult (info->interp);
                    Tcl_AppendResult(info->interp, "error \"",
                                     XML_ErrorString(XML_GetErrorCode(extparser)),
                                     "\" in entity \"", systemId,
                                     "\" at line ", s, " character ", NULL);
                    sprintf(s, "%ld", XML_GetCurrentColumnNumber(extparser));
                    Tcl_AppendResult(info->interp, s, NULL);
                } else {
                    Tcl_AppendResult(info->interp, ", referenced in entity \"",
                                     systemId, 
                                     "\" at line ", s, " character ", NULL);
                    sprintf(s, "%ld", XML_GetCurrentColumnNumber(extparser));
                    Tcl_AppendResult(info->interp, s, NULL);
                }
                result = 0;
                keepresult = 1;
                done = 1;
                break;
            case XML_STATUS_SUSPENDED:
                XML_StopParser (oldparser, 1);
                keepresult = 1;
                done = 1;
                break;
            default:
                break;
            }
        } while (!done);
    }

    if (result) {
        DispatchPCDATA (info);
    }
    if (!keepresult) {
        Tcl_ResetResult (info->interp);
    }
    XML_ParserFree (extparser);
    info->parser = oldparser;
    info->nextFeedbackPosition = storedNextFeedbackPosition;
    Tcl_DecrRefCount (resultObj);
    return result;

 wrongScriptResult:
    Tcl_DecrRefCount (resultObj);
    Tcl_ResetResult (info->interp);
    XML_ParserFree (extparser);
    if (oldparser) {
        info->parser = oldparser;
    }
    info->status = TCL_ERROR;
    Tcl_AppendResult (info->interp, "The -externalentitycommand script "
                      "has to return a Tcl list with 3 elements.\n"
                      "Syntax: {string|channel|filename <baseurl> <data>}\n",
                      NULL);
    return 0;
}

/*---------------------------------------------------------------------------
|   startDoctypeDeclHandler
|
\--------------------------------------------------------------------------*/
static void
startDoctypeDeclHandler (
    void       *userData,
    const char *doctypeName,
    const char *sysid,
    const char *pubid,
    int         has_internal_subset
)
{
    domReadInfo                  *info = (domReadInfo *) userData;

    if (pubid) {
        info->document->doctype = (domDocInfo*)MALLOC (sizeof (domDocInfo));
        memset (info->document->doctype, 0, sizeof (domDocInfo));
        info->document->doctype->systemId = tdomstrdup (sysid);
        info->document->doctype->publicId = tdomstrdup (pubid);
    } else if (sysid) {
        info->document->doctype = (domDocInfo*)MALLOC (sizeof (domDocInfo));
        memset (info->document->doctype, 0, sizeof (domDocInfo));
        info->document->doctype->systemId = tdomstrdup (sysid);
    }
    info->insideDTD = 1;
}

/*---------------------------------------------------------------------------
|   endDoctypeDeclHandler
|
\--------------------------------------------------------------------------*/
static void
endDoctypeDeclHandler (
    void *userData
)
{
    domReadInfo *info = (domReadInfo *) userData;

    info->insideDTD = 0;
}

/*
 *----------------------------------------------------------------------------
 *
 * TncElementDeclCommand --
 *
 *	This procedure is called for every element declaration.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stores the tag name of the element in a lookup table.
 *
 *----------------------------------------------------------------------------
 */

static void
TncElementDeclCommand (userData, name, model)
    void *userData;
    const char *name;
    XML_Content *model;
{
    domReadInfo *tncdata = (domReadInfo *) userData;
    Tcl_HashEntry *entryPtr;
    int newPtr;
    unsigned int i, j;

    entryPtr = Tcl_CreateHashEntry (tncdata->tagNames, name, &newPtr);
    /* "No element type may be declared more than once." (rec. 3.2) */
    if (!newPtr) {
        XML_FreeContentModel (tncdata->parser, model);
        signalNotValid (userData, TNC_ERROR_DUPLICATE_ELEMENT_DECL);
        return;
    }
    /* "The same name must not appear more than once in a
        single mixed-content declaration." (rec. 3.2.2)
        NOTE: OK, OK, doing it this way may not be optimal or even fast
        in some cases. Please step in with a more fancy solution, if you
        feel the need. */
    if (model->type == XML_CTYPE_MIXED && model->quant == XML_CQUANT_REP) {
        for (i = 0; i < model->numchildren; i++) {
            for (j = i + 1; j < model->numchildren; j++) {
                if (strcmp ((&model->children[i])->name,
                            (&model->children[j])->name) == 0) {
                    XML_FreeContentModel (tncdata->parser, model);
                    signalNotValid (userData,
                                    TNC_ERROR_DUPLICATE_MIXED_ELEMENT);
                    return;
                }
            }
        }
    }
    Tcl_SetHashValue (entryPtr, model);
    return;
}

/*
 *----------------------------------------------------------------------------
 *
 * TncAttDeclCommand --
 *
 *	This procedure is called for *each* attribute in an XML
 *      ATTLIST declaration. It stores the attribute definition in
 *      an element specific hash table.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stores the tag name of the element in a lookup table.
 *
 *----------------------------------------------------------------------------
 */

static void
TncAttDeclCommand (userData, elname, attname, att_type, dflt, isrequired)
    void       *userData;
    const char *elname;
    const char *attname;
    const char *att_type;
    const char *dflt;
    int         isrequired;
{
    domReadInfo *tncdata = (domReadInfo *) userData;
    Tcl_HashEntry *entryPtr, *entryPtr1;
    Tcl_HashTable *elemAtts;
    TNC_ElemAttInfo *elemAttInfo;
    TNC_AttDecl *attDecl;
    TNC_EntityInfo *entityInfo;
    int newPtr, start, i, clen;
    char *copy;

    entryPtr = Tcl_CreateHashEntry (tncdata->attDefsTables, elname, &newPtr);
    if (newPtr) {
        elemAttInfo = (TNC_ElemAttInfo *) MALLOC (sizeof (TNC_ElemAttInfo));
        elemAtts = (Tcl_HashTable *) MALLOC (sizeof (Tcl_HashTable));
        Tcl_InitHashTable (elemAtts, TCL_STRING_KEYS);
        elemAttInfo->attributes = elemAtts;
        elemAttInfo->nrOfreq = 0;
        elemAttInfo->nrOfIdAtts = 0;
        Tcl_SetHashValue (entryPtr, elemAttInfo);
    } else {
        elemAttInfo = (TNC_ElemAttInfo *) Tcl_GetHashValue (entryPtr);
        elemAtts = elemAttInfo->attributes;
    }
    entryPtr = Tcl_CreateHashEntry (elemAtts, attname, &newPtr);
    /* Multiple Attribute declarations are allowed, but later declarations
       are ignored. See rec 3.3. */
    if (newPtr) {
        attDecl = (TNC_AttDecl *) MALLOC (sizeof (TNC_AttDecl));
        if (strcmp (att_type, "CDATA") == 0) {
            attDecl->att_type = TNC_ATTTYPE_CDATA;
        }
        else if (strcmp (att_type, "ID") == 0) {
            if (elemAttInfo->nrOfIdAtts) {
                signalNotValid (userData, TNC_ERROR_MORE_THAN_ONE_ID_ATT);
                return;
            }
            elemAttInfo->nrOfIdAtts++;
            if (dflt != NULL) {
                signalNotValid (userData, TNC_ERROR_ID_ATT_DEFAULT);
                return;
            }
            attDecl->att_type = TNC_ATTTYPE_ID;
        }
        else if (strcmp (att_type, "IDREF") == 0) {
            attDecl->att_type = TNC_ATTTYPE_IDREF;
        }
        else if (strcmp (att_type, "IDREFS") == 0) {
            attDecl->att_type = TNC_ATTTYPE_IDREFS;
        }
        else if (strcmp (att_type, "ENTITY") == 0) {
            attDecl->att_type = TNC_ATTTYPE_ENTITY;
        }
        else if (strcmp (att_type, "ENTITIES") == 0) {
            attDecl->att_type = TNC_ATTTYPE_ENTITIES;
        }
        else if (strcmp (att_type, "NMTOKEN") == 0) {
            attDecl->att_type = TNC_ATTTYPE_NMTOKEN;
        }
        else if (strcmp (att_type, "NMTOKENS") == 0) {
            attDecl->att_type = TNC_ATTTYPE_NMTOKENS;
        }
        else if (strncmp (att_type, "NOTATION(", 9) == 0) {
            /* This is a bit puzzling. expat returns something like
               <!NOTATION gif PUBLIC "gif">
               <!ATTLIST c type NOTATION (gif) #IMPLIED>
               as att_type "NOTATION(gif)". */
            attDecl->att_type = TNC_ATTTYPE_NOTATION;
            attDecl->lookupTable =
                (Tcl_HashTable *) MALLOC (sizeof (Tcl_HashTable));
            Tcl_InitHashTable (attDecl->lookupTable, TCL_STRING_KEYS);
            copy = tdomstrdup (att_type);
            start = i = 9;
            while (i) {
                if (copy[i] == ')') {
                    copy[i] = '\0';
#ifdef TNC_DEBUG
                    printf ("att type NOTATION: notation %s allowed\n",
                            &copy[start]);
#endif
                    Tcl_CreateHashEntry (attDecl->lookupTable,
                                                    &copy[start], &newPtr);
                    entryPtr1 = Tcl_CreateHashEntry (tncdata->notationDecls,
                                                    &copy[start], &newPtr);
#ifdef TNC_DEBUG
                    if (newPtr) {
                        printf ("up to now unknown NOTATION\n");
                    } else {
                        printf ("NOTATION already known\n");
                    }
#endif
                    FREE (copy);
                    break;
                }
                if (copy[i] == '|') {
                    copy[i] = '\0';
#ifdef TNC_DEBUG
                    printf ("att type NOTATION: notation %s allowed\n",
                            &copy[start]);
#endif
                    Tcl_CreateHashEntry (attDecl->lookupTable,
                                                    &copy[start], &newPtr);
                    entryPtr1 = Tcl_CreateHashEntry (tncdata->notationDecls,
                                                    &copy[start], &newPtr);
#ifdef TNC_DEBUG
                    if (newPtr) {
                        printf ("up to now unknown NOTATION\n");
                    } else {
                        printf ("NOTATION already known\n");
                    }
#endif
                    start = ++i;
                    continue;
                }
                clen = UTF8_CHAR_LEN (copy[i]);
                CHECK_UTF_CHARLEN_COPY (clen);
                if (!UTF8_GET_NAMING_NMTOKEN (&copy[i], clen)) {
                    signalNotValid (userData, TNC_ERROR_NMTOKEN_REQUIRED);
                    FREE (copy);
                    return;
                }
                i += clen;
            }
        }
        else {
            /* expat returns something like
               <!ATTLIST a type (  numbered
                   |bullets ) #IMPLIED>
               as att_type "(numbered|bullets)", e.g. in some
               "non-official" normalized way.
               Makes things easier for us. */
            attDecl->att_type = TNC_ATTTYPE_ENUMERATION;
            attDecl->lookupTable =
                (Tcl_HashTable *) MALLOC (sizeof (Tcl_HashTable));
            Tcl_InitHashTable (attDecl->lookupTable, TCL_STRING_KEYS);
            copy = tdomstrdup (att_type);
            start = i = 1;
            while (1) {
                if (copy[i] == ')') {
                    copy[i] = '\0';
                    Tcl_CreateHashEntry (attDecl->lookupTable,
                                                    &copy[start], &newPtr);
                    FREE (copy);
                    break;
                }
                if (copy[i] == '|') {
                    copy[i] = '\0';
                    Tcl_CreateHashEntry (attDecl->lookupTable,
                                                    &copy[start], &newPtr);
                    start = ++i;
                    continue;
                }
                clen = UTF8_CHAR_LEN (copy[i]);
                CHECK_UTF_CHARLEN_COPY (clen);
                if (!UTF8_GET_NAMING_NMTOKEN (&copy[i], clen)) {
                    signalNotValid (userData, TNC_ERROR_NMTOKEN_REQUIRED);
                    FREE (copy);
                    return;
                }
                i += clen;
            }
        }
        if (dflt != NULL) {
            switch (attDecl->att_type) {
            case TNC_ATTTYPE_ENTITY:
            case TNC_ATTTYPE_IDREF:
                clen = UTF8_CHAR_LEN (*dflt);
                CHECK_UTF_CHARLEN (clen);
                if (!UTF8_GET_NAME_START (dflt, clen)) {
                    signalNotValid (userData, TNC_ERROR_NAME_REQUIRED);
                    return;
                }
                i = clen;
                while (1) {
                    if (dflt[i] == '\0') {
                        break;
                    }
                    clen = UTF8_CHAR_LEN (dflt[i]);
                    CHECK_UTF_CHARLEN (clen);
                    if (!UTF8_GET_NAMING_NMTOKEN (&dflt[i], clen)) {
                        signalNotValid (userData, TNC_ERROR_NAME_REQUIRED);
                        return;
                    }
                    i += clen;
                }
                if (attDecl->att_type == TNC_ATTTYPE_ENTITY) {
                    entryPtr1 = Tcl_CreateHashEntry (tncdata->entityDecls,
                                                     dflt, &newPtr);
                    if (!newPtr) {
                        entityInfo =
                            (TNC_EntityInfo *) Tcl_GetHashValue (entryPtr1);
                        if (!entityInfo->is_notation) {
                            signalNotValid (userData,TNC_ERROR_ATT_ENTITY_DEFAULT_MUST_BE_DECLARED);
                        }
                    }
                }
                break;
            case TNC_ATTTYPE_IDREFS:
                start = i = 0;
                while (1) {
                    if (dflt[i] == '\0') {
                        break;
                    }
                    if (dflt[i] == ' ') {
                        start = ++i;
                    }
                    if (start == i) {
                        clen = UTF8_CHAR_LEN (dflt[i]);
                        CHECK_UTF_CHARLEN (clen);
                        if (!UTF8_GET_NAME_START (&dflt[i], clen)) {
                            signalNotValid (userData, TNC_ERROR_NAME_REQUIRED);
                            return;
                        }
                        i += clen;
                    }
                    else {
                        clen = UTF8_CHAR_LEN (dflt[i]);
                        CHECK_UTF_CHARLEN (clen);
                        if (!UTF8_GET_NAMING_NMTOKEN (&dflt[i], clen)) {
                            signalNotValid (userData, TNC_ERROR_NAME_REQUIRED);
                            return;
                        }
                        i += clen;
                    }
                }
                break;
            case TNC_ATTTYPE_ENTITIES:
                copy = tdomstrdup (dflt);
                start = i = 0;
                while (1) {
                    if (copy[i] == '\0') {
                        FREE (copy);
                        break;
                    }
                    if (copy[i] == ' ') {
                        copy[i] = '\0';
                        entryPtr1 = Tcl_CreateHashEntry (tncdata->entityDecls,
                                                         &copy[start],
                                                         &newPtr);
                        if (!newPtr) {
                            entityInfo =
                                (TNC_EntityInfo *) Tcl_GetHashValue (entryPtr1);
                            if (!entityInfo->is_notation) {
                                signalNotValid (userData,TNC_ERROR_ATT_ENTITY_DEFAULT_MUST_BE_DECLARED);
                            }
                        }
                        start = ++i;
                    }
                    if (start == i) {
                        clen = UTF8_CHAR_LEN (copy[i]);
                        CHECK_UTF_CHARLEN_COPY (clen);
                        if (!UTF8_GET_NAME_START (&copy[i], clen)) {
                            signalNotValid (userData, TNC_ERROR_NAME_REQUIRED);
                            FREE (copy);
                            return;
                        }
                        i += clen;
                    }
                    else {
                        clen = UTF8_CHAR_LEN (copy[i]);
                        CHECK_UTF_CHARLEN_COPY (clen);
                        if (!UTF8_GET_NAMING_NMTOKEN (&copy[i], clen)) {
                            signalNotValid (userData, TNC_ERROR_NAME_REQUIRED);
                            FREE (copy);
                            return;
                        }
                        i += clen;
                    }
                }
                break;
            case TNC_ATTTYPE_NMTOKEN:
                i = 0;
                while (1) {
                    if (dflt[i] == '\0') {
                        break;
                    }
                    clen = UTF8_CHAR_LEN (dflt[i]);
                    CHECK_UTF_CHARLEN (clen);
                    if (!UTF8_GET_NAMING_NMTOKEN (&dflt[i], clen)) {
                        signalNotValid (userData, TNC_ERROR_NMTOKEN_REQUIRED);
                        return;
                    }
                    i += clen;
                }
                if (!i) signalNotValid (userData, TNC_ERROR_NMTOKEN_REQUIRED);
                break;
            case TNC_ATTTYPE_NMTOKENS:
                i = 0;
                while (1) {
                    if (dflt[i] == '\0') {
                        break;
                    }
                    if (dflt[i] == ' ') {
                        i++;
                    }
                    clen = UTF8_CHAR_LEN (dflt[i]);
                    CHECK_UTF_CHARLEN (clen);
                    if (!UTF8_GET_NAMING_NMTOKEN (&dflt[i], clen)) {
                        signalNotValid (userData, TNC_ERROR_NMTOKEN_REQUIRED);
                        return;
                    }
                    i += clen;
                }
                if (!i) signalNotValid (userData, TNC_ERROR_NMTOKEN_REQUIRED);
                break;
            case TNC_ATTTYPE_NOTATION:
                if (!Tcl_FindHashEntry (attDecl->lookupTable, dflt)) {
                    signalNotValid (userData, TNC_ERROR_IMPOSSIBLE_DEFAULT);
                    return;
                }
            case TNC_ATTTYPE_ENUMERATION:
                if (!Tcl_FindHashEntry (attDecl->lookupTable, dflt)) {
                    signalNotValid (userData, TNC_ERROR_IMPOSSIBLE_DEFAULT);
                    return;
                }
            case TNC_ATTTYPE_CDATA:
            case TNC_ATTTYPE_ID:
                /* This both cases are only there, to pacify -Wall.
                   CDATA may have any allowed characters (and
                   everything else is detected by extpat).  ID's not
                   allowed to have defaults (handled above). */
                ;
            }
            attDecl->dflt = tdomstrdup (dflt);
        }
        else {
            attDecl->dflt = NULL;
        }
        if (isrequired) {
            elemAttInfo->nrOfreq++;
        }
        attDecl->isrequired = isrequired;
        Tcl_SetHashValue (entryPtr, attDecl);
    }
}

/*
 *----------------------------------------------------------------------------
 *
 * TncNotationDeclHandler --
 *
 *	This procedure is called for every notation declaration.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stores the notationName in the notationDecls table with value
 *      one.
 *
 *----------------------------------------------------------------------------
 */

static void
TncNotationDeclHandler (userData, notationName, base, systemId, publicId)
    void       *userData;
    const char *notationName;
    const char *base;
    const char *systemId;
    const char *publicId;
{
    domReadInfo *tncdata = (domReadInfo *) userData;
    Tcl_HashEntry *entryPtr;
    int newPtr;

    entryPtr = Tcl_CreateHashEntry (tncdata->notationDecls,
                                    notationName,
                                    &newPtr);
#ifdef TNC_DEBUG
    printf ("Notation %s declared\n", notationName);
#endif
    Tcl_SetHashValue (entryPtr, (char *) 1);
}

/*
 *----------------------------------------------------------------------------
 *
 * TncFreeTncModel --
 *
 *	This helper procedure frees recursively TNC_Contents.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory.
 *
 *----------------------------------------------------------------------------
 */

static void
TncFreeTncModel (tmodel)
    TNC_Content *tmodel;
{
    unsigned int i;

    if (tmodel->children) {
        for (i = 0; i < tmodel->numchildren; i++) {
            TncFreeTncModel (&tmodel->children[i]);
        }
        FREE ((char *) tmodel->children);
    }
}

static void
TncFreeValidationData (
    domReadInfo *info
    )
{
    Tcl_HashEntry *entryPtr, *attentryPtr;
    Tcl_HashSearch search, attsearch;
    TNC_Content *model;
    TNC_ElemAttInfo *elemAttInfo;
    TNC_EntityInfo *entityInfo;
    TNC_AttDecl *attDecl;
    
    if (!info->dtdvalidation) return;
    
    entryPtr = Tcl_FirstHashEntry (info->tagNames, &search);
    while (entryPtr) {
        model = Tcl_GetHashValue (entryPtr);
        if (info->elemContentsRewriten) {
            if (model) {
                TncFreeTncModel (model);
                FREE ((char *) model);
            }
        } else {
            XML_FreeContentModel(info->parser, (XML_Content *)model);
        }
        entryPtr = Tcl_NextHashEntry (&search);
    }
    Tcl_DeleteHashTable (info->tagNames);
    entryPtr = Tcl_FirstHashEntry (info->attDefsTables, &search);
    while (entryPtr) {
        elemAttInfo = Tcl_GetHashValue (entryPtr);
        if (!elemAttInfo) {
            entryPtr = Tcl_NextHashEntry (&search);
            continue;
        }
        attentryPtr = Tcl_FirstHashEntry (elemAttInfo->attributes, &attsearch);
        while (attentryPtr) {
            attDecl = Tcl_GetHashValue (attentryPtr);
            if (attDecl) {
                if (attDecl->att_type == TNC_ATTTYPE_NOTATION ||
                    attDecl->att_type == TNC_ATTTYPE_ENUMERATION) {
                    Tcl_DeleteHashTable (attDecl->lookupTable);
                    FREE ((char *) attDecl->lookupTable);
                }
                if (attDecl->dflt) {
                    FREE (attDecl->dflt);
                }
                FREE ((char *) attDecl);
            }
            attentryPtr = Tcl_NextHashEntry (&attsearch);
        }
        Tcl_DeleteHashTable (elemAttInfo->attributes);
        FREE ((char *) elemAttInfo->attributes);
        FREE ((char *) elemAttInfo);
        entryPtr = Tcl_NextHashEntry (&search);
    }
    Tcl_DeleteHashTable (info->attDefsTables);
    entryPtr = Tcl_FirstHashEntry (info->entityDecls, &search);
    while (entryPtr) {
        entityInfo = Tcl_GetHashValue (entryPtr);
        if (entityInfo) {
            if (entityInfo->is_notation) {
                FREE (entityInfo->notationName);
            }
            FREE ((char *) entityInfo);
        }
        entryPtr = Tcl_NextHashEntry (&search);
    }
    Tcl_DeleteHashTable (info->entityDecls);
    Tcl_DeleteHashTable (info->notationDecls);
    Tcl_DeleteHashTable (info->ids);
    if (info->doctypeName) {
        FREE (info->doctypeName);
    }
    FREE ((char *) info->tagNames);
    FREE ((char *) info->attDefsTables);
    FREE ((char *) info->entityDecls);
    FREE ((char *) info->notationDecls);
    FREE ((char *) info->ids);
    FREE ((char *) info->contentStack);
}


/*---------------------------------------------------------------------------
|   domReadDocument
|
\--------------------------------------------------------------------------*/
domDocument *
domReadDocument (
    XML_Parser  parser,
    char       *xml,
    int         length,
    int         ignoreWhiteSpaces,
    int         keepCDATA,
    int         storeLineColumn,
    int         ignorexmlns,
    int         feedbackAfter,
    Tcl_Obj    *feedbackCmd,
    Tcl_Channel channel,
    const char *baseurl,
    Tcl_Obj    *extResolver,
    int         useForeignDTD,
    int         paramEntityParsing,
    int         dtdvalidation,
    Tcl_Interp *interp,
    int        *resultcode
)
{
    int             done, tclLen;
    enum XML_Status status;
    size_t          len;
    domReadInfo     info;
    char            buf[8192];
    Tcl_Obj        *bufObj;
    Tcl_DString     dStr;
    int             useBinary;
    char           *str;
    domDocument    *doc = domCreateDoc(baseurl, storeLineColumn);

    if (extResolver) {
        doc->extResolver = tdomstrdup (Tcl_GetString (extResolver));
    }
    if (ignorexmlns) {
        doc->nodeFlags |= IGNORE_XMLNS;
    }

    info.parser               = parser;
    info.document             = doc;
    info.currentNode          = NULL;
    info.depth                = 0;
    info.ignoreWhiteSpaces    = ignoreWhiteSpaces;
    info.cdata                = (Tcl_DString*) MALLOC (sizeof (Tcl_DString));
    Tcl_DStringInit (info.cdata);
    info.cdataSection         = 0;
    info.storeLineColumn      = storeLineColumn;
    info.ignorexmlns          = ignorexmlns;
    info.feedbackAfter        = feedbackAfter;
    info.feedbackCmd          = feedbackCmd;
    info.nextFeedbackPosition = feedbackAfter;
    info.interp               = interp;
    info.activeNSpos          = -1;
    info.activeNSsize         = 8;
    info.activeNS             = (domActiveNS*) MALLOC (sizeof(domActiveNS) 
                                                       * info.activeNSsize);
    info.baseURIstackPos      = 0;
    info.baseURIstackSize     = INITIAL_BASEURISTACK_SIZE;
    info.baseURIstack         = (domActiveBaseURI*) 
        MALLOC (sizeof(domActiveBaseURI) * info.baseURIstackSize);
    info.insideDTD            = 0;
    info.status               = 0;
    info.dtdvalidation        = dtdvalidation;
    
    if (dtdvalidation) {
        info.tagNames = (Tcl_HashTable *) MALLOC (sizeof (Tcl_HashTable));
        Tcl_InitHashTable (info.tagNames, TCL_STRING_KEYS);
        info.elemContentsRewriten = 0;
        info.dtdstatus = 0;
        info.idCheck = 1;
        info.attDefsTables = 
            (Tcl_HashTable *) MALLOC (sizeof (Tcl_HashTable));
        Tcl_InitHashTable (info.attDefsTables, TCL_STRING_KEYS);
        info.entityDecls = 
            (Tcl_HashTable *) MALLOC (sizeof (Tcl_HashTable));
        Tcl_InitHashTable (info.entityDecls, TCL_STRING_KEYS);
        info.notationDecls =
            (Tcl_HashTable *) MALLOC (sizeof (Tcl_HashTable));
        Tcl_InitHashTable (info.notationDecls, TCL_STRING_KEYS);
        info.ids = (Tcl_HashTable *) MALLOC (sizeof (Tcl_HashTable));
        Tcl_InitHashTable (info.ids, TCL_STRING_KEYS);
        info.doctypeName = NULL;
        info.skipWhiteCDATAs = 1;
        info.ignorePCDATA = 0;
        info.contentStack = (TNC_ContentStack *)
            MALLOC (sizeof (TNC_ContentStack) * TNC_INITCONTENTSTACKSIZE);
        info.contentStackSize = TNC_INITCONTENTSTACKSIZE;
        info.contentStackPtr = 0;
    }
    
    XML_SetUserData(parser, &info);
    XML_SetBase (parser, baseurl);
    /* We must use XML_GetBase(), because XML_SetBase copies the baseURI,
       and we want to compare the pointers */
    info.baseURIstack[0].baseURI = XML_GetBase (parser);
    info.baseURIstack[0].depth = 0;
    XML_UseForeignDTD (parser, (unsigned char) useForeignDTD);
    XML_SetElementHandler(parser, startElement, endElement);
    XML_SetCharacterDataHandler(parser, characterDataHandler);
    XML_SetCommentHandler(parser, commentHandler);
    XML_SetProcessingInstructionHandler(parser, processingInstructionHandler);
    XML_SetEntityDeclHandler (parser, entityDeclHandler);
    if (extResolver) {
        XML_SetExternalEntityRefHandler (parser, externalEntityRefHandler);
    }
    XML_SetParamEntityParsing (parser, 
                             (enum XML_ParamEntityParsing) paramEntityParsing);
    XML_SetDoctypeDeclHandler (parser, startDoctypeDeclHandler,
                               endDoctypeDeclHandler);
    if (keepCDATA) {
        XML_SetCdataSectionHandler(parser, startCDATA, endCDATA);
    }
    if (dtdvalidation) {
        XML_SetElementDeclHandler (parser, TncElementDeclCommand);
        XML_SetAttlistDeclHandler (parser, TncAttDeclCommand);
        XML_SetNotationDeclHandler (parser, TncNotationDeclHandler);
    }

    if (channel == NULL) {
        status = XML_Parse(parser, xml, length, 1);
        switch (status) {
        case XML_STATUS_SUSPENDED:
            DBG(fprintf(stderr, "XML_STATUS_SUSPENDED\n"));
            if (info.status == TCL_BREAK) {
                Tcl_ResetResult(interp);
            }
            /* fall throu */
        case XML_STATUS_ERROR:
            DBG(fprintf(stderr, "XML_STATUS_ERROR\n");)
            FREE ( info.activeNS );
            FREE ( info.baseURIstack );
            Tcl_DStringFree (info.cdata);
            FREE ( info.cdata);
            if (info.dtdvalidation) TncFreeValidationData (&info);
            domFreeDocument (doc, NULL, NULL);
            *resultcode = info.status;
            return NULL;
        case XML_STATUS_OK:
            break;
        }
    } else {
        Tcl_DStringInit (&dStr);
        if (Tcl_GetChannelOption (interp, channel, "-encoding", &dStr) != TCL_OK) {
            FREE ( (char*) info.activeNS );
            FREE ( info.baseURIstack );
            Tcl_DStringFree (info.cdata);
            FREE ( info.cdata);
            if (info.dtdvalidation) TncFreeValidationData (&info);
            domFreeDocument (doc, NULL, NULL);
            *resultcode = info.status;
            return NULL;
        }
        if (strcmp (Tcl_DStringValue (&dStr), "utf-8")==0 ) useBinary = 1;
        else useBinary = 0;
        Tcl_DStringFree (&dStr);
        if (useBinary) {
            do {
                len = Tcl_Read (channel, buf, sizeof(buf));
                done = len < sizeof(buf);
                status = XML_Parse (parser, buf, len, done);
                switch (status) {
                case XML_STATUS_SUSPENDED:
                    DBG(fprintf(stderr, "XML_STATUS_SUSPENDED\n"););
                    if (info.status == TCL_BREAK) {
                        Tcl_ResetResult(interp);
                    }
                    /* fall throu */
                case XML_STATUS_ERROR:
                    DBG(fprintf(stderr, "XML_STATUS_ERROR\n");)
                    FREE ( info.activeNS );
                    FREE ( info.baseURIstack );
                    Tcl_DStringFree (info.cdata);
                    FREE ( info.cdata);
                    if (info.dtdvalidation) TncFreeValidationData (&info);
                    domFreeDocument (doc, NULL, NULL);
                    *resultcode = info.status;
                    return NULL;
                case XML_STATUS_OK:
                    break;
                }
            } while (!done);
        } else {
            bufObj = Tcl_NewObj();
            Tcl_SetObjLength (bufObj, 6144);
            do {
                len = Tcl_ReadChars (channel, bufObj, 1024, 0);
                done = (len < 1024);
                str = Tcl_GetStringFromObj(bufObj, &tclLen);
                status = XML_Parse (parser, str, tclLen, done);
                switch (status) {
                case XML_STATUS_SUSPENDED:
                    DBG(fprintf(stderr, "XML_STATUS_SUSPENDED\n"););
                    if (info.status == TCL_BREAK) {
                        Tcl_ResetResult(interp);
                    }
                    /* fall throu */
                case XML_STATUS_ERROR:
                    DBG(fprintf(stderr, "XML_STATUS_ERROR\n");)
                    FREE ( info.activeNS );
                    FREE ( info.baseURIstack );
                    Tcl_DStringFree (info.cdata);
                    FREE ( info.cdata);
                    if (info.dtdvalidation) TncFreeValidationData (&info);
                    domFreeDocument (doc, NULL, NULL);
                    Tcl_DecrRefCount (bufObj);
                    *resultcode = info.status;
                    return NULL;
                case XML_STATUS_OK:
                    break;
                }
            } while (!done);
            Tcl_DecrRefCount (bufObj);
        }
    }
    FREE ( info.activeNS );
    FREE ( info.baseURIstack );
    Tcl_DStringFree (info.cdata);
    FREE ( info.cdata);
    if (info.dtdvalidation) TncFreeValidationData (&info);

    domSetDocumentElement (doc);

    return doc;
}


#endif /* ifndef TDOM_NO_EXPAT */



/*---------------------------------------------------------------------------
|   domException2String
|
\--------------------------------------------------------------------------*/
const char *
domException2String (
    domException exception
)
{
    return domException2StringTable[exception];
}


/*---------------------------------------------------------------------------
|   domGetLineColumn
|
\--------------------------------------------------------------------------*/
int
domGetLineColumn (
    domNode *node,
    int     *line,
    int     *column
)
{
    char *v;
    domLineColumn  *lc;

    *line   = -1;
    *column = -1;

    if (node->nodeFlags & HAS_LINE_COLUMN) {
        v = (char*)node;
        switch (node->nodeType) {
            case ELEMENT_NODE:
                v = v + sizeof(domNode);
                break;

            case TEXT_NODE:
            case CDATA_SECTION_NODE:
            case COMMENT_NODE:
                v = v + sizeof(domTextNode);
                break;

            case PROCESSING_INSTRUCTION_NODE:
                v = v + sizeof(domProcessingInstructionNode);
                break;

            default:
                return -1;
        }
        lc = (domLineColumn *)v;
        *line   = lc->line;
        *column = lc->column;
        return 0;
    } else {
        return -1;
    }
}

domAttrNode *
domCreateXMLNamespaceNode (
    domNode  *parent
)
{
    Tcl_HashEntry  *h;
    int             hnew;
    domAttrNode    *attr;
    domNS          *ns;

    attr = (domAttrNode *) domAlloc (sizeof (domAttrNode));
    memset (attr, 0, sizeof (domAttrNode));
    h = Tcl_CreateHashEntry(&HASHTAB(parent->ownerDocument,tdom_attrNames),
                            "xmlns:xml", &hnew);
    ns = domNewNamespace (parent->ownerDocument, "xml", XML_NAMESPACE);
    attr->nodeType      = ATTRIBUTE_NODE;
    attr->nodeFlags     = IS_NS_NODE;
    attr->namespace     = ns->index;
    attr->nodeName      = (char *)&(h->key);
    attr->parentNode    = parent;
    attr->valueLength   = strlen (XML_NAMESPACE);
    attr->nodeValue     = tdomstrdup (XML_NAMESPACE);
    return attr;
}


/*
 *----------------------------------------------------------------------
 *
 * domCreateDoc --
 *
 *      This procedure allocates a new domDocument, initialize it and
 *      creates its rootNode (with initialization).
 *
 * Results:
 *	The domDocument node:
 *
 * Side effects:
 *	Allocates memory for the returned domDocument and its
 *	rootNode.
 *
 *----------------------------------------------------------------------
 */

domDocument *
domCreateDoc (
    const char * baseURI,
    int          storeLineColumn
)
{
    Tcl_HashEntry *h;
    int            hnew;
    domNode       *rootNode;
    domDocument   *doc;
    domLineColumn *lc;

    doc = (domDocument *) MALLOC (sizeof (domDocument));
    memset(doc, 0, sizeof(domDocument));
    doc->nodeType       = DOCUMENT_NODE;
    doc->documentNumber = DOC_NO(doc);
    doc->nsptr          = -1;
    doc->nslen          =  4;
    doc->namespaces     = (domNS**) MALLOC (sizeof (domNS*) * doc->nslen);
    
    /* We malloc and initialze the baseURIs hash table here to avoid
       cluttering of the code all over the place with checks. */
    doc->baseURIs = MALLOC (sizeof (Tcl_HashTable));
    Tcl_InitHashTable (doc->baseURIs, TCL_ONE_WORD_KEYS);

    TDomThreaded(
        domLocksAttach(doc);
        Tcl_InitHashTable(&doc->tdom_tagNames, TCL_STRING_KEYS);
        Tcl_InitHashTable(&doc->tdom_attrNames, TCL_STRING_KEYS);
    )

    if (storeLineColumn) {
        rootNode = (domNode*) domAlloc(sizeof(domNode)+sizeof(domLineColumn));
    } else {
        rootNode = (domNode*) domAlloc(sizeof(domNode));
    }
    memset(rootNode, 0, sizeof(domNode));
    rootNode->nodeType      = ELEMENT_NODE;
    if (baseURI) {
        h = Tcl_CreateHashEntry (doc->baseURIs, (char*)rootNode, &hnew);
        Tcl_SetHashValue (h, tdomstrdup (baseURI));
        rootNode->nodeFlags |= HAS_BASEURI;
    } else {
        rootNode->nodeFlags = 0;
    }
    rootNode->namespace     = 0;
    h = Tcl_CreateHashEntry(&HASHTAB(doc,tdom_tagNames), "", &hnew);
    rootNode->nodeName      = (char *)&(h->key);
    rootNode->nodeNumber    = NODE_NO(doc);
    rootNode->ownerDocument = doc;
    rootNode->parentNode    = NULL;
    rootNode->firstChild    = rootNode->lastChild = NULL;
    rootNode->firstAttr     = domCreateXMLNamespaceNode (rootNode);
    if (storeLineColumn) {
        lc = (domLineColumn*) ( ((char*)rootNode) + sizeof(domNode));
        rootNode->nodeFlags |= HAS_LINE_COLUMN;
        lc->line            = 0;
        lc->column          = 0;
    }
    doc->rootNode = rootNode;

    return doc;
}

/*---------------------------------------------------------------------------
|   domCreateDocument
|
\--------------------------------------------------------------------------*/
domDocument *
domCreateDocument (
    const char *uri,
    char       *documentElementTagName
)
{
    Tcl_HashEntry *h;
    int            hnew;
    domNode       *node;
    domDocument   *doc;
    char           prefix[MAX_PREFIX_LEN];
    const char    *localName;
    domNS         *ns = NULL;

    if (uri) {
        domSplitQName (documentElementTagName, prefix, &localName);
        DBG(fprintf(stderr, 
                    "rootName: -->%s<--, prefix: -->%s<--, localName: -->%s<--\n", 
                    documentElementTagName, prefix, localName););
    }
    doc = domCreateDoc (NULL, 0);

    h = Tcl_CreateHashEntry(&HASHTAB(doc, tdom_tagNames),
                            documentElementTagName, &hnew);
    node = (domNode*) domAlloc(sizeof(domNode));
    memset(node, 0, sizeof(domNode));
    node->nodeType        = ELEMENT_NODE;
    node->nodeFlags       = 0;
    node->nodeNumber      = NODE_NO(doc);
    node->ownerDocument   = doc;
    node->nodeName        = (char *)&(h->key);
    doc->documentElement  = node;
    if (uri) {
        ns = domNewNamespace (doc, prefix, uri);
        node->namespace   = ns->index;
        domAddNSToNode (node, ns);
    }
    doc->rootNode->firstChild = doc->rootNode->lastChild = doc->documentElement;

    return doc;
}


/*---------------------------------------------------------------------------
|   domSetDocumentElement
|
\--------------------------------------------------------------------------*/
void
domSetDocumentElement (
    domDocument     *doc
    )
{
    domNode *node;
    
    doc->documentElement = NULL;
    node = doc->rootNode->firstChild;
    while (node) {
        if (node->nodeType == ELEMENT_NODE) {
            doc->documentElement = node;
            break;
        }
        node = node->nextSibling;
    }
    if (!doc->documentElement) {
        doc->documentElement = doc->rootNode->firstChild;
    }
}

/*---------------------------------------------------------------------------
|   domFreeNode
|
\--------------------------------------------------------------------------*/
void
domFreeNode (
    domNode         * node,
    domFreeCallback   freeCB,
    void            * clientData,
    int               dontfree
)
{
    int            shared = 0;
    domNode       *child, *ctemp;
    domAttrNode   *atemp, *attr, *aprev;
    Tcl_HashEntry *entryPtr;

    if (node == NULL) {
        DBG(fprintf (stderr, "null ptr in domFreeNode (dom.c) !\n");)
        return;
    }
    TDomThreaded (
        shared = node->ownerDocument && node->ownerDocument->refCount > 1;
    )
 
    /*----------------------------------------------------------------
    |   dontfree instruct us to walk the node tree and apply the 
    |   user-supplied callback, *w/o* actually deleting nodes.
    |   This is normally done when a thread detaches from the
    |   shared DOM tree and wants to garbage-collect all nodecmds
    |   in it's interpreter which attached to the tree nodes.
    \---------------------------------------------------------------*/

    if (dontfree) {
        shared = 1;
    } else {
        node->nodeFlags |= IS_DELETED;
    }

    if (node->nodeType == ATTRIBUTE_NODE && !shared) {
        attr = ((domAttrNode*)node)->parentNode->firstAttr;
        aprev = NULL;
        while (attr && (attr != (domAttrNode*)node)) {
            aprev = attr;
            attr = attr->nextSibling;
        }
        if (attr) {
            if (aprev) {
                aprev->nextSibling = attr->nextSibling;
            } else {
                ((domAttrNode*)node)->parentNode->firstAttr = attr->nextSibling;
            }
            FREE (attr->nodeValue);
            domFree ((void*)attr);
        }
    } else if (node->nodeType == ELEMENT_NODE) {
        child = node->lastChild;
        while (child) {
            ctemp = child->previousSibling;
            if (freeCB) {
                freeCB(child, clientData);
            }
            domFreeNode (child, freeCB, clientData, dontfree);
            child = ctemp;
        }
        if (shared) {
            return;
        }
        attr = node->firstAttr;
        while (attr) {
            atemp = attr;
            attr = attr->nextSibling;
            FREE (atemp->nodeValue);
            domFree ((void*)atemp);
        }
        if (node->nodeFlags & HAS_BASEURI) {
            entryPtr = Tcl_FindHashEntry (node->ownerDocument->baseURIs,
                                          (char*)node);
            if (entryPtr) {
                FREE ((char *) Tcl_GetHashValue (entryPtr));
                Tcl_DeleteHashEntry (entryPtr);
            }
        }
        domFree ((void*)node);

    } else if (node->nodeType == PROCESSING_INSTRUCTION_NODE && !shared) {
        FREE (((domProcessingInstructionNode*)node)->dataValue);
        FREE (((domProcessingInstructionNode*)node)->targetValue);
        domFree ((void*)node);

    } else if (!shared) {
        FREE (((domTextNode*)node)->nodeValue);
        domFree ((void*)node);
    }
}


/*---------------------------------------------------------------------------
|   domDeleteNode    - unlinks node from tree and free all child nodes
|                      and itself
|
\--------------------------------------------------------------------------*/
domException
domDeleteNode (
    domNode         * node,
    domFreeCallback   freeCB,
    void            * clientData
)
{
    TDomThreaded(int shared = 0;)
    domDocument *doc;

    if (node->nodeType == ATTRIBUTE_NODE) {
        domPanic("domDeleteNode on ATTRIBUTE_NODE not supported!");
    }
    TDomThreaded (
        shared = node->ownerDocument->refCount > 1;
    )
    doc = node->ownerDocument;

    /*----------------------------------------------------------------
    |   unlink node from child or fragment list
    \---------------------------------------------------------------*/
    if (node->previousSibling) {
        node->previousSibling->nextSibling = node->nextSibling;
    } else {
        if (node->parentNode) {
            node->parentNode->firstChild = node->nextSibling;
        } else {
            /* Node may be a top level node */
            if (doc->rootNode->firstChild == node) {
                doc->rootNode->firstChild = node->nextSibling;
            }
        }
    }
    if (node->nextSibling) {
        node->nextSibling->previousSibling = node->previousSibling;
    } else {
        if (node->parentNode) {
            node->parentNode->lastChild = node->previousSibling;
        } else {
            /* Node may be a top level node */
            if (doc->rootNode->lastChild == node) {
                doc->rootNode->lastChild = node->previousSibling;
            }
        }
    }
    if (doc->fragments == node) {
        doc->fragments = node->nextSibling;
    }
    if (!node->parentNode) {
        domSetDocumentElement (doc);
    }

    /*----------------------------------------------------------------
    |   for shared docs, append node to the delete nodes list
    |   otherwise delete the node physically
    \---------------------------------------------------------------*/
    if (freeCB) {
        freeCB(node, clientData);
    }
    TDomThreaded (    
        if (shared) {
            if (doc->deletedNodes) {
                node->nextSibling = doc->deletedNodes;
            } else {
                node->nextSibling = NULL;
            }
            doc->deletedNodes = node;
            node->nodeFlags |= IS_DELETED;
        }
    )
    MutationEvent3(DOMNodeRemoved, childToRemove, node);
    MutationEvent2(DOMSubtreeModified, node);
    domFreeNode(node, freeCB, clientData, 0);

    return OK;
}


/*---------------------------------------------------------------------------
|   domFreeDocument
|
\--------------------------------------------------------------------------*/
void
domFreeDocument (
    domDocument     * doc,
    domFreeCallback   freeCB,
    void            * clientData
)
{
    domNode      *node, *next;
    domNS        *ns;
    int           i, dontfree = 0;
    Tcl_HashEntry *entryPtr;
    Tcl_HashSearch search;

    if (doc->nodeFlags & DONT_FREE) {
        doc->nodeFlags &= ~DONT_FREE;
        dontfree = 1;
    }
    /*-----------------------------------------------------------
    |   delete main trees, including top level PIs, etc.
    \-----------------------------------------------------------*/
    node = doc->rootNode;
    if (node) {
        if (freeCB) {
            freeCB(node, clientData);
        }
        domFreeNode (node, freeCB, clientData, dontfree);
    }

    /*-----------------------------------------------------------
    | delete fragment trees
    \-----------------------------------------------------------*/
    node = doc->fragments;
    while (node) {
        next = node->nextSibling;
        if (freeCB) {
            freeCB(node, clientData);
        }
        domFreeNode (node, freeCB, clientData, dontfree);
        node = next;
    }

    if (dontfree) return;
    
    /*-----------------------------------------------------------
    | delete namespaces
    \-----------------------------------------------------------*/
    for (i = 0; i <= doc->nsptr; i++) {
        ns = doc->namespaces[i];
        FREE(ns->uri);
        FREE(ns->prefix);
        FREE ((char*) ns);
    }
    FREE ((char *)doc->namespaces);

    /*-----------------------------------------------------------
    | delete global selectNodes prefix namespace mappings
    \-----------------------------------------------------------*/
    if (doc->prefixNSMappings) {
        i = 0;
        while (doc->prefixNSMappings[i]) {
            FREE (doc->prefixNSMappings[i]);
            i++;
        }
        FREE (doc->prefixNSMappings);
    }

    /*-----------------------------------------------------------
    | delete doctype info
    \-----------------------------------------------------------*/
    if (doc->doctype) {
#define DOCINFO_FREE(item) if (doc->doctype->item) FREE(doc->doctype->item)
        DOCINFO_FREE(systemId);
        DOCINFO_FREE(publicId);
        DOCINFO_FREE(internalSubset);
        DOCINFO_FREE(encoding);
        DOCINFO_FREE(mediaType);
        DOCINFO_FREE(method);
        if (doc->doctype->cdataSectionElements) {
            Tcl_DeleteHashTable (doc->doctype->cdataSectionElements);
            FREE (doc->doctype->cdataSectionElements);
        }

        FREE((char*) doc->doctype);
    }

    /*-----------------------------------------------------------
    | delete ID hash table
    \-----------------------------------------------------------*/
    if (doc->ids) {
        Tcl_DeleteHashTable (doc->ids);
        FREE (doc->ids);
    }

    /*-----------------------------------------------------------
    | delete unparsed entities hash table
    \-----------------------------------------------------------*/
    if (doc->unparsedEntities) {
        entryPtr = Tcl_FirstHashEntry (doc->unparsedEntities, &search);
        while (entryPtr) {
            FREE (Tcl_GetHashValue (entryPtr));
            entryPtr = Tcl_NextHashEntry (&search);
        }
        Tcl_DeleteHashTable (doc->unparsedEntities);
        FREE (doc->unparsedEntities);
    }

    /*-----------------------------------------------------------
    | delete base URIs hash table
    \-----------------------------------------------------------*/
    entryPtr = Tcl_FirstHashEntry (doc->baseURIs, &search);
    while (entryPtr) {
        FREE (Tcl_GetHashValue (entryPtr));
        entryPtr = Tcl_NextHashEntry (&search);
    }
    Tcl_DeleteHashTable (doc->baseURIs);
    FREE (doc->baseURIs);
    
    /*-----------------------------------------------------------
    | delete XPath cache hash table
    \-----------------------------------------------------------*/
    if (doc->xpathCache) {
        entryPtr = Tcl_FirstHashEntry (doc->xpathCache, &search);
        while (entryPtr) {
            xpathFreeAst((ast)Tcl_GetHashValue (entryPtr));
            entryPtr = Tcl_NextHashEntry (&search);
        }
        Tcl_DeleteHashTable (doc->xpathCache);
        FREE (doc->xpathCache);
    }

    if (doc->extResolver) {
        FREE (doc->extResolver);
    }

    /*-----------------------------------------------------------
    | delete tag/attribute hash tables (for threaded builds only)
    \-----------------------------------------------------------*/
    TDomThreaded (
        {
            Tcl_HashEntry *entryPtr;
            Tcl_HashSearch search;
            entryPtr = Tcl_FirstHashEntry(&doc->tdom_tagNames, &search);
            while (entryPtr) {
                Tcl_DeleteHashEntry(entryPtr);
                entryPtr = Tcl_NextHashEntry(&search);
            }
            Tcl_DeleteHashTable(&doc->tdom_tagNames);
            entryPtr = Tcl_FirstHashEntry(&doc->tdom_attrNames, &search);
            while (entryPtr) {
                Tcl_DeleteHashEntry(entryPtr);
                entryPtr = Tcl_NextHashEntry(&search);
            }
            Tcl_DeleteHashTable(&doc->tdom_attrNames);
            domLocksDetach(doc);
            node = doc->deletedNodes;
            while (node) {
                next = node->nextSibling;
                domFreeNode (node, freeCB, clientData, 0);
                node = next;
            }
        }
    )

    FREE ((char*)doc);
}

/*---------------------------------------------------------------------------
|   domSetAttribute
|
\--------------------------------------------------------------------------*/
domAttrNode *
domSetAttribute (
    domNode    *node,
    const char *attributeName,
    const char *attributeValue
)
{
    domAttrNode   *attr, *lastAttr;
    Tcl_HashEntry *h;
    int            hnew;

    if (!node || node->nodeType != ELEMENT_NODE) {
        return NULL;
    }

    /*----------------------------------------------------
    |   try to find an existing attribute
    \---------------------------------------------------*/
    attr = node->firstAttr;
    while (attr && strcmp(attr->nodeName, attributeName)) {
        attr = attr->nextSibling;
    }
    if (attr) {
        if (attr->nodeFlags & IS_ID_ATTRIBUTE) {
            h = Tcl_FindHashEntry (node->ownerDocument->ids, attr->nodeValue);
            if (h) {
                Tcl_DeleteHashEntry (h);
                h = Tcl_CreateHashEntry (node->ownerDocument->ids,
                                         attributeValue, &hnew);
                /* XXX what to do, if hnew = 0  ??? */
                Tcl_SetHashValue (h, node);
            }
        }
        FREE (attr->nodeValue);
        attr->valueLength = strlen(attributeValue);
        attr->nodeValue   = (char*)MALLOC(attr->valueLength+1);
        strcpy(attr->nodeValue, attributeValue);
    } else {
        /*-----------------------------------------------
        |   add a complete new attribute node
        \----------------------------------------------*/
        attr = (domAttrNode*) domAlloc(sizeof(domAttrNode));
        memset(attr, 0, sizeof(domAttrNode));
        h = Tcl_CreateHashEntry(&HASHTAB(node->ownerDocument,tdom_attrNames),
                                attributeName, &hnew);
        attr->nodeType    = ATTRIBUTE_NODE;
        attr->nodeFlags   = 0;
        attr->namespace   = 0;
        attr->nodeName    = (char *)&(h->key);
        attr->parentNode  = node;
        attr->valueLength = strlen(attributeValue);
        attr->nodeValue   = (char*)MALLOC(attr->valueLength+1);
        strcpy(attr->nodeValue, attributeValue);

        if (node->firstAttr) {
            lastAttr = node->firstAttr;
            /* move to the end of the attribute list */
            while (lastAttr->nextSibling) lastAttr = lastAttr->nextSibling;
            lastAttr->nextSibling = attr;
        } else {
            node->firstAttr = attr;
        }
    }
    MutationEvent();
    return attr;
}

/*---------------------------------------------------------------------------
|   domSetAttributeNS
|
\--------------------------------------------------------------------------*/
domAttrNode *
domSetAttributeNS (
    domNode *node,
    const char *attributeName,
    const char *attributeValue,
    const char *uri,
    int         createNSIfNeeded
)
{
    domAttrNode   *attr, *lastAttr;
    Tcl_HashEntry *h;
    int            hnew, hasUri = 1, isNSAttr = 0, isDftNS = 0;
    domNS         *ns;
    char           prefix[MAX_PREFIX_LEN];
    const char    *localName, *newLocalName;
    Tcl_DString    dStr;
    
    DBG(fprintf (stderr, "domSetAttributeNS: attributeName %s, attributeValue %s, uri %s\n", attributeName, attributeValue, uri);)
    if (!node || node->nodeType != ELEMENT_NODE) {
        return NULL;
    }

    domSplitQName (attributeName, prefix, &localName);
    if (!uri || uri[0]=='\0') hasUri = 0;
    if (hasUri && (prefix[0] == '\0')) return NULL;
    if ((prefix[0] == '\0' && strcmp (localName, "xmlns")==0)
        || (strcmp (prefix, "xmlns")==0)) {
        isNSAttr = 1;
        createNSIfNeeded = 0;
        if (prefix[0] == '\0') {
            isDftNS = 1;
            ns = domLookupPrefix (node, "");
        } else {
            ns = domLookupPrefix (node, prefix);
        }
        if (ns && (strcmp (ns->uri, attributeValue)==0)) return NULL;
        if (!hasUri) {
            uri = attributeValue;
            isNSAttr = 1;
            hasUri = 1;
            if (strcmp (localName, "xmlns")==0) isDftNS = 1;
        } else {
            return NULL;
        }
    }
    if (!hasUri) {
        if (prefix[0] != '\0' && strcmp (prefix, "xml")==0) {
            uri = "http://www.w3.org/XML/1998/namespace";
            hasUri = 1;
        }
    }
    if (!hasUri && prefix[0] != '\0') return NULL;

    /*----------------------------------------------------
    |   try to find an existing attribute
    \---------------------------------------------------*/
    attr = node->firstAttr;
    while (attr) {
        if (hasUri) {
            if (attr->nodeFlags & IS_NS_NODE) {
                if (isNSAttr) {
                    if (strcmp (attributeName, attr->nodeName)==0) {
                        break;
                    }
                }
            } else {
                if (attr->namespace && !isNSAttr) {
                    ns = domGetNamespaceByIndex (node->ownerDocument,
                                                 attr->namespace);
                    if (strcmp (uri, ns->uri)==0) {
                        newLocalName = localName;
                        domSplitQName (attr->nodeName, prefix, &localName);
                        if (strcmp (newLocalName, localName)==0) break;
                    }
                }
            }
        } else {
            if (!attr->namespace) {
                if (strcmp (attr->nodeName, localName)==0) break;
            }
        }
        attr = attr->nextSibling;
    }
    if (attr) {
        DBG(fprintf (stderr, "domSetAttributeNS: reseting existing attribute %s ; old value: %s\n", attr->nodeName, attr->nodeValue);)
        if (attr->nodeFlags & IS_ID_ATTRIBUTE) {
            h = Tcl_FindHashEntry (node->ownerDocument->ids, attr->nodeValue);
            if (h) {
                Tcl_DeleteHashEntry (h);
                h = Tcl_CreateHashEntry (node->ownerDocument->ids,
                                         attributeValue, &hnew);
                Tcl_SetHashValue (h, node);
            }
        }
        FREE (attr->nodeValue);
        attr->valueLength = strlen(attributeValue);
        attr->nodeValue   = (char*)MALLOC(attr->valueLength+1);
        strcpy(attr->nodeValue, attributeValue);
    } else {
        /*--------------------------------------------------------
        |   add a complete new attribute node
        \-------------------------------------------------------*/
        attr = (domAttrNode*) domAlloc(sizeof(domAttrNode));
        memset(attr, 0, sizeof(domAttrNode));
        h = Tcl_CreateHashEntry(&HASHTAB(node->ownerDocument,tdom_attrNames),
                                attributeName, &hnew);
        attr->nodeType = ATTRIBUTE_NODE;
        if (hasUri) {
            if (isNSAttr) {
                if (isDftNS) {
                    ns = domLookupNamespace (node->ownerDocument, "", uri);
                } else {
                    ns = domLookupNamespace (node->ownerDocument, localName, uri);
                }
            } else {
                ns = domLookupPrefix (node, prefix);
                if (ns && (strcmp (ns->uri, uri)!=0)) ns = NULL;
            }
            if (!ns) {
                if (isNSAttr) {
                    if (isDftNS) {
                        ns = domNewNamespace (node->ownerDocument, "", uri);
                    } else {
                        ns = domNewNamespace (node->ownerDocument, localName, uri);
                    }
                } else {
                    ns = domNewNamespace (node->ownerDocument, prefix, uri);
                    if (createNSIfNeeded) {
                        if (prefix[0] == '\0') {
                            domSetAttributeNS (node, "xmlns", uri, NULL, 0);
                        } else {
                            Tcl_DStringInit (&dStr);
                            Tcl_DStringAppend (&dStr, "xmlns:", 6);
                            Tcl_DStringAppend (&dStr, prefix, -1);
                            domSetAttributeNS (node, Tcl_DStringValue (&dStr),
                                               uri, NULL, 0);
                        }
                    }
                }
            }
            attr->namespace = ns->index;
            if (isNSAttr) {
                attr->nodeFlags = IS_NS_NODE;
            }
        }
        attr->nodeName    = (char *)&(h->key);
        attr->parentNode  = node;
        attr->valueLength = strlen(attributeValue);
        attr->nodeValue   = (char*)MALLOC(attr->valueLength+1);
        strcpy(attr->nodeValue, attributeValue);

        if (isNSAttr) {
            if (node->firstAttr && (node->firstAttr->nodeFlags & IS_NS_NODE)) {
                lastAttr = node->firstAttr;
                while (lastAttr->nextSibling
                       && (lastAttr->nextSibling->nodeFlags & IS_NS_NODE)) {
                    lastAttr = lastAttr->nextSibling;
                }
                attr->nextSibling = lastAttr->nextSibling;
                lastAttr->nextSibling = attr;
            } else {
                attr->nextSibling = node->firstAttr;
                node->firstAttr = attr;
            }
        } else {
            if (node->firstAttr) {
                lastAttr = node->firstAttr;
                /* move to the end of the attribute list */
                while (lastAttr->nextSibling) lastAttr = lastAttr->nextSibling;
                lastAttr->nextSibling = attr;
            } else {
                node->firstAttr = attr;
            }
        }
    }
    MutationEvent();
    return attr;
}


/*---------------------------------------------------------------------------
|   domRemoveAttribute
|
\--------------------------------------------------------------------------*/
int
domRemoveAttribute (
    domNode    *node,
    const char *attributeName
)
{
    domAttrNode *attr, *previous = NULL;
    Tcl_HashEntry *h;

    if (!node || node->nodeType != ELEMENT_NODE) {
        return -1;
    }

    /*----------------------------------------------------
    |   try to find the attribute
    \---------------------------------------------------*/
    attr = node->firstAttr;
    while (attr && strcmp(attr->nodeName, attributeName)) {
        previous = attr;
        attr = attr->nextSibling;
    }
    if (attr) {
        if (previous) {
            previous->nextSibling = attr->nextSibling;
        } else {
            attr->parentNode->firstAttr = attr->nextSibling;
        }

        if (attr->nodeFlags & IS_ID_ATTRIBUTE) {
            h = Tcl_FindHashEntry (node->ownerDocument->ids, attr->nodeValue);
            if (h) Tcl_DeleteHashEntry (h);
        }
        FREE (attr->nodeValue);
        MutationEvent();

        domFree ((void*)attr);
        return 0;
    }
    return -1;
}


/*---------------------------------------------------------------------------
|   domRemoveAttributeNS
|
\--------------------------------------------------------------------------*/
int
domRemoveAttributeNS (
    domNode    *node,
    const char *uri,
    const char *localName
)
{
    domAttrNode *attr, *previous = NULL;
    domNS       *ns = NULL;
    char         prefix[MAX_PREFIX_LEN];
    const char  *str;
    Tcl_HashEntry *h;

    if (!node || node->nodeType != ELEMENT_NODE) {
        return -1;
    }

    attr = node->firstAttr;
    while (attr) {
        domSplitQName (attr->nodeName, prefix, &str);
        if (strcmp(localName,str)==0) {
            ns = domGetNamespaceByIndex(node->ownerDocument, attr->namespace);
            if (ns && strcmp(ns->uri, uri)==0) {
                if (previous) {
                    previous->nextSibling = attr->nextSibling;
                } else {
                    attr->parentNode->firstAttr = attr->nextSibling;
                }

                if (attr->nodeFlags & IS_ID_ATTRIBUTE) {
                    h = Tcl_FindHashEntry (node->ownerDocument->ids, 
                                           attr->nodeValue);
                    if (h) Tcl_DeleteHashEntry (h);
                }
                FREE (attr->nodeValue);
                MutationEvent();
                domFree ((void*)attr);
                return 0;
            }
        }
        previous = attr;
        attr = attr->nextSibling;
    }
    return -1;
}


/*---------------------------------------------------------------------------
|   __dbgAttr
|
\--------------------------------------------------------------------------*/
DBG(
static void __dbgAttr (domAttrNode *node) {

    DBG(fprintf(stderr, " %s=%s", node->nodeName, node->nodeValue);)
    if (node->nextSibling) __dbgAttr(node->nextSibling);
}
)


/*---------------------------------------------------------------------------
|   domSetDocument
|
\--------------------------------------------------------------------------*/
void
domSetDocument (
    domNode     *node,
    domDocument *doc
)
{
    domNode *child;
    domNS   *ns, *origNS;
    domDocument *origDoc;
    domAttrNode *attr;
    Tcl_HashEntry *h;
    TDomThreaded (
        int hnew;
    )
    
    if (node->nodeFlags & HAS_BASEURI) {
        h = Tcl_FindHashEntry (node->ownerDocument->baseURIs, (char*)node);
        if (h) {
            FREE ((char *) Tcl_GetHashValue (h));
            Tcl_DeleteHashEntry (h);
        }
        node->nodeFlags &= ~HAS_BASEURI;
    }
    if (node->nodeType == ELEMENT_NODE) {
        origDoc = node->ownerDocument;
        node->ownerDocument = doc;
        for (attr = node->firstAttr; attr != NULL; attr = attr->nextSibling) {
            if (attr->nodeFlags & IS_NS_NODE) {
                origNS = origDoc->namespaces[attr->namespace-1];
                ns = domNewNamespace (doc, origNS->prefix, origNS->uri);
                attr->namespace = ns->index;
            } else if (attr->namespace) {
                ns = domAddNSToNode (node, 
                                     origDoc->namespaces[attr->namespace-1]);
                if (ns) attr->namespace = ns->index;
            }
        }
        if (node->namespace) {
            ns = domAddNSToNode (node, origDoc->namespaces[node->namespace-1]);
            if (ns) node->namespace = ns->index;
        } else {
            ns = domAddNSToNode (node, NULL);
            if (ns) {
                node->namespace = ns->index;
            }
        }
        DBG(fprintf(stderr, "domSetDocument node%s ", node->nodeName);
             __dbgAttr(node->firstAttr);
             fprintf(stderr, "\n");
        )
                
        TDomThreaded (
            if (origDoc != doc) {
                /* Make hash table entries as necessary for
                 * tdom_tagNames and tdom_attrNames. */
                h = Tcl_CreateHashEntry(&doc->tdom_tagNames, node->nodeName,
                                        &hnew);
                node->nodeName = (domString) &(h->key);
                for (attr = node->firstAttr; 
                     attr != NULL; 
                     attr = attr->nextSibling) {
                    h = Tcl_CreateHashEntry(&doc->tdom_attrNames, 
                                            attr->nodeName, &hnew);
                    attr->nodeName = (domString) &(h->key);
                }
            }
        )
        child = node->firstChild;
        while (child != NULL) {
            domSetDocument (child, doc);
            child = child->nextSibling;
        }
    } else {
        node->ownerDocument = doc;
    }

    DBG(fprintf(stderr, "end domSetDocument node %s\n", node->nodeName);)
}


/*---------------------------------------------------------------------------
|   domSetNodeValue
|
\--------------------------------------------------------------------------*/
domException
domSetNodeValue (
    domNode    *node,
    const char *nodeValue,
    int         valueLen
)
{
    domTextNode   *textnode;

    if ((node->nodeType != TEXT_NODE) &&
        (node->nodeType != CDATA_SECTION_NODE) &&
        (node->nodeType != COMMENT_NODE)
    ) {
        return NO_MODIFICATION_ALLOWED_ERR;
    }

    textnode = (domTextNode*) node;
    FREE(textnode->nodeValue);
    textnode->nodeValue   = MALLOC (valueLen);
    textnode->valueLength = valueLen;
    memmove(textnode->nodeValue, nodeValue, valueLen);
    MutationEvent();
    return OK;
}


/*
 *----------------------------------------------------------------------
 *
 * domRemoveChild --
 *
 *      This procedure implements the dom method removeChild. Removes
 *      child from the list of children of node.
 *
 * Results:
 *	Returns a domException:
 *
 *      NOT_FOUND_ERR: Raised if the node child is not a child of node.
 *
 *      OK: otherwise
 *
 * Side effects:
 *	Alters the involved document.
 *
 *----------------------------------------------------------------------
 */

domException
domRemoveChild (
    domNode *node,
    domNode *child
)
{
    domNode *n;

    /* check, if node is in deed the parent of child */
    if (child->parentNode != node) {
        /* If node is the root node of a document and child
           is in deed a child of this node, then 
           child->parentNode will be NULL. In this case, we
           loop throu the childs of node, to see, if the child
           is valid. */
        if (node->ownerDocument->rootNode == node) {
            n = node->firstChild;
            while (n) {
                if (n == child) {
                    /* child is in deed a child of node */
                    break;
                }
                n = n->nextSibling;
            }
            if (!n) {
                return NOT_FOUND_ERR;
            }
        } else {
            return NOT_FOUND_ERR;
        }
    }

    if (child->previousSibling) {
        child->previousSibling->nextSibling =  child->nextSibling;
    } else {
        node->firstChild = child->nextSibling;
    }
    if (child->nextSibling) {
        child->nextSibling->previousSibling =  child->previousSibling;
    } else {
        node->lastChild = child->previousSibling;
    }

    /* link child into the fragments list */
    if (child->ownerDocument->fragments) {
        child->nextSibling = child->ownerDocument->fragments;
        child->ownerDocument->fragments->previousSibling = child;
        child->ownerDocument->fragments = child;
    } else {
        child->ownerDocument->fragments = child;
        child->nextSibling = NULL;
    }
    child->parentNode = NULL;
    child->previousSibling = NULL;
    MutationEvent3(DOMNodeRemoved, child, node);
    MutationEvent2(DOMSubtreeModified, node);
    return OK;
}


/*
 *----------------------------------------------------------------------
 *
 * domAppendChild --
 *
 *      This procedure implements the dom method appendChild.  Adds the
 *      node newChild to the end of the list of children of this
 *      node. If the newChild is already in the tree, it is first
 *      removed.
 *
 * Results:
 *	Returns a domException:
 *
 *      HIERARCHY_REQUEST_ERR: Raised if node is of a type that does
 *      not allow children of the type of the childToAppend node, or
 *      if the node to append is one of node's ancestors or the
 *      rootNode of node's document.
 *
 *      NOT_SUPPORTED_ERR: Raised if the childToInsert is the rootNode
 *      of another document or if node is a rootNode.
 *
 *      OK: otherwise
 *
 * Side effects:
 *	Alters the involved document(s).
 *
 *----------------------------------------------------------------------
 */

domException
domAppendChild (
    domNode *node,
    domNode *childToAppend
)
{
    domNode *n;

    if (node->nodeType != ELEMENT_NODE) {
        return HIERARCHY_REQUEST_ERR;
    }

    /* check, whether childToAppend is node or one of node's ancestors */
    n = node;
    while (n) {
        if (n == childToAppend) {
            return HIERARCHY_REQUEST_ERR;
        }
        n = n->parentNode;
    }

    if (childToAppend == childToAppend->ownerDocument->rootNode) {
        if (childToAppend == node->ownerDocument->rootNode) {
            return HIERARCHY_REQUEST_ERR;
        } else {
            return NOT_SUPPORTED_ERR;
        }
    }

    /* unlink childToAppend */
    if (childToAppend->previousSibling) {
        childToAppend->previousSibling->nextSibling = 
            childToAppend->nextSibling;
    } else {
        if (childToAppend->parentNode) {
            childToAppend->parentNode->firstChild = childToAppend->nextSibling;
        } else {
            /* childToAppend is either out of the fragment list or
               a child of the rootNode of its document */
            if (childToAppend->ownerDocument->fragments == childToAppend) {
                childToAppend->ownerDocument->fragments = 
                    childToAppend->nextSibling;
            } else {
                childToAppend->ownerDocument->rootNode->firstChild =
                    childToAppend->nextSibling;
            }
        }
    }
    if (childToAppend->nextSibling) {
        childToAppend->nextSibling->previousSibling =
            childToAppend->previousSibling;
    } else {
        if (childToAppend->parentNode) {
            childToAppend->parentNode->lastChild = 
                childToAppend->previousSibling;
        } else {
            if (childToAppend->ownerDocument->rootNode->lastChild
                == childToAppend) {
                childToAppend->ownerDocument->rootNode->lastChild =
                    childToAppend->previousSibling;
            }
        }
    }

    if (node->lastChild) {
        node->lastChild->nextSibling = childToAppend;
        childToAppend->previousSibling = node->lastChild;
    } else {
        node->firstChild = childToAppend;
        childToAppend->previousSibling = NULL;
    }
    node->lastChild = childToAppend;
    childToAppend->nextSibling = NULL;
    if (!childToAppend->parentNode &&
        (childToAppend->ownerDocument->documentElement == childToAppend)) {
        childToAppend->ownerDocument->documentElement =
            childToAppend->ownerDocument->rootNode->firstChild;
    }
    if (node == node->ownerDocument->rootNode) {
        childToAppend->parentNode = NULL;
    } else {
        childToAppend->parentNode = node;
    }

    if ((node->ownerDocument != childToAppend->ownerDocument)
        || node->ownerDocument->nsptr
        || childToAppend->ownerDocument->baseURIs->numEntries) {
        domSetDocument (childToAppend, node->ownerDocument);
    }
    node->ownerDocument->nodeFlags |= NEEDS_RENUMBERING;
    MutationEvent();
    return OK;
}


/*
 *----------------------------------------------------------------------
 *
 * domInsertBefore --
 *
 *	This procedure implements the dom method insertBefore.
 *      It inserts the node childToInsert before the existing child
 *      node referenceChild. If referenceChild is null, insert
 *      childToInsert at the end of the list of children of node. The
 *      arguments node and childToInsert must be non NULL. The
 *      childToInsert is unlinked from its previous place (fragment
 *      list or tree).
 *
 * Results:
 *	Returns a domException:
 *
 *      HIERARCHY_REQUEST_ERR: Raised if node is of a type that does
 *      not allow children of the type of the childToInsert node, or
 *      if the node to insert is node or one of node's ancestors or the
 *      rootNode of node's document.
 *
 *      NOT_FOUND_ERR: Raised if refChild is not a child of this node.
 *
 *      NOT_SUPPORTED_ERR: Raised if the childToInsert is the rootNode
 *      of another document or if node is a rootNode.
 *
 *      OK: otherwise
 *
 * Side effects:
 *	Alters the involved document(s).
 *
 *----------------------------------------------------------------------
 */

domException
domInsertBefore (
    domNode *node,
    domNode *childToInsert,
    domNode *referenceChild
)
{
    domNode *n;


    if (node->nodeType != ELEMENT_NODE) {
        return HIERARCHY_REQUEST_ERR;
    }

    /* check, if node is in deed the parent of referenceChild */
    if (referenceChild) {
        if (referenceChild->parentNode != node) {
            /* If node is the root node of a document and referenceChild
               is in deed a child of this node, then 
               referenceChild->parentNode will be NULL. In this case, we
               loop throu the childs of node, to see, if the referenceChild
               is valid. */
            if (node->ownerDocument->rootNode == node) {
                n = node->firstChild;
                while (n) {
                    if (n == referenceChild) {
                        /* referenceChild is in deed a child of node */
                        break;
                    }
                    n = n->nextSibling;
                }
                if (!n) {
                    return NOT_FOUND_ERR;
                }
            } else {
                return NOT_FOUND_ERR;
            }
        }
    }
    
    if (childToInsert == referenceChild) {
        return OK;
    }

    /* check, whether childToInsert is one of node's ancestors */
    n = node;
    while (n) {
        if (n == childToInsert) {
            return HIERARCHY_REQUEST_ERR;
        }
        n = n->parentNode;
    }

    if (childToInsert == childToInsert->ownerDocument->rootNode) {
        if (childToInsert == node->ownerDocument->rootNode) {
            return HIERARCHY_REQUEST_ERR;
        } else {
            /* For now, we simply don't allow the rootNode of
               another element as childToInsert. The way to go may
               be simply to treat the rootNode as DocumentFragment
               and to insert all childs of that rootNode before the
               referenceChild.  This would result in a document
               without documentElement, which then should be
               handled right by other methods. This is planed, but
               not carefully considered, yet.  */
            return NOT_SUPPORTED_ERR;
        }
    }


    /* unlink childToInsert */
    if (childToInsert->previousSibling) {
        childToInsert->previousSibling->nextSibling = 
            childToInsert->nextSibling;
    } else {
        if (childToInsert->parentNode) {
            childToInsert->parentNode->firstChild = childToInsert->nextSibling;
        } else {
            /* childToInsert is either out of the fragment list or
               a child of the rootNode of its document */
            if (childToInsert->ownerDocument->fragments == childToInsert) {
                childToInsert->ownerDocument->fragments = 
                    childToInsert->nextSibling;
            } else {
                childToInsert->ownerDocument->rootNode->firstChild =
                    childToInsert->nextSibling;
            }
        }
    }
    if (childToInsert->nextSibling) {
        childToInsert->nextSibling->previousSibling =
            childToInsert->previousSibling;
    } else {
        if (childToInsert->parentNode) {
            childToInsert->parentNode->lastChild = 
                childToInsert->previousSibling;
        } else {
            if (childToInsert->ownerDocument->rootNode->lastChild
                == childToInsert) {
                childToInsert->ownerDocument->rootNode->lastChild =
                    childToInsert->previousSibling;
            }
        }
    }

    childToInsert->nextSibling = referenceChild;
    if (referenceChild) {
        if (referenceChild->previousSibling) {
            childToInsert->previousSibling = referenceChild->previousSibling;
            referenceChild->previousSibling->nextSibling = childToInsert;
        } else {
            node->firstChild = childToInsert;
            childToInsert->previousSibling = NULL;
        }
        referenceChild->previousSibling = childToInsert;
    } else {
        if (node->lastChild) {
            node->lastChild->nextSibling = childToInsert;
            childToInsert->previousSibling = node->lastChild;
        } else {
            node->firstChild = childToInsert;
            childToInsert->previousSibling = NULL;
        }
        node->lastChild = childToInsert;
    }
    if (!childToInsert->parentNode &&
        (childToInsert->ownerDocument->documentElement == childToInsert)) {
        childToInsert->ownerDocument->documentElement =
            childToInsert->ownerDocument->rootNode->firstChild;
    }
    if (node == node->ownerDocument->rootNode) {
        childToInsert->parentNode = NULL;
    } else {
        childToInsert->parentNode = node;
    }
    if (node->ownerDocument != childToInsert->ownerDocument
        || node->ownerDocument->nsptr
        || childToInsert->ownerDocument->baseURIs->numEntries) {
        domSetDocument (childToInsert, node->ownerDocument);
    }
    node->ownerDocument->nodeFlags |= NEEDS_RENUMBERING;
    MutationEvent3(DOMNodeInsert, childToInsert, node);
    MutationEvent2(DOMSubtreeModified, node);
    return OK;
}



/*
 *----------------------------------------------------------------------
 *
 * domReplaceChild --
 *
 *	This procedure implements the dom method replaceChild.
 *      Replaces the child node oldChild with newChild in the list of
 *      children of node 'node'.
 *
 * Results:
 *	Returns a domException:
 *
 *      HIERARCHY_REQUEST_ERR: Raised if node is of a type that does
 *      not allow children of the type of the newChild node, or
 *      if newChild is node or one of node's ancestors or the
 *      rootNode of node's document.
 *
 *      NOT_FOUND_ERR: Raised if oldChild is not a child of node.
 *
 *      NOT_SUPPORTED_ERR: Raised if the newChild is the rootNode
 *      of another document.
 *
 *      OK: otherwise
 *
 * Side effects:
 *	Alters the involved document(s).
 *
 *----------------------------------------------------------------------
 */

domException
domReplaceChild (
    domNode *node,
    domNode *newChild,
    domNode *oldChild
)
{
    domNode *n;


    if (node->nodeType != ELEMENT_NODE) {
        return HIERARCHY_REQUEST_ERR;
    }

    /* check, if node is in deed the parent of oldChild */
    if (oldChild->parentNode != node) {
        /* If node is the root node of a document and oldChild
           is in deed a child of this node, then 
           oldChild->parentNode will be NULL. In this case, we
           loop throu the childs of node, to see, if the oldChild
           is valid. */
        if (node->ownerDocument->rootNode == node) {
            n = node->firstChild;
            while (n) {
                if (n == oldChild) {
                    /* oldChild is in deed a child of node */
                    break;
                }
                n = n->nextSibling;
            }
            if (!n) {
                return NOT_FOUND_ERR;
            }
        } else {
            return NOT_FOUND_ERR;
        }
    }
    
    if (oldChild == newChild) {
        return OK;
    }
    
    /* check, whether newChild is node or one of node's ancestors */
    n = node;
    while (n) {
        if (n == newChild) {
            return HIERARCHY_REQUEST_ERR;
        }
        n = n->parentNode;
    }

    if (newChild == newChild->ownerDocument->rootNode) {
        if (newChild == node->ownerDocument->rootNode) {
            return HIERARCHY_REQUEST_ERR;
        } else {
            return NOT_SUPPORTED_ERR;
        }
    }

    /* unlink newChild */
    if (newChild->previousSibling) {
        newChild->previousSibling->nextSibling = newChild->nextSibling;
    } else {
        if (newChild->parentNode) {
            newChild->parentNode->firstChild = newChild->nextSibling;
        } else {
            /* newChild is either out of the fragment list or
               a child of the rootNode of its document */
            if (newChild->ownerDocument->fragments == newChild) {
                newChild->ownerDocument->fragments = newChild->nextSibling;
            } else {
                newChild->ownerDocument->rootNode->firstChild =
                    newChild->nextSibling;
            }
        }
    }
    if (newChild->nextSibling) {
        newChild->nextSibling->previousSibling = newChild->previousSibling;
    } else {
        if (newChild->parentNode) {
            newChild->parentNode->lastChild = newChild->previousSibling;
        } else {
            if (newChild->ownerDocument->rootNode->lastChild == newChild) {
                newChild->ownerDocument->rootNode->lastChild =
                    newChild->previousSibling;
            }
        }
    }

    newChild->nextSibling     = oldChild->nextSibling;
    newChild->previousSibling = oldChild->previousSibling;
    if (!newChild->parentNode &&
        (newChild->ownerDocument->documentElement == newChild)) {
        newChild->ownerDocument->documentElement =
            newChild->ownerDocument->rootNode->firstChild;
    }
    if (node == node->ownerDocument->rootNode) {
        newChild->parentNode  = NULL;
    } else {
        newChild->parentNode  = node;
    }
    if (oldChild->previousSibling) {
        oldChild->previousSibling->nextSibling = newChild;
    } else {
        node->firstChild = newChild;
    }
    if (oldChild->nextSibling) {
        oldChild->nextSibling->previousSibling = newChild;
    } else {
        node->lastChild = newChild;
    }

    if (node->ownerDocument != newChild->ownerDocument
        || node->ownerDocument->nsptr
        || newChild->ownerDocument->baseURIs->numEntries) {
        domSetDocument (newChild, node->ownerDocument);
    }

    /* add old child into his fragment list */
    if (oldChild->ownerDocument->fragments) {
        oldChild->nextSibling = oldChild->ownerDocument->fragments;
        oldChild->ownerDocument->fragments->previousSibling = oldChild;
        oldChild->ownerDocument->fragments = oldChild;
    } else {
        oldChild->ownerDocument->fragments = oldChild;
        oldChild->nextSibling = oldChild->previousSibling = NULL;
    }
    oldChild->parentNode = NULL;
    node->ownerDocument->nodeFlags |= NEEDS_RENUMBERING;
    MutationEvent();
    return OK;
}


/*---------------------------------------------------------------------------
|   domNewTextNode
|
\--------------------------------------------------------------------------*/
domTextNode *
domNewTextNode(
    domDocument *doc,
    const char  *value,
    int          length,
    domNodeType  nodeType	
)
{
    domTextNode   *node;

    node = (domTextNode*) domAlloc(sizeof(domTextNode));
    memset(node, 0, sizeof(domTextNode));
    node->nodeType      = nodeType;
    node->nodeFlags     = 0;
    node->nodeNumber    = NODE_NO(doc);
    node->ownerDocument = doc;
    node->valueLength   = length;
    node->nodeValue     = (char*)MALLOC(length);
    memmove(node->nodeValue, value, length);

    if (doc->fragments) {
        node->nextSibling = doc->fragments;
        doc->fragments->previousSibling = (domNode*)node;
        doc->fragments = (domNode*)node;
    } else {
        doc->fragments = (domNode*)node;

    }
    return node;
}



void
domEscapeCData (
    char        *value,
    int          length,
    Tcl_DString *escapedData
)
{
    int i, start = 0;
    char *pc;

    Tcl_DStringInit (escapedData);
    pc = value;
    for (i = 0; i < length; i++) {
        if (*pc == '&') {
            Tcl_DStringAppend (escapedData, &value[start], i - start);
            Tcl_DStringAppend (escapedData, "&amp;", 5);
            start = i+1;
        } else
        if (*pc == '<') {
            Tcl_DStringAppend (escapedData, &value[start], i - start);
            Tcl_DStringAppend (escapedData, "&lt;", 4);
            start = i+1;
        } else
        if (*pc == '>') {
            Tcl_DStringAppend (escapedData, &value[start], i - start);
            Tcl_DStringAppend (escapedData, "&gt;", 4);
            start = i+1;
        } 
        pc++;
    }
    if (start) {
        Tcl_DStringAppend (escapedData, &value[start], length - start);
    }
}


/*---------------------------------------------------------------------------
|   domAppendNewTextNode
|
\--------------------------------------------------------------------------*/
domTextNode *
domAppendNewTextNode(
    domNode     *parent,
    char        *value,
    int          length,
    domNodeType  nodeType,
    int          disableOutputEscaping
)
{
    domTextNode   *node;

    if (!length) {
        return NULL;
    }

    if (parent->lastChild
         && parent->lastChild->nodeType == TEXT_NODE
         && nodeType == TEXT_NODE
    ) {
        /*------------------------------------------------------------------
        |    append to already existing text node
        \-----------------------------------------------------------------*/
        domAppendData ((domTextNode *) (parent->lastChild), value, length,
                       disableOutputEscaping);
        MutationEvent();
        return (domTextNode*)parent->lastChild;
    }
    node = (domTextNode*) domAlloc(sizeof(domTextNode));
    memset(node, 0, sizeof(domTextNode));
    node->nodeType      = nodeType;
    node->nodeFlags     = 0;
    if (disableOutputEscaping) {
        node->nodeFlags |= DISABLE_OUTPUT_ESCAPING;
    }
    node->nodeNumber    = NODE_NO(parent->ownerDocument);
    node->ownerDocument = parent->ownerDocument;
    node->valueLength   = length;
    node->nodeValue     = (char*)MALLOC(length);
    memmove(node->nodeValue, value, length);

    if (parent->lastChild) {
        parent->lastChild->nextSibling = (domNode*)node;
        node->previousSibling          = parent->lastChild;
    } else {
        parent->firstChild    = (domNode*)node;
        node->previousSibling = NULL;
    }
    parent->lastChild = (domNode*)node;
    node->nextSibling = NULL;
    if (parent != parent->ownerDocument->rootNode) {
        node->parentNode  = parent;
    }

    MutationEvent();
    return node;
}


/*---------------------------------------------------------------------------
|   domAppendNewElementNode
|
\--------------------------------------------------------------------------*/
domNode *
domAppendNewElementNode(
    domNode     *parent,
    const char  *tagName,
    const char  *uri
)
{
    Tcl_HashEntry *h;
    domNode       *node;
    domNS         *ns;
    domAttrNode   *NSattr;
    int            hnew;
    char           prefix[MAX_PREFIX_LEN];
    const char    *localname;
    Tcl_DString    dStr;

    if (parent == NULL) { 
        DBG(fprintf(stderr, "dom.c: Error parent == NULL!\n");)
        return NULL;
    }

    h = Tcl_CreateHashEntry(&HASHTAB(parent->ownerDocument,tdom_tagNames),
                            tagName, &hnew);
    node = (domNode*) domAlloc(sizeof(domNode));
    memset(node, 0, sizeof(domNode));
    node->nodeType      = ELEMENT_NODE;
    node->nodeNumber    = NODE_NO(parent->ownerDocument);
    node->ownerDocument = parent->ownerDocument;
    node->nodeName      = (char *)&(h->key);

    if (parent->lastChild) {
        parent->lastChild->nextSibling = node;
        node->previousSibling          = parent->lastChild;
    } else {
        parent->firstChild    = node;
        node->previousSibling = NULL;
    }
    parent->lastChild = node;
    node->nextSibling = NULL;
    if (parent != parent->ownerDocument->rootNode) {
        node->parentNode  = parent;
    }

    /*--------------------------------------------------------
    |   re-use existing namespace or create a new one
    \-------------------------------------------------------*/
    if (uri) {
        domSplitQName (tagName, prefix, &localname);
        DBG(fprintf(stderr, "tag '%s' has prefix='%s' \n", tagName, prefix);)
        ns = domLookupPrefix (node, prefix);
        if (!ns || (strcmp (uri, ns->uri)!=0)) {
            ns = domNewNamespace(node->ownerDocument, prefix, uri);
            if (prefix[0] == '\0') {
                domSetAttributeNS (node, "xmlns", uri, NULL, 1);
            } else {
                Tcl_DStringInit (&dStr);
                Tcl_DStringAppend (&dStr, "xmlns:", 6);
                Tcl_DStringAppend (&dStr, prefix, -1);
                domSetAttributeNS (node, Tcl_DStringValue (&dStr), uri, NULL,
                                   1);
            }
        }
        node->namespace = ns->index;
    } else {
        ns = domLookupPrefix (node, "");
        if (ns) {
            if (strcmp (ns->uri, "")!=0) {
                NSattr = domSetAttributeNS (node, "xmlns", "", NULL, 1);
                if (NSattr) {
                    node->namespace = NSattr->namespace;
                }
            } else {
                node->namespace = ns->index;
            }
        }
    }
    MutationEvent();
    return node;
}


/*
 *----------------------------------------------------------------------
 *
 * domAppendData --
 *
 *      This procedure implements the dom method appendData. It is
 *      also used by domNormalize and domAppendNewTextNode.
 *
 * Results:
 *	A domException; currently always OK.
 *
 * Side effects:
 *	Appends the data to node.
 *
 *----------------------------------------------------------------------
 */

domException
domAppendData (
    domTextNode *node,          /* The node, to append value to. Must be
                                   a TEXT_NODE, COMMENT_NODE or 
                                   CDATA_SECTION_NODE*/
    char        *value,         /* The data to append */ 
    int          length,        /* The length of value in byte */
    int          disableOutputEscaping   /* If true, disable output 
                                            escaping on the node */
    )
{
    Tcl_DString    escData;

    if (node->nodeFlags & DISABLE_OUTPUT_ESCAPING) {
        if (disableOutputEscaping) {
            node->nodeValue = REALLOC (node->nodeValue,
                                        node->valueLength + length);
            memmove (node->nodeValue + node->valueLength, value, length);
            node->valueLength += length;
        } else {
            domEscapeCData (value, length, &escData);
            if (Tcl_DStringLength (&escData)) {
                node->nodeValue = REALLOC (node->nodeValue,
                                            node->valueLength +
                                            Tcl_DStringLength (&escData));
                memmove (node->nodeValue + node->valueLength,
                         Tcl_DStringValue (&escData),
                         Tcl_DStringLength (&escData));
                node->valueLength += Tcl_DStringLength (&escData);
            } else {
                node->nodeValue = REALLOC (node->nodeValue,
                                            node->valueLength+length);
                memmove (node->nodeValue + node->valueLength,
                         value, length);
                node->valueLength += length;
            }
            Tcl_DStringFree (&escData);
        }
    } else {
        if (disableOutputEscaping) {
            node->nodeFlags |= DISABLE_OUTPUT_ESCAPING;
            domEscapeCData (node->nodeValue, node->valueLength,
                            &escData);
            if (Tcl_DStringLength (&escData)) {
                FREE (node->nodeValue);
                node->nodeValue =
                    MALLOC (Tcl_DStringLength (&escData) + length);
                memmove (node->nodeValue, Tcl_DStringValue (&escData),
                         Tcl_DStringLength (&escData));
                node->valueLength = Tcl_DStringLength (&escData);
            } else {
                node->nodeValue = REALLOC (node->nodeValue,
                                            node->valueLength+length);
            }
            Tcl_DStringFree (&escData);
        } else {
            node->nodeValue = REALLOC (node->nodeValue,
                                        node->valueLength + length);
        }
        memmove (node->nodeValue + node->valueLength, value, length);
        node->valueLength += length;
    }

    return OK;
}


/*
 *----------------------------------------------------------------------
 *
 * domNormalize --
 *
 *      This procedure implements the dom method normalize. Puts all
 *      Text nodes in the full depth of the sub-tree underneath node,
 *      including attribute nodes, into a "normal" form where only
 *      structure (e.g., elements, comments, processing instructions,
 *      CDATA sections, and entity references) separates Text nodes,
 *      i.e., there are neither adjacent Text nodes nor empty Text
 *      nodes. If the flag forXPath is true, then CDATASection nodes
 *      are treated as if they are text nodes (and merged with
 *      circumjacent text nodes). Node must be an ELEMENT_NODE.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May alter the tree.
 *
 *----------------------------------------------------------------------
 */

void
domNormalize (
    domNode         *node,      /* root of the sub-tree to normalize */
    int              forXPath,  /* if true, treat CDATA_SECTION_NODEs as if
                                   they where TEXT_NODEs */
    domFreeCallback  freeCB,    /* Function to call, if a node must be freed */
    void            *clientData /* ClientData, to provide to the freeCB */
    
    )
{
    domNode     *child, *nextChild;
    int          merge = 0;
    
    if (node->nodeType != ELEMENT_NODE) return;
    
    child = node->firstChild;
    while (child) {
        merge = 0;
        switch (child->nodeType) {
        case ELEMENT_NODE:
            domNormalize (child, forXPath, freeCB, clientData);
            break;
        case TEXT_NODE:
            if (child->previousSibling 
                && child->previousSibling->nodeType == TEXT_NODE) {
                merge = 1;
            } else {
                if (((domTextNode *)child)->valueLength == 0) {
                    nextChild = child->nextSibling;
                    domDeleteNode (child, freeCB, clientData);
                    child = nextChild;
                    continue;
                }
            }
            break;
        case CDATA_SECTION_NODE:
            if (forXPath) {
                if (child->previousSibling
                    && child->previousSibling->nodeType == TEXT_NODE) {
                    merge = 1;
                } else {
                    if (((domTextNode *)child)->valueLength == 0) {
                        nextChild = child->nextSibling;
                        domDeleteNode (child, freeCB, clientData);
                        child = nextChild;
                        continue;
                    }
                    child->nodeType = TEXT_NODE;
                }
            }
            break;
        default:
            break;
        }
        if (merge) {
            domAppendData ( (domTextNode *)(child->previousSibling),
                            ((domTextNode *)child)->nodeValue,
                            ((domTextNode *)child)->valueLength,
                            (child->nodeFlags & DISABLE_OUTPUT_ESCAPING) );
            nextChild = child->nextSibling;
            domDeleteNode (child, freeCB, clientData);
            child = nextChild;
        } else {
            child = child->nextSibling;
        }
    }
}

/*---------------------------------------------------------------------------
|   domAddNSToNode
|
\--------------------------------------------------------------------------*/
domNS *
domAddNSToNode (
    domNode *node,
    domNS   *nsToAdd
    )
{
    domAttrNode   *attr, *lastNSAttr;
    domNS         *ns, noNS;
    Tcl_HashEntry *h;
    int            hnew;
    Tcl_DString    dStr;

    if (!nsToAdd) {
        noNS.uri    = "";
        noNS.prefix = "";
        noNS.index  = 0;
        nsToAdd = &noNS;
    }
    DBG(fprintf (stderr, "domAddNSToNode to node '%s': prefix: %s, uri: %s\n", node->nodeName, nsToAdd->prefix, nsToAdd->uri);)

    ns = domLookupPrefix (node, nsToAdd->prefix);
    if (ns) {
        if (strcmp (ns->uri, nsToAdd->uri)==0) {
            /* namespace already in scope, we're done. */
            return ns;
        }
    } else {
        /* If the NS to set was no NS and there isn't a default NS
           we're done */
        if (nsToAdd->prefix[0] == '\0' && nsToAdd->uri[0] == '\0') return NULL;
    }
    ns = domNewNamespace (node->ownerDocument, nsToAdd->prefix, nsToAdd->uri);
    Tcl_DStringInit (&dStr);
    if (nsToAdd->prefix[0] == '\0') {
        Tcl_DStringAppend (&dStr, "xmlns", 5);
    } else {
        Tcl_DStringAppend (&dStr, "xmlns:", 6);
        Tcl_DStringAppend (&dStr, nsToAdd->prefix, -1);
    }
    /* Add new namespace attribute */
    attr = (domAttrNode*) domAlloc(sizeof(domAttrNode));
    memset(attr, 0, sizeof(domAttrNode));
    h = Tcl_CreateHashEntry(&HASHTAB(node->ownerDocument,tdom_attrNames),
                            Tcl_DStringValue(&dStr), &hnew);
    attr->nodeType    = ATTRIBUTE_NODE;
    attr->nodeFlags   = IS_NS_NODE;
    attr->namespace   = ns->index;
    attr->nodeName    = (char *)&(h->key);
    attr->parentNode  = node;
    attr->valueLength = strlen(nsToAdd->uri);
    attr->nodeValue   = (char*)MALLOC(attr->valueLength+1);
    strcpy(attr->nodeValue, nsToAdd->uri);

    lastNSAttr = NULL;
    if (node->firstAttr && (node->firstAttr->nodeFlags & IS_NS_NODE)) {
        lastNSAttr = node->firstAttr;
        while (lastNSAttr->nextSibling
               && (lastNSAttr->nextSibling->nodeFlags & IS_NS_NODE)) {
            lastNSAttr = lastNSAttr->nextSibling;
        }
    }
    if (lastNSAttr) {
        attr->nextSibling = lastNSAttr->nextSibling;
        lastNSAttr->nextSibling = attr;
    } else {
        attr->nextSibling = node->firstAttr;
        node->firstAttr = attr;
    }
    Tcl_DStringFree (&dStr);
    return ns;
}

/*---------------------------------------------------------------------------
|   domAppendLiteralNode
|
\--------------------------------------------------------------------------*/
domNode *
domAppendLiteralNode(
    domNode     *parent,
    domNode     *literalNode
)
{
    Tcl_HashEntry *h;
    domNode       *node;
    int            hnew;

    if (parent == NULL) { 
        DBG(fprintf(stderr, "dom.c: Error parent == NULL!\n");)
        return NULL;
    }

    h = Tcl_CreateHashEntry(&HASHTAB(parent->ownerDocument, tdom_tagNames),
                             literalNode->nodeName, &hnew);
    node = (domNode*) domAlloc(sizeof(domNode));
    memset(node, 0, sizeof(domNode));
    node->nodeType      = ELEMENT_NODE;
    node->nodeNumber    = NODE_NO(parent->ownerDocument);
    node->ownerDocument = parent->ownerDocument;
    node->nodeName      = (char *)&(h->key);

    if (parent->lastChild) {
        parent->lastChild->nextSibling = node;
        node->previousSibling          = parent->lastChild;
    } else {
        parent->firstChild    = node;
        node->previousSibling = NULL;
    }
    parent->lastChild = node;
    node->nextSibling = NULL;
    if (parent != parent->ownerDocument->rootNode) {
        node->parentNode  = parent;
    }

    MutationEvent();
    return node;
}


/*---------------------------------------------------------------------------
|   domNewProcessingInstructionNode
|
\--------------------------------------------------------------------------*/
domProcessingInstructionNode *
domNewProcessingInstructionNode(
    domDocument *doc,
    const char  *targetValue,
    int          targetLength,
    const char  *dataValue,
    int          dataLength
)
{
    domProcessingInstructionNode   *node;

    node = (domProcessingInstructionNode*) domAlloc(sizeof(domProcessingInstructionNode));
    memset(node, 0, sizeof(domProcessingInstructionNode));
    node->nodeType      = PROCESSING_INSTRUCTION_NODE;
    node->nodeFlags     = 0;
    node->namespace     = 0;
    node->nodeNumber    = NODE_NO(doc);
    node->ownerDocument = doc;
    node->targetLength  = targetLength;
    node->targetValue   = (char*)MALLOC(targetLength);
    memmove(node->targetValue, targetValue, targetLength);

    node->dataLength    = dataLength;
    node->dataValue     = (char*)MALLOC(dataLength);
    memmove(node->dataValue, dataValue, dataLength);

    if (doc->fragments) {
        node->nextSibling = doc->fragments;
        doc->fragments->previousSibling = (domNode*)node;
        doc->fragments = (domNode*)node;
    } else {
        doc->fragments = (domNode*)node;

    }
    MutationEvent();
    return node;
}


/*---------------------------------------------------------------------------
|   domNewElementNode
|
\--------------------------------------------------------------------------*/
domNode *
domNewElementNode(
    domDocument *doc,
    const char  *tagName
)
{
    domNode       *node;
    Tcl_HashEntry *h;
    int           hnew;

    h = Tcl_CreateHashEntry(&HASHTAB(doc, tdom_tagNames), tagName, &hnew);
    node = (domNode*) domAlloc(sizeof(domNode));
    memset(node, 0, sizeof(domNode));
    node->nodeType      = ELEMENT_NODE;
    node->nodeFlags     = 0;
    node->namespace     = 0;
    node->nodeNumber    = NODE_NO(doc);
    node->ownerDocument = doc;
    node->nodeName      = (char *)&(h->key);

    if (doc->fragments) {
        node->nextSibling = doc->fragments;
        doc->fragments->previousSibling = node;
        doc->fragments = node;
    } else {
        doc->fragments = node;

    }
    return node;
}


/*---------------------------------------------------------------------------
|   domNewElementNodeNS
|
\--------------------------------------------------------------------------*/
domNode *
domNewElementNodeNS (
    domDocument *doc,
    const char  *tagName,
    const char  *uri
)
{
    domNode       *node;
    Tcl_HashEntry *h;
    int            hnew;
    char           prefix[MAX_PREFIX_LEN];
    const char    *localname;
    domNS         *ns;

    domSplitQName (tagName, prefix, &localname);
    if (prefix[0] == '\0' && uri[0] == '\0') {
        return NULL;
    }

    h = Tcl_CreateHashEntry(&HASHTAB(doc, tdom_tagNames), tagName, &hnew);
    node = (domNode*) domAlloc(sizeof(domNode));
    memset(node, 0, sizeof(domNode));
    node->nodeType      = ELEMENT_NODE;
    node->nodeFlags     = 0;
    node->namespace     = 0;
    node->nodeNumber    = NODE_NO(doc);
    node->ownerDocument = doc;
    node->nodeName      = (char *)&(h->key);

    ns = domNewNamespace(doc, prefix, uri);
    node->namespace = ns->index;

    if (doc->fragments) {
        node->nextSibling = doc->fragments;
        doc->fragments->previousSibling = node;
        doc->fragments = node;
    } else {
        doc->fragments = node;

    }
    return node;
}

/*---------------------------------------------------------------------------
|   domCloneNode
|
\--------------------------------------------------------------------------*/
domNode *
domCloneNode (
    domNode *node,
    int      deep
)
{
    domAttrNode *attr, *nattr;
    domNode     *n, *child, *newChild;

    /*------------------------------------------------------------------
    |   create new node
    \-----------------------------------------------------------------*/
    if (node->nodeType == PROCESSING_INSTRUCTION_NODE) {
        domProcessingInstructionNode *pinode = (domProcessingInstructionNode*)node;
        return (domNode*) domNewProcessingInstructionNode(
                                         pinode->ownerDocument,
                                         pinode->targetValue,
                                         pinode->targetLength,
                                         pinode->dataValue,
                                         pinode->dataLength);
    }
    if (node->nodeType != ELEMENT_NODE) {
        domTextNode *t1node, *tnode = (domTextNode*)node;
        if (tnode->info) {
            t1node = domNewTextNode(tnode->ownerDocument,
                                    tnode->nodeValue, tnode->valueLength,
                                    tnode->nodeType);
            t1node->info = tnode->info;
            return (domNode*) t1node;
        } else {
            return (domNode*) domNewTextNode(tnode->ownerDocument,
                                             tnode->nodeValue, tnode->valueLength,
                                             tnode->nodeType);
        }
    }

    n = domNewElementNode(node->ownerDocument, node->nodeName);
    n->namespace = node->namespace;
    n->info = node->info;

    /*------------------------------------------------------------------
    |   copy attributes (if any)
    \-----------------------------------------------------------------*/
    attr = node->firstAttr;
    while (attr != NULL) {
        nattr = domSetAttribute (n, attr->nodeName, attr->nodeValue );
        nattr->namespace = attr->namespace;
        if (attr->nodeFlags & IS_NS_NODE) {
            nattr->nodeFlags |= IS_NS_NODE;
        }
        attr = attr->nextSibling;
    }

    if (deep) {
        child = node->firstChild;
        while (child) {
            newChild = domCloneNode(child, deep);

            /* append new (cloned)child to cloned node, its new parent.
               Don't use domAppendChild for this, because that would
               mess around with the namespaces */
            if (n->ownerDocument->fragments->nextSibling) {
                n->ownerDocument->fragments = 
                    n->ownerDocument->fragments->nextSibling;
                n->ownerDocument->fragments->previousSibling = NULL;
                newChild->nextSibling = NULL;
            } else {
                n->ownerDocument->fragments = NULL;
            }
            if (n->firstChild) {
                newChild->previousSibling = n->lastChild;
                n->lastChild->nextSibling = newChild;
            } else {
                n->firstChild = newChild;
            }
            n->lastChild = newChild;
            newChild->parentNode = n;

            /* clone next child */
            child = child->nextSibling;
        }
    }
    return n;
}

/*----------------------------------------------------------------------------
|   domCopyNS
|
\---------------------------------------------------------------------------*/
void
domCopyNS (
    domNode *from,
    domNode *to
    )
{
    domNode     *n, *n1;
    domNS       *ns, *ns1;
    domAttrNode *attr, *attr1;
    int          skip;

    n = from;
    while (n) {
        attr = n->firstAttr;
        while (attr && (attr->nodeFlags & IS_NS_NODE)) {
            ns = n->ownerDocument->namespaces[attr->namespace-1];
            skip = 0;
            n1 = from;
            while (n1 != n) {
                attr1 = n1->firstAttr;
                while (attr1 && (attr1->nodeFlags & IS_NS_NODE)) {
                    ns1 = n1->ownerDocument->namespaces[attr1->namespace-1];
                    if ((ns1->prefix == NULL && ns->prefix == NULL) 
                         || (strcmp (ns1->prefix, ns->prefix)==0)) {
                        skip = 1;
                        break;
                    }
                    attr1 = attr1->nextSibling;
                }
                if (skip) break;
                n1 = n1->parentNode;
            }
            if (!skip) {
                /* Add this prefix/uri combination only to the
                   destination, if it isn't already in scope */
                ns1 = domLookupPrefix (to, ns->prefix);
                if (!ns1 || (strcmp (ns->uri, ns1->uri)!=0)) {
                    domAddNSToNode (to, ns);
                }
            }
            attr = attr->nextSibling;
        }
        n = n->parentNode;
    }
}


/*---------------------------------------------------------------------------
|   domCopyTo
|
\--------------------------------------------------------------------------*/
void
domCopyTo (
    domNode *node,
    domNode *parent,
    int      copyNS
)
{
    domAttrNode *attr, *nattr;
    domNode     *n, *child;
    domNS       *ns, *ns1;

    /*------------------------------------------------------------------
    |   create new node
    \-----------------------------------------------------------------*/
    if (node->nodeType == PROCESSING_INSTRUCTION_NODE) {
        domProcessingInstructionNode *pinode = (domProcessingInstructionNode*)node;
        n = (domNode*) domNewProcessingInstructionNode(
                                         parent->ownerDocument,
                                         pinode->targetValue,
                                         pinode->targetLength,
                                         pinode->dataValue,
                                         pinode->dataLength);
        domAppendChild (parent, n);
        return;
    }
    if (node->nodeType != ELEMENT_NODE) {
        domTextNode *tnode = (domTextNode*)node;
        n =  (domNode*) domNewTextNode(parent->ownerDocument,
                                         tnode->nodeValue, tnode->valueLength,
					 tnode->nodeType);
        domAppendChild (parent, n);
        return;
    }

    n = domAppendLiteralNode (parent, node);
    if (copyNS) {
        domCopyNS (node, n);
    }
    
    /*------------------------------------------------------------------
    |   copy attributes (if any)
    \-----------------------------------------------------------------*/
    attr = node->firstAttr;
    while (attr != NULL) {
        if (attr->nodeFlags & IS_NS_NODE) {
            if (copyNS) {
                /* If copyNS is true, then all namespaces in scope
                 * (including the one declared with the node to copy)
                 * are already copied over. */
                attr = attr->nextSibling;
                continue;
                
            }
            ns = node->ownerDocument->namespaces[attr->namespace-1];
            ns1 = domLookupPrefix (n, ns->prefix);
            if (ns1 && strcmp (ns->uri, ns1->uri)==0) {
                /* This namespace is already in scope, so we
                   don't copy the namespace attribute over */
                attr = attr->nextSibling;
                continue;
            }
            nattr = domSetAttribute (n, attr->nodeName, attr->nodeValue );
            nattr->nodeFlags = attr->nodeFlags;
            ns1 = domNewNamespace (n->ownerDocument, ns->prefix, ns->uri);
            nattr->namespace = ns1->index;
        } else {
            nattr = domSetAttribute (n, attr->nodeName, attr->nodeValue );
            nattr->nodeFlags = attr->nodeFlags;
            if (attr->namespace) {
                ns = node->ownerDocument->namespaces[attr->namespace-1];
                ns1 = domLookupPrefix (n, ns->prefix);
                if (ns1) {
                    nattr->namespace = ns1->index;
                }
            }
        }
        attr = attr->nextSibling;
    }

    /* We have to set the node namespace index after copying the
       attribute nodes over, because the node may be in a namespace,
       which is declared just at the node. */
    if (node->namespace) {
        ns = node->ownerDocument->namespaces[node->namespace-1];
        ns1 = domLookupPrefix (n, ns->prefix);
        n->namespace = ns1->index;
    }

    child = node->firstChild;
    while (child) {
        domCopyTo(child, n, 0);
        child = child->nextSibling;
    }
}


/*---------------------------------------------------------------------------
|   domXPointerChild
|
\--------------------------------------------------------------------------*/
int
domXPointerChild (
    domNode      * node,
    int            all,
    int            instance,
    domNodeType    type,
    char         * element,
    char         * attrName,
    char         * attrValue,
    int            attrLen,
    domAddCallback addCallback,
    void         * clientData
)
{
    domNode     *child;
    domAttrNode *attr;
    int          i=0, result;


    if (node->nodeType != ELEMENT_NODE) {
        return 0;
    }

    if (instance<0) {
        child = node->lastChild;
    } else {
        child = node->firstChild;
    }
    while (child) {
        if ((type == ALL_NODES) || (child->nodeType == type)) {
            if ((element == NULL) ||
                ((child->nodeType == ELEMENT_NODE) && (strcmp(child->nodeName,element)==0))
               ) {
                if (attrName == NULL) {
                    i = (instance<0) ? i-1 : i+1;
                    if (all || (i == instance)) {
                        result = addCallback (child, clientData);
                        if (result) {
                            return result;
                        }
                    }
                } else {
                    attr = child->firstAttr;
                    while (attr) {
                        if ((strcmp(attr->nodeName,attrName)==0) &&
                            ( (strcmp(attrValue,"*")==0) ||
                              ( (attr->valueLength == attrLen) &&
                               (strcmp(attr->nodeValue,attrValue)==0)
                              )
                            )
                           ) {
                            i = (instance<0) ? i-1 : i+1;
                            if (all || (i == instance)) {
                                result = addCallback (child, clientData);
                                if (result) {
                                    return result;
                                }
                            }
                        }
                        attr = attr->nextSibling;
                    }
                }
            }
        }
        if (instance<0) {
            child = child->previousSibling;
        } else {
            child = child->nextSibling;
        }
    }
    return 0;
}


/*---------------------------------------------------------------------------
|   domXPointerXSibling
|
\--------------------------------------------------------------------------*/
int
domXPointerXSibling (
    domNode      * node,
    int            forward_mode,
    int            all,
    int            instance,
    domNodeType    type,
    char         * element,
    char         * attrName,
    char         * attrValue,
    int            attrLen,
    domAddCallback addCallback,
    void         * clientData
)
{
    domNode     *sibling, *endSibling;
    domAttrNode *attr;
    int          i=0, result;


    if (forward_mode) {
        if (instance<0) {
            endSibling = node;
            sibling = node;
            if (node->parentNode) {
                sibling = node->parentNode->lastChild;
            }
        } else {
            sibling = node->nextSibling;
            endSibling = NULL;
        }
    } else {
        if (instance<0) {
            endSibling = node;
            sibling = node;
            if (node->parentNode) {
                sibling = node->parentNode->firstChild;
            }
        } else {
            sibling = node->previousSibling;
            endSibling = NULL;
        }
        instance = -1 * instance;
    }

    while (sibling != endSibling) {
        if ((type == ALL_NODES) || (sibling->nodeType == type)) {
            if ((element == NULL) ||
                ((sibling->nodeType == ELEMENT_NODE) && (strcmp(sibling->nodeName,element)==0))
               ) {
                if (attrName == NULL) {
                    i = (instance<0) ? i-1 : i+1;
                    if (all || (i == instance)) {
                        result = addCallback (sibling, clientData);
                        if (result) {
                            return result;
                        }
                    }
                } else {
                    attr = sibling->firstAttr;
                    while (attr) {
                        if ((strcmp(attr->nodeName,attrName)==0) &&
                            ( (strcmp(attrValue,"*")==0) ||
                              ( (attr->valueLength == attrLen) &&
                                (strcmp(attr->nodeValue,attrValue)==0)
                              )
                            )
                           ) {
                            i = (instance<0) ? i-1 : i+1;
                            if (all || (i == instance)) {
                                result = addCallback (sibling, clientData);
                                if (result) {
                                    return result;
                                }
                            }
                        }
                        attr = attr->nextSibling;
                    }
                }
            }
        }
        if (instance<0) {
            sibling = sibling->previousSibling;
        } else {
            sibling = sibling->nextSibling;
        }
    }
    return 0;
}


/*---------------------------------------------------------------------------
|   domXPointerDescendant
|
\--------------------------------------------------------------------------*/
int
domXPointerDescendant (
    domNode      * node,
    int            all,
    int            instance,
    int          * i,
    domNodeType    type,
    char         * element,
    char         * attrName,
    char         * attrValue,
    int            attrLen,
    domAddCallback addCallback,
    void         * clientData
)
{
    domNode     *child;
    domAttrNode *attr;
    int          found=0, result;


    if (node->nodeType != ELEMENT_NODE) {
        return 0;
    }

    if (instance<0) {
        child = node->lastChild;
    } else {
        child = node->firstChild;
    }
    while (child) {
        found = 0;
        if ((type == ALL_NODES) || (child->nodeType == type)) {
            if ((element == NULL) ||
                ((child->nodeType == ELEMENT_NODE) && (strcmp(child->nodeName,element)==0))
               ) {
                if (attrName == NULL) {
                    *i = (instance<0) ? (*i)-1 : (*i)+1;
                    if (all || (*i == instance)) {
                        result = addCallback (child, clientData);
                        if (result) {
                            return result;
                        }
                        found = 1;
                    }
                } else {
                    attr = child->firstAttr;
                    while (attr) {
                        if ((strcmp(attr->nodeName,attrName)==0) &&
                            ( (strcmp(attrValue,"*")==0) ||
                              ( (attr->valueLength == attrLen) &&
                               (strcmp(attr->nodeValue,attrValue)==0)
                              )
                            )
                           ) {
                            *i = (instance<0) ? (*i)-1 : (*i)+1;
                            if (all || (*i == instance)) {
                                result = addCallback (child, clientData);
                                if (result) {
                                    return result;
                                }
                                found = 1;
                            }
                        }
                        attr = attr->nextSibling;
                    }
                }
            }
        }
        if (!found) {
            /* recurs into childs */
            result = domXPointerDescendant (child, all, instance, i,
                                            type, element, attrName,
                                            attrValue, attrLen,
                                            addCallback, clientData);
            if (result) {
                return result;
            }
        }
        if (instance<0) {
            child = child->previousSibling;
        } else {
            child = child->nextSibling;
        }
    }
    return 0;
}


/*---------------------------------------------------------------------------
|   domXPointerAncestor
|
\--------------------------------------------------------------------------*/
int
domXPointerAncestor (
    domNode      * node,
    int            all,
    int            instance,
    int          * i,
    domNodeType    type,
    char         * element,
    char         * attrName,
    char         * attrValue,
    int            attrLen,
    domAddCallback addCallback,
    void         * clientData
)
{
    domNode     *ancestor;
    domAttrNode *attr;
    int          result;


    ancestor = node->parentNode;
    if (ancestor) {
        if ((type == ALL_NODES) || (ancestor->nodeType == type)) {
            if ((element == NULL) ||
                ((ancestor->nodeType == ELEMENT_NODE) && (strcmp(ancestor->nodeName,element)==0))
               ) {
                if (attrName == NULL) {
                    *i = (instance<0) ? (*i)-1 : (*i)+1;
                    if (all || (*i == instance)) {
                        result = addCallback (ancestor, clientData);
                        if (result) {
                            return result;
                        }
                    }
                } else {
                    attr = ancestor->firstAttr;
                    while (attr) {
                        if ((strcmp(attr->nodeName,attrName)==0) &&
                            ( (strcmp(attrValue,"*")==0) ||
                              ( (attr->valueLength == attrLen) &&
                               (strcmp(attr->nodeValue,attrValue)==0)
                              )
                            )
                           ) {
                            *i = (instance<0) ? (*i)-1 : (*i)+1;
                            if (all || (*i == instance)) {
                                result = addCallback (ancestor, clientData);
                                if (result) {
                                    return result;
                                }
                            }
                        }
                        attr = attr->nextSibling;
                    }
                }
            }
        }

        /* go up */
        result = domXPointerAncestor (ancestor, all, instance, i,
                                      type, element, attrName,
                                      attrValue, attrLen,
                                      addCallback, clientData);
        if (result) {
            return result;
        }
    }
    return 0;
}



/*---------------------------------------------------------------------------
|   type tdomCmdReadInfo
|
\--------------------------------------------------------------------------*/
typedef struct _tdomCmdReadInfo {

    XML_Parser        parser;
    domDocument      *document;
    domNode          *currentNode;
    int               depth;
    int               ignoreWhiteSpaces;
    int               cdataSection;
    Tcl_DString      *cdata;
    int               storeLineColumn;
    int               ignorexmlns;
    int               feedbackAfter;
    Tcl_Obj          *feedbackCmd;
    int               nextFeedbackPosition;
    Tcl_Interp       *interp;
    int               activeNSsize;
    int               activeNSpos;
    domActiveNS      *activeNS;
    int               baseURIstackSize;
    int               baseURIstackPos;
    domActiveBaseURI *baseURIstack;
    int               insideDTD;
    int               dtdvalidation;
    /* Now the tdom cmd specific elements */
    int               tdomStatus;
    Tcl_Obj          *extResolver;

} tdomCmdReadInfo;

EXTERN int tcldom_returnDocumentObj (Tcl_Interp *interp, 
                                     domDocument *document,
                                     int setVariable, Tcl_Obj *var_name,
                                     int trace, int forOwnerDocument);

void
tdom_freeProc (
    Tcl_Interp *interp,
    void       *userData
)
{
    tdomCmdReadInfo *info = (tdomCmdReadInfo *) userData;

    if (info->document) {
        domFreeDocument (info->document, NULL, NULL);
    }
    if (info->activeNS) {
        FREE (info->activeNS);
    }
    if (info->baseURIstack) {
        FREE (info->baseURIstack);
    }
        
    Tcl_DStringFree (info->cdata);
    FREE (info->cdata);
    if (info->extResolver) {
        Tcl_DecrRefCount (info->extResolver);
    }
    FREE (info);
}

void
tdom_parserResetProc (
    XML_Parser parser,
    void      *userData
)
{
    tdomCmdReadInfo *info = (tdomCmdReadInfo *) userData;

    info->parser = parser;
}

void
tdom_resetProc (
    Tcl_Interp *interp,
    void       *userData
)
{
    tdomCmdReadInfo *info = (tdomCmdReadInfo *) userData;

    if (!info->tdomStatus) return;

    if (info->document) {
        domFreeDocument (info->document, NULL, NULL);
    }

    info->document          = NULL;
    info->currentNode       = NULL;
    info->depth             = 0;
    info->feedbackAfter     = 0;
    info->ignorexmlns       = 0;
    Tcl_DStringSetLength (info->cdata, 0);
    info->nextFeedbackPosition = info->feedbackAfter;
    info->interp            = interp;
    info->activeNSpos       = -1;
    info->insideDTD         = 0;
    info->dtdvalidation     = 0;
    info->baseURIstackPos   = 0;
    info->tdomStatus        = 0;

}

void
tdom_initParseProc (
    Tcl_Interp *interp,
    void       *userData
    )
{
    tdomCmdReadInfo *info = (tdomCmdReadInfo *) userData;

    info->document   = domCreateDoc(XML_GetBase (info->parser), 
                                    info->storeLineColumn);
    if (info->extResolver) {
        info->document->extResolver = 
            tdomstrdup (Tcl_GetString (info->extResolver));
    }
    info->baseURIstack[0].baseURI = XML_GetBase (info->parser);
    info->baseURIstack[0].depth = 0;
    info->tdomStatus = 2;
    
}

static void
tdom_charDataHandler (
    void        *userData,
    const char  *s,
    int          len
)
{
    domReadInfo   *info = userData;

    Tcl_DStringAppend (info->cdata, s, len);
    DispatchPCDATA (info);
    return;
}

int
TclTdomObjCmd (dummy, interp, objc, objv)
     ClientData dummy;
     Tcl_Interp *interp;
     int objc;
     Tcl_Obj *const objv[];
{
    CHandlerSet     *handlerSet;
    int              methodIndex, result, bool;
    tdomCmdReadInfo *info;
    TclGenExpatInfo *expat;
    Tcl_Obj         *newObjName = NULL;

    static const char *tdomMethods[] = {
        "enable", "getdoc",
        "setStoreLineColumn",
        "setExternalEntityResolver", "keepEmpties",
        "remove", "ignorexmlns", "keepCDATA",
        NULL
    };
    enum tdomMethod {
        m_enable, m_getdoc,
        m_setStoreLineColumn,
        m_setExternalEntityResolver, m_keepEmpties,
        m_remove, m_ignorexmlns, m_keepCDATA
    };

    if (objc < 3 || objc > 4) {
        Tcl_WrongNumArgs (interp, 1, objv, tdom_usage);
        return TCL_ERROR;
    }

    if (!CheckExpatParserObj (interp, objv[1])) {
        Tcl_SetResult (interp, "First argument has to be a expat parser object", NULL);
        return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj (interp, objv[2], tdomMethods, "method", 0,
                             &methodIndex) != TCL_OK)
    {
        Tcl_SetResult (interp, tdom_usage, NULL);
        return TCL_ERROR;
    }

    switch ((enum tdomMethod) methodIndex) {

    default:
        Tcl_SetResult (interp, "unknown method", NULL);
        return TCL_ERROR;

    case m_enable:
        expat = GetExpatInfo (interp, objv[1]);
        if (expat->parsingState != 0) {
            Tcl_SetResult (interp, 
                           "Parser is not in init or reset state.", NULL);
            return TCL_ERROR;
        }

        handlerSet = CHandlerSetCreate ("tdom");
        handlerSet->ignoreWhiteCDATAs       = 1;
        handlerSet->resetProc               = tdom_resetProc;
        handlerSet->freeProc                = tdom_freeProc;
        handlerSet->parserResetProc         = tdom_parserResetProc;
        handlerSet->initParseProc           = tdom_initParseProc;
        handlerSet->elementstartcommand     = startElement;
        handlerSet->elementendcommand       = endElement;
        handlerSet->datacommand             = tdom_charDataHandler;
/*         handlerSet->datacommand             = characterDataHandler; */
        handlerSet->commentCommand          = commentHandler;
        handlerSet->picommand               = processingInstructionHandler;
        handlerSet->entityDeclCommand       = entityDeclHandler;
        handlerSet->startDoctypeDeclCommand = startDoctypeDeclHandler;
        handlerSet->endDoctypeDeclCommand   = endDoctypeDeclHandler;

        info = (tdomCmdReadInfo *) MALLOC (sizeof (tdomCmdReadInfo));
        info->parser            = expat->parser;
        info->document          = NULL;
        info->currentNode       = NULL;
        info->depth             = 0;
        info->ignoreWhiteSpaces = 1;
        info->cdataSection      = 0;
        info->cdata             = (Tcl_DString*) MALLOC (sizeof (Tcl_DString));
        Tcl_DStringInit (info->cdata);
        info->storeLineColumn   = 0;
        info->ignorexmlns       = 0;
        info->feedbackAfter     = 0;
        info->feedbackCmd       = NULL;
        info->nextFeedbackPosition = 0;
        info->interp            = interp;
        info->activeNSpos       = -1;
        info->activeNSsize      = 8;
        info->activeNS          = 
            (domActiveNS*) MALLOC(sizeof(domActiveNS) * info->activeNSsize);
        info->baseURIstackPos   = 0;
        info->baseURIstackSize  = INITIAL_BASEURISTACK_SIZE;
        info->baseURIstack      = (domActiveBaseURI*) 
            MALLOC (sizeof(domActiveBaseURI) * info->baseURIstackSize);
        info->insideDTD         = 0;
        info->dtdvalidation     = 0;
        info->tdomStatus        = 0;
        info->extResolver       = NULL;

        handlerSet->userData    = info;

        CHandlerSetInstall (interp, objv[1], handlerSet);
        break;
        
    case m_getdoc:
        info = CHandlerSetGetUserData (interp, objv[1], "tdom");
        if (!info) {
            Tcl_SetResult (interp, "parser object isn't tdom enabled.", NULL);
            return TCL_ERROR;
        }
        expat = GetExpatInfo (interp, objv[1]);
        if (info->tdomStatus != 2 || !expat->finished) {
            Tcl_SetResult (interp, "No DOM tree available.", NULL);
            return TCL_ERROR;
        }
        domSetDocumentElement (info->document);
        result = tcldom_returnDocumentObj (interp, info->document, 0,
                                           newObjName, 0, 0);
        info->document = NULL;
        return result;

    case m_setStoreLineColumn:
        info = CHandlerSetGetUserData (interp, objv[1], "tdom");
        if (!info) {
            Tcl_SetResult (interp, "parser object isn't tdom enabled.", NULL);
            return TCL_ERROR;
        }
        Tcl_SetIntObj (Tcl_GetObjResult (interp), info->storeLineColumn);
        if (objc == 4) {
            if (Tcl_GetBooleanFromObj (interp, objv[3], &bool) != TCL_OK) {
                return TCL_ERROR;
            }
            info->storeLineColumn = bool;
        }
        info->tdomStatus = 1;
        break;
        
    case m_remove:
        result = CHandlerSetRemove (interp, objv[1], "tdom");
        if (result == 2) {
            Tcl_SetResult (interp, "expat parser obj hasn't a C handler set named \"tdom\"", NULL);
            return TCL_ERROR;
        }
        break;

    case m_setExternalEntityResolver:
        if (objc != 4) {
            Tcl_SetResult (interp, "You must name a Tcl command as external entity resolver for setExternalEntityResolver.", NULL);
            return TCL_ERROR;
        }
        info = CHandlerSetGetUserData (interp, objv[1], "tdom");
        if (!info) {
            Tcl_SetResult (interp, "parser object isn't tdom enabled.", NULL);
            return TCL_ERROR;
        }
        if (info->extResolver) {
            Tcl_DecrRefCount (info->extResolver);
        }
        if (strcmp (Tcl_GetString (objv[3]), "") == 0) {
            info->extResolver = NULL;
        } else {
            info->extResolver = objv[3];
            Tcl_IncrRefCount (info->extResolver);
        }
        info->tdomStatus = 1;
        break;

    case m_keepEmpties:
        if (objc != 4) {
            Tcl_SetResult (interp, "wrong # of args for method keepEmpties.",
                           NULL);
            return TCL_ERROR;
        }
        handlerSet = CHandlerSetGet (interp, objv[1], "tdom");
        if (!handlerSet) {
            Tcl_SetResult (interp, "parser object isn't tdom enabled.", NULL);
            return TCL_ERROR;
        }
        info = handlerSet->userData;
        if (!info) {
            Tcl_SetResult (interp, "parser object isn't tdom enabled.", NULL);
            return TCL_ERROR;
        }
        Tcl_SetIntObj (Tcl_GetObjResult (interp), info->ignoreWhiteSpaces);
        if (Tcl_GetBooleanFromObj (interp, objv[3], &bool) != TCL_OK) {
            return TCL_ERROR;
        }
        info->ignoreWhiteSpaces = !bool;
        handlerSet->ignoreWhiteCDATAs = !bool;
        info->tdomStatus = 1;
        break;

    case m_keepCDATA:
        if (objc != 4) {
            Tcl_SetResult (interp, "wrong # of args for method keepCDATA.",
                           NULL);
            return TCL_ERROR;
        }
        handlerSet = CHandlerSetGet (interp, objv[1], "tdom");
        if (!handlerSet) {
            Tcl_SetResult (interp, "parser object isn't tdom enabled.", NULL);
            return TCL_ERROR;
        }
        info = handlerSet->userData;
        if (!info) {
            Tcl_SetResult (interp, "parser object isn't tdom enabled.", NULL);
            return TCL_ERROR;
        }
        if (Tcl_GetBooleanFromObj (interp, objv[3], &bool) != TCL_OK) {
            return TCL_ERROR;
        }
        if (bool) {
            handlerSet->startCdataSectionCommand = startCDATA;
            handlerSet->endCdataSectionCommand = endCDATA;
        } else {
            handlerSet->startCdataSectionCommand = startCDATA;
            handlerSet->endCdataSectionCommand = endCDATA;
        }
        info->tdomStatus = 1;
        break;
        
    case m_ignorexmlns:
        info = CHandlerSetGetUserData (interp, objv[1], "tdom");
        if (!info) {
            Tcl_SetResult (interp, "parser object isn't tdom enabled.", NULL);
            return TCL_ERROR;
        }
        Tcl_SetIntObj (Tcl_GetObjResult (interp), info->ignorexmlns);
        if (objc == 4) {
            if (Tcl_GetBooleanFromObj (interp, objv[3], &bool) != TCL_OK) {
                return TCL_ERROR;
            }
            info->ignorexmlns = bool;
        }
        info->tdomStatus = 1;
        break;
        

    }

    return TCL_OK;
}
