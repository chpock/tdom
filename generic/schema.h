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
|   November 2018
|
\---------------------------------------------------------------------------*/

#ifndef __SCHEMA_H__
#define __SCHEMA_H__

typedef enum {
  SCHEMA_CTYPE_EMPTY,
  SCHEMA_CTYPE_ANY,
  SCHEMA_CTYPE_MIXED,
  SCHEMA_CTYPE_NAME,
  SCHEMA_CTYPE_CHOICE,
  SCHEMA_CTYPE_INTERLEAVE,
  SCHEMA_CTYPE_PATTERN,
  SCHEMA_CTYPE_GROUP,
  SCHEMA_CTYPE_TEXT
} Schema_CP_Type;

typedef enum {
  SCHEMA_CQUANT_ONE,
  SCHEMA_CQUANT_OPT,
  SCHEMA_CQUANT_REP,
  SCHEMA_CQUANT_PLUS,
  SCHEMA_CQUANT_NM
} Schema_Content_Quant;

typedef unsigned int QuantFlags;

typedef struct
{
    Schema_Content_Quant  type;
    int                   minOccur;
    int                   maxOccur;
}  SchemaQuant;

typedef unsigned int SchemaFlags;

typedef struct SchemaCP
{
    Schema_CP_Type    type;
    char             *namespace;
    char             *name;
    struct SchemaCP  *next;
    SchemaFlags       flags;
    struct SchemaCP **content;
    SchemaQuant     **quants;
    unsigned int      numChildren;
} SchemaCP;

typedef struct SchemaValidationStack
{
    SchemaCP *pattern;
    struct SchemaValidationStack *next;
    struct SchemaValidationStack *down;
    int               activeChild;
    int               deep;
    int               nrMatched;
} SchemaValidationStack;

typedef enum {
    VALIDATION_READY,
    VALIDATION_STARTED,
    VALIDATION_FINISHED
} ValidationState;

typedef struct 
{
    char *start;
    char *startNamespace;
    Tcl_HashTable element;
    Tcl_HashTable namespace;
    Tcl_HashEntry *emptyNamespace;
    Tcl_HashTable pattern;
    SchemaCP **patternList;
    unsigned int numPatternList;
    unsigned int patternListSize;
    unsigned int forwardPatternDefs;
    SchemaQuant **quants;
    unsigned int numQuants;
    unsigned int quantsSize;
    Tcl_Obj **evalStub;
    char *currentNamespace;
    char *currentAttributeNamespace;
    int   defineToplevel;
    int   isAttribute;
    SchemaCP **currentContent;
    SchemaQuant **currentQuants;
    unsigned int numChildren;
    unsigned int contentSize;
    SchemaValidationStack *stack;
    ValidationState validationState;
    SchemaValidationStack *stackPool;
    unsigned int skipDeep;
} SchemaData;

int 
schemaInstanceCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    );

int tDOM_SchemaObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *CONST objv[]
    );

void tDOM_SchemaInit (
    Tcl_Interp *interp
    );

int
probeElement (
    Tcl_Interp *interp,
    SchemaData *sdata,
    const char *name,
    void *namespace
    );

int
probeElementEnd (
    Tcl_Interp * interp,
    SchemaData *sdata
    );

int
probeText (
    Tcl_Interp *interp,
    SchemaData *sdata,
    char *text
    );


#endif 
