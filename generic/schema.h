/*----------------------------------------------------------------------------
|   Copyright (c) 2018  Rolf Ade (rolf@pointsman.de)
|-----------------------------------------------------------------------------
|
|
|   The contents of this file are subject to the Mozilla Public License
|   Version 2.0 (the "License"); you may not use this file except in
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
|   2018-2020
|
\---------------------------------------------------------------------------*/

#ifndef __SCHEMA_H__
#define __SCHEMA_H__

#include <tcldom.h>
#include <domxpath.h>

typedef enum {
  SCHEMA_CTYPE_ANY,
  SCHEMA_CTYPE_NAME,
  SCHEMA_CTYPE_CHOICE,
  SCHEMA_CTYPE_INTERLEAVE,
  SCHEMA_CTYPE_PATTERN,
  SCHEMA_CTYPE_TEXT,
  SCHEMA_CTYPE_VIRTUAL,
  SCHEMA_CTYPE_KEYSPACE,
  SCHEMA_CTYPE_KEYSPACE_END,
} Schema_CP_Type;

typedef enum {
  SCHEMA_CQUANT_ONE,
  SCHEMA_CQUANT_OPT,
  SCHEMA_CQUANT_REP,
  SCHEMA_CQUANT_PLUS,
  SCHEMA_CQUANT_NM,
  SCHEMA_CQUANT_ERROR,
} SchemaQuant;

typedef int (*SchemaConstraintFunc) (Tcl_Interp *interp,
                                     void *constraintData, char *text);
typedef void (*SchemaConstraintFreeFunc) (void *constraintData);

typedef struct 
{
    void *constraintData;
    SchemaConstraintFunc constraint;
    SchemaConstraintFreeFunc freeData;
} SchemaConstraint;

typedef struct SchemaAttr
{
    char              *namespace;
    char              *name;
    int                required;
    struct SchemaAttr *next;
    struct SchemaCP   *cp;
} SchemaAttr;

typedef unsigned int SchemaFlags;

/* The SchemaFlags flags */
#define FORWARD_PATTERN_DEF     1
#define PLACEHOLDER_PATTERN_DEF 2
#define AMBIGUOUS_PATTERN       4
#define LOCAL_DEFINED_ELEMENT   8
#define CONSTRAINT_TEXT_CHILD  16
#define MIXED_CONTENT          32
#define ELEMENTTYPE_DEF        64

typedef struct domKeyConstraint {
    char  *name;
    ast    selector;
    ast   *fields;
    int    nrFields;
    int    flags;
    char  *emptyFieldSetValue;
    int    efsv_len;
    struct domKeyConstraint *next;
} domKeyConstraint;

typedef struct 
{
    char *name;
    int active;
    Tcl_HashTable ids;
    int unknownIDrefs;
} SchemaKeySpace;

typedef struct SchemaCP
{
    Schema_CP_Type    type;
    char             *namespace;
    char             *name;
    char             *typeName;
    struct SchemaCP  *next;
    SchemaFlags       flags;
    struct SchemaCP **content;
    SchemaQuant      *quants;
    unsigned int      nc;
    void             *typedata;
    SchemaAttr      **attrs;
    unsigned int      numAttr;
    unsigned int      numReqAttr;
    domKeyConstraint *domKeys;
    SchemaKeySpace   *keySpace;
    Tcl_Obj          *defScript;
    Tcl_Obj          *associated;
} SchemaCP;

typedef struct SchemaValidationStack
{
    SchemaCP *pattern;
    struct SchemaValidationStack *next;
    struct SchemaValidationStack *down;
    int               activeChild;
    int               hasMatched;
    int              *interleaveState;
} SchemaValidationStack;

typedef enum {
    VALIDATION_READY,
    VALIDATION_STARTED,
    VALIDATION_ERROR,
    VALIDATION_FINISHED
} ValidationState;

typedef struct 
{
    Tcl_HashTable ids;
    int unknownIDrefs;
} SchemaDocKey;

typedef struct SchemaData_
{
    Tcl_Obj *self;
    char *start;
    char *startNamespace;
    Tcl_HashTable element;
    Tcl_HashTable elementType;
    Tcl_HashTable namespace;
    Tcl_HashEntry *emptyNamespace;
    char **prefixns;
    Tcl_HashTable prefix;
    Tcl_HashTable pattern;
    Tcl_HashTable attrNames;
    Tcl_HashTable textDef;
    SchemaCP **patternList; 
    unsigned int numPatternList;
    unsigned int patternListSize;
    unsigned int forwardPatternDefs;
    SchemaQuant *quants;
    int       inuse;
    int       currentEvals;
    int       cleanupAfterUse;
    int       evalError;
    Tcl_Obj  *reportCmd;
    SchemaValidationStack *lastMatchse;
    int       recoverFlags;
    Tcl_Obj **evalStub;
    Tcl_Obj **textStub;
    char *currentNamespace;
    int   defineToplevel;
    int   isTextConstraint;
    int   isAttributeConstraint;
    SchemaCP *cp;
    unsigned int contentSize;
    SchemaAttr **currentAttrs;
    unsigned int numAttr;
    unsigned int numReqAttr;
    unsigned int attrSize;
    SchemaValidationStack *stack;
    SchemaValidationStack *stackPool;
    ValidationState validationState;
    int vaction;
    const char *vname;
    const char *vns;
    const char *vtext;
    unsigned int skipDeep;
    Tcl_DString *cdata;
    Tcl_HashTable ids;
    int unknownIDrefs;
    Tcl_HashTable idTables;
    Tcl_HashTable keySpaces;
    XML_Parser parser;
    domNode *node;
    domNode *insideNode;
    int choiceHashThreshold;
    int attributeHashThreshold;
    char *wsbuf;
    int wsbufLen;
} SchemaData;

int 
tDOM_schemaInstanceCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    );

void tDOM_SchemaInit (
    Tcl_Interp *interp
    );

int
tDOM_probeElement (
    Tcl_Interp *interp,
    SchemaData *sdata,
    const char *name,
    void *namespace
    );

int
tDOM_probeAttributes (
    Tcl_Interp *interp,
    SchemaData *sdata,
    const char **attr
    );

typedef struct domDocument domDocument;
typedef struct domNode domNode;
typedef struct domAttrNode domAttrNode;
typedef struct domTextNode domTextNode;

int tDOM_probeDomAttributes (
    Tcl_Interp *interp,
    SchemaData *sdata,
    domAttrNode *attr
    );
    
int
tDOM_probeElementEnd (
    Tcl_Interp * interp,
    SchemaData *sdata
    );

int
tDOM_probeText (
    Tcl_Interp *interp,
    SchemaData *sdata,
    char *text,
    int *only_whites
    );

void
tDOM_schemaReset (
    SchemaData *sdata,
    int lookforCleanup
    );

#endif 
