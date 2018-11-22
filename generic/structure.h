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

#ifndef __STRUCTURE_H__
#define __STRUCTURE_H__

typedef enum {
  STRUCTURE_CTYPE_EMPTY,
  STRUCTURE_CTYPE_ANY,
  STRUCTURE_CTYPE_MIXED,
  STRUCTURE_CTYPE_NAME,
  STRUCTURE_CTYPE_CHOICE,
  STRUCTURE_CTYPE_INTERLEAVE,
  STRUCTURE_CTYPE_PATTERN,
  STRUCTURE_CTYPE_GROUP,
  STRUCTURE_CTYPE_TEXT
} Structure_CP_Type;

typedef enum {
  STRUCTURE_CQUANT_NONE,
  STRUCTURE_CQUANT_OPT,
  STRUCTURE_CQUANT_REP,
  STRUCTURE_CQUANT_PLUS,
  STRUCTURE_CQUANT_N,
  STRUCTURE_CQUANT_NM
} Structure_Content_Quant;

typedef unsigned int StructureFlags;

typedef struct
{
    Structure_Content_Quant  quant;
    int                      minOccur;
    int                      maxOccur;
}  StructureQuant;

typedef struct StructureCP
{
    Structure_CP_Type    type;
    char                *namespace;
    char                *name;
    struct StructureCP  *next;
    StructureFlags       flags;
    struct StructureCP **content;
    StructureQuant     **quants;
    unsigned int         numChildren;
} StructureCP;

typedef struct StructureValidationStack
{
    StructureCP *pattern;
    struct StructureValidationStack *next;
    int               activeChild;
    int               deep;
    int               nrMatched;
} StructureValidationStack;

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
    StructureCP **patternList;
    unsigned int numPatternList;
    unsigned int patternListSize;
    unsigned int forwardPatternDefs;
    StructureQuant **quants;
    unsigned int numQuants;
    unsigned int quantsSize;
    char *currentNamespace;
    char *currentAttributeNamespace;
    int   isAttribute;
    StructureCP **currentContent;
    StructureQuant **currentQuants;
    unsigned int numChildren;
    unsigned int contentSize;
    StructureValidationStack **stack;
    int                        stackSize;
    int                        stackPtr;
    ValidationState            validationState;
    StructureValidationStack **stackList;
    unsigned int numStackList;
    unsigned int stackListSize;
    unsigned int numStackAllocated;
} StructureData;

int 
structureInstanceCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[]
    );

int tDOM_StructureObjCmd (
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *CONST objv[]
    );

void tDOM_StructureInit (
    Tcl_Interp *interp
    );

int
probeElement (
    Tcl_Interp *interp,
    StructureData *sdata,
    const char *name,
    void *namespace
    );

int
probeElementEnd (
    Tcl_Interp * interp,
    StructureData *sdata
    );

int
probeText (
    Tcl_Interp *interp,
    StructureData *sdata,
    char *text
    );


#endif 
