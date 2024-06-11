/*----------------------------------------------------------------------------
|   Copyright (c) 1999 Jochen Loewer (loewerj@hotmail.com)
+-----------------------------------------------------------------------------
|
|   A DOM implementation for Tcl using James Clark's expat XML parser
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
|   The Original Code is tDOM.
|
|   The Initial Developer of the Original Code is Jochen Loewer
|   Portions created by Jochen Loewer are Copyright (C) 1998, 1999
|   Jochen Loewer. All Rights Reserved.
|
|   Contributor(s):
|       Sept99  Carsten Zerbst    Added comment and processing instructions
|                                 nodes.
|       June00  Zoran Vasiljevic  Made thread-safe.
|       July00  Zoran Vasiljevic  Added "domNode appendFromScript"
|
|
|   written by Jochen Loewer
|   April, 1999
|
\---------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------
|   Includes
|
\---------------------------------------------------------------------------*/
#include <tcl.h>
#include <dom.h>
#include <domxpath.h>
#include <domxslt.h>
#include <xmlsimple.h>
#include <domjson.h>
#include <domhtml.h>
#include <domhtml5.h>
#include <nodecmd.h>
#include <tcldom.h>
#include <schema.h>
#include <versionhash.h>
#include <float.h>

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

#ifndef C14N_ATTR_SORT_SIZE_INIT
#  define C14N_ATTR_SORT_SIZE_INIT 8
#endif

/*----------------------------------------------------------------------------
|   Macros
|
\---------------------------------------------------------------------------*/
#define XP_CHILD         0
#define XP_DESCENDANT    1
#define XP_ANCESTOR      2
#define XP_FSIBLING      3
#define XP_PSIBLING      4

#define MAX_REWRITE_ARGS 50

#define MAX_XSLT_APPLY_DEPTH 3000

#define SetResult(str) Tcl_ResetResult(interp); \
                     Tcl_SetStringObj(Tcl_GetObjResult(interp), (str), -1)

#define SetResult3(str1,str2,str3) Tcl_ResetResult(interp);     \
                     Tcl_AppendResult(interp, (str1), (str2), (str3), NULL)

#define SetIntResult(i) Tcl_ResetResult(interp);                        \
                     Tcl_SetDomLengthObj(Tcl_GetObjResult(interp), (i))
                     
#define SetLongResult(i) Tcl_ResetResult(interp);                        \
                     Tcl_SetLongObj(Tcl_GetObjResult(interp), (i))

#define SetDoubleResult(d) Tcl_ResetResult(interp);                     \
                     Tcl_SetDoubleObj(Tcl_GetObjResult(interp), (d))

#define SetBooleanResult(i) Tcl_ResetResult(interp); \
                     Tcl_SetBooleanObj(Tcl_GetObjResult(interp), (i))
 
#define AppendResult(str) {Tcl_Obj *o = Tcl_GetObjResult(interp); \
                     if (Tcl_IsShared(o)) { \
                          o = Tcl_DuplicateObj(o); \
                          Tcl_SetObjResult(interp, o); \
                     } \
                     Tcl_AppendToObj(o, (str), -1);}

#define CheckArgs(min,max,n,msg) \
                     if ((objc < min) || (objc >max)) { \
                         Tcl_WrongNumArgs(interp, n, objv, msg); \
                         return TCL_ERROR; \
                     }
#define CheckName(interp, name, errText, isFQ) \
                     if (!TcldomDATA(dontCheckName)) { \
                         if (!tcldom_nameCheck(interp, name, errText, isFQ)) {\
                             return TCL_ERROR; \
                         } \
                     }

#define CheckPIName(interp, name) \
                     if (!TcldomDATA(dontCheckName)) { \
                         if (!tcldom_PINameCheck(interp, name)) {\
                             return TCL_ERROR; \
                         } \
                     }

#define CheckText(interp, text, errText) \
                     if (!TcldomDATA(dontCheckCharData)) { \
                         if (!tcldom_textCheck(interp, text, errText)) {\
                             return TCL_ERROR; \
                         } \
                     }

#define CheckComment(interp, text) \
                     if (!TcldomDATA(dontCheckCharData)) { \
                         if (!tcldom_commentCheck(interp, text)) {\
                             return TCL_ERROR; \
                         } \
                     }

#define CheckCDATA(interp, text) \
                     if (!TcldomDATA(dontCheckCharData)) { \
                         if (!tcldom_CDATACheck(interp, text)) {\
                             return TCL_ERROR; \
                         } \
                     }

#define CheckPIValue(interp, text) \
                     if (!TcldomDATA(dontCheckCharData)) { \
                         if (!tcldom_PIValueCheck(interp, text)) {\
                             return TCL_ERROR; \
                         } \
                     }

#define writeChars(var,chan,buf,len)  (chan) ? \
                     ((void)Tcl_WriteChars ((chan), (buf), (len) )) : \
                     (Tcl_AppendToObj ((var), (buf), (len) ));

#define DOM_CREATECMDMODE_AUTO 0
#define DOM_CREATECMDMODE_CMDS 1
#define DOM_CREATECMDMODE_TOKENS 2

#define SERIALIZE_XML_DECLARATION 1
#define SERIALIZE_DOCTYPE_DECLARATION 2
#define SERIALIZE_FOR_ATTR 4 
#define SERIALIZE_ESCAPE_NON_ASCII 8
#define SERIALIZE_HTML_ENTITIES 16
#define SERIALIZE_ESCAPE_ALL_QUOT 32
#define SERIALIZE_NO_GT_ESCAPE 64
#define SERIALIZE_NO_EMPTY_ELEMENT_TAG 128
#define SERIALIZE_INDENT_WITH_TAB 256
#define SERIALIZE_INDENT_ATTR_WITH_TAB 512
#define SERIALIZE_ESCAPE_CR 1024
#define SERIALIZE_ESCAPE_TAB 2048

/*----------------------------------------------------------------------------
|   Module Globals
|
\---------------------------------------------------------------------------*/
typedef struct TcldomData {
    int        storeLineColumn;
    int        dontCreateObjCommands;
    int        dontCheckCharData;
    int        dontCheckName;
    int        domCreateCmdMode;
} TcldomData;
#define TcldomDATA(x)    tdPtr->x
#define GetTcldomDATA    TcldomData *tdPtr = tcldom_getdata(interp)

#ifdef TCL_THREADS
    static Tcl_HashTable     sharedDocs;
    static Tcl_Mutex         tableMutex;
    static int               tcldomInitialized;
#endif /* TCL_THREADS */

static char dom_usage[] =
    "Usage dom <subCommand> <args>, where subCommand can be:    \n"
    "    parse ?-keepEmpties? ?-channel <channel> ?-baseurl <baseurl>?  \n"
    "        ?-feedbackAfter <#Bytes>?                    \n"
    "        ?-feedbackcmd <cmd>?                         \n"
    "        ?-externalentitycommand <cmd>?               \n"
    "        ?-useForeignDTD <boolean>?                   \n"
    "        ?-paramentityparsing <none|always|standalone>\n"
    "        ?-simple? ?-html? ?-html5? ?-json?           \n"
    "        ?-jsonmaxnesting <#nr>?                      \n"
    "        ?-jsonroot name?                             \n"
    "        ?<xml|html|json>? ?<objVar>?                 \n"
    "    createDocument docElemName ?objVar?              \n"
    "    createDocumentNS uri docElemName ?objVar?        \n"
    "    createDocumentNode ?objVar?                      \n"
    TDomThreaded(
    "    attachDocument domDoc ?objVar?                   \n"
    "    detachDocument domDoc                            \n"
    )
    "    createNodeCmd ?-returnNodeCmd? ?-tagName name? ?-jsonType jsonType? ?-namespace URI? (element|comment|text|cdata|pi)Node cmdName \n"
    "    setStoreLineColumn ?boolean?                     \n"
    "    setNameCheck ?boolean?                           \n"
    "    setTextCheck ?boolean?                           \n"
    "    setObjectCommands ?(automatic|token|command)?    \n"
    "    isCharData string                                \n"
    "    clearString ?-replace ?replacement?? string      \n"
    "    isBMPCharData string                             \n"
    "    isComment string                                 \n"
    "    isCDATA string                                   \n"
    "    isPIValue string                                 \n"
    "    isName string                                    \n"
    "    isQName string                                   \n"
    "    isNCName string                                  \n"
    "    isPIName string                                  \n"
    "    featureinfo feature                              \n"
;

static char doc_usage[] =
    "Usage domDoc <method> <args>, where method can be:\n"
    "    documentElement ?objVar?                \n"
    "    getElementsByTagName name               \n"
    "    getElementsByTagNameNS uri localname    \n"
    "    createElement tagName ?objVar?          \n"
    "    createElementNS uri tagName ?objVar?    \n"
    "    createCDATASection data ?objVar?        \n"
    "    createTextNode text ?objVar?            \n"
    "    createComment text ?objVar?             \n"
    "    createProcessingInstruction target data ?objVar? \n"
    "    asXML ?-indent <none,tabs,0..8>? ?-channel <channel>? ?other options - see manual?>\n"
    "    asCanonicalXML ?-channel <channel>? ?-comments <boolean>\n"
    "    asHTML ?-channel <channelId>? ?-escapeNonASCII? ?-htmlEntities?\n"
    "    asText                                  \n"
    "    asJSON ?-indent <none,0..8>?            \n"
    "    getDefaultOutputMethod                  \n"
    "    publicId ?publicId?                     \n"
    "    systemId ?systemId?                     \n"
    "    internalSubset ?internalSubset?         \n"
    "    indent ?boolean?                        \n"
    "    omit-xml-declaration ?boolean?          \n"
    "    encoding ?value?                        \n"
    "    standalone ?boolean?                    \n"
    "    mediaType ?value?                       \n"
    "    delete                                  \n"
    "    xslt ?-parameters parameterList? ?-ignoreUndeclaredParameters? ?-xsltmessagecmd cmd? <xsltDocNode> ?objVar?\n"
    "    toXSLTcmd                               \n"
    "    cdataSectionElements (?URI:?localname|*) ?boolean?\n"
    "    normalize ?-forXPath?                   \n"
    "    nodeType                                \n"
    "    hasChildNodes                           \n"
    "    childNodes                              \n"
    "    firstChild ?nodeObjVar?                 \n"
    "    lastChild ?nodeObjVar?                  \n"
    "    appendChild new                         \n"
    "    insertBefore new ref                    \n"
    "    replaceChild new old                    \n"
    "    removeChild child                       \n"
    "    ownerDocument                           \n"
    "    getElementById id                       \n"
    "    baseURI ?URI?                           \n"
    "    appendFromList nestedList               \n"
    "    appendFromScript script                 \n"        
    "    insertBeforeFromScript script ref       \n"
    "    appendXML xmlString                     \n"
    "    selectNodesNamespaces ?prefixUriList?   \n" 
    "    selectNodes ?-namespaces prefixUriList? ?-cache <boolean>? xpathQuery ?typeVar? \n"
    "    renameNode <nodelist> <newName>         \n"
    "    deleteXPathCache ?xpathQuery?           \n"
    TDomThreaded(
    "    readlock                                \n"
    "    writelock                               \n"
    "    renumber                                \n"
    )
;

static char node_usage[] =
    "Usage nodeObj <method> <args>, where method can be:\n"
    "    nodeType                     \n"
    "    nodeName                     \n"
    "    nodeValue ?newValue?         \n"
    "    hasChildNodes                \n"
    "    childNodes                   \n"
    "    childNodesLive               \n"
    "    parentNode                   \n"
    "    firstChild ?nodeObjVar?      \n"
    "    lastChild ?nodeObjVar?       \n"
    "    nextSibling ?nodeObjVar?     \n"
    "    previousSibling ?nodeObjVar? \n"
    "    hasAttribute attrName        \n"
    "    getAttribute attrName ?defaultValue? \n"
    "    setAttribute attrName value ?attrName value ...? \n"
    "    removeAttribute attrName     \n"
    "    hasAttributeNS uri localName \n"
    "    getAttributeNS uri localName ?defaultValue? \n"
    "    setAttributeNS uri attrName value ?attrName value ...? \n"
    "    removeAttributeNS uri attrName \n"
    "    attributes ?attrNamePattern?   \n"
    "    attributeNames ?attrNamePattern?   \n"
    "    appendChild new              \n"
    "    insertBefore new ref         \n"
    "    replaceChild new old         \n"
    "    removeChild child            \n"
    "    cloneNode ?-deep?            \n"
    "    ownerDocument                \n"
    "    getElementsByTagName name    \n"
    "    getElementsByTagNameNS uri localname \n"
    "    getElementById id            \n"
    "    find attrName attrValue ?nodeObjVar?   \n"
    "    child      number|all ?type? ?attrName attrValue? \n"
    "    descendant number|all ?type? ?attrName attrValue? \n"
    "    ancestor   number|all ?type? ?attrName attrValue? \n"
    "    fsibling   number|all ?type? ?attrName attrValue? \n"
    "    psibling   number|all ?type? ?attrName attrValue? \n"
    "    root ?nodeObjVar?            \n"
    "    target                       \n"
    "    data                         \n"
    "    text                         \n"
    "    prefix                       \n"
    "    namespaceURI                 \n"
    "    getBaseURI                   \n"
    "    baseURI ?URI?                \n"
    "    localName                    \n"
    "    delete                       \n"
    "    getLine                      \n"
    "    getColumn                    \n"
    "    @<attrName> ?defaultValue?   \n"
    "    asList                       \n"
    "    asXML ?-indent <none,tabs,0..8>? ?-channel <channel>? ?other options - see manual?>\n"
    "    asCanonicalXML ?-channel <channel>? ?-comments <boolean>\n"
    "    asHTML ?-channel <channelId>? ?-escapeNonASCII? ?-htmlEntities?\n"
    "    asText                       \n"
    "    asJSON ?-indent <none,0..8>? \n"
    "    appendFromList nestedList    \n"
    "    appendFromScript script      \n"
    "    insertBeforeFromScript script ref \n"
    "    appendXML xmlString          \n"
    "    selectNodes ?-namespaces prefixUriList? ?-cache <boolean>? xpathQuery ?typeVar? \n"
    "    toXPath ?-legacy?            \n"
    "    disableOutputEscaping ?boolean? \n"
    "    precedes node                \n"
    "    normalize ?-forXPath?        \n"
    "    xslt ?-parameters parameterList? <xsltDocNode>\n"
    "    jsonType ?jsonType?          \n"
    TDomThreaded(
    "    readlock                     \n"
    "    writelock                    \n"
    )
;

static const char *jsonTypes[] = {
    "NONE",
    "ARRAY",
    "OBJECT",
    "NULL",
    "TRUE",
    "FALSE",
    "STRING",
    "NUMBER",
    NULL
};

/*----------------------------------------------------------------------------
|   Types
|
\---------------------------------------------------------------------------*/

typedef struct XsltMsgCBInfo {
    Tcl_Interp * interp;
    Tcl_Obj    * msgcmd;
} XsltMsgCBInfo;


static void UpdateStringOfTdomNode(Tcl_Obj *objPtr);
static int  SetTdomNodeFromAny(Tcl_Interp *interp, Tcl_Obj *objPtr);

const Tcl_ObjType tdomNodeType = {
    "tdom-node",
    NULL,
    NULL,
    UpdateStringOfTdomNode,
    SetTdomNodeFromAny
};

/*----------------------------------------------------------------------------
|   Prototypes for procedures defined later in this file:
|
\---------------------------------------------------------------------------*/
static Tcl_VarTraceProc  tcldom_docTrace;
static Tcl_CmdDeleteProc tcldom_docCmdDeleteProc;

static void tcldom_treeAsJSON(Tcl_Obj *jstring, domNode *node,
                              Tcl_Channel channel, int indent,
                              int outputFlags, int level, int inside);

#ifdef TCL_THREADS

static int tcldom_EvalLocked(Tcl_Interp* interp, Tcl_Obj** objv,
                             domDocument* doc, int flag);

static int tcldom_RegisterDocShared(domDocument* doc);
static int tcldom_CheckDocShared(domDocument* doc);
static int tcldom_UnregisterDocShared(Tcl_Interp* interp, domDocument* doc);

/*----------------------------------------------------------------------------
|   tcldom_Finalize
|
|   Activated in application exit handler to delete shared document table
|   Table entries are deleted by the object command deletion callbacks,
|   so at this time, table should be empty. If not, we will leave some
|   memory leaks. This is not fatal, though: we're exiting the app anyway.
|   This is a private function to this file. 
\---------------------------------------------------------------------------*/

static void 
tcldom_Finalize(
    ClientData UNUSED(unused)
)
{
    DBG(fprintf(stderr, "--> tcldom_Finalize\n"));
    Tcl_MutexLock(&tableMutex);
    Tcl_DeleteHashTable(&sharedDocs);
    tcldomInitialized = 0;
    Tcl_MutexUnlock(&tableMutex);
}

/*----------------------------------------------------------------------------
|   tcldom_initialize
|   Activated at module load to initialize shared document table.
|   This is exported since we need it in tdominit.c.
\---------------------------------------------------------------------------*/

void tcldom_initialize(void)
{
    if (!tcldomInitialized) {
        DBG(fprintf(stderr, "--> tcldom_initialize\n"));
        Tcl_MutexLock(&tableMutex);
        Tcl_InitHashTable(&sharedDocs, TCL_ONE_WORD_KEYS);
        Tcl_CreateExitHandler(tcldom_Finalize, NULL);
        tcldomInitialized = 1;
        Tcl_MutexUnlock(&tableMutex);
    }
}

#endif /* TCL_THREADS */

/*----------------------------------------------------------------------------
|   tcldom_findata and tcldom_getdata
|   Some interpreter global information is managed in an assoc data struct.
\---------------------------------------------------------------------------*/
static void tcldom_findata(
    ClientData clientData,
    Tcl_Interp *UNUSED(interp)
)
{
    ckfree((char *) clientData);
}

static inline TcldomData * tcldom_getdata(
    Tcl_Interp * interp
)
{
    TcldomData *tdPtr =
	(TcldomData *) Tcl_GetAssocData(interp, "tdom_data", NULL);

    if (tdPtr == NULL) {
	tdPtr = (TcldomData *) ckalloc(sizeof(TcldomData));
	memset(tdPtr, 0, sizeof(TcldomData));
	Tcl_SetAssocData(interp, "tdom_data", tcldom_findata,
			 (ClientData) tdPtr);
    }
    return tdPtr;
}

/*----------------------------------------------------------------------------
|   tcldom_deleteNode
|
\---------------------------------------------------------------------------*/
static void
tcldom_deleteNode (
    domNode  * node,
    void     * clientData
)
{
    Tcl_Interp *interp = clientData;
    char        objCmdName[80];

    /* Try to delete the node object commands, ignore errors */
    if (node->nodeFlags & VISIBLE_IN_TCL) {
        NODE_CMD(objCmdName, node);
        Tcl_DeleteCommand(interp, objCmdName);
        node->nodeFlags &= ~VISIBLE_IN_TCL;
    }
}

/*----------------------------------------------------------------------------
|   tcldom_deleteDoc
|
\---------------------------------------------------------------------------*/
void tcldom_deleteDoc (
    Tcl_Interp  * interp,
    domDocument * doc
)
{
    int deleted = 1;

    if (doc->nodeFlags & INSIDE_FROM_SCRIPT) {
        doc->nodeFlags |= DELETE_AFTER_FROM_SCRIPT;
        return;
    }
    TDomThreaded(deleted = tcldom_UnregisterDocShared(interp, doc));
    if (deleted) {
        domFreeDocument(doc, tcldom_deleteNode, interp);
    }
}

/*----------------------------------------------------------------------------
|   tcldom_docCmdDeleteProc
|
\---------------------------------------------------------------------------*/
static
void tcldom_docCmdDeleteProc(
    ClientData clientData
)
{
    domDeleteInfo *dinfo = (domDeleteInfo *)clientData;
    domDocument   *doc   = dinfo->document;
    int            hasTrace  = dinfo->document->nodeFlags & VAR_TRACE;
    
    DBG(fprintf(stderr, "--> tcldom_docCmdDeleteProc doc %p\n", doc));
    tcldom_deleteDoc(dinfo->interp, doc);

    if (hasTrace) {
        dinfo->document = NULL;
    } else {
        FREE((void*)dinfo);
    }
}

/*----------------------------------------------------------------------------
|   tcldom_docTrace
|
\---------------------------------------------------------------------------*/
static
char * tcldom_docTrace (
    ClientData    clientData,
    Tcl_Interp   *interp,
    const char *name1,
    const char *name2,
    int           flags
)
{
    domDeleteInfo *dinfo = (domDeleteInfo*) clientData;
    domDocument   *doc   = dinfo->document;
    char           objCmdName[80];

    DBG(fprintf(stderr, "--> tcldom_docTrace %x %p\n", flags, doc));

    if (doc == NULL) {
        if (!Tcl_InterpDeleted (interp)) {
            Tcl_UntraceVar(dinfo->interp, dinfo->traceVarName,
                           TCL_TRACE_WRITES|TCL_TRACE_UNSETS,
                           tcldom_docTrace, clientData);
        }
        FREE (dinfo->traceVarName);
        FREE (dinfo);
        return NULL;
    }
    if (flags & TCL_TRACE_WRITES) {
        DOC_CMD(objCmdName, doc);
        Tcl_SetVar2 (interp, name1, name2, objCmdName, TCL_LEAVE_ERR_MSG);
        return "var is read-only";
    }
    if (flags & TCL_TRACE_UNSETS) {
        DOC_CMD(objCmdName, doc);
        DBG(fprintf(stderr, "--> tcldom_docTrace delete doc %p\n", doc));
        Tcl_DeleteCommand(interp, objCmdName);
        FREE (dinfo->traceVarName);
        FREE (dinfo);
    }

    return NULL;
}

/*----------------------------------------------------------------------------
|   UpdateStringOfTdomNode
|
\---------------------------------------------------------------------------*/
static void
UpdateStringOfTdomNode(
    Tcl_Obj *objPtr)
{
    char nodeName[80];
    domLength  len;
    
    NODE_CMD(nodeName, objPtr->internalRep.otherValuePtr);
    len = strlen(nodeName);
    objPtr->bytes = (ckalloc((unsigned char) len+1));
    memcpy(objPtr->bytes, nodeName, len+1);
    objPtr->length = len;
}

/*----------------------------------------------------------------------------
|   SetTdomNodeFromAny
|
\---------------------------------------------------------------------------*/
static int
SetTdomNodeFromAny(
    Tcl_Interp *interp,		/* Tcl interpreter or NULL */
    Tcl_Obj *objPtr)		/* Pointer to the object to parse */
{
    Tcl_CmdInfo  cmdInfo;
    domNode     *node = NULL;
    char        *nodeName;
    char         eolcheck;

    if (objPtr->typePtr == &tdomNodeType) {
        return TCL_OK;
    }

    nodeName = Tcl_GetString(objPtr);
    if (strncmp(nodeName, "domNode", 7)) {
        if (interp) {
            SetResult3("Parameter \"", nodeName, "\" is not a domNode.");
            return TCL_ERROR;
        }
    }
    if (sscanf(&nodeName[7], "%p%1c", (void **)&node, &eolcheck) != 1) {
        if (!Tcl_GetCommandInfo(interp, nodeName, &cmdInfo)) {
            if (interp) {
                SetResult3("Parameter \"", nodeName, "\" is not a domNode.");
                return TCL_ERROR;
            }
        }
        if (   (cmdInfo.isNativeObjectProc == 0)
            || (cmdInfo.objProc != (Tcl_ObjCmdProc*)tcldom_NodeObjCmd)) {
            if (interp) {
                SetResult3("Parameter \"", nodeName, "\" is not a domNode"
                    " object command");
                return TCL_ERROR;
            }
        }
        node = (domNode*)cmdInfo.objClientData;
    }
    if (objPtr->typePtr && objPtr->typePtr->freeIntRepProc) {
        objPtr->typePtr->freeIntRepProc(objPtr);
    }
    objPtr->internalRep.otherValuePtr = node;
    objPtr->typePtr = &tdomNodeType;
    
    return TCL_OK;
}

/*----------------------------------------------------------------------------
|   tcldom_createNodeObj
|
\---------------------------------------------------------------------------*/
void tcldom_createNodeObj (
    Tcl_Interp * interp,
    domNode    * node,
    char       * objCmdName
)
{
    GetTcldomDATA;

    NODE_CMD(objCmdName, node);

    if (TcldomDATA(dontCreateObjCommands) == 0) {
        DBG(fprintf(stderr,"--> creating node %s\n", objCmdName));
        
        Tcl_CreateObjCommand(interp, objCmdName,
                             (Tcl_ObjCmdProc *)  tcldom_NodeObjCmd,
                             (ClientData)        node,
                             (Tcl_CmdDeleteProc*)NULL);
        node->nodeFlags |= VISIBLE_IN_TCL;
    }
}

/*----------------------------------------------------------------------------
|   tcldom_setInterpAndReturnVar
|
\---------------------------------------------------------------------------*/
int tcldom_setInterpAndReturnVar (
    Tcl_Interp *interp,
    domNode    *node,
    int         setVariable,
    Tcl_Obj    *var_name
)
{
    char     objCmdName[80];
    Tcl_Obj *resultObj;
    
    GetTcldomDATA;

    if (node == NULL) {
        if (setVariable) {
            if (!Tcl_ObjSetVar2 (interp, var_name, NULL,
                                 Tcl_NewStringObj("",0),
                                 TCL_LEAVE_ERR_MSG)) {
                return TCL_ERROR;
            }
        }
        SetResult("");
        return TCL_OK;
    }
    resultObj = Tcl_NewObj();
    resultObj->bytes = NULL;
    resultObj->length = 0;
    resultObj->internalRep.otherValuePtr = node;
    resultObj->typePtr = &tdomNodeType;
    Tcl_SetObjResult (interp, resultObj);
    if (TcldomDATA(dontCreateObjCommands) == 0) {
        tcldom_createNodeObj(interp, node, objCmdName);
    }
    if (setVariable) {
        if (!Tcl_ObjSetVar2 (interp, var_name, NULL, resultObj,
                             TCL_LEAVE_ERR_MSG)) {
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}

/*----------------------------------------------------------------------------
|   tcldom_returnNodeObj
|
\---------------------------------------------------------------------------*/
static
Tcl_Obj *tcldom_returnNodeObj (
    Tcl_Interp *interp,
    domNode    *node)
{
    char     objCmdName[80];
    Tcl_Obj *resultObj;
    
    GetTcldomDATA;

    resultObj = Tcl_NewObj();
    if (node == NULL) {
        return resultObj;
    }
    if (TcldomDATA(dontCreateObjCommands) == 0) {
        tcldom_createNodeObj(interp, node, objCmdName);
    }
    resultObj->bytes = NULL;
    resultObj->length = 0;
    resultObj->internalRep.otherValuePtr = node;
    resultObj->typePtr = &tdomNodeType;
    return resultObj;
}

/*----------------------------------------------------------------------------
|   tcldom_returnDocumentObj
|
\---------------------------------------------------------------------------*/
int tcldom_returnDocumentObj (
    Tcl_Interp  *interp,
    domDocument *document,
    int          setVariable,
    Tcl_Obj     *var_name,
    int          trace,
    int          forOwnerDocument
)
{
    char           objCmdName[80], *objVar;
    domDeleteInfo *dinfo;
    Tcl_CmdInfo    cmd_info;

    GetTcldomDATA;

    if (document == NULL) {
        if (setVariable) {
            objVar = Tcl_GetString(var_name);
            Tcl_UnsetVar(interp, objVar, 0);
            Tcl_SetVar  (interp, objVar, "", 0);
        }
        SetResult("");
        return TCL_OK;
    }

    DOC_CMD(objCmdName, document);

    if (TcldomDATA(dontCreateObjCommands)) {
        if (setVariable) {
            objVar = Tcl_GetString(var_name);
            Tcl_SetVar(interp, objVar, objCmdName, 0);
        }
    } else {
        if (!Tcl_GetCommandInfo(interp, objCmdName, &cmd_info)) {
            dinfo = (domDeleteInfo*)MALLOC(sizeof(domDeleteInfo));
            dinfo->interp       = interp;
            dinfo->document     = document;
            document->nodeFlags |= DOCUMENT_CMD;
            dinfo->traceVarName = NULL;
            Tcl_CreateObjCommand(interp, objCmdName,
                                 (Tcl_ObjCmdProc *)  tcldom_DocObjCmd,
                                 (ClientData)        dinfo,
                                 (Tcl_CmdDeleteProc*)tcldom_docCmdDeleteProc);
        } else {
            dinfo = (domDeleteInfo*)cmd_info.objClientData;
        }
        if (setVariable) {
            objVar = Tcl_GetString(var_name);
            Tcl_UnsetVar(interp, objVar, 0);
            Tcl_SetVar  (interp, objVar, objCmdName, 0);
            if (trace) {
                document->nodeFlags |= VAR_TRACE;
                dinfo->traceVarName = tdomstrdup(objVar);
                Tcl_TraceVar(interp,objVar,TCL_TRACE_WRITES|TCL_TRACE_UNSETS,
                             (Tcl_VarTraceProc*)tcldom_docTrace,
                             (ClientData)dinfo);
            }
        }
    }
    
    if (!forOwnerDocument) {
        TDomThreaded(tcldom_RegisterDocShared(document));
    }
    SetResult(objCmdName);

    return TCL_OK;
}


/*----------------------------------------------------------------------------
|   tcldom_getElementsByTagName
|
\---------------------------------------------------------------------------*/
static int
tcldom_getElementsByTagName (
    Tcl_Interp *interp,
    char       *namePattern,
    domNode    *node,
    int         nsIndex,
    const char *uri
)
{
    int         result;
    domNode    *child;
    char        prefix[MAX_PREFIX_LEN];
    const char *localName;
    Tcl_Obj    *namePtr, *resultPtr;

    /* nsIndex == -1 ==> DOM 1 no NS i.e getElementsByTagName
       nsIndex != -1 are the NS aware cases
       nsIndex == -2 ==> more than one namespace in the document with the 
                         requested namespace, we have to strcmp the URI
                         with the namespace uri of every node
       nsIndex == -3 ==> NS wildcard '*'
       nsIndex == -4 ==> special handled case uri == "", i.e. all
                         nodes not in a namespace */

    while (node) {
        if (node->nodeType != ELEMENT_NODE) {
            node = node->nextSibling;
            continue;
        }
        if ( (nsIndex == -1)
             || (nsIndex == (int)node->namespace)
             || (nsIndex == -3)
             || (nsIndex == -2 
                 && node->namespace 
                 && strcmp(uri, domNamespaceURI (node)) == 0)
             || (nsIndex == -4
                 && (!node->namespace 
                     || strcmp ("", domNamespaceURI (node))==0)) )
        {
            if (nsIndex == -1) {
                localName = node->nodeName;
            } else {
                domSplitQName(node->nodeName, prefix, &localName);
            }
            if (Tcl_StringMatch(localName, namePattern)) {
                resultPtr = Tcl_GetObjResult(interp);
                namePtr = tcldom_returnNodeObj(interp, node);
                result = Tcl_ListObjAppendElement(interp, resultPtr, namePtr);
                if (result != TCL_OK) {
                    Tcl_DecrRefCount(namePtr);
                    return result;
                }
            }
        }

        /* recurs to the child nodes */
        child = node->firstChild;
        result = tcldom_getElementsByTagName(interp, namePattern, child,
                                             nsIndex, uri);
        if (result != TCL_OK) {
            return result;
        }
        node = node->nextSibling;
    }
    
    return TCL_OK;
}


/*----------------------------------------------------------------------------
|   tcldom_find
|
\---------------------------------------------------------------------------*/
static
domNode * tcldom_find (
    domNode    *node,
    char       *attrName,
    char       *attrVal,
    domLength   length
)
{
    domNode     *child, *result;
    domAttrNode *attrs;

    if (node->nodeType != ELEMENT_NODE) return NULL;

    attrs = node->firstAttr;
    while (attrs) {
        if ((strcmp(attrs->nodeName, attrName)==0) &&
            (length == attrs->valueLength)         &&
            (strncmp(attrs->nodeValue, attrVal, length)==0)) {

            return node;
        }
        attrs = attrs->nextSibling;
    }
    child = node->firstChild;
    while (child != NULL) {

        result = tcldom_find(child, attrName, attrVal, length);
        if (result != NULL) {
            return result;
        }
        child = child->nextSibling;
    }
    return NULL;
}


/*----------------------------------------------------------------------------
|   tcldom_xpointerAddCallback
|
\---------------------------------------------------------------------------*/
static
int tcldom_xpointerAddCallback (
    domNode    * node,
    void       * clientData
)
{
    Tcl_Interp * interp = (Tcl_Interp*)clientData;
    Tcl_Obj    * resultPtr = Tcl_GetObjResult(interp);
    Tcl_Obj    * namePtr;
    int          result;


    namePtr = tcldom_returnNodeObj(interp, node);
    result  = Tcl_ListObjAppendElement(interp, resultPtr, namePtr);
    if (result != TCL_OK) {
        Tcl_DecrRefCount(namePtr);
    }
    return result;
}


/*----------------------------------------------------------------------------
|   tcldom_xpointerSearch
|
\---------------------------------------------------------------------------*/
static
int tcldom_xpointerSearch (
    Tcl_Interp * interp,
    int          mode,
    domNode    * node,
    int          objc,
    Tcl_Obj    * const  objv[]
)
{
    char *str;
    int   i = 0;
    int   result = 0;
    int   all = 0;
    int   instance = 0;
    int   type = ELEMENT_NODE;
    char *element   = NULL;
    char *attrName  = NULL;
    char *attrValue = NULL;
    domLength attrLen;


    str = Tcl_GetString(objv[2]);
    if (strcmp(str, "all")==0) {
        all = 1;
    } else {
        if (Tcl_GetIntFromObj(interp, objv[2], &instance) != TCL_OK) {
            SetResult( "instance must be integer or 'all'");
            return TCL_ERROR;
        }
    }
    if (objc > 3) {
        str = Tcl_GetString(objv[3]);
        if (*str == '#') {
            if (strcmp(str,"#text")==0) {
                type = TEXT_NODE;
            } else if (strcmp(str,"#cdata")==0) {
                type = CDATA_SECTION_NODE;
            } else if (strcmp(str,"#all")==0) {
                type = ALL_NODES;
            } else if (strcmp(str,"#element")==0) {
                type = ELEMENT_NODE;
            } else {
                SetResult( "wrong node type");
                return TCL_ERROR;
            }
        } else {
            element = str;
        }
    }
    if (objc >= 5) {
        if ((type != ELEMENT_NODE) && (type != ALL_NODES)) {
            SetResult( "Attribute search only for element nodes");
            return TCL_ERROR;
        }
        attrName  = Tcl_GetString(objv[4]);
        if (objc == 6) {
            attrValue = Tcl_GetStringFromObj(objv[5], &attrLen);
        } else {
            attrValue = "*";
            attrLen = 1;
        }
    }
    Tcl_ResetResult(interp);
    switch (mode) {
        case XP_CHILD:
            result = domXPointerChild
                (node, all, instance, type, element, attrName, 
                 attrValue, attrLen, tcldom_xpointerAddCallback, interp);
            break;

        case XP_DESCENDANT:
            result = domXPointerDescendant
                (node, all, instance, &i, type, element, attrName, 
                 attrValue, attrLen, tcldom_xpointerAddCallback, interp);
            break;

        case XP_ANCESTOR:
            result = domXPointerAncestor
                (node, all, instance, &i, type, element, attrName, 
                 attrValue, attrLen, tcldom_xpointerAddCallback, interp);
            break;

        case XP_FSIBLING:
            result = domXPointerXSibling
                (node, 1, all, instance, type, element, attrName, 
                 attrValue, attrLen, tcldom_xpointerAddCallback, interp);
            break;

        case XP_PSIBLING:
            result = domXPointerXSibling
                (node, 0, all, instance, type, element, attrName, 
                 attrValue,  attrLen, tcldom_xpointerAddCallback, interp);
            break;
    }
    if (result != 0) {
        return TCL_ERROR;
    }
    return TCL_OK;
}


/*----------------------------------------------------------------------------
|   tcldom_getNodeFromObj
|
\---------------------------------------------------------------------------*/
domNode * tcldom_getNodeFromObj (
    Tcl_Interp  *interp,
    Tcl_Obj     *nodeObj
)
{
    Tcl_CmdInfo  cmdInfo;
    domNode     *node = NULL;
    char        *nodeName;
    char         eolcheck;

    GetTcldomDATA;

    if (nodeObj->typePtr == &tdomNodeType) {
        return (domNode*)nodeObj->internalRep.otherValuePtr;
    }
    
    if (TcldomDATA(dontCreateObjCommands)) {
        if (SetTdomNodeFromAny (interp, nodeObj) == TCL_OK) {
            return (domNode*)nodeObj->internalRep.otherValuePtr;
        }
        return NULL;
    }
    
    nodeName = Tcl_GetString(nodeObj);
    if (strncmp(nodeName, "domNode", 7)) {
        SetResult3("Parameter \"", nodeName, "\" is not a domNode.");
        return NULL;
    }
    if (sscanf(&nodeName[7], "%p%1c", (void **)&node, &eolcheck) != 1) {
        if (!Tcl_GetCommandInfo(interp, nodeName, &cmdInfo)) {
            SetResult3("Parameter \"", nodeName, "\" is not a domNode.");
            return NULL;
        }
        if (   (cmdInfo.isNativeObjectProc == 0)
            || (cmdInfo.objProc != (Tcl_ObjCmdProc*)tcldom_NodeObjCmd)) {
            SetResult3("Parameter \"", nodeName, "\" is not a domNode"
                       " object command.");
            return NULL;
        }
        node = (domNode*)cmdInfo.objClientData;
    }

    return node;
}

/*----------------------------------------------------------------------------
|   tcldom_getNodeFromName
|
\---------------------------------------------------------------------------*/
domNode * tcldom_getNodeFromName (
    Tcl_Interp  *interp,
    char        *nodeName,
    char       **errMsg
)
{
    Tcl_CmdInfo  cmdInfo;
    domNode     *node = NULL;
    char         eolcheck;

    if (strncmp(nodeName, "domNode", 7)) {
        *errMsg = "parameter not a domNode!";
        return NULL;
    }
    if (sscanf(&nodeName[7], "%p%1c", (void **)&node, &eolcheck) != 1) {
        if (!Tcl_GetCommandInfo(interp, nodeName, &cmdInfo)) {
           *errMsg = "parameter not a domNode!";
           return NULL;
        }
        if (   (cmdInfo.isNativeObjectProc == 0)
            || (cmdInfo.objProc != (Tcl_ObjCmdProc*)tcldom_NodeObjCmd)) {
            *errMsg = "parameter not a domNode object command!";
            return NULL;
        }
        node = (domNode*)cmdInfo.objClientData;
    }

    return node;
}

/*----------------------------------------------------------------------------
|   tcldom_getDocumentFromName
|
\---------------------------------------------------------------------------*/
domDocument * tcldom_getDocumentFromName (
    Tcl_Interp  *interp,
    char        *docName,
    char       **errMsg
)
{
    Tcl_CmdInfo  cmdInfo;
    domDocument *doc = NULL;
    int          shared = 1;
    char         eolcheck;

    if (strncmp(docName, "domDoc", 6)) {
        *errMsg = "parameter not a domDoc!";
        return NULL;
    }
    if (sscanf(&docName[6], "%p%1c", (void **)&doc, &eolcheck) != 1) {
        if (!Tcl_GetCommandInfo(interp, docName, &cmdInfo)) {
            *errMsg = "parameter not a domDoc!";
            return NULL;
        }
        if (   (cmdInfo.isNativeObjectProc == 0)
            || (cmdInfo.objProc != (Tcl_ObjCmdProc*)tcldom_DocObjCmd)) {
            *errMsg = "parameter not a domDoc object command!";
            return NULL;
        }
        doc = ((domDeleteInfo*)cmdInfo.objClientData)->document;
    }

    TDomThreaded(shared = tcldom_CheckDocShared(doc));

    if (!shared) {
        *errMsg = "parameter not a shared domDoc!";
        return NULL;
    }

    return doc;
}


/*----------------------------------------------------------------------------
|   tcldom_appendXML
|
\---------------------------------------------------------------------------*/
int tcldom_appendXML (
    Tcl_Interp *interp,
    domNode    *node,
    Tcl_Obj    *obj
)
{
    char        *xml_string;
    Tcl_Obj     *extResolver = NULL;
    domLength    xml_string_len;
    int          resultcode = 0;
    int          ignorexmlns = 0;
    domDocument *doc;
    domNode     *nodeToAppend;
    XML_Parser   parser;
    domParseForestErrorData forestError;

    GetTcldomDATA;

    xml_string = Tcl_GetStringFromObj(obj, &xml_string_len);

#ifdef TDOM_NO_EXPAT
    SetResult("tDOM was compiled without Expat!");
    return TCL_ERROR;
#else
    parser = XML_ParserCreate_MM(NULL, MEM_SUITE, NULL);

    if (node->ownerDocument->extResolver) {
        extResolver = Tcl_NewStringObj(node->ownerDocument->extResolver, -1);
        Tcl_IncrRefCount (extResolver);
    }
    if (node->ownerDocument->nodeFlags & IGNORE_XMLNS) {
        ignorexmlns = 1;
    }

    doc = domReadDocument(parser,
                          xml_string,
                          xml_string_len,
                          1,
                          0,
                          TcldomDATA(storeLineColumn),
                          ignorexmlns,
                          0,
                          NULL,
                          NULL,
                          NULL,
                          extResolver,
                          0,
                          0,
                          (int) XML_PARAM_ENTITY_PARSING_ALWAYS,
#ifndef TDOM_NO_SCHEMA
                          NULL,
#endif
                          interp,
                          &forestError,
                          &resultcode);
    if (extResolver) {
        Tcl_DecrRefCount(extResolver);
    }
    if (doc == NULL) {
        tcldom_reportErrorLocation (
            interp, 20, 40, XML_GetCurrentLineNumber(parser),
            XML_GetCurrentColumnNumber(parser), xml_string, NULL,
            XML_GetCurrentByteIndex(parser),
            XML_ErrorString(XML_GetErrorCode(parser)));
        XML_ParserFree(parser);
        return TCL_ERROR;
    }
    XML_ParserFree(parser);
    
    nodeToAppend = doc->rootNode->firstChild;
    while (nodeToAppend) {
        domAppendChild(node, nodeToAppend);
        nodeToAppend = nodeToAppend->nextSibling;
    }
    domFreeDocument(doc, NULL, NULL);

    return tcldom_setInterpAndReturnVar(interp, node, 0, NULL);
#endif
}


/*----------------------------------------------------------------------------
|   tcldom_xpathResultSet
|
\---------------------------------------------------------------------------*/
static
int tcldom_xpathResultSet (
    Tcl_Interp      *interp,
    xpathResultSet  *rs,
    xpathResultType *type,
    Tcl_Obj         *value
)
{
    int          rc, i;
    Tcl_Obj     *namePtr, *objv[2];
    domAttrNode *attr;
    domNodeType  startType;
    int          mixedNodeSet;

    switch (rs->type) {
        case EmptyResult:
            *type = EmptyResult;
             Tcl_SetStringObj(value, "", -1);
             break;

        case BoolResult:
            *type = BoolResult;
             Tcl_SetIntObj(value, rs->intvalue);
             break;

        case IntResult:
            *type = IntResult;
             Tcl_SetLongObj(value, rs->intvalue);
             break;

        case RealResult:
            *type = RealResult;
             Tcl_SetDoubleObj(value, rs->realvalue);
             break;
             
        case NaNResult:
            *type = NaNResult;
             Tcl_SetStringObj(value, "NaN", -1);
             break;

        case InfResult:
            *type = InfResult;
             Tcl_SetStringObj(value, "Infinity", -1);
             break;

        case NInfResult:
            *type = NInfResult;
             Tcl_SetStringObj(value, "-Infinity", -1);
             break;
             
        case StringResult:
            *type = StringResult;
             Tcl_SetStringObj(value, rs->string, rs->string_len);
             break;

        case xNodeSetResult:
             startType = rs->nodes[0]->nodeType;
             mixedNodeSet = 0;
             for (i=0; i<rs->nr_nodes; i++) {
                 if (rs->nodes[i]->nodeType != startType) mixedNodeSet = 1;

                 if (rs->nodes[i]->nodeType == ATTRIBUTE_NODE) {
                     attr = (domAttrNode*)rs->nodes[i];
                     objv[0] = Tcl_NewStringObj(attr->nodeName, -1);
                     objv[1] = Tcl_NewStringObj(attr->nodeValue,
                                                attr->valueLength);
                     namePtr = Tcl_NewListObj(2, objv);
                 } else {
                     namePtr = tcldom_returnNodeObj(interp, rs->nodes[i]);
                 }
                 rc = Tcl_ListObjAppendElement(interp, value, namePtr);
                 if (rc != TCL_OK) {
                     Tcl_DecrRefCount(namePtr);
                     return rc;
                 }
             }
             if (mixedNodeSet) {
                 *type = MixedResult;
             } else {
                 if (startType == ATTRIBUTE_NODE)
                     *type = AttrnodesResult;
                 else
                     *type = NodesResult;
             }
             break;

        default:
            Tcl_Panic("Invalid xpathResultType %s in tcldom_xpathResultSet!",
                      xpathResultType2string(rs->type));
            break;
            
    }
    return TCL_OK;
}


/*----------------------------------------------------------------------------
|   tcldom_xpathFuncCallBack
|
\---------------------------------------------------------------------------*/
int tcldom_xpathFuncCallBack (
    void            *clientData,
    char            *functionName,
    domNode         *ctxNode,
    int              position,
    xpathResultSet  *nodeList,
    domNode         *UNUSED(exprContext),
    int              argc,
    xpathResultSets *args,
    xpathResultSet  *result,
    char           **errMsg
)
{
    Tcl_Interp  *interp = (Tcl_Interp*) clientData;
    char         tclxpathFuncName[220], objCmdName[80];
    char         *errStr, *typeStr;
    Tcl_Obj     *resultPtr, *objv[MAX_REWRITE_ARGS], *type, *value, *nodeObj,
                *tmpObj;
    Tcl_CmdInfo  cmdInfo;
    int          objc, rc, res, boolValue;
    domLength    errStrLen, listLen, i, longValue;
    xpathResultType rstype;
    double       doubleValue;
    domNode     *node;

    DBG(fprintf(stderr, "tcldom_xpathFuncCallBack functionName=%s "
                "position=%d argc=%d\n", functionName, position, argc);)

    if (strlen(functionName) > 200) {
        *errMsg = (char*)MALLOC (80 + strlen (functionName));
        strcpy (*errMsg, "Unreasonable long XPath function name: \"");
        strcat (*errMsg, functionName);
        strcat (*errMsg, "\"!");
        return XPATH_EVAL_ERR;
    }
    sprintf (tclxpathFuncName, "::dom::xpathFunc::%s", functionName);
    DBG(fprintf(stderr, "testing %s\n", tclxpathFuncName);)
    rc = Tcl_GetCommandInfo (interp, tclxpathFuncName, &cmdInfo);
    if (!rc) {
        *errMsg = (char*)MALLOC (80 + strlen (functionName));
        strcpy (*errMsg, "Unknown XPath function: \"");
        strcat (*errMsg, functionName);
        strcat (*errMsg, "\"!");
        return XPATH_EVAL_ERR;
    }
    if (!cmdInfo.isNativeObjectProc) {
        *errMsg = (char*)tdomstrdup("can't access Tcl level method!");
        return XPATH_EVAL_ERR;
    }
    if ( (5+(2*argc)) >= MAX_REWRITE_ARGS) {
        *errMsg = (char*)tdomstrdup("too many args for Tcl level method!");
        return XPATH_EVAL_ERR;
    }
    objc = 0;
    objv[objc] = Tcl_NewStringObj(tclxpathFuncName, -1);
    Tcl_IncrRefCount(objv[objc++]);
    if (ctxNode->nodeType == ATTRIBUTE_NODE) {
        tcldom_createNodeObj(interp, ((domAttrNode*)ctxNode)->parentNode,
                             objCmdName);
        tmpObj = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, tmpObj, 
                                 Tcl_NewStringObj(objCmdName, -1));
        Tcl_ListObjAppendElement(
            interp, tmpObj,
            Tcl_NewStringObj(((domAttrNode*)ctxNode)->nodeName, -1));
    } else {
        tmpObj = tcldom_returnNodeObj(interp, ctxNode);
    }
    objv[objc] = tmpObj;
    Tcl_IncrRefCount(objv[objc++]);

    objv[objc] = Tcl_NewIntObj(position);
    Tcl_IncrRefCount(objv[objc++]);
    value = Tcl_NewObj();
    tcldom_xpathResultSet(interp, nodeList, &rstype, value);
    objv[objc] = Tcl_NewStringObj(xpathResultType2string(rstype), -1);
    Tcl_IncrRefCount(objv[objc++]);
    objv[objc] = value;
    Tcl_IncrRefCount(objv[objc++]);

    for (i=0; i<argc; i++) {
        value = Tcl_NewObj();
        tcldom_xpathResultSet(interp, args[i], &rstype, value);
        objv[objc] = Tcl_NewStringObj (xpathResultType2string(rstype), -1);
        Tcl_IncrRefCount(objv[objc++]);
        objv[objc] = value;
        Tcl_IncrRefCount(objv[objc++]);
    }
    rc = (cmdInfo.objProc(cmdInfo.objClientData, interp, objc, objv));
    if (rc == TCL_OK) {
        xpathRSInit(result);
        resultPtr = Tcl_GetObjResult(interp);
        rc = Tcl_ListObjLength(interp, resultPtr, &listLen);
        if (rc == TCL_OK) {
            if (listLen == 1) {
                rsSetString(result, Tcl_GetString(resultPtr));
                res = XPATH_OK;
                Tcl_ResetResult(interp);
                goto funcCallCleanup;
            }
            if (listLen != 2) {
                *errMsg = (char*)tdomstrdup("wrong return tuple; "
                                            "must be {type value}!");
                res = XPATH_EVAL_ERR;
                goto funcCallCleanup;
            }
            rc = Tcl_ListObjIndex(interp, resultPtr, 0, &type);
            rc = Tcl_ListObjIndex(interp, resultPtr, 1, &value);
            typeStr = Tcl_GetString(type);
            if (strcmp(typeStr, "bool")==0) {
                rc = Tcl_GetBooleanFromObj(interp, value, &boolValue);
                rsSetBool(result, boolValue );
            } else
            if (strcmp(typeStr, "number")==0) {
                rc = Tcl_GetSizeIntFromObj(interp, value, &longValue);
                if (rc == TCL_OK) {
                    rsSetLong(result, longValue);
                } else {
                    rc = Tcl_GetDoubleFromObj(interp, value, &doubleValue);
                    rsSetReal(result, doubleValue);
                }
            } else
            if (strcmp(typeStr, "string")==0) {
                rsSetString(result, Tcl_GetString(value));
            } else
            if (strcmp(typeStr, "nodes")==0) {
                rc = Tcl_ListObjLength(interp, value, &listLen);
                if (rc != TCL_OK) {
                    *errMsg = tdomstrdup("value not a node list!");
                    res = XPATH_EVAL_ERR;
                    goto funcCallCleanup;
                }
                for (i=0; i < listLen; i++) {
                    rc = Tcl_ListObjIndex(interp, value, i, &nodeObj);
                    node = tcldom_getNodeFromObj(interp, nodeObj);
                    if (node == NULL) {
                        *errMsg = tdomstrdup(Tcl_GetStringResult(interp));
                        res = XPATH_EVAL_ERR;
                        goto funcCallCleanup;
                    }
                    rsAddNode(result, node);
                }
                sortByDocOrder(result);
            } else
            if (strcmp(typeStr, "attrnodes")==0) {
                *errMsg = tdomstrdup("attrnodes not implemented yet!");
                res = XPATH_EVAL_ERR;
                goto funcCallCleanup;
            } else
            if (strcmp(typeStr, "attrvalues")==0) {
                rsSetString(result, Tcl_GetString(value));
            } else {
                *errMsg = (char*)MALLOC (80 + strlen (typeStr)
                                         + strlen (functionName));
                strcpy(*errMsg, "Unknown type of return value \"");
                strcat(*errMsg, typeStr);
                strcat(*errMsg, "\" from Tcl coded XPath function \"");
                strcat(*errMsg, functionName);
                strcat(*errMsg, "\"!");
                res = XPATH_EVAL_ERR;
                goto funcCallCleanup;
            }
        } else {
            DBG(fprintf(stderr, "ListObjLength != TCL_OK "
                        "--> returning XPATH_EVAL_ERR \n");)
            res = XPATH_EVAL_ERR;
            goto funcCallCleanup;
        }
        Tcl_ResetResult(interp);
        res = XPATH_OK;
    } else {
        errStr = Tcl_GetStringFromObj( Tcl_GetObjResult(interp), &errStrLen);
        *errMsg = (char*)MALLOC(120+strlen(functionName) + errStrLen);
        strcpy(*errMsg, "Tcl error while executing XPath extension function '");
        strcat(*errMsg, functionName );
        strcat(*errMsg, "':\n" );
        strcat(*errMsg, errStr);
        Tcl_ResetResult(interp);
        DBG(fprintf(stderr, "returning XPATH_EVAL_ERR \n");)
        res = XPATH_EVAL_ERR;
    }
 funcCallCleanup:
    for (i = 0; i < objc; i++) {
        Tcl_DecrRefCount(objv[i]);
    }
    return res;
}

/*----------------------------------------------------------------------------
|   tcldom_xsltMsgCB
|
\---------------------------------------------------------------------------*/
static
int tcldom_xsltMsgCB (
    void *clientData,
    char *str,
    domLength length,
    int   terminate
    )
{
    XsltMsgCBInfo *msgCBInfo = (XsltMsgCBInfo *)clientData;
    Tcl_Obj       *cmdPtr;
    int            rc;
    
    if (msgCBInfo->msgcmd == NULL) {
        return 0;
    }
    
    cmdPtr = Tcl_DuplicateObj(msgCBInfo->msgcmd);
    Tcl_IncrRefCount(cmdPtr);
    if (Tcl_ListObjAppendElement(msgCBInfo->interp, cmdPtr, 
                                 Tcl_NewStringObj(str, length)) != TCL_OK) {
        Tcl_DecrRefCount(cmdPtr);
        return 1;
    }
    if (terminate) {
        Tcl_ListObjAppendElement(msgCBInfo->interp, cmdPtr,
                                 Tcl_NewBooleanObj(1));
    } else {
        Tcl_ListObjAppendElement(msgCBInfo->interp, cmdPtr,
                                 Tcl_NewBooleanObj(0));
    }
    rc = Tcl_GlobalEvalObj(msgCBInfo->interp, cmdPtr);
    Tcl_DecrRefCount(cmdPtr);
    switch (rc) {
    case TCL_OK: return 0;
    case TCL_BREAK: return 3;
    default: return rc;
    }
}

/*----------------------------------------------------------------------------
|   tcldom_xpathResolveVar
|
\---------------------------------------------------------------------------*/
static
char * tcldom_xpathResolveVar (
    void  *clientData,
    char  *strToParse,
    domLength *offset,
    char **errMsg
    )
{
    const char *varValue;
    const char *termPtr;
    Tcl_Interp *interp = (Tcl_Interp *) clientData;
    
    *offset = 0;
    varValue = Tcl_ParseVar(interp, strToParse, &termPtr);
    if (varValue) {
        *offset = termPtr - strToParse;
        /* If strToParse start with a single '$' without a following
         * var name (according to Tcl var name rules), Tcl_ParseVar()
         * doesn't report a parsing error but returns just a pointer
         * to a static string "$". */ 
        if (*offset == 1) {
            *errMsg = tdomstrdup ("Missing var name after '$'.");
            varValue = NULL;
        }
    } else {
        *errMsg = tdomstrdup (Tcl_GetStringResult(interp));
    }
    Tcl_ResetResult (interp);
    return (char*)varValue;
}

static
int selectNodesQueryList (
    Tcl_Interp       * interp,
    domNode          * node,
    Tcl_Obj          * queryList,
    ast                tt, 
    int                queryListInd,
    int                queryListLen,
    char            ** prefixMappings,
    xpathCBs         * cbs,
    xpathParseVarCB  * parseVarCB,
    Tcl_HashTable    * cache,
    Tcl_Obj          * result,
    xpathResultType  * type
)
{

    Tcl_Obj       *queryObj, *thisResult;
    char          *query, *errMsg = NULL;
    xpathResultSet nodeList, rs;
    int            rc, hnew = 1, docOrder = 1, i;
    ast            t;
    Tcl_HashEntry *h = NULL;
    xpathResultType rstype;

    xpathRSInit( &nodeList);
    rsAddNodeFast( &nodeList, node);
    xpathRSInit(&rs);

    rc = xpathEvalSteps( tt, &nodeList, node, node, 0, &docOrder, cbs,
                         &rs, &errMsg);
    xpathRSFree( &nodeList );
    if (rc) {
        Tcl_ResetResult (interp);
        Tcl_AppendResult (interp, "invalid XPath query: '",
                          errMsg, "'", NULL);
        if (errMsg) {
            FREE(errMsg);
        }
        xpathRSFree( &rs );
        return TCL_ERROR;
    }
    queryListInd++;
    if (queryListInd < queryListLen) {
        if (rs.type != xNodeSetResult && rs.type != EmptyResult) {
            Tcl_ResetResult (interp);
            Tcl_AppendResult (interp, "only the last XPath query in the query "
                              "list is allowed to select something else then "
                              "nodes", NULL);
            xpathRSFree( &rs );
            return TCL_ERROR;
        }
        if (rs.type == EmptyResult) {
            if (*type) {
                if (*type != EmptyResult) {
                    *type = MixedResult;
                }
            } else {
                *type = EmptyResult;
            }
            return TCL_OK;
        }
        Tcl_ListObjIndex(interp, queryList, queryListInd, &queryObj);
        query = Tcl_GetString (queryObj);

        if (cache) {
            h = Tcl_CreateHashEntry (cache, query, &hnew);
        }
        if (hnew) {
            rc = xpathParse(query, node, XPATH_EXPR, prefixMappings,
                            parseVarCB, &t, &errMsg);
            if (rc) {
                if (h != NULL) {
                    Tcl_DeleteHashEntry(h);
                }
                Tcl_ResetResult (interp);
                Tcl_AppendResult (interp, "invalid XPath query '", query, "': ",
                                  errMsg, NULL);
                FREE (errMsg);
                xpathRSFree( &rs );
                return TCL_ERROR;
            }
            if (cache) {
                Tcl_SetHashValue(h, t);
            }
        } else {
            t = (ast)Tcl_GetHashValue(h);
        }
        rc = TCL_OK;
        for (i=0; i < rs.nr_nodes; i++) {
            rc = selectNodesQueryList (interp, rs.nodes[i], queryList, t,
                                       queryListInd, queryListLen,
                                       prefixMappings, cbs, parseVarCB, cache,
                                       result, type);
            if (rc != TCL_OK) {
                break;
            }                
        }
        if (!cache) {
            xpathFreeAst(t);
        }
        if (rc != TCL_OK) {
            xpathRSFree (&rs);
            return TCL_ERROR;
        }
    } else {
        thisResult = Tcl_NewListObj (0, NULL);
        Tcl_IncrRefCount (thisResult);
        tcldom_xpathResultSet(interp, &rs, &rstype, thisResult);
        Tcl_ListObjAppendElement (interp, result, thisResult);
        if (*type) {
            if (*type != rstype) {
                *type = MixedResult;
            }
        } else {
            *type = rstype;
        }
        Tcl_DecrRefCount (thisResult);
    }
    xpathRSFree (&rs);
    return TCL_OK;
}

/*----------------------------------------------------------------------------
|   tcldom_selectNodes
|
\---------------------------------------------------------------------------*/
static
int tcldom_selectNodes (
    Tcl_Interp *interp,
    domNode    *node,
    int         objc,
    Tcl_Obj    *const objv[]
)
{
    char          *xpathQuery, *typeVar, *option, *query;
    char          *errMsg = NULL, **mappings = NULL;
    int            rc, optionIndex, localmapping = 0, cache = 0;
    int            mappingListObjLen = 0, list = 0, hnew;
    domLength      i, len, xpathListLen;
    xpathResultSet rs;
    Tcl_Obj       *objPtr, *objPtr1, *mappingListObj = NULL;
    Tcl_Obj       *queryObj, *result;
    xpathCBs       cbs;
    xpathParseVarCB parseVarCB;
    Tcl_HashTable *xpathCache = NULL;
    Tcl_HashEntry *h = NULL;
    ast            t;
    xpathResultType rstype;

    static const char *selectNodesOptions[] = {
        "-namespaces", "-cache", "-list", NULL
    };
    enum selectNodesOption {
        o_namespaces, o_cache, o_list
    };

    if (objc < 2) {
        SetResult("Wrong # of arguments.");
        return TCL_ERROR;
    }
    while (objc > 2) {
        option = Tcl_GetString (objv[1]);
        if (option[0] != '-') {
            break;
        }
        if (Tcl_GetIndexFromObj (NULL, objv[1], selectNodesOptions, "option",
                                 0, &optionIndex) != TCL_OK) {
            break;
        }
        switch ((enum selectNodesOption) optionIndex) {
        case o_namespaces:
            rc = Tcl_ListObjLength (interp, objv[2], &len);
            if (rc != TCL_OK || (len % 2) != 0) {
                SetResult ("The \"-namespaces\" option requires a 'prefix"
                           " namespace' pairs list as argument");
                rc = TCL_ERROR;
                goto cleanup;
            }
            if (mappings) {
                for (i = 0; i < mappingListObjLen; i++) {
                    Tcl_ListObjIndex (interp, mappingListObj, i, &objPtr1);
                    Tcl_DecrRefCount (objPtr1);
                }
                Tcl_DecrRefCount (mappingListObj);
                FREE (mappings);
            }
            mappings = MALLOC (sizeof (char *) * (len + 1));
            localmapping = 1;
            for (i = 0; i < len; i++) {
                Tcl_ListObjIndex (interp, objv[2], i, &objPtr);
                Tcl_IncrRefCount (objPtr);
                mappings[i] = Tcl_GetString (objPtr);
            }
            mappings[len] = NULL;
            mappingListObj = objv[2];
            Tcl_IncrRefCount (mappingListObj);
            mappingListObjLen = len;
            objc -= 2;
            objv += 2;
            break;

        case o_cache:
            if (Tcl_GetBooleanFromObj (interp, objv[2], &cache) != TCL_OK) {
                return TCL_ERROR;
            }
            objc -= 2;
            objv += 2;
            break;

        case o_list:
            list = 1;
            objc--;
            objv++;
            break;
            
        default:
            Tcl_ResetResult (interp);
            Tcl_AppendResult (interp, "bad option \"", 
                              Tcl_GetString (objv[1]), "\"; must be "
                              "-namespaces, -cache or -list", NULL);
            return TCL_ERROR;
        }
    }
    if (objc != 2 && objc != 3) {
        SetResult("Wrong # of arguments.");
        rc = TCL_ERROR;
        goto cleanup;
    }

    xpathQuery = Tcl_GetString(objv[1]);

    xpathRSInit(&rs);

    cbs.funcCB         = tcldom_xpathFuncCallBack;
    cbs.funcClientData = interp;
    cbs.varCB          = NULL;
    cbs.varClientData  = NULL;

    parseVarCB.parseVarCB         = tcldom_xpathResolveVar;
    parseVarCB.parseVarClientData = interp;
    
    if (mappings == NULL) {
        mappings = node->ownerDocument->prefixNSMappings;
    }

    typeVar = NULL;
    if (objc > 2) {
        typeVar = Tcl_GetString(objv[2]);
    }

    if (cache) {
        if (!node->ownerDocument->xpathCache) {
            node->ownerDocument->xpathCache = MALLOC (sizeof (Tcl_HashTable));
            Tcl_InitHashTable (node->ownerDocument->xpathCache,
                               TCL_STRING_KEYS);
        }
        xpathCache = node->ownerDocument->xpathCache;
    }

    if (list) {
        if (Tcl_ListObjLength (interp, objv[1], &xpathListLen) != TCL_OK) {
            SetResult ("If the -list option is given the xpathQuery argument "
                       "must be a valid Tcl list of XPath expressions.");
            return TCL_ERROR;
        }
        if (xpathListLen == 0) {
            Tcl_ResetResult (interp);
            return TCL_OK;
        }
        Tcl_ListObjIndex(interp, objv[1], 0, &queryObj);
        query = Tcl_GetString (queryObj);

        if (cache) {
            h = Tcl_CreateHashEntry (xpathCache, query, &hnew);
        } else {
            hnew = 1;
        }
        if (hnew) {
            rc = xpathParse(query, node, XPATH_EXPR, mappings,
                            &parseVarCB, &t, &errMsg);
            if (rc) {
                if (h != NULL) {
                    Tcl_DeleteHashEntry(h);
                }
                Tcl_ResetResult (interp);
                Tcl_AppendResult (interp, "invalid XPath query '", query, "': ",
                                  errMsg, NULL);
                FREE (errMsg);
                return TCL_ERROR;
            }
            if (cache) {
                Tcl_SetHashValue(h, t);
            }
        } else {
            t = (ast)Tcl_GetHashValue(h);
        }
        result = Tcl_NewListObj (0, NULL);
        rstype = UnknownResult;
        rc = selectNodesQueryList (interp, node, objv[1], t, 0, xpathListLen,
                                   mappings, &cbs, &parseVarCB, xpathCache,
                                   result, &rstype);
        if (!xpathCache) {
            xpathFreeAst (t);
        }
        if (rc != TCL_OK) {
            Tcl_DecrRefCount (result);
            return TCL_ERROR;
        }
        if (typeVar) {
            Tcl_SetVar(interp, typeVar, xpathResultType2string(rstype), 0);
        }
        Tcl_SetObjResult (interp, result);
        return TCL_OK;
    }

    rc = xpathEval (node, node, xpathQuery, mappings, &cbs, &parseVarCB,
                    xpathCache, &errMsg, &rs);

    if (rc != XPATH_OK) {
        xpathRSFree(&rs);
        SetResult(errMsg);
        DBG(fprintf(stderr, "errMsg = %s \n", errMsg);)
        if (errMsg) {
            FREE(errMsg);
        }
        rc = TCL_ERROR;
        goto cleanup;
    }
    if (errMsg) {
        fprintf (stderr, "Why this: '%s'\n", errMsg);
        FREE(errMsg);
    }
    DBG(fprintf(stderr, "before tcldom_xpathResultSet \n");)
    tcldom_xpathResultSet(interp, &rs, &rstype, Tcl_GetObjResult(interp));
    DBG(fprintf(stderr, "after tcldom_xpathResultSet \n");)
    if (typeVar) {
        Tcl_SetVar(interp, typeVar, xpathResultType2string(rstype), 0);
    }
    rc = TCL_OK;

    xpathRSFree( &rs );
cleanup:
    if (localmapping) {
        for (i = 0; i < mappingListObjLen; i++) {
            Tcl_ListObjIndex (interp, mappingListObj, i, &objPtr1);
            Tcl_DecrRefCount (objPtr1);
        }
        Tcl_DecrRefCount (mappingListObj);
        FREE (mappings);
    }
    return rc;
}

/*----------------------------------------------------------------------------
|   tcldom_nameCheck
|
\---------------------------------------------------------------------------*/
int tcldom_nameCheck (
    Tcl_Interp *interp,
    char       *name,
    char       *nameType,
    int         isFQName
)
{
    int         result;

    if (isFQName) {
        result = domIsQNAME (name);
    } else {
        result = domIsNAME (name);
    }
    if (!result) {
        Tcl_ResetResult (interp);
        Tcl_AppendResult (interp, "Invalid ", nameType, " name '", name, "'",
                          (char *) NULL);
        return 0;
    }
    return 1;
}

/*----------------------------------------------------------------------------
|   tcldom_PINameCheck
|
\---------------------------------------------------------------------------*/
int tcldom_PINameCheck (
    Tcl_Interp *interp,
    char       *name
)
{
    /* XML rec, production 17 */
    if (!domIsPINAME (name)) {
        Tcl_ResetResult (interp);
        Tcl_AppendResult (interp, "Invalid processing instruction name '", 
                          name, "'", NULL);
        return 0;
    }
    return 1;
}

/*----------------------------------------------------------------------------
|   tcldom_textCheck
|
\---------------------------------------------------------------------------*/
int tcldom_textCheck (
    Tcl_Interp *interp,
    char       *text,
    char       *errText
)
{
    if (!domIsChar (text)) {
        Tcl_ResetResult (interp);
        Tcl_AppendResult (interp, "Invalid ", errText, " value '", text, "'",
                          (char *) NULL);
        return 0;
    }
    return 1;
}


/*----------------------------------------------------------------------------
|   tcldom_commentCheck
|
\---------------------------------------------------------------------------*/
int tcldom_commentCheck (
    Tcl_Interp *interp,
    char       *text
)
{
    if (!domIsComment (text)) {
        Tcl_ResetResult (interp);
        Tcl_AppendResult (interp, "Invalid comment value '", text, "'",
                          (char *) NULL);
        return 0;
    }
    return 1;
}

/*----------------------------------------------------------------------------
|   tcldom_CDATACheck
|
\---------------------------------------------------------------------------*/
int tcldom_CDATACheck (
    Tcl_Interp *interp,
    char       *text
)
{
    if (!domIsCDATA (text)) {
        Tcl_ResetResult (interp);
        Tcl_AppendResult (interp, "Invalid CDATA section value '", text, "'",
                          (char *) NULL);
        return 0;
    }
    return 1;
}

/*----------------------------------------------------------------------------
|   tcldom_PIValueCheck
|
\---------------------------------------------------------------------------*/
int tcldom_PIValueCheck (
    Tcl_Interp *interp,
    char       *text
)
{
    if (!domIsPIValue (text)) {
        Tcl_ResetResult (interp);
        Tcl_AppendResult (interp, "Invalid processing instruction value '", 
                          text, "'", (char *) NULL);
        return 0;
    }
    return 1;
}

/*----------------------------------------------------------------------------
|   tcldom_appendFromTclList
|
\---------------------------------------------------------------------------*/
static
int tcldom_appendFromTclList (
    Tcl_Interp *interp,
    domNode    *node,
    Tcl_Obj    *obj
)
{
    int       i, rc;
    domLength valueLength, length, attrLength, attrValueLength;
    domLength childListLength;
    Tcl_Obj  *lnode, *tagNameObj, *piNameObj, *valueObj,
             *attrListObj, *attrObj, *childListObj, *childObj;
    char     *tag_name, *pi_name, *value, *attrName, *attrValue;
    domNode  *newnode;

    GetTcldomDATA;

    /*------------------------------------------------------------------------
    |   check format of Tcl list node
    \-----------------------------------------------------------------------*/
    lnode = obj;
    if ((rc = Tcl_ListObjLength(interp, lnode, &length)) != TCL_OK) {
        return rc;
    }
    if ((length != 3) && (length != 2)) {
        SetResult( "invalid node list format!");
        return TCL_ERROR;
    }

    /*------------------------------------------------------------------------
    |   create node
    \-----------------------------------------------------------------------*/
    if ((rc = Tcl_ListObjIndex(interp, lnode, 0, &tagNameObj)) != TCL_OK) {
        return rc;
    }
    tag_name = Tcl_GetString(tagNameObj);

    if (   (strcmp(tag_name,"#cdata")==0) 
        || (strcmp(tag_name,"#text")==0)
        || (strcmp(tag_name,"#comment")==0) ) {
        if (length != 2) {
            SetResult( "invalid text or comment node list format!");
            return TCL_ERROR;
        }
        /*--------------------------------------------------------------------
        |   create text node
        \-------------------------------------------------------------------*/
        if ((rc = Tcl_ListObjIndex(interp, lnode, 1, &valueObj)) != TCL_OK) {
            return rc;
        }
        value = Tcl_GetStringFromObj(valueObj, &valueLength);
        if (strcmp(tag_name, "#text")==0) {
            CheckText (interp, value, "text");
            newnode = (domNode*)domNewTextNode(node->ownerDocument, value,
                                               valueLength, TEXT_NODE);
        } else if (strcmp(tag_name, "#comment")==0) {
            CheckComment (interp, value);
            newnode = (domNode*)domNewTextNode(node->ownerDocument, value,
                                               valueLength, COMMENT_NODE);
        } else {
            CheckCDATA (interp, value);
            newnode = (domNode*)domNewTextNode(node->ownerDocument, value,
                                              valueLength, CDATA_SECTION_NODE);
        }
        domAppendChild(node, newnode);
        return TCL_OK;
    }

    if (strcmp(tag_name,"#pi")==0) {
        if (length != 3) {
            SetResult( "invalid PI node list format!");
            return TCL_ERROR;
        }
        /*--------------------------------------------------------------------
        |   create processing instruction node
        \-------------------------------------------------------------------*/
        if ((rc = Tcl_ListObjIndex(interp, lnode, 1, &piNameObj)) != TCL_OK) {
            return rc;
        }
        if ((rc = Tcl_ListObjIndex(interp, lnode, 2, &valueObj)) != TCL_OK) {
            return rc;
        }
        pi_name = Tcl_GetStringFromObj(piNameObj, &length);
        CheckPIName (interp, pi_name);
        value   = Tcl_GetStringFromObj(valueObj, &valueLength);
        CheckPIValue (interp, value);
        newnode = (domNode*)domNewProcessingInstructionNode
            (node->ownerDocument, pi_name, length, value, valueLength);
        
        domAppendChild(node, newnode);
        return TCL_OK;
    }
    
    /*------------------------------------------------------------------------
    |   create element node
    \-----------------------------------------------------------------------*/
    if (length != 3) {
        SetResult("invalid element node list format!");
        return TCL_ERROR;
    }
    CheckName (interp, tag_name, "tag", 0);
    newnode = domNewElementNode(node->ownerDocument, tag_name);
    domAppendChild(node, newnode);

    /*------------------------------------------------------------------------
    |   create attributes
    \-----------------------------------------------------------------------*/
    if ((rc = Tcl_ListObjIndex(interp, lnode, 1, &attrListObj)) != TCL_OK) {
        return rc;
    }
    if ((rc = Tcl_ListObjLength(interp, attrListObj, &attrLength))
        != TCL_OK) {
        return rc;
    }
    if (attrLength % 2) {
        SetResult("invalid attributes list format!");
        return TCL_ERROR;
    }
    for (i=0; i<attrLength; i++) {
        
        if ((rc = Tcl_ListObjIndex(interp, attrListObj, i, &attrObj))
            != TCL_OK) {
            return rc;
        }
        attrName = Tcl_GetString(attrObj);
        CheckName (interp, attrName, "attribute", 0);
        i++;
        
        if ((rc = Tcl_ListObjIndex(interp, attrListObj, i, &attrObj))
            != TCL_OK) {
            return rc;
        }
        attrValue = Tcl_GetStringFromObj(attrObj, &attrValueLength);
        CheckText (interp, attrValue, "attribute");
        domSetAttribute(newnode, attrName, attrValue);
    }

    /*------------------------------------------------------------------------
    |   add child nodes
    \-----------------------------------------------------------------------*/
    if ((rc = Tcl_ListObjIndex(interp, lnode, 2, &childListObj)) 
        != TCL_OK) {
        return rc;
    }
    if ((rc = Tcl_ListObjLength(interp, childListObj, &childListLength)) 
        != TCL_OK) {
        return rc;
    }
    for (i=0; i<childListLength; i++) {
        if ((rc = Tcl_ListObjIndex(interp, childListObj, i, &childObj))
            != TCL_OK) {
            return rc;
        }
        if ((rc = tcldom_appendFromTclList(interp, newnode, childObj))
            != TCL_OK) {
            return rc;
        }
    }
    return tcldom_setInterpAndReturnVar(interp, node, 0, NULL);
}


/*----------------------------------------------------------------------------
|   tcldom_treeAsTclList
|
\---------------------------------------------------------------------------*/
static
Tcl_Obj * tcldom_treeAsTclList (
    Tcl_Interp *interp,
    domNode    *node
)
{
    Tcl_Obj *name, *value;
    Tcl_Obj *attrsList, *attrName, *attrValue;
    Tcl_Obj *childList;
    Tcl_Obj *objv[4];
    int     result;
    domNode     *child;
    domAttrNode *attrs;



    if (   (node->nodeType == TEXT_NODE) 
        || (node->nodeType == CDATA_SECTION_NODE)) {

        value   = Tcl_NewStringObj(((domTextNode*)node)->nodeValue,
                                     ((domTextNode*)node)->valueLength);
        objv[0] = Tcl_NewStringObj("#text", -1);
        objv[1] = value;
        return Tcl_NewListObj(2, objv);
    }

    if (node->nodeType == COMMENT_NODE) {
        value   = Tcl_NewStringObj(((domTextNode*)node)->nodeValue,
                                     ((domTextNode*)node)->valueLength);
        objv[0] = Tcl_NewStringObj("#comment", -1);
        objv[1] = value;
        return Tcl_NewListObj(2, objv);
    }

    if (node->nodeType == PROCESSING_INSTRUCTION_NODE) {
        domProcessingInstructionNode *dpn;
        dpn = (domProcessingInstructionNode *)node;
        name    = Tcl_NewStringObj(dpn->targetValue, dpn->targetLength);
        value   = Tcl_NewStringObj(dpn->dataValue, dpn->dataLength);
        objv[0] = Tcl_NewStringObj("#pi", -1);
        objv[1] = name;
        objv[2] = value;
        return Tcl_NewListObj(3, objv);
    }

    name = Tcl_NewStringObj(node->nodeName, -1);

    attrsList = Tcl_NewListObj(0, NULL);
    attrs = node->firstAttr;
    while (attrs) {
        attrName = Tcl_NewStringObj(attrs->nodeName, -1);
        attrValue = Tcl_NewStringObj(attrs->nodeValue, attrs->valueLength);
        Tcl_ListObjAppendElement(interp, attrsList, attrName);
        Tcl_ListObjAppendElement(interp, attrsList, attrValue);
        attrs = attrs->nextSibling;
    }

    childList = Tcl_NewListObj(0, NULL);
    if (node->nodeType == ELEMENT_NODE) {
        child = node->firstChild;
        while (child != NULL) {
            result = Tcl_ListObjAppendElement
                (interp, childList, tcldom_treeAsTclList(interp, child));
            if (result != TCL_OK) {
                return NULL;
            }
            child = child->nextSibling;
        }
    }

    objv[0] = name;
    objv[1] = attrsList;
    objv[2] = childList;

    return Tcl_NewListObj(3, objv);
}

/*----------------------------------------------------------------------------
|   tcldom_AppendEscaped
|
\---------------------------------------------------------------------------*/
static
void tcldom_AppendEscaped (
    Tcl_Obj    *xmlString,
    Tcl_Channel chan,
    char       *value,
    domLength   value_length,
    int         outputFlags
)
{
#define APESC_BUF_SIZE 512
#define AP(c)  *b++ = c;
#define AE(s)  pc1 = s; while(*pc1) *b++ = *pc1++;
#define TWOCPE clen2 = UTF8_CHAR_LEN(*(pc+clen)); \
    if (clen) Tcl_UtfToUniChar(pc+clen, &uniChar2);
#define MCP    pc += clen; clen = clen2;
    char  buf[APESC_BUF_SIZE+80], *b, *bLimit,  *pc, *pc1, *pEnd,
          charRef[10];
    int   charDone, i;
    int   clen = 0, clen2 = 0;
    int   unicode;
    Tcl_UniChar uniChar, uniChar2;
    
    b = buf;
    bLimit = b + APESC_BUF_SIZE;
    pc = pEnd = value;
    if (value_length != -1) {
        pEnd = pc + value_length;
    }
    while (   (value_length == -1 && *pc)
           || (value_length != -1 && pc != pEnd)
    ) {
        if ((*pc == '"') && (outputFlags & SERIALIZE_FOR_ATTR
                             || outputFlags & SERIALIZE_ESCAPE_ALL_QUOT)) { 
            AP('&') AP('q') AP('u') AP('o') AP('t') AP(';')
        } else
        if (*pc == '&') { AP('&') AP('a') AP('m') AP('p') AP(';')
        } else
        if (*pc == '<') { AP('&') AP('l') AP('t') AP(';')
        } else
        if (*pc == '>' && !(outputFlags & SERIALIZE_NO_GT_ESCAPE)) {
            AP('&') AP('g') AP('t') AP(';')
        } else
        if ((*pc == '\n') && outputFlags & SERIALIZE_FOR_ATTR) {
            AP('&') AP('#') AP('x') AP('A') AP(';')
        } else
        if ((*pc == '\r') && outputFlags & SERIALIZE_ESCAPE_CR) {
            AP('&') AP('#') AP('x') AP('D') AP(';')
        } else 
        if ((*pc == '\t') && outputFlags & SERIALIZE_ESCAPE_TAB) {
            AP('&') AP('#') AP('x') AP('9') AP(';')
        } else 
        {
            charDone = 0;
            clen = UTF8_CHAR_LEN(*pc);
            if (outputFlags & SERIALIZE_HTML_ENTITIES) {
                charDone = 1;
                Tcl_UtfToUniChar(pc, &uniChar);
                switch (uniChar) 
                {
                case 9:      AE("&Tab;");                  break; /* \t */
                case 10:     AE("&NewLine;");              break; /* \n */
                case 33:     AE("&excl;");                 break; /* ! */
                /* quot aka QUOT */
                case 34:     AE("&quot;");                 break; /* " */
                case 35:     AE("&num;");                  break; /* # */
                case 36:     AE("&dollar;");               break; /* $ */
                case 37:     AE("&percnt;");               break; /* % */
                /* amp aka AMP */
                case 38:     AE("&amp;");                  break; /* & */
                case 39:     AE("&apos;");                 break; /* ' */
                case 40:     AE("&lpar;");                 break; /* ( */
                case 41:     AE("&rpar;");                 break; /* ) */
                /* ast aka midast */
                case 42:     AE("&ast;");                  break; /* * */
                case 43:     AE("&plus;");                 break; /* + */
                case 44:     AE("&comma;");                break; /* , */
                case 46:     AE("&period;");               break; /* . */
                case 47:     AE("&sol;");                  break; /* / */
                case 58:     AE("&colon;");                break; /* : */
                case 59:     AE("&semi;");                 break; /* ; */
                case 60:     TWOCPE;
                    if (clen2 && uniChar2 == 8402) { 
                        AE("&nvlt;");                             /* <⃒ */
                    } else {
                        /* lt aka LT */
                        AE("&lt;");                               /* < */
                    }; MCP;                                break;
                case 61:     TWOCPE;
                    if (clen2 && uniChar2 == 8421) { 
                        AE("&bne;");                              /* =⃥ */
                    } else {
                        AE("&equals;");                           /* = */
                    }; MCP;                                break;
                case 62:     TWOCPE;
                    if (clen2 && uniChar2 == 8402) { 
                        AE("&nvgt;");                             /* >⃒ */
                    } else {
                        /* gt aka GT */
                        AE("&gt;");                               /* > */
                    }; MCP;                                break;
                case 63:     AE("&quest;");                break; /* ? */
                case 64:     AE("&commat;");               break; /* @ */
                /* lsqb aka lbrack */
                case 91:     AE("&lsqb;");                 break; /* [ */
                case 92:     AE("&bsol;");                 break; /* \ */
                /* rsqb aka rbrack */
                case 93:     AE("&rsqb;");                 break; /* ] */
                case 94:     AE("&Hat;");                  break; /* ^ */
                /* lowbar aka UnderBar */
                case 95:     AE("&lowbar;");               break; /* _ */
                /* grave aka DiacriticalGrave */
                case 96:     AE("&grave;");                break; /* ` */
                case 102:    TWOCPE;
                    if (uniChar2 == 106) {
                        AE("&fjlig;"); MCP;                       /* fj */
                    } else {charDone = 0;};                break;
                /* lcub aka lbrace */
                case 123:    AE("&lcub;");                 break; /* { */
                /* vert aka VerticalLine or verbar */
                case 124:    AE("&vert;");                 break; /* | */
                /* rcub aka rbrace */
                case 125:    AE("&rcub;");                 break; /* } */
                /* nbsp aka NonBreakingSpace */
                case 160:    AE("&nbsp;");                 break; /*   */
                case 161:    AE("&iexcl;");                break; /* ¡ */
                case 162:    AE("&cent;");                 break; /* ¢ */
                case 163:    AE("&pound;");                break; /* £ */
                case 164:    AE("&curren;");               break; /* ¤ */
                case 165:    AE("&yen;");                  break; /* ¥ */
                case 166:    AE("&brvbar;");               break; /* ¦ */
                case 167:    AE("&sect;");                 break; /* § */
                /* die aka Dot or DoubleDot or uml */
                case 168:    AE("&die;");                  break; /* ¨ */
                /* copy aka COPY */
                case 169:    AE("&copy;");                 break; /* © */
                case 170:    AE("&ordf;");                 break; /* ª */
                case 171:    AE("&laquo;");                break; /* « */
                case 172:    AE("&not;");                  break; /* ¬ */
                case 173:    AE("&shy;");                  break; /* ­ */
                /* reg aka REG or circledR */
                case 174:    AE("&reg;");                  break; /* ® */
                /* macr aka strns */
                case 175:    AE("&macr;");                 break; /* ¯ */
                case 176:    AE("&deg;");                  break; /* ° */
                /* pm aka PlusMinus or plusmn */
                case 177:    AE("&pm;");                   break; /* ± */
                case 178:    AE("&sup2;");                 break; /* ² */
                case 179:    AE("&sup3;");                 break; /* ³ */
                /* acute aka DiacriticalAcute */
                case 180:    AE("&acute;");                break; /* ´ */
                case 181:    AE("&micro;");                break; /* µ */
                case 182:    AE("&para;");                 break; /* ¶ */
                /* middot aka CenterDot or centerdot */
                case 183:    AE("&middot;");               break; /* · */
                /* cedil aka Cedilla */
                case 184:    AE("&cedil;");                break; /* ¸ */
                case 185:    AE("&sup1;");                 break; /* ¹ */
                case 186:    AE("&ordm;");                 break; /* º */
                case 187:    AE("&raquo;");                break; /* » */
                case 188:    AE("&frac14;");               break; /* ¼ */
                /* half aka frac12 */
                case 189:    AE("&half;");                 break; /* ½ */
                case 190:    AE("&frac34;");               break; /* ¾ */
                case 191:    AE("&iquest;");               break; /* ¿ */
                case 192:    AE("&Agrave;");               break; /* À */
                case 193:    AE("&Aacute;");               break; /* Á */
                case 194:    AE("&Acirc;");                break; /* Â */
                case 195:    AE("&Atilde;");               break; /* Ã */
                case 196:    AE("&Auml;");                 break; /* Ä */
                /* angst aka Aring */
                case 197:    AE("&angst;");                break; /* Å */
                case 198:    AE("&AElig;");                break; /* Æ */
                case 199:    AE("&Ccedil;");               break; /* Ç */
                case 200:    AE("&Egrave;");               break; /* È */
                case 201:    AE("&Eacute;");               break; /* É */
                case 202:    AE("&Ecirc;");                break; /* Ê */
                case 203:    AE("&Euml;");                 break; /* Ë */
                case 204:    AE("&Igrave;");               break; /* Ì */
                case 205:    AE("&Iacute;");               break; /* Í */
                case 206:    AE("&Icirc;");                break; /* Î */
                case 207:    AE("&Iuml;");                 break; /* Ï */
                case 208:    AE("&ETH;");                  break; /* Ð */
                case 209:    AE("&Ntilde;");               break; /* Ñ */
                case 210:    AE("&Ograve;");               break; /* Ò */
                case 211:    AE("&Oacute;");               break; /* Ó */
                case 212:    AE("&Ocirc;");                break; /* Ô */
                case 213:    AE("&Otilde;");               break; /* Õ */
                case 214:    AE("&Ouml;");                 break; /* Ö */
                case 215:    AE("&times;");                break; /* × */
                case 216:    AE("&Oslash;");               break; /* Ø */
                case 217:    AE("&Ugrave;");               break; /* Ù */
                case 218:    AE("&Uacute;");               break; /* Ú */
                case 219:    AE("&Ucirc;");                break; /* Û */
                case 220:    AE("&Uuml;");                 break; /* Ü */
                case 221:    AE("&Yacute;");               break; /* Ý */
                case 222:    AE("&THORN;");                break; /* Þ */
                case 223:    AE("&szlig;");                break; /* ß */
                case 224:    AE("&agrave;");               break; /* à */
                case 225:    AE("&aacute;");               break; /* á */
                case 226:    AE("&acirc;");                break; /* â */
                case 227:    AE("&atilde;");               break; /* ã */
                case 228:    AE("&auml;");                 break; /* ä */
                case 229:    AE("&aring;");                break; /* å */
                case 230:    AE("&aelig;");                break; /* æ */
                case 231:    AE("&ccedil;");               break; /* ç */
                case 232:    AE("&egrave;");               break; /* è */
                case 233:    AE("&eacute;");               break; /* é */
                case 234:    AE("&ecirc;");                break; /* ê */
                case 235:    AE("&euml;");                 break; /* ë */
                case 236:    AE("&igrave;");               break; /* ì */
                case 237:    AE("&iacute;");               break; /* í */
                case 238:    AE("&icirc;");                break; /* î */
                case 239:    AE("&iuml;");                 break; /* ï */
                case 240:    AE("&eth;");                  break; /* ð */
                case 241:    AE("&ntilde;");               break; /* ñ */
                case 242:    AE("&ograve;");               break; /* ò */
                case 243:    AE("&oacute;");               break; /* ó */
                case 244:    AE("&ocirc;");                break; /* ô */
                case 245:    AE("&otilde;");               break; /* õ */
                case 246:    AE("&ouml;");                 break; /* ö */
                /* div aka divide */
                case 247:    AE("&div;");                  break; /* ÷ */
                case 248:    AE("&oslash;");               break; /* ø */
                case 249:    AE("&ugrave;");               break; /* ù */
                case 250:    AE("&uacute;");               break; /* ú */
                case 251:    AE("&ucirc;");                break; /* û */
                case 252:    AE("&uuml;");                 break; /* ü */
                case 253:    AE("&yacute;");               break; /* ý */
                case 254:    AE("&thorn;");                break; /* þ */
                case 255:    AE("&yuml;");                 break; /* ÿ */
                case 256:    AE("&Amacr;");                break; /* Ā */
                case 257:    AE("&amacr;");                break; /* ā */
                case 258:    AE("&Abreve;");               break; /* Ă */
                case 259:    AE("&abreve;");               break; /* ă */
                case 260:    AE("&Aogon;");                break; /* Ą */
                case 261:    AE("&aogon;");                break; /* ą */
                case 262:    AE("&Cacute;");               break; /* Ć */
                case 263:    AE("&cacute;");               break; /* ć */
                case 264:    AE("&Ccirc;");                break; /* Ĉ */
                case 265:    AE("&ccirc;");                break; /* ĉ */
                case 266:    AE("&Cdot;");                 break; /* Ċ */
                case 267:    AE("&cdot;");                 break; /* ċ */
                case 268:    AE("&Ccaron;");               break; /* Č */
                case 269:    AE("&ccaron;");               break; /* č */
                case 270:    AE("&Dcaron;");               break; /* Ď */
                case 271:    AE("&dcaron;");               break; /* ď */
                case 272:    AE("&Dstrok;");               break; /* Đ */
                case 273:    AE("&dstrok;");               break; /* đ */
                case 274:    AE("&Emacr;");                break; /* Ē */
                case 275:    AE("&emacr;");                break; /* ē */
                case 278:    AE("&Edot;");                 break; /* Ė */
                case 279:    AE("&edot;");                 break; /* ė */
                case 280:    AE("&Eogon;");                break; /* Ę */
                case 281:    AE("&eogon;");                break; /* ę */
                case 282:    AE("&Ecaron;");               break; /* Ě */
                case 283:    AE("&ecaron;");               break; /* ě */
                case 284:    AE("&Gcirc;");                break; /* Ĝ */
                case 285:    AE("&gcirc;");                break; /* ĝ */
                case 286:    AE("&Gbreve;");               break; /* Ğ */
                case 287:    AE("&gbreve;");               break; /* ğ */
                case 288:    AE("&Gdot;");                 break; /* Ġ */
                case 289:    AE("&gdot;");                 break; /* ġ */
                case 290:    AE("&Gcedil;");               break; /* Ģ */
                case 292:    AE("&Hcirc;");                break; /* Ĥ */
                case 293:    AE("&hcirc;");                break; /* ĥ */
                case 294:    AE("&Hstrok;");               break; /* Ħ */
                case 295:    AE("&hstrok;");               break; /* ħ */
                case 296:    AE("&Itilde;");               break; /* Ĩ */
                case 297:    AE("&itilde;");               break; /* ĩ */
                case 298:    AE("&Imacr;");                break; /* Ī */
                case 299:    AE("&imacr;");                break; /* ī */
                case 302:    AE("&Iogon;");                break; /* Į */
                case 303:    AE("&iogon;");                break; /* į */
                case 304:    AE("&Idot;");                 break; /* İ */
                /* imath aka inodot */
                case 305:    AE("&imath;");                break; /* ı */
                case 306:    AE("&IJlig;");                break; /* Ĳ */
                case 307:    AE("&ijlig;");                break; /* ĳ */
                case 308:    AE("&Jcirc;");                break; /* Ĵ */
                case 309:    AE("&jcirc;");                break; /* ĵ */
                case 310:    AE("&Kcedil;");               break; /* Ķ */
                case 311:    AE("&kcedil;");               break; /* ķ */
                case 312:    AE("&kgreen;");               break; /* ĸ */
                case 313:    AE("&Lacute;");               break; /* Ĺ */
                case 314:    AE("&lacute;");               break; /* ĺ */
                case 315:    AE("&Lcedil;");               break; /* Ļ */
                case 316:    AE("&lcedil;");               break; /* ļ */
                case 317:    AE("&Lcaron;");               break; /* Ľ */
                case 318:    AE("&lcaron;");               break; /* ľ */
                case 319:    AE("&Lmidot;");               break; /* Ŀ */
                case 320:    AE("&lmidot;");               break; /* ŀ */
                case 321:    AE("&Lstrok;");               break; /* Ł */
                case 322:    AE("&lstrok;");               break; /* ł */
                case 323:    AE("&Nacute;");               break; /* Ń */
                case 324:    AE("&nacute;");               break; /* ń */
                case 325:    AE("&Ncedil;");               break; /* Ņ */
                case 326:    AE("&ncedil;");               break; /* ņ */
                case 327:    AE("&Ncaron;");               break; /* Ň */
                case 328:    AE("&ncaron;");               break; /* ň */
                case 329:    AE("&napos;");                break; /* ŉ */
                case 330:    AE("&ENG;");                  break; /* Ŋ */
                case 331:    AE("&eng;");                  break; /* ŋ */
                case 332:    AE("&Omacr;");                break; /* Ō */
                case 333:    AE("&omacr;");                break; /* ō */
                case 336:    AE("&Odblac;");               break; /* Ő */
                case 337:    AE("&odblac;");               break; /* ő */
                case 338:    AE("&OElig;");                break; /* Œ */
                case 339:    AE("&oelig;");                break; /* œ */
                case 340:    AE("&Racute;");               break; /* Ŕ */
                case 341:    AE("&racute;");               break; /* ŕ */
                case 342:    AE("&Rcedil;");               break; /* Ŗ */
                case 343:    AE("&rcedil;");               break; /* ŗ */
                case 344:    AE("&Rcaron;");               break; /* Ř */
                case 345:    AE("&rcaron;");               break; /* ř */
                case 346:    AE("&Sacute;");               break; /* Ś */
                case 347:    AE("&sacute;");               break; /* ś */
                case 348:    AE("&Scirc;");                break; /* Ŝ */
                case 349:    AE("&scirc;");                break; /* ŝ */
                case 350:    AE("&Scedil;");               break; /* Ş */
                case 351:    AE("&scedil;");               break; /* ş */
                case 352:    AE("&Scaron;");               break; /* Š */
                case 353:    AE("&scaron;");               break; /* š */
                case 354:    AE("&Tcedil;");               break; /* Ţ */
                case 355:    AE("&tcedil;");               break; /* ţ */
                case 356:    AE("&Tcaron;");               break; /* Ť */
                case 357:    AE("&tcaron;");               break; /* ť */
                case 358:    AE("&Tstrok;");               break; /* Ŧ */
                case 359:    AE("&tstrok;");               break; /* ŧ */
                case 360:    AE("&Utilde;");               break; /* Ũ */
                case 361:    AE("&utilde;");               break; /* ũ */
                case 362:    AE("&Umacr;");                break; /* Ū */
                case 363:    AE("&umacr;");                break; /* ū */
                case 364:    AE("&Ubreve;");               break; /* Ŭ */
                case 365:    AE("&ubreve;");               break; /* ŭ */
                case 366:    AE("&Uring;");                break; /* Ů */
                case 367:    AE("&uring;");                break; /* ů */
                case 368:    AE("&Udblac;");               break; /* Ű */
                case 369:    AE("&udblac;");               break; /* ű */
                case 370:    AE("&Uogon;");                break; /* Ų */
                case 371:    AE("&uogon;");                break; /* ų */
                case 372:    AE("&Wcirc;");                break; /* Ŵ */
                case 373:    AE("&wcirc;");                break; /* ŵ */
                case 374:    AE("&Ycirc;");                break; /* Ŷ */
                case 375:    AE("&ycirc;");                break; /* ŷ */
                case 376:    AE("&Yuml;");                 break; /* Ÿ */
                case 377:    AE("&Zacute;");               break; /* Ź */
                case 378:    AE("&zacute;");               break; /* ź */
                case 379:    AE("&Zdot;");                 break; /* Ż */
                case 380:    AE("&zdot;");                 break; /* ż */
                case 381:    AE("&Zcaron;");               break; /* Ž */
                case 382:    AE("&zcaron;");               break; /* ž */
                case 402:    AE("&fnof;");                 break; /* ƒ */
                case 437:    AE("&imped;");                break; /* Ƶ */
                case 501:    AE("&gacute;");               break; /* ǵ */
                case 567:    AE("&jmath;");                break; /* ȷ */
                case 710:    AE("&circ;");                 break; /* ˆ */
                /* caron aka Hacek */
                case 711:    AE("&caron;");                break; /* ˇ */
                /* breve aka Breve */
                case 728:    AE("&breve;");                break; /* ˘ */
                /* dot aka DiacriticalDot */
                case 729:    AE("&dot;");                  break; /* ˙ */
                case 730:    AE("&ring;");                 break; /* ˚ */
                case 731:    AE("&ogon;");                 break; /* ˛ */
                /* tilde aka DiacriticalTilde */
                case 732:    AE("&tilde;");                break; /* ˜ */
                /* dblac aka DiacriticalDoubleAcute */
                case 733:    AE("&dblac;");                break; /* ˝ */
                case 785:    AE("&DownBreve;");            break; /* ̑ */
                case 913:    AE("&Alpha;");                break; /* Α */
                case 914:    AE("&Beta;");                 break; /* Β */
                case 915:    AE("&Gamma;");                break; /* Γ */
                case 916:    AE("&Delta;");                break; /* Δ */
                case 917:    AE("&Epsilon;");              break; /* Ε */
                case 918:    AE("&Zeta;");                 break; /* Ζ */
                case 919:    AE("&Eta;");                  break; /* Η */
                case 920:    AE("&Theta;");                break; /* Θ */
                case 921:    AE("&Iota;");                 break; /* Ι */
                case 922:    AE("&Kappa;");                break; /* Κ */
                case 923:    AE("&Lambda;");               break; /* Λ */
                case 924:    AE("&Mu;");                   break; /* Μ */
                case 925:    AE("&Nu;");                   break; /* Ν */
                case 926:    AE("&Xi;");                   break; /* Ξ */
                case 927:    AE("&Omicron;");              break; /* Ο */
                case 928:    AE("&Pi;");                   break; /* Π */
                case 929:    AE("&Rho;");                  break; /* Ρ */
                case 931:    AE("&Sigma;");                break; /* Σ */
                case 932:    AE("&Tau;");                  break; /* Τ */
                case 933:    AE("&Upsilon;");              break; /* Υ */
                case 934:    AE("&Phi;");                  break; /* Φ */
                case 935:    AE("&Chi;");                  break; /* Χ */
                case 936:    AE("&Psi;");                  break; /* Ψ */
                /* ohm aka Omega */
                case 937:    AE("&ohm;");                  break; /* Ω */
                case 945:    AE("&alpha;");                break; /* α */
                case 946:    AE("&beta;");                 break; /* β */
                case 947:    AE("&gamma;");                break; /* γ */
                case 948:    AE("&delta;");                break; /* δ */
                /* epsi aka epsilon */
                case 949:    AE("&epsi;");                 break; /* ε */
                case 950:    AE("&zeta;");                 break; /* ζ */
                case 951:    AE("&eta;");                  break; /* η */
                case 952:    AE("&theta;");                break; /* θ */
                case 953:    AE("&iota;");                 break; /* ι */
                case 954:    AE("&kappa;");                break; /* κ */
                case 955:    AE("&lambda;");               break; /* λ */
                case 956:    AE("&mu;");                   break; /* μ */
                case 957:    AE("&nu;");                   break; /* ν */
                case 958:    AE("&xi;");                   break; /* ξ */
                case 959:    AE("&omicron;");              break; /* ο */
                case 960:    AE("&pi;");                   break; /* π */
                case 961:    AE("&rho;");                  break; /* ρ */
                /* sigmaf aka sigmav or varsigma */
                case 962:    AE("&sigmaf;");               break; /* ς */
                case 963:    AE("&sigma;");                break; /* σ */
                case 964:    AE("&tau;");                  break; /* τ */
                /* upsi aka upsilon */
                case 965:    AE("&upsi;");                 break; /* υ */
                case 966:    AE("&phi;");                  break; /* φ */
                case 967:    AE("&chi;");                  break; /* χ */
                case 968:    AE("&psi;");                  break; /* ψ */
                case 969:    AE("&omega;");                break; /* ω */
                /* thetav aka thetasym or vartheta */
                case 977:    AE("&thetav;");               break; /* ϑ */
                /* Upsi aka upsih */
                case 978:    AE("&Upsi;");                 break; /* ϒ */
                /* phiv aka straightphi or varphi */
                case 981:    AE("&phiv;");                 break; /* ϕ */
                /* piv aka varpi */
                case 982:    AE("&piv;");                  break; /* ϖ */
                case 988:    AE("&Gammad;");               break; /* Ϝ */
                /* gammad aka digamma */
                case 989:    AE("&gammad;");               break; /* ϝ */
                /* kappav aka varkappa */
                case 1008:   AE("&kappav;");               break; /* ϰ */
                /* rhov aka varrho */
                case 1009:   AE("&rhov;");                 break; /* ϱ */
                /* epsiv aka straightepsilon or varepsilon */
                case 1013:   AE("&epsiv;");                break; /* ϵ */
                /* bepsi aka backepsilon */
                case 1014:   AE("&bepsi;");                break; /* ϶ */
                case 1025:   AE("&IOcy;");                 break; /* Ё */
                case 1026:   AE("&DJcy;");                 break; /* Ђ */
                case 1027:   AE("&GJcy;");                 break; /* Ѓ */
                case 1028:   AE("&Jukcy;");                break; /* Є */
                case 1029:   AE("&DScy;");                 break; /* Ѕ */
                case 1030:   AE("&Iukcy;");                break; /* І */
                case 1031:   AE("&YIcy;");                 break; /* Ї */
                case 1032:   AE("&Jsercy;");               break; /* Ј */
                case 1033:   AE("&LJcy;");                 break; /* Љ */
                case 1034:   AE("&NJcy;");                 break; /* Њ */
                case 1035:   AE("&TSHcy;");                break; /* Ћ */
                case 1036:   AE("&KJcy;");                 break; /* Ќ */
                case 1038:   AE("&Ubrcy;");                break; /* Ў */
                case 1039:   AE("&DZcy;");                 break; /* Џ */
                case 1040:   AE("&Acy;");                  break; /* А */
                case 1041:   AE("&Bcy;");                  break; /* Б */
                case 1042:   AE("&Vcy;");                  break; /* В */
                case 1043:   AE("&Gcy;");                  break; /* Г */
                case 1044:   AE("&Dcy;");                  break; /* Д */
                case 1045:   AE("&IEcy;");                 break; /* Е */
                case 1046:   AE("&ZHcy;");                 break; /* Ж */
                case 1047:   AE("&Zcy;");                  break; /* З */
                case 1048:   AE("&Icy;");                  break; /* И */
                case 1049:   AE("&Jcy;");                  break; /* Й */
                case 1050:   AE("&Kcy;");                  break; /* К */
                case 1051:   AE("&Lcy;");                  break; /* Л */
                case 1052:   AE("&Mcy;");                  break; /* М */
                case 1053:   AE("&Ncy;");                  break; /* Н */
                case 1054:   AE("&Ocy;");                  break; /* О */
                case 1055:   AE("&Pcy;");                  break; /* П */
                case 1056:   AE("&Rcy;");                  break; /* Р */
                case 1057:   AE("&Scy;");                  break; /* С */
                case 1058:   AE("&Tcy;");                  break; /* Т */
                case 1059:   AE("&Ucy;");                  break; /* У */
                case 1060:   AE("&Fcy;");                  break; /* Ф */
                case 1061:   AE("&KHcy;");                 break; /* Х */
                case 1062:   AE("&TScy;");                 break; /* Ц */
                case 1063:   AE("&CHcy;");                 break; /* Ч */
                case 1064:   AE("&SHcy;");                 break; /* Ш */
                case 1065:   AE("&SHCHcy;");               break; /* Щ */
                case 1066:   AE("&HARDcy;");               break; /* Ъ */
                case 1067:   AE("&Ycy;");                  break; /* Ы */
                case 1068:   AE("&SOFTcy;");               break; /* Ь */
                case 1069:   AE("&Ecy;");                  break; /* Э */
                case 1070:   AE("&YUcy;");                 break; /* Ю */
                case 1071:   AE("&YAcy;");                 break; /* Я */
                case 1072:   AE("&acy;");                  break; /* а */
                case 1073:   AE("&bcy;");                  break; /* б */
                case 1074:   AE("&vcy;");                  break; /* в */
                case 1075:   AE("&gcy;");                  break; /* г */
                case 1076:   AE("&dcy;");                  break; /* д */
                case 1077:   AE("&iecy;");                 break; /* е */
                case 1078:   AE("&zhcy;");                 break; /* ж */
                case 1079:   AE("&zcy;");                  break; /* з */
                case 1080:   AE("&icy;");                  break; /* и */
                case 1081:   AE("&jcy;");                  break; /* й */
                case 1082:   AE("&kcy;");                  break; /* к */
                case 1083:   AE("&lcy;");                  break; /* л */
                case 1084:   AE("&mcy;");                  break; /* м */
                case 1085:   AE("&ncy;");                  break; /* н */
                case 1086:   AE("&ocy;");                  break; /* о */
                case 1087:   AE("&pcy;");                  break; /* п */
                case 1088:   AE("&rcy;");                  break; /* р */
                case 1089:   AE("&scy;");                  break; /* с */
                case 1090:   AE("&tcy;");                  break; /* т */
                case 1091:   AE("&ucy;");                  break; /* у */
                case 1092:   AE("&fcy;");                  break; /* ф */
                case 1093:   AE("&khcy;");                 break; /* х */
                case 1094:   AE("&tscy;");                 break; /* ц */
                case 1095:   AE("&chcy;");                 break; /* ч */
                case 1096:   AE("&shcy;");                 break; /* ш */
                case 1097:   AE("&shchcy;");               break; /* щ */
                case 1098:   AE("&hardcy;");               break; /* ъ */
                case 1099:   AE("&ycy;");                  break; /* ы */
                case 1100:   AE("&softcy;");               break; /* ь */
                case 1101:   AE("&ecy;");                  break; /* э */
                case 1102:   AE("&yucy;");                 break; /* ю */
                case 1103:   AE("&yacy;");                 break; /* я */
                case 1105:   AE("&iocy;");                 break; /* ё */
                case 1106:   AE("&djcy;");                 break; /* ђ */
                case 1107:   AE("&gjcy;");                 break; /* ѓ */
                case 1108:   AE("&jukcy;");                break; /* є */
                case 1109:   AE("&dscy;");                 break; /* ѕ */
                case 1110:   AE("&iukcy;");                break; /* і */
                case 1111:   AE("&yicy;");                 break; /* ї */
                case 1112:   AE("&jsercy;");               break; /* ј */
                case 1113:   AE("&ljcy;");                 break; /* љ */
                case 1114:   AE("&njcy;");                 break; /* њ */
                case 1115:   AE("&tshcy;");                break; /* ћ */
                case 1116:   AE("&kjcy;");                 break; /* ќ */
                case 1118:   AE("&ubrcy;");                break; /* ў */
                case 1119:   AE("&dzcy;");                 break; /* џ */
                case 8194:   AE("&ensp;");                 break; /*   */
                case 8195:   AE("&emsp;");                 break; /*   */
                case 8196:   AE("&emsp13;");               break; /*   */
                case 8197:   AE("&emsp14;");               break; /*   */
                case 8199:   AE("&numsp;");                break; /*   */
                case 8200:   AE("&puncsp;");               break; /*   */
                /* thinsp aka ThinSpace */
                case 8201:   AE("&thinsp;");               break; /*   */
                /* hairsp aka VeryThinSpace */
                case 8202:   AE("&hairsp;");               break; /*   */
                /* ZeroWidthSpace aka NegativeMediumSpace or NegativeThickSpace or NegativeThinSpace or NegativeVeryThinSpace */
                case 8203:   AE("&ZeroWidthSpace;");       break; /* ​ */
                case 8204:   AE("&zwnj;");                 break; /* ‌ */
                case 8205:   AE("&zwj;");                  break; /* ‍ */
                case 8206:   AE("&lrm;");                  break; /* ‎ */
                case 8207:   AE("&rlm;");                  break; /* ‏ */
                /* dash aka hyphen */
                case 8208:   AE("&dash;");                 break; /* ‐ */
                case 8211:   AE("&ndash;");                break; /* – */
                case 8212:   AE("&mdash;");                break; /* — */
                case 8213:   AE("&horbar;");               break; /* ― */
                /* Vert aka Verbar */
                case 8214:   AE("&Vert;");                 break; /* ‖ */
                /* lsquo aka OpenCurlyQuote */
                case 8216:   AE("&lsquo;");                break; /* ‘ */
                /* rsquo aka CloseCurlyQuote or rsquor */
                case 8217:   AE("&rsquo;");                break; /* ’ */
                /* sbquo aka lsquor */
                case 8218:   AE("&sbquo;");                break; /* ‚ */
                /* ldquo aka OpenCurlyDoubleQuote */
                case 8220:   AE("&ldquo;");                break; /* “ */
                /* rdquo aka CloseCurlyDoubleQuote or rdquor */
                case 8221:   AE("&rdquo;");                break; /* ” */
                /* bdquo aka ldquor */
                case 8222:   AE("&bdquo;");                break; /* „ */
                case 8224:   AE("&dagger;");               break; /* † */
                /* Dagger aka ddagger */
                case 8225:   AE("&Dagger;");               break; /* ‡ */
                /* bull aka bullet */
                case 8226:   AE("&bull;");                 break; /* • */
                case 8229:   AE("&nldr;");                 break; /* ‥ */
                /* mldr aka hellip */
                case 8230:   AE("&mldr;");                 break; /* … */
                case 8240:   AE("&permil;");               break; /* ‰ */
                case 8241:   AE("&pertenk;");              break; /* ‱ */
                case 8242:   AE("&prime;");                break; /* ′ */
                case 8243:   AE("&Prime;");                break; /* ″ */
                case 8244:   AE("&tprime;");               break; /* ‴ */
                /* bprime aka backprime */
                case 8245:   AE("&bprime;");               break; /* ‵ */
                case 8249:   AE("&lsaquo;");               break; /* ‹ */
                case 8250:   AE("&rsaquo;");               break; /* › */
                /* oline aka OverBar */
                case 8254:   AE("&oline;");                break; /* ‾ */
                case 8257:   AE("&caret;");                break; /* ⁁ */
                case 8259:   AE("&hybull;");               break; /* ⁃ */
                case 8260:   AE("&frasl;");                break; /* ⁄ */
                case 8271:   AE("&bsemi;");                break; /* ⁏ */
                case 8279:   AE("&qprime;");               break; /* ⁗ */
                case 8287:   TWOCPE;
                    if (clen2 && uniChar2 == 8202) { 
                        AE("&ThickSpace;");                       /*    */
                    } else {
                        AE("&MediumSpace;");                      /*   */
                    }; MCP;                                break;
                case 8288:   AE("&NoBreak;");              break; /* ⁠ */
                /* af aka ApplyFunction */
                case 8289:   AE("&af;");                   break; /* ⁡ */
                /* it aka InvisibleTimes */
                case 8290:   AE("&it;");                   break; /* ⁢ */
                /* ic aka InvisibleComma */
                case 8291:   AE("&ic;");                   break; /* ⁣ */
                case 8364:   AE("&euro;");                 break; /* € */
                /* tdot aka TripleDot */
                case 8411:   AE("&tdot;");                 break; /* ⃛ */
                case 8412:   AE("&DotDot;");               break; /* ⃜ */
                /* Copf aka complexes */
                case 8450:   AE("&Copf;");                 break; /* ℂ */
                case 8453:   AE("&incare;");               break; /* ℅ */
                case 8458:   AE("&gscr;");                 break; /* ℊ */
                /* Hscr aka HilbertSpace or hamilt */
                case 8459:   AE("&Hscr;");                 break; /* ℋ */
                /* Hfr aka Poincareplane */
                case 8460:   AE("&Hfr;");                  break; /* ℌ */
                /* Hopf aka quaternions */
                case 8461:   AE("&Hopf;");                 break; /* ℍ */
                case 8462:   AE("&planckh;");              break; /* ℎ */
                /* hbar aka hslash or planck or plankv */
                case 8463:   AE("&hbar;");                 break; /* ℏ */
                /* Iscr aka imagline */
                case 8464:   AE("&Iscr;");                 break; /* ℐ */
                /* Im aka Ifr or image or imagpart */
                case 8465:   AE("&Im;");                   break; /* ℑ */
                /* Lscr aka Laplacetrf or lagran */
                case 8466:   AE("&Lscr;");                 break; /* ℒ */
                case 8467:   AE("&ell;");                  break; /* ℓ */
                /* Nopf aka naturals */
                case 8469:   AE("&Nopf;");                 break; /* ℕ */
                case 8470:   AE("&numero;");               break; /* № */
                case 8471:   AE("&copysr;");               break; /* ℗ */
                /* wp aka weierp */
                case 8472:   AE("&wp;");                   break; /* ℘ */
                /* Popf aka primes */
                case 8473:   AE("&Popf;");                 break; /* ℙ */
                /* Qopf aka rationals */
                case 8474:   AE("&Qopf;");                 break; /* ℚ */
                /* Rscr aka realine */
                case 8475:   AE("&Rscr;");                 break; /* ℛ */
                /* Re aka Rfr or real or realpart */
                case 8476:   AE("&Re;");                   break; /* ℜ */
                /* Ropf aka reals */
                case 8477:   AE("&Ropf;");                 break; /* ℝ */
                case 8478:   AE("&rx;");                   break; /* ℞ */
                /* trade aka TRADE */
                case 8482:   AE("&trade;");                break; /* ™ */
                /* Zopf aka integers */
                case 8484:   AE("&Zopf;");                 break; /* ℤ */
                case 8487:   AE("&mho;");                  break; /* ℧ */
                /* Zfr aka zeetrf */
                case 8488:   AE("&Zfr;");                  break; /* ℨ */
                case 8489:   AE("&iiota;");                break; /* ℩ */
                /* Bscr aka Bernoullis or bernou */
                case 8492:   AE("&Bscr;");                 break; /* ℬ */
                /* Cfr aka Cayleys */
                case 8493:   AE("&Cfr;");                  break; /* ℭ */
                case 8495:   AE("&escr;");                 break; /* ℯ */
                /* Escr aka expectation */
                case 8496:   AE("&Escr;");                 break; /* ℰ */
                /* Fscr aka Fouriertrf */
                case 8497:   AE("&Fscr;");                 break; /* ℱ */
                /* Mscr aka Mellintrf or phmmat */
                case 8499:   AE("&Mscr;");                 break; /* ℳ */
                /* oscr aka order or orderof */
                case 8500:   AE("&oscr;");                 break; /* ℴ */
                /* aleph aka alefsym */
                case 8501:   AE("&aleph;");                break; /* ℵ */
                case 8502:   AE("&beth;");                 break; /* ℶ */
                case 8503:   AE("&gimel;");                break; /* ℷ */
                case 8504:   AE("&daleth;");               break; /* ℸ */
                /* DD aka CapitalDifferentialD */
                case 8517:   AE("&DD;");                   break; /* ⅅ */
                /* dd aka DifferentialD */
                case 8518:   AE("&dd;");                   break; /* ⅆ */
                /* ee aka ExponentialE or exponentiale */
                case 8519:   AE("&ee;");                   break; /* ⅇ */
                /* ii aka ImaginaryI */
                case 8520:   AE("&ii;");                   break; /* ⅈ */
                case 8531:   AE("&frac13;");               break; /* ⅓ */
                case 8532:   AE("&frac23;");               break; /* ⅔ */
                case 8533:   AE("&frac15;");               break; /* ⅕ */
                case 8534:   AE("&frac25;");               break; /* ⅖ */
                case 8535:   AE("&frac35;");               break; /* ⅗ */
                case 8536:   AE("&frac45;");               break; /* ⅘ */
                case 8537:   AE("&frac16;");               break; /* ⅙ */
                case 8538:   AE("&frac56;");               break; /* ⅚ */
                case 8539:   AE("&frac18;");               break; /* ⅛ */
                case 8540:   AE("&frac38;");               break; /* ⅜ */
                case 8541:   AE("&frac58;");               break; /* ⅝ */
                case 8542:   AE("&frac78;");               break; /* ⅞ */
                /* larr aka LeftArrow or ShortLeftArrow or leftarrow or slarr */
                case 8592:   AE("&larr;");                 break; /* ← */
                /* uarr aka ShortUpArrow or UpArrow or uparrow */
                case 8593:   AE("&uarr;");                 break; /* ↑ */
                /* rarr aka RightArrow or ShortRightArrow or rightarrow or srarr */
                case 8594:   AE("&rarr;");                 break; /* → */
                /* darr aka DownArrow or ShortDownArrow or downarrow */
                case 8595:   AE("&darr;");                 break; /* ↓ */
                /* harr aka LeftRightArrow or leftrightarrow */
                case 8596:   AE("&harr;");                 break; /* ↔ */
                /* varr aka UpDownArrow or updownarrow */
                case 8597:   AE("&varr;");                 break; /* ↕ */
                /* nwarr aka UpperLeftArrow or nwarrow */
                case 8598:   AE("&nwarr;");                break; /* ↖ */
                /* nearr aka UpperRightArrow or nearrow */
                case 8599:   AE("&nearr;");                break; /* ↗ */
                /* searr aka LowerRightArrow or searrow */
                case 8600:   AE("&searr;");                break; /* ↘ */
                /* swarr aka LowerLeftArrow or swarrow */
                case 8601:   AE("&swarr;");                break; /* ↙ */
                /* nlarr aka nleftarrow */
                case 8602:   AE("&nlarr;");                break; /* ↚ */
                /* nrarr aka nrightarrow */
                case 8603:   AE("&nrarr;");                break; /* ↛ */
                case 8605:   TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        AE("&nrarrw;");                           /* ↝̸ */
                    } else {
                        /* rarrw aka rightsquigarrow */
                        AE("&rarrw;");                            /* ↝ */
                    }; MCP;                                break;
                /* Larr aka twoheadleftarrow */
                case 8606:   AE("&Larr;");                 break; /* ↞ */
                case 8607:   AE("&Uarr;");                 break; /* ↟ */
                /* Rarr aka twoheadrightarrow */
                case 8608:   AE("&Rarr;");                 break; /* ↠ */
                case 8609:   AE("&Darr;");                 break; /* ↡ */
                /* larrtl aka leftarrowtail */
                case 8610:   AE("&larrtl;");               break; /* ↢ */
                /* rarrtl aka rightarrowtail */
                case 8611:   AE("&rarrtl;");               break; /* ↣ */
                /* mapstoleft aka LeftTeeArrow */
                case 8612:   AE("&mapstoleft;");           break; /* ↤ */
                /* mapstoup aka UpTeeArrow */
                case 8613:   AE("&mapstoup;");             break; /* ↥ */
                /* map aka RightTeeArrow or mapsto */
                case 8614:   AE("&map;");                  break; /* ↦ */
                /* mapstodown aka DownTeeArrow */
                case 8615:   AE("&mapstodown;");           break; /* ↧ */
                /* larrhk aka hookleftarrow */
                case 8617:   AE("&larrhk;");               break; /* ↩ */
                /* rarrhk aka hookrightarrow */
                case 8618:   AE("&rarrhk;");               break; /* ↪ */
                /* larrlp aka looparrowleft */
                case 8619:   AE("&larrlp;");               break; /* ↫ */
                /* rarrlp aka looparrowright */
                case 8620:   AE("&rarrlp;");               break; /* ↬ */
                /* harrw aka leftrightsquigarrow */
                case 8621:   AE("&harrw;");                break; /* ↭ */
                /* nharr aka nleftrightarrow */
                case 8622:   AE("&nharr;");                break; /* ↮ */
                /* lsh aka Lsh */
                case 8624:   AE("&lsh;");                  break; /* ↰ */
                /* rsh aka Rsh */
                case 8625:   AE("&rsh;");                  break; /* ↱ */
                case 8626:   AE("&ldsh;");                 break; /* ↲ */
                case 8627:   AE("&rdsh;");                 break; /* ↳ */
                case 8629:   AE("&crarr;");                break; /* ↵ */
                /* cularr aka curvearrowleft */
                case 8630:   AE("&cularr;");               break; /* ↶ */
                /* curarr aka curvearrowright */
                case 8631:   AE("&curarr;");               break; /* ↷ */
                /* olarr aka circlearrowleft */
                case 8634:   AE("&olarr;");                break; /* ↺ */
                /* orarr aka circlearrowright */
                case 8635:   AE("&orarr;");                break; /* ↻ */
                /* lharu aka LeftVector or leftharpoonup */
                case 8636:   AE("&lharu;");                break; /* ↼ */
                /* lhard aka DownLeftVector or leftharpoondown */
                case 8637:   AE("&lhard;");                break; /* ↽ */
                /* uharr aka RightUpVector or upharpoonright */
                case 8638:   AE("&uharr;");                break; /* ↾ */
                /* uharl aka LeftUpVector or upharpoonleft */
                case 8639:   AE("&uharl;");                break; /* ↿ */
                /* rharu aka RightVector or rightharpoonup */
                case 8640:   AE("&rharu;");                break; /* ⇀ */
                /* rhard aka DownRightVector or rightharpoondown */
                case 8641:   AE("&rhard;");                break; /* ⇁ */
                /* dharr aka RightDownVector or downharpoonright */
                case 8642:   AE("&dharr;");                break; /* ⇂ */
                /* dharl aka LeftDownVector or downharpoonleft */
                case 8643:   AE("&dharl;");                break; /* ⇃ */
                /* rlarr aka RightArrowLeftArrow or rightleftarrows */
                case 8644:   AE("&rlarr;");                break; /* ⇄ */
                /* udarr aka UpArrowDownArrow */
                case 8645:   AE("&udarr;");                break; /* ⇅ */
                /* lrarr aka LeftArrowRightArrow or leftrightarrows */
                case 8646:   AE("&lrarr;");                break; /* ⇆ */
                /* llarr aka leftleftarrows */
                case 8647:   AE("&llarr;");                break; /* ⇇ */
                /* uuarr aka upuparrows */
                case 8648:   AE("&uuarr;");                break; /* ⇈ */
                /* rrarr aka rightrightarrows */
                case 8649:   AE("&rrarr;");                break; /* ⇉ */
                /* ddarr aka downdownarrows */
                case 8650:   AE("&ddarr;");                break; /* ⇊ */
                /* lrhar aka ReverseEquilibrium or leftrightharpoons */
                case 8651:   AE("&lrhar;");                break; /* ⇋ */
                /* rlhar aka Equilibrium or rightleftharpoons */
                case 8652:   AE("&rlhar;");                break; /* ⇌ */
                /* nlArr aka nLeftarrow */
                case 8653:   AE("&nlArr;");                break; /* ⇍ */
                /* nhArr aka nLeftrightarrow */
                case 8654:   AE("&nhArr;");                break; /* ⇎ */
                /* nrArr aka nRightarrow */
                case 8655:   AE("&nrArr;");                break; /* ⇏ */
                /* lArr aka DoubleLeftArrow or Leftarrow */
                case 8656:   AE("&lArr;");                 break; /* ⇐ */
                /* uArr aka DoubleUpArrow or Uparrow */
                case 8657:   AE("&uArr;");                 break; /* ⇑ */
                /* rArr aka DoubleRightArrow or Implies or Rightarrow */
                case 8658:   AE("&rArr;");                 break; /* ⇒ */
                /* dArr aka DoubleDownArrow or Downarrow */
                case 8659:   AE("&dArr;");                 break; /* ⇓ */
                /* iff aka DoubleLeftRightArrow or Leftrightarrow or hArr */
                case 8660:   AE("&iff;");                  break; /* ⇔ */
                /* vArr aka DoubleUpDownArrow or Updownarrow */
                case 8661:   AE("&vArr;");                 break; /* ⇕ */
                case 8662:   AE("&nwArr;");                break; /* ⇖ */
                case 8663:   AE("&neArr;");                break; /* ⇗ */
                case 8664:   AE("&seArr;");                break; /* ⇘ */
                case 8665:   AE("&swArr;");                break; /* ⇙ */
                /* lAarr aka Lleftarrow */
                case 8666:   AE("&lAarr;");                break; /* ⇚ */
                /* rAarr aka Rrightarrow */
                case 8667:   AE("&rAarr;");                break; /* ⇛ */
                case 8669:   AE("&zigrarr;");              break; /* ⇝ */
                /* larrb aka LeftArrowBar */
                case 8676:   AE("&larrb;");                break; /* ⇤ */
                /* rarrb aka RightArrowBar */
                case 8677:   AE("&rarrb;");                break; /* ⇥ */
                /* duarr aka DownArrowUpArrow */
                case 8693:   AE("&duarr;");                break; /* ⇵ */
                case 8701:   AE("&loarr;");                break; /* ⇽ */
                case 8702:   AE("&roarr;");                break; /* ⇾ */
                case 8703:   AE("&hoarr;");                break; /* ⇿ */
                /* forall aka ForAll */
                case 8704:   AE("&forall;");               break; /* ∀ */
                /* comp aka complement */
                case 8705:   AE("&comp;");                 break; /* ∁ */
                case 8706:   TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        AE("&npart;");                            /* ∂̸ */
                    } else {
                        /* part aka PartialD */
                        AE("&part;");                             /* ∂ */
                    }; MCP;                                break;
                /* exist aka Exists */
                case 8707:   AE("&exist;");                break; /* ∃ */
                /* nexist aka NotExists or nexists */
                case 8708:   AE("&nexist;");               break; /* ∄ */
                /* empty aka emptyset or emptyv or varnothing */
                case 8709:   AE("&empty;");                break; /* ∅ */
                /* Del aka nabla */
                case 8711:   AE("&Del;");                  break; /* ∇ */
                /* in aka Element or isin or isinv */
                case 8712:   AE("&in;");                   break; /* ∈ */
                /* notin aka NotElement or notinva */
                case 8713:   AE("&notin;");                break; /* ∉ */
                /* ni aka ReverseElement or SuchThat or niv */
                case 8715:   AE("&ni;");                   break; /* ∋ */
                /* notni aka NotReverseElement or notniva */
                case 8716:   AE("&notni;");                break; /* ∌ */
                /* prod aka Product */
                case 8719:   AE("&prod;");                 break; /* ∏ */
                /* coprod aka Coproduct */
                case 8720:   AE("&coprod;");               break; /* ∐ */
                /* sum aka Sum */
                case 8721:   AE("&sum;");                  break; /* ∑ */
                case 8722:   AE("&minus;");                break; /* − */
                /* mp aka MinusPlus or mnplus */
                case 8723:   AE("&mp;");                   break; /* ∓ */
                /* plusdo aka dotplus */
                case 8724:   AE("&plusdo;");               break; /* ∔ */
                /* setmn aka Backslash or setminus or smallsetminus or ssetmn */
                case 8726:   AE("&setmn;");                break; /* ∖ */
                case 8727:   AE("&lowast;");               break; /* ∗ */
                /* compfn aka SmallCircle */
                case 8728:   AE("&compfn;");               break; /* ∘ */
                /* Sqrt aka radic */
                case 8730:   AE("&Sqrt;");                 break; /* √ */
                /* prop aka Proportional or propto or varpropto or vprop */
                case 8733:   AE("&prop;");                 break; /* ∝ */
                case 8734:   AE("&infin;");                break; /* ∞ */
                case 8735:   AE("&angrt;");                break; /* ∟ */
                case 8736:   TWOCPE;
                    if (clen2 && uniChar2 == 8402) { 
                        AE("&nang;");                             /* ∠⃒ */
                    } else {
                        /* ang aka angle */
                        AE("&ang;");                              /* ∠ */
                    }; MCP;                                break;
                /* angmsd aka measuredangle */
                case 8737:   AE("&angmsd;");               break; /* ∡ */
                case 8738:   AE("&angsph;");               break; /* ∢ */
                /* mid aka VerticalBar or shortmid or smid */
                case 8739:   AE("&mid;");                  break; /* ∣ */
                /* nmid aka NotVerticalBar or nshortmid or nsmid */
                case 8740:   AE("&nmid;");                 break; /* ∤ */
                /* par aka DoubleVerticalBar or parallel or shortparallel or spar */
                case 8741:   AE("&par;");                  break; /* ∥ */
                /* npar aka NotDoubleVerticalBar or nparallel or nshortparallel or nspar */
                case 8742:   AE("&npar;");                 break; /* ∦ */
                /* and aka wedge */
                case 8743:   AE("&and;");                  break; /* ∧ */
                /* or aka vee */
                case 8744:   AE("&or;");                   break; /* ∨ */
                case 8745:   TWOCPE;
                    if (clen2 && uniChar2 == 65024) { 
                        AE("&caps;");                             /* ∩︀ */
                    } else {
                        AE("&cap;");                              /* ∩ */
                    }; MCP;                                break;
                case 8746:   TWOCPE;
                    if (clen2 && uniChar2 == 65024) { 
                        AE("&cups;");                             /* ∪︀ */
                    } else {
                        AE("&cup;");                              /* ∪ */
                    }; MCP;                                break;
                /* int aka Integral */
                case 8747:   AE("&int;");                  break; /* ∫ */
                case 8748:   AE("&Int;");                  break; /* ∬ */
                /* tint aka iiint */
                case 8749:   AE("&tint;");                 break; /* ∭ */
                /* oint aka ContourIntegral or conint */
                case 8750:   AE("&oint;");                 break; /* ∮ */
                /* Conint aka DoubleContourIntegral */
                case 8751:   AE("&Conint;");               break; /* ∯ */
                case 8752:   AE("&Cconint;");              break; /* ∰ */
                case 8753:   AE("&cwint;");                break; /* ∱ */
                /* cwconint aka ClockwiseContourIntegral */
                case 8754:   AE("&cwconint;");             break; /* ∲ */
                /* awconint aka CounterClockwiseContourIntegral */
                case 8755:   AE("&awconint;");             break; /* ∳ */
                /* there4 aka Therefore or therefore */
                case 8756:   AE("&there4;");               break; /* ∴ */
                /* becaus aka Because or because */
                case 8757:   AE("&becaus;");               break; /* ∵ */
                case 8758:   AE("&ratio;");                break; /* ∶ */
                /* Colon aka Proportion */
                case 8759:   AE("&Colon;");                break; /* ∷ */
                /* minusd aka dotminus */
                case 8760:   AE("&minusd;");               break; /* ∸ */
                case 8762:   AE("&mDDot;");                break; /* ∺ */
                case 8763:   AE("&homtht;");               break; /* ∻ */
                case 8764:   TWOCPE;
                    if (clen2 && uniChar2 == 8402) { 
                        AE("&nvsim;");                            /* ∼⃒ */
                    } else {
                        /* sim aka Tilde or thicksim or thksim */
                        AE("&sim;");                              /* ∼ */
                    }; MCP;                                break;
                case 8765:   TWOCPE;
                    if (clen2 && uniChar2 == 817) { 
                        AE("&race;");                             /* ∽̱ */
                    } else {
                        /* bsim aka backsim */
                        AE("&bsim;");                             /* ∽ */
                    }; MCP;                                break;
                case 8766:   TWOCPE;
                    if (clen2 && uniChar2 == 819) { 
                        AE("&acE;");                              /* ∾̳ */
                    } else {
                        /* ac aka mstpos */
                        AE("&ac;");                               /* ∾ */
                    }; MCP;                                break;
                case 8767:   AE("&acd;");                  break; /* ∿ */
                /* wr aka VerticalTilde or wreath */
                case 8768:   AE("&wr;");                   break; /* ≀ */
                /* nsim aka NotTilde */
                case 8769:   AE("&nsim;");                 break; /* ≁ */
                case 8770:   TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        /* esim aka NotEqualTilde */
                        AE("&nesim;");                            /* ≂̸ */
                    } else {
                        /* esim aka EqualTilde or eqsim */
                        AE("&esim;");                             /* ≂ */
                    }; MCP;                                break;
                /* sime aka TildeEqual or simeq */
                case 8771:   AE("&sime;");                 break; /* ≃ */
                /* nsime aka NotTildeEqual or nsimeq */
                case 8772:   AE("&nsime;");                break; /* ≄ */
                /* cong aka TildeFullEqual */
                case 8773:   AE("&cong;");                 break; /* ≅ */
                case 8774:   AE("&simne;");                break; /* ≆ */
                /* ncong aka NotTildeFullEqual */
                case 8775:   AE("&ncong;");                break; /* ≇ */
                /* ap aka TildeTilde or approx or asymp or thickapprox or thkap */
                case 8776:   AE("&ap;");                   break; /* ≈ */
                /* nap aka NotTildeTilde or napprox */
                case 8777:   AE("&nap;");                  break; /* ≉ */
                /* ape aka approxeq */
                case 8778:   AE("&ape;");                  break; /* ≊ */
                case 8779:   TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        AE("&napid;");                            /* ≋̸ */
                    } else {
                        AE("&apid;");                             /* ≋ */
                    }; MCP;                                break;
                /* bcong aka backcong */
                case 8780:   AE("&bcong;");                break; /* ≌ */
                case 8781:   TWOCPE;
                    if (clen2 && uniChar2 == 8402) { 
                        AE("&nvap;");                             /* ≍⃒ */
                    } else {
                        /* CupCap aka asympeq */
                        AE("&CupCap;");                           /* ≍ */
                    }; MCP;                                break;
                case 8782:   TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        /* bump aka NotHumpDownHump */
                        AE("&nbump;");                            /* ≎̸ */
                    } else {
                        /* bump aka Bumpeq or HumpDownHump */
                        AE("&bump;");                             /* ≎ */
                    }; MCP;                                break;
                case 8783:   TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        /* bumpe aka NotHumpEqual */
                        AE("&nbumpe;");                           /* ≏̸ */
                    } else {
                        /* bumpe aka HumpEqual or bumpeq */
                        AE("&bumpe;");                            /* ≏ */
                    }; MCP;                                break;
                case 8784:   TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        AE("&nedot;");                            /* ≐̸ */
                    } else {
                        /* doteq aka DotEqual or esdot */
                        AE("&doteq;");                            /* ≐ */
                    }; MCP;                                break;
                /* eDot aka doteqdot */
                case 8785:   AE("&eDot;");                 break; /* ≑ */
                /* efDot aka fallingdotseq */
                case 8786:   AE("&efDot;");                break; /* ≒ */
                /* erDot aka risingdotseq */
                case 8787:   AE("&erDot;");                break; /* ≓ */
                /* Assign aka colone or coloneq */
                case 8788:   AE("&Assign;");               break; /* ≔ */
                /* ecolon aka eqcolon */
                case 8789:   AE("&ecolon;");               break; /* ≕ */
                /* ecir aka eqcirc */
                case 8790:   AE("&ecir;");                 break; /* ≖ */
                /* cire aka circeq */
                case 8791:   AE("&cire;");                 break; /* ≗ */
                case 8793:   AE("&wedgeq;");               break; /* ≙ */
                case 8794:   AE("&veeeq;");                break; /* ≚ */
                /* trie aka triangleq */
                case 8796:   AE("&trie;");                 break; /* ≜ */
                /* equest aka questeq */
                case 8799:   AE("&equest;");               break; /* ≟ */
                /* ne aka NotEqual */
                case 8800:   AE("&ne;");                   break; /* ≠ */
                case 8801:   TWOCPE;
                    if (clen2 && uniChar2 == 8421) { 
                        AE("&bnequiv;");                          /* ≡⃥ */
                    } else {
                        /* equiv aka Congruent */
                        AE("&equiv;");                            /* ≡ */
                    }; MCP;                                break;
                /* nequiv aka NotCongruent */
                case 8802:   AE("&nequiv;");               break; /* ≢ */
                case 8804:   TWOCPE;
                    if (clen2 && uniChar2 == 8402) { 
                        AE("&nvle;");                             /* ≤⃒ */
                    } else {
                        /* le aka leq */
                        AE("&le;");                               /* ≤ */
                    }; MCP;                                break;
                case 8805:   TWOCPE;
                    if (clen2 && uniChar2 == 8402) { 
                        AE("&nvge;");                             /* ≥⃒ */
                    } else {
                        /* ge aka GreaterEqual or geq */
                        AE("&ge;");                               /* ≥ */
                    }; MCP;                                break;
                case 8806:   TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        /* lE aka nleqq */
                        AE("&nlE;");                              /* ≦̸ */
                    } else {
                        /* lE aka LessFullEqual or leqq */
                        AE("&lE;");                               /* ≦ */
                    }; MCP;                                break;
                case 8807:   TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        /* gE aka NotGreaterFullEqual or ngeqq */
                        AE("&ngE;");                              /* ≧̸ */
                    } else {
                        /* gE aka GreaterFullEqual or geqq */
                        AE("&gE;");                               /* ≧ */
                    }; MCP;                                break;
                case 8808:   TWOCPE;
                    if (clen2 && uniChar2 == 65024) { 
                        /* lnE aka lvertneqq */
                        AE("&lvnE;");                             /* ≨︀ */
                    } else {
                        /* lnE aka lneqq */
                        AE("&lnE;");                              /* ≨ */
                    }; MCP;                                break;
                case 8809:   TWOCPE;
                    if (clen2 && uniChar2 == 65024) { 
                        /* gnE aka gvertneqq */
                        AE("&gvnE;");                             /* ≩︀ */
                    } else {
                        /* gnE aka gneqq */
                        AE("&gnE;");                              /* ≩ */
                    }; MCP;                                break;
                case 8810:   TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        /* nLtv aka NotLessLess */
                         AE("&nLtv;");                              /* ≪̸ */
                    } else if (clen2 && uniChar2 == 8402) {
                         AE("&nLt;");                               /* ≪⃒ */
                    } else {
                        /* ll aka Lt or NestedLessLess */
                        AE("&ll;");                               /* ≪ */
                    }; MCP;                                break;
                case 8811:   TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        /* nGtv aka NotGreaterGreater */
                         AE("&nGtv;");                              /* ≫̸ */
                    } else if (clen2 && uniChar2 == 8402) {
                         AE("&nGt;");                               /* ≫⃒ */
                    } else {
                        /* gg aka Gt or NestedGreaterGreater */
                        AE("&gg;");                               /* ≫ */
                    }; MCP;                                break;
                /* twixt aka between */
                case 8812:   AE("&twixt;");                break; /* ≬ */
                case 8813:   AE("&NotCupCap;");            break; /* ≭ */
                /* nlt aka NotLess or nless */
                case 8814:   AE("&nlt;");                  break; /* ≮ */
                /* ngt aka NotGreater or ngtr */
                case 8815:   AE("&ngt;");                  break; /* ≯ */
                /* nle aka NotLessEqual or nleq */
                case 8816:   AE("&nle;");                  break; /* ≰ */
                /* nge aka NotGreaterEqual or ngeq */
                case 8817:   AE("&nge;");                  break; /* ≱ */
                /* lsim aka LessTilde or lesssim */
                case 8818:   AE("&lsim;");                 break; /* ≲ */
                /* gsim aka GreaterTilde or gtrsim */
                case 8819:   AE("&gsim;");                 break; /* ≳ */
                /* nlsim aka NotLessTilde */
                case 8820:   AE("&nlsim;");                break; /* ≴ */
                /* ngsim aka NotGreaterTilde */
                case 8821:   AE("&ngsim;");                break; /* ≵ */
                /* lg aka LessGreater or lessgtr */
                case 8822:   AE("&lg;");                   break; /* ≶ */
                /* gl aka GreaterLess or gtrless */
                case 8823:   AE("&gl;");                   break; /* ≷ */
                /* ntlg aka NotLessGreater */
                case 8824:   AE("&ntlg;");                 break; /* ≸ */
                /* ntgl aka NotGreaterLess */
                case 8825:   AE("&ntgl;");                 break; /* ≹ */
                /* pr aka Precedes or prec */
                case 8826:   AE("&pr;");                   break; /* ≺ */
                /* sc aka Succeeds or succ */
                case 8827:   AE("&sc;");                   break; /* ≻ */
                /* prcue aka PrecedesSlantEqual or preccurlyeq */
                case 8828:   AE("&prcue;");                break; /* ≼ */
                /* sccue aka SucceedsSlantEqual or succcurlyeq */
                case 8829:   AE("&sccue;");                break; /* ≽ */
                /* prsim aka PrecedesTilde or precsim */
                case 8830:   AE("&prsim;");                break; /* ≾ */
                case 8831:   TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        AE("&NotSucceedsTilde;");                 /* ≿̸ */
                    } else {
                        /* scsim aka SucceedsTilde or succsim */
                        AE("&scsim;");                            /* ≿ */
                    }; MCP;                                break;
                /* npr aka NotPrecedes or nprec */
                case 8832:   AE("&npr;");                  break; /* ⊀ */
                /* nsc aka NotSucceeds or nsucc */
                case 8833:   AE("&nsc;");                  break; /* ⊁ */
                case 8834:   TWOCPE;
                    if (clen2 && uniChar2 == 8402) { 
                        /* sub aka NotSubset or nsubset */
                        AE("&vnsub;");                            /* ⊂⃒ */
                    } else {
                        /* sub aka subset */
                        AE("&sub;");                              /* ⊂ */
                    }; MCP;                                break;
                case 8835:   TWOCPE;
                    if (clen2 && uniChar2 == 8402) { 
                        /* sup aka NotSuperset or nsupset */
                        AE("&vnsup;");                            /* ⊃⃒ */
                    } else {
                        /* sup aka Superset or supset */
                        AE("&sup;");                              /* ⊃ */
                    }; MCP;                                break;
                case 8836:   AE("&nsub;");                 break; /* ⊄ */
                case 8837:   AE("&nsup;");                 break; /* ⊅ */
                /* sube aka SubsetEqual or subseteq */
                case 8838:   AE("&sube;");                 break; /* ⊆ */
                /* supe aka SupersetEqual or supseteq */
                case 8839:   AE("&supe;");                 break; /* ⊇ */
                /* nsube aka NotSubsetEqual or nsubseteq */
                case 8840:   AE("&nsube;");                break; /* ⊈ */
                /* nsupe aka NotSupersetEqual or nsupseteq */
                case 8841:   AE("&nsupe;");                break; /* ⊉ */
                case 8842:   TWOCPE;
                    if (clen2 && uniChar2 == 65024) { 
                        /* subne aka varsubsetneq */
                        AE("&vsubne;");                           /* ⊊︀ */
                    } else {
                        /* subne aka subsetneq */
                        AE("&subne;");                            /* ⊊ */
                    }; MCP;                                break;
                case 8843:   TWOCPE;
                    if (clen2 && uniChar2 == 65024) { 
                        /* supne aka varsupsetneq */
                        AE("&vsupne;");                           /* ⊋︀ */
                    } else {
                        /* supne aka supsetneq */
                        AE("&supne;");                            /* ⊋ */
                    }; MCP;                                break;
                case 8845:   AE("&cupdot;");               break; /* ⊍ */
                /* uplus aka UnionPlus */
                case 8846:   AE("&uplus;");                break; /* ⊎ */
                case 8847:   TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        AE("&NotSquareSubset;");                  /* ⊏̸ */
                    } else {
                        /* sqsub aka SquareSubset or sqsubset */
                        AE("&sqsub;");                            /* ⊏ */
                    }; MCP;                                break;
                case 8848:   TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        AE("&NotSquareSuperset;");                /* ⊐̸ */
                    } else {
                        /* sqsup aka SquareSuperset or sqsupset */
                        AE("&sqsup;");                            /* ⊐ */
                    }; MCP;                                break;
                /* sqsube aka SquareSubsetEqual or sqsubseteq */
                case 8849:   AE("&sqsube;");               break; /* ⊑ */
                /* sqsupe aka SquareSupersetEqual or sqsupseteq */
                case 8850:   AE("&sqsupe;");               break; /* ⊒ */
                case 8851:   TWOCPE;
                    if (clen2 && uniChar2 == 65024) { 
                        AE("&sqcaps;");                           /* ⊓︀ */
                    } else {
                        /* sqcap aka SquareIntersection */
                        AE("&sqcap;");                            /* ⊓ */
                    }; MCP;                                break;
                case 8852:   TWOCPE;
                    if (clen2 && uniChar2 == 65024) { 
                        AE("&sqcups;");                           /* ⊔︀ */
                    } else {
                        /* sqcup aka SquareUnion */
                        AE("&sqcup;");                            /* ⊔ */
                    }; MCP;                                break;
                /* oplus aka CirclePlus */
                case 8853:   AE("&oplus;");                break; /* ⊕ */
                /* ominus aka CircleMinus */
                case 8854:   AE("&ominus;");               break; /* ⊖ */
                /* otimes aka CircleTimes */
                case 8855:   AE("&otimes;");               break; /* ⊗ */
                case 8856:   AE("&osol;");                 break; /* ⊘ */
                /* odot aka CircleDot */
                case 8857:   AE("&odot;");                 break; /* ⊙ */
                /* ocir aka circledcirc */
                case 8858:   AE("&ocir;");                 break; /* ⊚ */
                /* oast aka circledast */
                case 8859:   AE("&oast;");                 break; /* ⊛ */
                /* odash aka circleddash */
                case 8861:   AE("&odash;");                break; /* ⊝ */
                /* plusb aka boxplus */
                case 8862:   AE("&plusb;");                break; /* ⊞ */
                /* minusb aka boxminus */
                case 8863:   AE("&minusb;");               break; /* ⊟ */
                /* timesb aka boxtimes */
                case 8864:   AE("&timesb;");               break; /* ⊠ */
                /* sdotb aka dotsquare */
                case 8865:   AE("&sdotb;");                break; /* ⊡ */
                /* vdash aka RightTee */
                case 8866:   AE("&vdash;");                break; /* ⊢ */
                /* dashv aka LeftTee */
                case 8867:   AE("&dashv;");                break; /* ⊣ */
                /* top aka DownTee */
                case 8868:   AE("&top;");                  break; /* ⊤ */
                /* bot aka UpTee or bottom or perp */
                case 8869:   AE("&bot;");                  break; /* ⊥ */
                case 8871:   AE("&models;");               break; /* ⊧ */
                /* vDash aka DoubleRightTee */
                case 8872:   AE("&vDash;");                break; /* ⊨ */
                case 8873:   AE("&Vdash;");                break; /* ⊩ */
                case 8874:   AE("&Vvdash;");               break; /* ⊪ */
                case 8875:   AE("&VDash;");                break; /* ⊫ */
                case 8876:   AE("&nvdash;");               break; /* ⊬ */
                case 8877:   AE("&nvDash;");               break; /* ⊭ */
                case 8878:   AE("&nVdash;");               break; /* ⊮ */
                case 8879:   AE("&nVDash;");               break; /* ⊯ */
                case 8880:   AE("&prurel;");               break; /* ⊰ */
                /* vltri aka LeftTriangle or vartriangleleft */
                case 8882:   AE("&vltri;");                break; /* ⊲ */
                /* vrtri aka RightTriangle or vartriangleright */
                case 8883:   AE("&vrtri;");                break; /* ⊳ */
                case 8884:   TWOCPE;
                    if (clen2 && uniChar2 == 8402) { 
                        AE("&nvltrie;");                          /* ⊴⃒ */
                    } else {
                        /* ltrie aka LeftTriangleEqual or trianglelefteq */
                        AE("&ltrie;");                            /* ⊴ */
                    }; MCP;                                break;
                case 8885:   TWOCPE;
                    if (clen2 && uniChar2 == 8402) { 
                        AE("&nvrtrie;");                          /* ⊵⃒ */
                    } else {
                        /* rtrie aka RightTriangleEqual or trianglerighteq */
                        AE("&rtrie;");                            /* ⊵ */
                    }; MCP;                                break;
                case 8886:   AE("&origof;");               break; /* ⊶ */
                case 8887:   AE("&imof;");                 break; /* ⊷ */
                /* mumap aka multimap */
                case 8888:   AE("&mumap;");                break; /* ⊸ */
                case 8889:   AE("&hercon;");               break; /* ⊹ */
                /* intcal aka intercal */
                case 8890:   AE("&intcal;");               break; /* ⊺ */
                case 8891:   AE("&veebar;");               break; /* ⊻ */
                case 8893:   AE("&barvee;");               break; /* ⊽ */
                case 8894:   AE("&angrtvb;");              break; /* ⊾ */
                case 8895:   AE("&lrtri;");                break; /* ⊿ */
                /* Wedge aka bigwedge or xwedge */
                case 8896:   AE("&Wedge;");                break; /* ⋀ */
                /* Vee aka bigvee or xvee */
                case 8897:   AE("&Vee;");                  break; /* ⋁ */
                /* xcap aka Intersection or bigcap */
                case 8898:   AE("&xcap;");                 break; /* ⋂ */
                /* xcup aka Union or bigcup */
                case 8899:   AE("&xcup;");                 break; /* ⋃ */
                /* diam aka Diamond or diamond */
                case 8900:   AE("&diam;");                 break; /* ⋄ */
                case 8901:   AE("&sdot;");                 break; /* ⋅ */
                /* Star aka sstarf */
                case 8902:   AE("&Star;");                 break; /* ⋆ */
                /* divonx aka divideontimes */
                case 8903:   AE("&divonx;");               break; /* ⋇ */
                case 8904:   AE("&bowtie;");               break; /* ⋈ */
                case 8905:   AE("&ltimes;");               break; /* ⋉ */
                case 8906:   AE("&rtimes;");               break; /* ⋊ */
                /* lthree aka leftthreetimes */
                case 8907:   AE("&lthree;");               break; /* ⋋ */
                /* rthree aka rightthreetimes */
                case 8908:   AE("&rthree;");               break; /* ⋌ */
                /* bsime aka backsimeq */
                case 8909:   AE("&bsime;");                break; /* ⋍ */
                /* cuvee aka curlyvee */
                case 8910:   AE("&cuvee;");                break; /* ⋎ */
                /* cuwed aka curlywedge */
                case 8911:   AE("&cuwed;");                break; /* ⋏ */
                /* Sub aka Subset */
                case 8912:   AE("&Sub;");                  break; /* ⋐ */
                /* Sup aka Supset */
                case 8913:   AE("&Sup;");                  break; /* ⋑ */
                case 8914:   AE("&Cap;");                  break; /* ⋒ */
                case 8915:   AE("&Cup;");                  break; /* ⋓ */
                /* fork aka pitchfork */
                case 8916:   AE("&fork;");                 break; /* ⋔ */
                case 8917:   AE("&epar;");                 break; /* ⋕ */
                /* ltdot aka lessdot */
                case 8918:   AE("&ltdot;");                break; /* ⋖ */
                /* gtdot aka gtrdot */
                case 8919:   AE("&gtdot;");                break; /* ⋗ */
                case 8920:   TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        AE("&nLl;");                              /* ⋘̸ */
                    } else {
                        AE("&Ll;");                               /* ⋘ */
                    }; MCP;                                break;
                case 8921:   TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        AE("&nGg;");                              /* ⋙̸ */
                    } else {
                        /* Gg aka ggg */
                        AE("&Gg;");                               /* ⋙ */
                    }; MCP;                                break;
                case 8922:   TWOCPE;
                    if (clen2 && uniChar2 == 65024) { 
                        AE("&lesg;");                             /* ⋚︀ */
                    } else {
                        /* leg aka LessEqualGreater or lesseqgtr */
                        AE("&leg;");                              /* ⋚ */
                    }; MCP;                                break;
                case 8923:   TWOCPE;
                    if (clen2 && uniChar2 == 65024) { 
                        AE("&gesl;");                             /* ⋛︀ */
                    } else {
                        /* gel aka GreaterEqualLess or gtreqless */
                        AE("&gel;");                              /* ⋛ */
                    }; MCP;                                break;
                /* cuepr aka curlyeqprec */
                case 8926:   AE("&cuepr;");                break; /* ⋞ */
                /* cuesc aka curlyeqsucc */
                case 8927:   AE("&cuesc;");                break; /* ⋟ */
                /* nprcue aka NotPrecedesSlantEqual */
                case 8928:   AE("&nprcue;");               break; /* ⋠ */
                /* nsccue aka NotSucceedsSlantEqual */
                case 8929:   AE("&nsccue;");               break; /* ⋡ */
                /* nsqsube aka NotSquareSubsetEqual */
                case 8930:   AE("&nsqsube;");              break; /* ⋢ */
                /* nsqsupe aka NotSquareSupersetEqual */
                case 8931:   AE("&nsqsupe;");              break; /* ⋣ */
                case 8934:   AE("&lnsim;");                break; /* ⋦ */
                case 8935:   AE("&gnsim;");                break; /* ⋧ */
                /* prnsim aka precnsim */
                case 8936:   AE("&prnsim;");               break; /* ⋨ */
                /* scnsim aka succnsim */
                case 8937:   AE("&scnsim;");               break; /* ⋩ */
                /* nltri aka NotLeftTriangle or ntriangleleft */
                case 8938:   AE("&nltri;");                break; /* ⋪ */
                /* nrtri aka NotRightTriangle or ntriangleright */
                case 8939:   AE("&nrtri;");                break; /* ⋫ */
                /* nltrie aka NotLeftTriangleEqual or ntrianglelefteq */
                case 8940:   AE("&nltrie;");               break; /* ⋬ */
                /* nrtrie aka NotRightTriangleEqual or ntrianglerighteq */
                case 8941:   AE("&nrtrie;");               break; /* ⋭ */
                case 8942:   AE("&vellip;");               break; /* ⋮ */
                case 8943:   AE("&ctdot;");                break; /* ⋯ */
                case 8944:   AE("&utdot;");                break; /* ⋰ */
                case 8945:   AE("&dtdot;");                break; /* ⋱ */
                case 8946:   AE("&disin;");                break; /* ⋲ */
                case 8947:   AE("&isinsv;");               break; /* ⋳ */
                case 8948:   AE("&isins;");                break; /* ⋴ */
                case 8949:   TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        AE("&notindot;");                         /* ⋵̸ */
                    } else {
                        AE("&isindot;");                          /* ⋵ */
                    }; MCP;                                break;
                case 8950:   AE("&notinvc;");              break; /* ⋶ */
                case 8951:   AE("&notinvb;");              break; /* ⋷ */
                case 8953:   TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        AE("&notinE;");                           /* ⋹̸ */
                    } else {
                        AE("&isinE;");                            /* ⋹ */
                    }; MCP;                                break;
                case 8954:   AE("&nisd;");                 break; /* ⋺ */
                case 8955:   AE("&xnis;");                 break; /* ⋻ */
                case 8956:   AE("&nis;");                  break; /* ⋼ */
                case 8957:   AE("&notnivc;");              break; /* ⋽ */
                case 8958:   AE("&notnivb;");              break; /* ⋾ */
                /* barwed aka barwedge */
                case 8965:   AE("&barwed;");               break; /* ⌅ */
                /* Barwed aka doublebarwedge */
                case 8966:   AE("&Barwed;");               break; /* ⌆ */
                /* lceil aka LeftCeiling */
                case 8968:   AE("&lceil;");                break; /* ⌈ */
                /* rceil aka RightCeiling */
                case 8969:   AE("&rceil;");                break; /* ⌉ */
                /* lfloor aka LeftFloor */
                case 8970:   AE("&lfloor;");               break; /* ⌊ */
                /* rfloor aka RightFloor */
                case 8971:   AE("&rfloor;");               break; /* ⌋ */
                case 8972:   AE("&drcrop;");               break; /* ⌌ */
                case 8973:   AE("&dlcrop;");               break; /* ⌍ */
                case 8974:   AE("&urcrop;");               break; /* ⌎ */
                case 8975:   AE("&ulcrop;");               break; /* ⌏ */
                case 8976:   AE("&bnot;");                 break; /* ⌐ */
                case 8978:   AE("&profline;");             break; /* ⌒ */
                case 8979:   AE("&profsurf;");             break; /* ⌓ */
                case 8981:   AE("&telrec;");               break; /* ⌕ */
                case 8982:   AE("&target;");               break; /* ⌖ */
                /* ulcorn aka ulcorner */
                case 8988:   AE("&ulcorn;");               break; /* ⌜ */
                /* urcorn aka urcorner */
                case 8989:   AE("&urcorn;");               break; /* ⌝ */
                /* dlcorn aka llcorner */
                case 8990:   AE("&dlcorn;");               break; /* ⌞ */
                /* drcorn aka lrcorner */
                case 8991:   AE("&drcorn;");               break; /* ⌟ */
                /* frown aka sfrown */
                case 8994:   AE("&frown;");                break; /* ⌢ */
                /* smile aka ssmile */
                case 8995:   AE("&smile;");                break; /* ⌣ */
                case 9005:   AE("&cylcty;");               break; /* ⌭ */
                case 9006:   AE("&profalar;");             break; /* ⌮ */
                case 9014:   AE("&topbot;");               break; /* ⌶ */
                case 9021:   AE("&ovbar;");                break; /* ⌽ */
                case 9023:   AE("&solbar;");               break; /* ⌿ */
                case 9084:   AE("&angzarr;");              break; /* ⍼ */
                /* lmoust aka lmoustache */
                case 9136:   AE("&lmoust;");               break; /* ⎰ */
                /* rmoust aka rmoustache */
                case 9137:   AE("&rmoust;");               break; /* ⎱ */
                /* tbrk aka OverBracket */
                case 9140:   AE("&tbrk;");                 break; /* ⎴ */
                /* bbrk aka UnderBracket */
                case 9141:   AE("&bbrk;");                 break; /* ⎵ */
                case 9142:   AE("&bbrktbrk;");             break; /* ⎶ */
                case 9180:   AE("&OverParenthesis;");      break; /* ⏜ */
                case 9181:   AE("&UnderParenthesis;");     break; /* ⏝ */
                case 9182:   AE("&OverBrace;");            break; /* ⏞ */
                case 9183:   AE("&UnderBrace;");           break; /* ⏟ */
                case 9186:   AE("&trpezium;");             break; /* ⏢ */
                case 9191:   AE("&elinters;");             break; /* ⏧ */
                case 9251:   AE("&blank;");                break; /* ␣ */
                /* oS aka circledS */
                case 9416:   AE("&oS;");                   break; /* Ⓢ */
                /* boxh aka HorizontalLine */
                case 9472:   AE("&boxh;");                 break; /* ─ */
                case 9474:   AE("&boxv;");                 break; /* │ */
                case 9484:   AE("&boxdr;");                break; /* ┌ */
                case 9488:   AE("&boxdl;");                break; /* ┐ */
                case 9492:   AE("&boxur;");                break; /* └ */
                case 9496:   AE("&boxul;");                break; /* ┘ */
                case 9500:   AE("&boxvr;");                break; /* ├ */
                case 9508:   AE("&boxvl;");                break; /* ┤ */
                case 9516:   AE("&boxhd;");                break; /* ┬ */
                case 9524:   AE("&boxhu;");                break; /* ┴ */
                case 9532:   AE("&boxvh;");                break; /* ┼ */
                case 9552:   AE("&boxH;");                 break; /* ═ */
                case 9553:   AE("&boxV;");                 break; /* ║ */
                case 9554:   AE("&boxdR;");                break; /* ╒ */
                case 9555:   AE("&boxDr;");                break; /* ╓ */
                case 9556:   AE("&boxDR;");                break; /* ╔ */
                case 9557:   AE("&boxdL;");                break; /* ╕ */
                case 9558:   AE("&boxDl;");                break; /* ╖ */
                case 9559:   AE("&boxDL;");                break; /* ╗ */
                case 9560:   AE("&boxuR;");                break; /* ╘ */
                case 9561:   AE("&boxUr;");                break; /* ╙ */
                case 9562:   AE("&boxUR;");                break; /* ╚ */
                case 9563:   AE("&boxuL;");                break; /* ╛ */
                case 9564:   AE("&boxUl;");                break; /* ╜ */
                case 9565:   AE("&boxUL;");                break; /* ╝ */
                case 9566:   AE("&boxvR;");                break; /* ╞ */
                case 9567:   AE("&boxVr;");                break; /* ╟ */
                case 9568:   AE("&boxVR;");                break; /* ╠ */
                case 9569:   AE("&boxvL;");                break; /* ╡ */
                case 9570:   AE("&boxVl;");                break; /* ╢ */
                case 9571:   AE("&boxVL;");                break; /* ╣ */
                case 9572:   AE("&boxHd;");                break; /* ╤ */
                case 9573:   AE("&boxhD;");                break; /* ╥ */
                case 9574:   AE("&boxHD;");                break; /* ╦ */
                case 9575:   AE("&boxHu;");                break; /* ╧ */
                case 9576:   AE("&boxhU;");                break; /* ╨ */
                case 9577:   AE("&boxHU;");                break; /* ╩ */
                case 9578:   AE("&boxvH;");                break; /* ╪ */
                case 9579:   AE("&boxVh;");                break; /* ╫ */
                case 9580:   AE("&boxVH;");                break; /* ╬ */
                case 9600:   AE("&uhblk;");                break; /* ▀ */
                case 9604:   AE("&lhblk;");                break; /* ▄ */
                case 9608:   AE("&block;");                break; /* █ */
                case 9617:   AE("&blk14;");                break; /* ░ */
                case 9618:   AE("&blk12;");                break; /* ▒ */
                case 9619:   AE("&blk34;");                break; /* ▓ */
                /* squ aka Square or square */
                case 9633:   AE("&squ;");                  break; /* □ */
                /* squf aka FilledVerySmallSquare or blacksquare or squarf */
                case 9642:   AE("&squf;");                 break; /* ▪ */
                case 9643:   AE("&EmptyVerySmallSquare;"); break; /* ▫ */
                case 9645:   AE("&rect;");                 break; /* ▭ */
                case 9646:   AE("&marker;");               break; /* ▮ */
                case 9649:   AE("&fltns;");                break; /* ▱ */
                /* xutri aka bigtriangleup */
                case 9651:   AE("&xutri;");                break; /* △ */
                /* utrif aka blacktriangle */
                case 9652:   AE("&utrif;");                break; /* ▴ */
                /* utri aka triangle */
                case 9653:   AE("&utri;");                 break; /* ▵ */
                /* rtrif aka blacktriangleright */
                case 9656:   AE("&rtrif;");                break; /* ▸ */
                /* rtri aka triangleright */
                case 9657:   AE("&rtri;");                 break; /* ▹ */
                /* xdtri aka bigtriangledown */
                case 9661:   AE("&xdtri;");                break; /* ▽ */
                /* dtrif aka blacktriangledown */
                case 9662:   AE("&dtrif;");                break; /* ▾ */
                /* dtri aka triangledown */
                case 9663:   AE("&dtri;");                 break; /* ▿ */
                /* ltrif aka blacktriangleleft */
                case 9666:   AE("&ltrif;");                break; /* ◂ */
                /* ltri aka triangleleft */
                case 9667:   AE("&ltri;");                 break; /* ◃ */
                /* loz aka lozenge */
                case 9674:   AE("&loz;");                  break; /* ◊ */
                case 9675:   AE("&cir;");                  break; /* ○ */
                case 9708:   AE("&tridot;");               break; /* ◬ */
                /* xcirc aka bigcirc */
                case 9711:   AE("&xcirc;");                break; /* ◯ */
                case 9720:   AE("&ultri;");                break; /* ◸ */
                case 9721:   AE("&urtri;");                break; /* ◹ */
                case 9722:   AE("&lltri;");                break; /* ◺ */
                case 9723:   AE("&EmptySmallSquare;");     break; /* ◻ */
                case 9724:   AE("&FilledSmallSquare;");    break; /* ◼ */
                /* starf aka bigstar */
                case 9733:   AE("&starf;");                break; /* ★ */
                case 9734:   AE("&star;");                 break; /* ☆ */
                case 9742:   AE("&phone;");                break; /* ☎ */
                case 9792:   AE("&female;");               break; /* ♀ */
                case 9794:   AE("&male;");                 break; /* ♂ */
                /* spades aka spadesuit */
                case 9824:   AE("&spades;");               break; /* ♠ */
                /* clubs aka clubsuit */
                case 9827:   AE("&clubs;");                break; /* ♣ */
                /* hearts aka heartsuit */
                case 9829:   AE("&hearts;");               break; /* ♥ */
                /* diams aka diamondsuit */
                case 9830:   AE("&diams;");                break; /* ♦ */
                case 9834:   AE("&sung;");                 break; /* ♪ */
                case 9837:   AE("&flat;");                 break; /* ♭ */
                /* natur aka natural */
                case 9838:   AE("&natur;");                break; /* ♮ */
                case 9839:   AE("&sharp;");                break; /* ♯ */
                /* check aka checkmark */
                case 10003:  AE("&check;");                break; /* ✓ */
                case 10007:  AE("&cross;");                break; /* ✗ */
                /* malt aka maltese */
                case 10016:  AE("&malt;");                 break; /* ✠ */
                case 10038:  AE("&sext;");                 break; /* ✶ */
                case 10072:  AE("&VerticalSeparator;");    break; /* ❘ */
                case 10098:  AE("&lbbrk;");                break; /* ❲ */
                case 10099:  AE("&rbbrk;");                break; /* ❳ */
                case 10184:  AE("&bsolhsub;");             break; /* ⟈ */
                case 10185:  AE("&suphsol;");              break; /* ⟉ */
                /* lobrk aka LeftDoubleBracket */
                case 10214:  AE("&lobrk;");                break; /* ⟦ */
                /* robrk aka RightDoubleBracket */
                case 10215:  AE("&robrk;");                break; /* ⟧ */
                /* lang aka LeftAngleBracket or langle */
                case 10216:  AE("&lang;");                 break; /* ⟨ */
                /* rang aka RightAngleBracket or rangle */
                case 10217:  AE("&rang;");                 break; /* ⟩ */
                case 10218:  AE("&Lang;");                 break; /* ⟪ */
                case 10219:  AE("&Rang;");                 break; /* ⟫ */
                case 10220:  AE("&loang;");                break; /* ⟬ */
                case 10221:  AE("&roang;");                break; /* ⟭ */
                /* xlarr aka LongLeftArrow or longleftarrow */
                case 10229:  AE("&xlarr;");                break; /* ⟵ */
                /* xrarr aka LongRightArrow or longrightarrow */
                case 10230:  AE("&xrarr;");                break; /* ⟶ */
                /* xharr aka LongLeftRightArrow or longleftrightarrow */
                case 10231:  AE("&xharr;");                break; /* ⟷ */
                /* xlArr aka DoubleLongLeftArrow or Longleftarrow */
                case 10232:  AE("&xlArr;");                break; /* ⟸ */
                /* xrArr aka DoubleLongRightArrow or Longrightarrow */
                case 10233:  AE("&xrArr;");                break; /* ⟹ */
                /* xhArr aka DoubleLongLeftRightArrow or Longleftrightarrow */
                case 10234:  AE("&xhArr;");                break; /* ⟺ */
                /* xmap aka longmapsto */
                case 10236:  AE("&xmap;");                 break; /* ⟼ */
                case 10239:  AE("&dzigrarr;");             break; /* ⟿ */
                case 10498:  AE("&nvlArr;");               break; /* ⤂ */
                case 10499:  AE("&nvrArr;");               break; /* ⤃ */
                case 10500:  AE("&nvHarr;");               break; /* ⤄ */
                case 10501:  AE("&Map;");                  break; /* ⤅ */
                case 10508:  AE("&lbarr;");                break; /* ⤌ */
                /* rbarr aka bkarow */
                case 10509:  AE("&rbarr;");                break; /* ⤍ */
                case 10510:  AE("&lBarr;");                break; /* ⤎ */
                /* rBarr aka dbkarow */
                case 10511:  AE("&rBarr;");                break; /* ⤏ */
                /* RBarr aka drbkarow */
                case 10512:  AE("&RBarr;");                break; /* ⤐ */
                case 10513:  AE("&DDotrahd;");             break; /* ⤑ */
                case 10514:  AE("&UpArrowBar;");           break; /* ⤒ */
                case 10515:  AE("&DownArrowBar;");         break; /* ⤓ */
                case 10518:  AE("&Rarrtl;");               break; /* ⤖ */
                case 10521:  AE("&latail;");               break; /* ⤙ */
                case 10522:  AE("&ratail;");               break; /* ⤚ */
                case 10523:  AE("&lAtail;");               break; /* ⤛ */
                case 10524:  AE("&rAtail;");               break; /* ⤜ */
                case 10525:  AE("&larrfs;");               break; /* ⤝ */
                case 10526:  AE("&rarrfs;");               break; /* ⤞ */
                case 10527:  AE("&larrbfs;");              break; /* ⤟ */
                case 10528:  AE("&rarrbfs;");              break; /* ⤠ */
                case 10531:  AE("&nwarhk;");               break; /* ⤣ */
                case 10532:  AE("&nearhk;");               break; /* ⤤ */
                /* searhk aka hksearow */
                case 10533:  AE("&searhk;");               break; /* ⤥ */
                /* swarhk aka hkswarow */
                case 10534:  AE("&swarhk;");               break; /* ⤦ */
                case 10535:  AE("&nwnear;");               break; /* ⤧ */
                /* toea aka nesear */
                case 10536:  AE("&toea;");                 break; /* ⤨ */
                /* tosa aka seswar */
                case 10537:  AE("&tosa;");                 break; /* ⤩ */
                case 10538:  AE("&swnwar;");               break; /* ⤪ */
                case 10547:  TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        AE("&nrarrc;");                           /* ⤳̸ */
                    } else {
                        AE("&rarrc;");                            /* ⤳ */
                    }; MCP;                                break;
                case 10549:  AE("&cudarrr;");              break; /* ⤵ */
                case 10550:  AE("&ldca;");                 break; /* ⤶ */
                case 10551:  AE("&rdca;");                 break; /* ⤷ */
                case 10552:  AE("&cudarrl;");              break; /* ⤸ */
                case 10553:  AE("&larrpl;");               break; /* ⤹ */
                case 10556:  AE("&curarrm;");              break; /* ⤼ */
                case 10557:  AE("&cularrp;");              break; /* ⤽ */
                case 10565:  AE("&rarrpl;");               break; /* ⥅ */
                case 10568:  AE("&harrcir;");              break; /* ⥈ */
                case 10569:  AE("&Uarrocir;");             break; /* ⥉ */
                case 10570:  AE("&lurdshar;");             break; /* ⥊ */
                case 10571:  AE("&ldrushar;");             break; /* ⥋ */
                case 10574:  AE("&LeftRightVector;");      break; /* ⥎ */
                case 10575:  AE("&RightUpDownVector;");    break; /* ⥏ */
                case 10576:  AE("&DownLeftRightVector;");  break; /* ⥐ */
                case 10577:  AE("&LeftUpDownVector;");     break; /* ⥑ */
                case 10578:  AE("&LeftVectorBar;");        break; /* ⥒ */
                case 10579:  AE("&RightVectorBar;");       break; /* ⥓ */
                case 10580:  AE("&RightUpVectorBar;");     break; /* ⥔ */
                case 10581:  AE("&RightDownVectorBar;");   break; /* ⥕ */
                case 10582:  AE("&DownLeftVectorBar;");    break; /* ⥖ */
                case 10583:  AE("&DownRightVectorBar;");   break; /* ⥗ */
                case 10584:  AE("&LeftUpVectorBar;");      break; /* ⥘ */
                case 10585:  AE("&LeftDownVectorBar;");    break; /* ⥙ */
                case 10586:  AE("&LeftTeeVector;");        break; /* ⥚ */
                case 10587:  AE("&RightTeeVector;");       break; /* ⥛ */
                case 10588:  AE("&RightUpTeeVector;");     break; /* ⥜ */
                case 10589:  AE("&RightDownTeeVector;");   break; /* ⥝ */
                case 10590:  AE("&DownLeftTeeVector;");    break; /* ⥞ */
                case 10591:  AE("&DownRightTeeVector;");   break; /* ⥟ */
                case 10592:  AE("&LeftUpTeeVector;");      break; /* ⥠ */
                case 10593:  AE("&LeftDownTeeVector;");    break; /* ⥡ */
                case 10594:  AE("&lHar;");                 break; /* ⥢ */
                case 10595:  AE("&uHar;");                 break; /* ⥣ */
                case 10596:  AE("&rHar;");                 break; /* ⥤ */
                case 10597:  AE("&dHar;");                 break; /* ⥥ */
                case 10598:  AE("&luruhar;");              break; /* ⥦ */
                case 10599:  AE("&ldrdhar;");              break; /* ⥧ */
                case 10600:  AE("&ruluhar;");              break; /* ⥨ */
                case 10601:  AE("&rdldhar;");              break; /* ⥩ */
                case 10602:  AE("&lharul;");               break; /* ⥪ */
                case 10603:  AE("&llhard;");               break; /* ⥫ */
                case 10604:  AE("&rharul;");               break; /* ⥬ */
                case 10605:  AE("&lrhard;");               break; /* ⥭ */
                /* udhar aka UpEquilibrium */
                case 10606:  AE("&udhar;");                break; /* ⥮ */
                /* duhar aka ReverseUpEquilibrium */
                case 10607:  AE("&duhar;");                break; /* ⥯ */
                case 10608:  AE("&RoundImplies;");         break; /* ⥰ */
                case 10609:  AE("&erarr;");                break; /* ⥱ */
                case 10610:  AE("&simrarr;");              break; /* ⥲ */
                case 10611:  AE("&larrsim;");              break; /* ⥳ */
                case 10612:  AE("&rarrsim;");              break; /* ⥴ */
                case 10613:  AE("&rarrap;");               break; /* ⥵ */
                case 10614:  AE("&ltlarr;");               break; /* ⥶ */
                case 10616:  AE("&gtrarr;");               break; /* ⥸ */
                case 10617:  AE("&subrarr;");              break; /* ⥹ */
                case 10619:  AE("&suplarr;");              break; /* ⥻ */
                case 10620:  AE("&lfisht;");               break; /* ⥼ */
                case 10621:  AE("&rfisht;");               break; /* ⥽ */
                case 10622:  AE("&ufisht;");               break; /* ⥾ */
                case 10623:  AE("&dfisht;");               break; /* ⥿ */
                case 10629:  AE("&lopar;");                break; /* ⦅ */
                case 10630:  AE("&ropar;");                break; /* ⦆ */
                case 10635:  AE("&lbrke;");                break; /* ⦋ */
                case 10636:  AE("&rbrke;");                break; /* ⦌ */
                case 10637:  AE("&lbrkslu;");              break; /* ⦍ */
                case 10638:  AE("&rbrksld;");              break; /* ⦎ */
                case 10639:  AE("&lbrksld;");              break; /* ⦏ */
                case 10640:  AE("&rbrkslu;");              break; /* ⦐ */
                case 10641:  AE("&langd;");                break; /* ⦑ */
                case 10642:  AE("&rangd;");                break; /* ⦒ */
                case 10643:  AE("&lparlt;");               break; /* ⦓ */
                case 10644:  AE("&rpargt;");               break; /* ⦔ */
                case 10645:  AE("&gtlPar;");               break; /* ⦕ */
                case 10646:  AE("&ltrPar;");               break; /* ⦖ */
                case 10650:  AE("&vzigzag;");              break; /* ⦚ */
                case 10652:  AE("&vangrt;");               break; /* ⦜ */
                case 10653:  AE("&angrtvbd;");             break; /* ⦝ */
                case 10660:  AE("&ange;");                 break; /* ⦤ */
                case 10661:  AE("&range;");                break; /* ⦥ */
                case 10662:  AE("&dwangle;");              break; /* ⦦ */
                case 10663:  AE("&uwangle;");              break; /* ⦧ */
                case 10664:  AE("&angmsdaa;");             break; /* ⦨ */
                case 10665:  AE("&angmsdab;");             break; /* ⦩ */
                case 10666:  AE("&angmsdac;");             break; /* ⦪ */
                case 10667:  AE("&angmsdad;");             break; /* ⦫ */
                case 10668:  AE("&angmsdae;");             break; /* ⦬ */
                case 10669:  AE("&angmsdaf;");             break; /* ⦭ */
                case 10670:  AE("&angmsdag;");             break; /* ⦮ */
                case 10671:  AE("&angmsdah;");             break; /* ⦯ */
                case 10672:  AE("&bemptyv;");              break; /* ⦰ */
                case 10673:  AE("&demptyv;");              break; /* ⦱ */
                case 10674:  AE("&cemptyv;");              break; /* ⦲ */
                case 10675:  AE("&raemptyv;");             break; /* ⦳ */
                case 10676:  AE("&laemptyv;");             break; /* ⦴ */
                case 10677:  AE("&ohbar;");                break; /* ⦵ */
                case 10678:  AE("&omid;");                 break; /* ⦶ */
                case 10679:  AE("&opar;");                 break; /* ⦷ */
                case 10681:  AE("&operp;");                break; /* ⦹ */
                case 10683:  AE("&olcross;");              break; /* ⦻ */
                case 10684:  AE("&odsold;");               break; /* ⦼ */
                case 10686:  AE("&olcir;");                break; /* ⦾ */
                case 10687:  AE("&ofcir;");                break; /* ⦿ */
                case 10688:  AE("&olt;");                  break; /* ⧀ */
                case 10689:  AE("&ogt;");                  break; /* ⧁ */
                case 10690:  AE("&cirscir;");              break; /* ⧂ */
                case 10691:  AE("&cirE;");                 break; /* ⧃ */
                case 10692:  AE("&solb;");                 break; /* ⧄ */
                case 10693:  AE("&bsolb;");                break; /* ⧅ */
                case 10697:  AE("&boxbox;");               break; /* ⧉ */
                case 10701:  AE("&trisb;");                break; /* ⧍ */
                case 10702:  AE("&rtriltri;");             break; /* ⧎ */
                case 10703:  TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        AE("&NotLeftTriangleBar;");               /* ⧏̸ */
                    } else {
                        AE("&LeftTriangleBar;");                  /* ⧏ */
                    }; MCP;                                break;
                case 10704:  TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        AE("&NotRightTriangleBar;");              /* ⧐̸ */
                    } else {
                        AE("&RightTriangleBar;");                 /* ⧐ */
                    }; MCP;                                break;
                case 10716:  AE("&iinfin;");               break; /* ⧜ */
                case 10717:  AE("&infintie;");             break; /* ⧝ */
                case 10718:  AE("&nvinfin;");              break; /* ⧞ */
                case 10723:  AE("&eparsl;");               break; /* ⧣ */
                case 10724:  AE("&smeparsl;");             break; /* ⧤ */
                case 10725:  AE("&eqvparsl;");             break; /* ⧥ */
                /* lozf aka blacklozenge */
                case 10731:  AE("&lozf;");                 break; /* ⧫ */
                case 10740:  AE("&RuleDelayed;");          break; /* ⧴ */
                case 10742:  AE("&dsol;");                 break; /* ⧶ */
                /* xodot aka bigodot */
                case 10752:  AE("&xodot;");                break; /* ⨀ */
                /* xoplus aka bigoplus */
                case 10753:  AE("&xoplus;");               break; /* ⨁ */
                /* xotime aka bigotimes */
                case 10754:  AE("&xotime;");               break; /* ⨂ */
                /* xuplus aka biguplus */
                case 10756:  AE("&xuplus;");               break; /* ⨄ */
                /* xsqcup aka bigsqcup */
                case 10758:  AE("&xsqcup;");               break; /* ⨆ */
                /* qint aka iiiint */
                case 10764:  AE("&qint;");                 break; /* ⨌ */
                case 10765:  AE("&fpartint;");             break; /* ⨍ */
                case 10768:  AE("&cirfnint;");             break; /* ⨐ */
                case 10769:  AE("&awint;");                break; /* ⨑ */
                case 10770:  AE("&rppolint;");             break; /* ⨒ */
                case 10771:  AE("&scpolint;");             break; /* ⨓ */
                case 10772:  AE("&npolint;");              break; /* ⨔ */
                case 10773:  AE("&pointint;");             break; /* ⨕ */
                case 10774:  AE("&quatint;");              break; /* ⨖ */
                case 10775:  AE("&intlarhk;");             break; /* ⨗ */
                case 10786:  AE("&pluscir;");              break; /* ⨢ */
                case 10787:  AE("&plusacir;");             break; /* ⨣ */
                case 10788:  AE("&simplus;");              break; /* ⨤ */
                case 10789:  AE("&plusdu;");               break; /* ⨥ */
                case 10790:  AE("&plussim;");              break; /* ⨦ */
                case 10791:  AE("&plustwo;");              break; /* ⨧ */
                case 10793:  AE("&mcomma;");               break; /* ⨩ */
                case 10794:  AE("&minusdu;");              break; /* ⨪ */
                case 10797:  AE("&loplus;");               break; /* ⨭ */
                case 10798:  AE("&roplus;");               break; /* ⨮ */
                case 10799:  AE("&Cross;");                break; /* ⨯ */
                case 10800:  AE("&timesd;");               break; /* ⨰ */
                case 10801:  AE("&timesbar;");             break; /* ⨱ */
                case 10803:  AE("&smashp;");               break; /* ⨳ */
                case 10804:  AE("&lotimes;");              break; /* ⨴ */
                case 10805:  AE("&rotimes;");              break; /* ⨵ */
                case 10806:  AE("&otimesas;");             break; /* ⨶ */
                case 10807:  AE("&Otimes;");               break; /* ⨷ */
                case 10808:  AE("&odiv;");                 break; /* ⨸ */
                case 10809:  AE("&triplus;");              break; /* ⨹ */
                case 10810:  AE("&triminus;");             break; /* ⨺ */
                case 10811:  AE("&tritime;");              break; /* ⨻ */
                /* iprod aka intprod */
                case 10812:  AE("&iprod;");                break; /* ⨼ */
                case 10815:  AE("&amalg;");                break; /* ⨿ */
                case 10816:  AE("&capdot;");               break; /* ⩀ */
                case 10818:  AE("&ncup;");                 break; /* ⩂ */
                case 10819:  AE("&ncap;");                 break; /* ⩃ */
                case 10820:  AE("&capand;");               break; /* ⩄ */
                case 10821:  AE("&cupor;");                break; /* ⩅ */
                case 10822:  AE("&cupcap;");               break; /* ⩆ */
                case 10823:  AE("&capcup;");               break; /* ⩇ */
                case 10824:  AE("&cupbrcap;");             break; /* ⩈ */
                case 10825:  AE("&capbrcup;");             break; /* ⩉ */
                case 10826:  AE("&cupcup;");               break; /* ⩊ */
                case 10827:  AE("&capcap;");               break; /* ⩋ */
                case 10828:  AE("&ccups;");                break; /* ⩌ */
                case 10829:  AE("&ccaps;");                break; /* ⩍ */
                case 10832:  AE("&ccupssm;");              break; /* ⩐ */
                case 10835:  AE("&And;");                  break; /* ⩓ */
                case 10836:  AE("&Or;");                   break; /* ⩔ */
                case 10837:  AE("&andand;");               break; /* ⩕ */
                case 10838:  AE("&oror;");                 break; /* ⩖ */
                case 10839:  AE("&orslope;");              break; /* ⩗ */
                case 10840:  AE("&andslope;");             break; /* ⩘ */
                case 10842:  AE("&andv;");                 break; /* ⩚ */
                case 10843:  AE("&orv;");                  break; /* ⩛ */
                case 10844:  AE("&andd;");                 break; /* ⩜ */
                case 10845:  AE("&ord;");                  break; /* ⩝ */
                case 10847:  AE("&wedbar;");               break; /* ⩟ */
                case 10854:  AE("&sdote;");                break; /* ⩦ */
                case 10858:  AE("&simdot;");               break; /* ⩪ */
                case 10861:  TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        AE("&ncongdot;");                         /* ⩭̸ */
                    } else {
                        AE("&congdot;");                          /* ⩭ */
                    }; MCP;                                break;
                case 10862:  AE("&easter;");               break; /* ⩮ */
                case 10863:  AE("&apacir;");               break; /* ⩯ */
                case 10864:  TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        AE("&napE;");                             /* ⩰̸ */
                    } else {
                        AE("&apE;");                              /* ⩰ */
                    }; MCP;                                break;
                case 10865:  AE("&eplus;");                break; /* ⩱ */
                case 10866:  AE("&pluse;");                break; /* ⩲ */
                case 10867:  AE("&Esim;");                 break; /* ⩳ */
                case 10868:  AE("&Colone;");               break; /* ⩴ */
                case 10869:  AE("&Equal;");                break; /* ⩵ */
                /* eDDot aka ddotseq */
                case 10871:  AE("&eDDot;");                break; /* ⩷ */
                case 10872:  AE("&equivDD;");              break; /* ⩸ */
                case 10873:  AE("&ltcir;");                break; /* ⩹ */
                case 10874:  AE("&gtcir;");                break; /* ⩺ */
                case 10875:  AE("&ltquest;");              break; /* ⩻ */
                case 10876:  AE("&gtquest;");              break; /* ⩼ */
                case 10877:  TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        /* les aka NotLessSlantEqual or nleqslant */
                        AE("&nles;");                             /* ⩽̸ */
                    } else {
                        /* les aka LessSlantEqual or leqslant */
                        AE("&les;");                              /* ⩽ */
                    }; MCP;                                break;
                case 10878:  TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        /* ges aka NotGreaterSlantEqual or ngeqslant */
                        AE("&nges;");                             /* ⩾̸ */
                    } else {
                        /* ges aka GreaterSlantEqual or geqslant */
                        AE("&ges;");                              /* ⩾ */
                    }; MCP;                                break;
                case 10879:  AE("&lesdot;");               break; /* ⩿ */
                case 10880:  AE("&gesdot;");               break; /* ⪀ */
                case 10881:  AE("&lesdoto;");              break; /* ⪁ */
                case 10882:  AE("&gesdoto;");              break; /* ⪂ */
                case 10883:  AE("&lesdotor;");             break; /* ⪃ */
                case 10884:  AE("&gesdotol;");             break; /* ⪄ */
                /* lap aka lessapprox */
                case 10885:  AE("&lap;");                  break; /* ⪅ */
                /* gap aka gtrapprox */
                case 10886:  AE("&gap;");                  break; /* ⪆ */
                /* lne aka lneq */
                case 10887:  AE("&lne;");                  break; /* ⪇ */
                /* gne aka gneq */
                case 10888:  AE("&gne;");                  break; /* ⪈ */
                /* lnap aka lnapprox */
                case 10889:  AE("&lnap;");                 break; /* ⪉ */
                /* gnap aka gnapprox */
                case 10890:  AE("&gnap;");                 break; /* ⪊ */
                /* lEg aka lesseqqgtr */
                case 10891:  AE("&lEg;");                  break; /* ⪋ */
                /* gEl aka gtreqqless */
                case 10892:  AE("&gEl;");                  break; /* ⪌ */
                case 10893:  AE("&lsime;");                break; /* ⪍ */
                case 10894:  AE("&gsime;");                break; /* ⪎ */
                case 10895:  AE("&lsimg;");                break; /* ⪏ */
                case 10896:  AE("&gsiml;");                break; /* ⪐ */
                case 10897:  AE("&lgE;");                  break; /* ⪑ */
                case 10898:  AE("&glE;");                  break; /* ⪒ */
                case 10899:  AE("&lesges;");               break; /* ⪓ */
                case 10900:  AE("&gesles;");               break; /* ⪔ */
                /* els aka eqslantless */
                case 10901:  AE("&els;");                  break; /* ⪕ */
                /* egs aka eqslantgtr */
                case 10902:  AE("&egs;");                  break; /* ⪖ */
                case 10903:  AE("&elsdot;");               break; /* ⪗ */
                case 10904:  AE("&egsdot;");               break; /* ⪘ */
                case 10905:  AE("&el;");                   break; /* ⪙ */
                case 10906:  AE("&eg;");                   break; /* ⪚ */
                case 10909:  AE("&siml;");                 break; /* ⪝ */
                case 10910:  AE("&simg;");                 break; /* ⪞ */
                case 10911:  AE("&simlE;");                break; /* ⪟ */
                case 10912:  AE("&simgE;");                break; /* ⪠ */
                case 10913:  TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        AE("&NotNestedLessLess;");                /* ⪡̸ */
                    } else {
                        AE("&LessLess;");                         /* ⪡ */
                    }; MCP;                                break;
                case 10914:  TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        AE("&NotNestedGreaterGreater;");          /* ⪢̸ */
                    } else {
                        AE("&GreaterGreater;");                   /* ⪢ */
                    }; MCP;                                break;
                case 10916:  AE("&glj;");                  break; /* ⪤ */
                case 10917:  AE("&gla;");                  break; /* ⪥ */
                case 10918:  AE("&ltcc;");                 break; /* ⪦ */
                case 10919:  AE("&gtcc;");                 break; /* ⪧ */
                case 10920:  AE("&lescc;");                break; /* ⪨ */
                case 10921:  AE("&gescc;");                break; /* ⪩ */
                case 10922:  AE("&smt;");                  break; /* ⪪ */
                case 10923:  AE("&lat;");                  break; /* ⪫ */
                case 10924:  TWOCPE;
                    if (clen2 && uniChar2 == 65024) { 
                        AE("&smtes;");                            /* ⪬︀ */
                    } else {
                        AE("&smte;");                             /* ⪬ */
                    }; MCP;                                break;
                case 10925:  TWOCPE;
                    if (clen2 && uniChar2 == 65024) { 
                        AE("&lates;");                            /* ⪭︀ */
                    } else {
                        AE("&late;");                             /* ⪭ */
                    }; MCP;                                break;
                case 10926:  AE("&bumpE;");                break; /* ⪮ */
                case 10927:  TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        /* pre aka NotPrecedesEqual or npreceq */
                        AE("&npre;");                             /* ⪯̸ */
                    } else {
                        /* pre aka PrecedesEqual or preceq */
                        AE("&pre;");                              /* ⪯ */
                    }; MCP;                                break;
                case 10928:  TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        /* sce aka NotSucceedsEqual or nsucceq */
                        AE("&nsce;");                             /* ⪰̸ */
                    } else {
                        /* sce aka SucceedsEqual or succeq */
                        AE("&sce;");                              /* ⪰ */
                    }; MCP;                                break;
                case 10931:  AE("&prE;");                  break; /* ⪳ */
                case 10932:  AE("&scE;");                  break; /* ⪴ */
                /* prnE aka precneqq */
                case 10933:  AE("&prnE;");                 break; /* ⪵ */
                /* scnE aka succneqq */
                case 10934:  AE("&scnE;");                 break; /* ⪶ */
                /* prap aka precapprox */
                case 10935:  AE("&prap;");                 break; /* ⪷ */
                /* scap aka succapprox */
                case 10936:  AE("&scap;");                 break; /* ⪸ */
                /* prnap aka precnapprox */
                case 10937:  AE("&prnap;");                break; /* ⪹ */
                /* scnap aka succnapprox */
                case 10938:  AE("&scnap;");                break; /* ⪺ */
                case 10939:  AE("&Pr;");                   break; /* ⪻ */
                case 10940:  AE("&Sc;");                   break; /* ⪼ */
                case 10941:  AE("&subdot;");               break; /* ⪽ */
                case 10942:  AE("&supdot;");               break; /* ⪾ */
                case 10943:  AE("&subplus;");              break; /* ⪿ */
                case 10944:  AE("&supplus;");              break; /* ⫀ */
                case 10945:  AE("&submult;");              break; /* ⫁ */
                case 10946:  AE("&supmult;");              break; /* ⫂ */
                case 10947:  AE("&subedot;");              break; /* ⫃ */
                case 10948:  AE("&supedot;");              break; /* ⫄ */
                case 10949:  TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        /* subE aka nsubseteqq */
                        AE("&nsubE;");                            /* ⫅̸ */
                    } else {
                        /* subE aka subseteqq */
                        AE("&subE;");                             /* ⫅ */
                    }; MCP;                                break;
                case 10950:  TWOCPE;
                    if (clen2 && uniChar2 == 824) { 
                        /* supE aka nsupseteqq */
                        AE("&nsupE;");                            /* ⫆̸ */
                    } else {
                        /* supE aka supseteqq */
                        AE("&supE;");                             /* ⫆ */
                    }; MCP;                                break;
                case 10951:  AE("&subsim;");               break; /* ⫇ */
                case 10952:  AE("&supsim;");               break; /* ⫈ */
                case 10955:  TWOCPE;
                    if (clen2 && uniChar2 == 65024) { 
                        /* subnE aka varsubsetneqq */
                        AE("&vsubnE;");                           /* ⫋︀ */
                    } else {
                        /* subnE aka subsetneqq */
                        AE("&subnE;");                            /* ⫋ */
                    }; MCP;                                break;
                case 10956:  TWOCPE;
                    if (clen2 && uniChar2 == 65024) { 
                        /* supnE aka varsupsetneqq */
                        AE("&vsupnE;");                           /* ⫌︀ */
                    } else {
                        /* supnE aka supsetneqq */
                        AE("&supnE;");                            /* ⫌ */
                    }; MCP;                                break;
                case 10959:  AE("&csub;");                 break; /* ⫏ */
                case 10960:  AE("&csup;");                 break; /* ⫐ */
                case 10961:  AE("&csube;");                break; /* ⫑ */
                case 10962:  AE("&csupe;");                break; /* ⫒ */
                case 10963:  AE("&subsup;");               break; /* ⫓ */
                case 10964:  AE("&supsub;");               break; /* ⫔ */
                case 10965:  AE("&subsub;");               break; /* ⫕ */
                case 10966:  AE("&supsup;");               break; /* ⫖ */
                case 10967:  AE("&suphsub;");              break; /* ⫗ */
                case 10968:  AE("&supdsub;");              break; /* ⫘ */
                case 10969:  AE("&forkv;");                break; /* ⫙ */
                case 10970:  AE("&topfork;");              break; /* ⫚ */
                case 10971:  AE("&mlcp;");                 break; /* ⫛ */
                /* Dashv aka DoubleLeftTee */
                case 10980:  AE("&Dashv;");                break; /* ⫤ */
                case 10982:  AE("&Vdashl;");               break; /* ⫦ */
                case 10983:  AE("&Barv;");                 break; /* ⫧ */
                case 10984:  AE("&vBar;");                 break; /* ⫨ */
                case 10985:  AE("&vBarv;");                break; /* ⫩ */
                case 10987:  AE("&Vbar;");                 break; /* ⫫ */
                case 10988:  AE("&Not;");                  break; /* ⫬ */
                case 10989:  AE("&bNot;");                 break; /* ⫭ */
                case 10990:  AE("&rnmid;");                break; /* ⫮ */
                case 10991:  AE("&cirmid;");               break; /* ⫯ */
                case 10992:  AE("&midcir;");               break; /* ⫰ */
                case 10993:  AE("&topcir;");               break; /* ⫱ */
                case 10994:  AE("&nhpar;");                break; /* ⫲ */
                case 10995:  AE("&parsim;");               break; /* ⫳ */
                case 11005:  TWOCPE;
                    if (clen2 && uniChar2 == 8421) { 
                        AE("&nparsl;");                           /* ⫽⃥ */
                    } else {
                        AE("&parsl;");                            /* ⫽ */
                    }; MCP;                                break;
                case 64256:  AE("&fflig;");                break; /* ﬀ */
                case 64257:  AE("&filig;");                break; /* ﬁ */
                case 64258:  AE("&fllig;");                break; /* ﬂ */
                case 64259:  AE("&ffilig;");               break; /* ﬃ */
                case 64260:  AE("&ffllig;");               break; /* ﬄ */
                case 119964: AE("&Ascr;");                 break; /* 𝒜 */
                case 119966: AE("&Cscr;");                 break; /* 𝒞 */
                case 119967: AE("&Dscr;");                 break; /* 𝒟 */
                case 119970: AE("&Gscr;");                 break; /* 𝒢 */
                case 119973: AE("&Jscr;");                 break; /* 𝒥 */
                case 119974: AE("&Kscr;");                 break; /* 𝒦 */
                case 119977: AE("&Nscr;");                 break; /* 𝒩 */
                case 119978: AE("&Oscr;");                 break; /* 𝒪 */
                case 119979: AE("&Pscr;");                 break; /* 𝒫 */
                case 119980: AE("&Qscr;");                 break; /* 𝒬 */
                case 119982: AE("&Sscr;");                 break; /* 𝒮 */
                case 119983: AE("&Tscr;");                 break; /* 𝒯 */
                case 119984: AE("&Uscr;");                 break; /* 𝒰 */
                case 119985: AE("&Vscr;");                 break; /* 𝒱 */
                case 119986: AE("&Wscr;");                 break; /* 𝒲 */
                case 119987: AE("&Xscr;");                 break; /* 𝒳 */
                case 119988: AE("&Yscr;");                 break; /* 𝒴 */
                case 119989: AE("&Zscr;");                 break; /* 𝒵 */
                case 119990: AE("&ascr;");                 break; /* 𝒶 */
                case 119991: AE("&bscr;");                 break; /* 𝒷 */
                case 119992: AE("&cscr;");                 break; /* 𝒸 */
                case 119993: AE("&dscr;");                 break; /* 𝒹 */
                case 119995: AE("&fscr;");                 break; /* 𝒻 */
                case 119997: AE("&hscr;");                 break; /* 𝒽 */
                case 119998: AE("&iscr;");                 break; /* 𝒾 */
                case 119999: AE("&jscr;");                 break; /* 𝒿 */
                case 120000: AE("&kscr;");                 break; /* 𝓀 */
                case 120001: AE("&lscr;");                 break; /* 𝓁 */
                case 120002: AE("&mscr;");                 break; /* 𝓂 */
                case 120003: AE("&nscr;");                 break; /* 𝓃 */
                case 120005: AE("&pscr;");                 break; /* 𝓅 */
                case 120006: AE("&qscr;");                 break; /* 𝓆 */
                case 120007: AE("&rscr;");                 break; /* 𝓇 */
                case 120008: AE("&sscr;");                 break; /* 𝓈 */
                case 120009: AE("&tscr;");                 break; /* 𝓉 */
                case 120010: AE("&uscr;");                 break; /* 𝓊 */
                case 120011: AE("&vscr;");                 break; /* 𝓋 */
                case 120012: AE("&wscr;");                 break; /* 𝓌 */
                case 120013: AE("&xscr;");                 break; /* 𝓍 */
                case 120014: AE("&yscr;");                 break; /* 𝓎 */
                case 120015: AE("&zscr;");                 break; /* 𝓏 */
                case 120068: AE("&Afr;");                  break; /* 𝔄 */
                case 120069: AE("&Bfr;");                  break; /* 𝔅 */
                case 120071: AE("&Dfr;");                  break; /* 𝔇 */
                case 120072: AE("&Efr;");                  break; /* 𝔈 */
                case 120073: AE("&Ffr;");                  break; /* 𝔉 */
                case 120074: AE("&Gfr;");                  break; /* 𝔊 */
                case 120077: AE("&Jfr;");                  break; /* 𝔍 */
                case 120078: AE("&Kfr;");                  break; /* 𝔎 */
                case 120079: AE("&Lfr;");                  break; /* 𝔏 */
                case 120080: AE("&Mfr;");                  break; /* 𝔐 */
                case 120081: AE("&Nfr;");                  break; /* 𝔑 */
                case 120082: AE("&Ofr;");                  break; /* 𝔒 */
                case 120083: AE("&Pfr;");                  break; /* 𝔓 */
                case 120084: AE("&Qfr;");                  break; /* 𝔔 */
                case 120086: AE("&Sfr;");                  break; /* 𝔖 */
                case 120087: AE("&Tfr;");                  break; /* 𝔗 */
                case 120088: AE("&Ufr;");                  break; /* 𝔘 */
                case 120089: AE("&Vfr;");                  break; /* 𝔙 */
                case 120090: AE("&Wfr;");                  break; /* 𝔚 */
                case 120091: AE("&Xfr;");                  break; /* 𝔛 */
                case 120092: AE("&Yfr;");                  break; /* 𝔜 */
                case 120094: AE("&afr;");                  break; /* 𝔞 */
                case 120095: AE("&bfr;");                  break; /* 𝔟 */
                case 120096: AE("&cfr;");                  break; /* 𝔠 */
                case 120097: AE("&dfr;");                  break; /* 𝔡 */
                case 120098: AE("&efr;");                  break; /* 𝔢 */
                case 120099: AE("&ffr;");                  break; /* 𝔣 */
                case 120100: AE("&gfr;");                  break; /* 𝔤 */
                case 120101: AE("&hfr;");                  break; /* 𝔥 */
                case 120102: AE("&ifr;");                  break; /* 𝔦 */
                case 120103: AE("&jfr;");                  break; /* 𝔧 */
                case 120104: AE("&kfr;");                  break; /* 𝔨 */
                case 120105: AE("&lfr;");                  break; /* 𝔩 */
                case 120106: AE("&mfr;");                  break; /* 𝔪 */
                case 120107: AE("&nfr;");                  break; /* 𝔫 */
                case 120108: AE("&ofr;");                  break; /* 𝔬 */
                case 120109: AE("&pfr;");                  break; /* 𝔭 */
                case 120110: AE("&qfr;");                  break; /* 𝔮 */
                case 120111: AE("&rfr;");                  break; /* 𝔯 */
                case 120112: AE("&sfr;");                  break; /* 𝔰 */
                case 120113: AE("&tfr;");                  break; /* 𝔱 */
                case 120114: AE("&ufr;");                  break; /* 𝔲 */
                case 120115: AE("&vfr;");                  break; /* 𝔳 */
                case 120116: AE("&wfr;");                  break; /* 𝔴 */
                case 120117: AE("&xfr;");                  break; /* 𝔵 */
                case 120118: AE("&yfr;");                  break; /* 𝔶 */
                case 120119: AE("&zfr;");                  break; /* 𝔷 */
                case 120120: AE("&Aopf;");                 break; /* 𝔸 */
                case 120121: AE("&Bopf;");                 break; /* 𝔹 */
                case 120123: AE("&Dopf;");                 break; /* 𝔻 */
                case 120124: AE("&Eopf;");                 break; /* 𝔼 */
                case 120125: AE("&Fopf;");                 break; /* 𝔽 */
                case 120126: AE("&Gopf;");                 break; /* 𝔾 */
                case 120128: AE("&Iopf;");                 break; /* 𝕀 */
                case 120129: AE("&Jopf;");                 break; /* 𝕁 */
                case 120130: AE("&Kopf;");                 break; /* 𝕂 */
                case 120131: AE("&Lopf;");                 break; /* 𝕃 */
                case 120132: AE("&Mopf;");                 break; /* 𝕄 */
                case 120134: AE("&Oopf;");                 break; /* 𝕆 */
                case 120138: AE("&Sopf;");                 break; /* 𝕊 */
                case 120139: AE("&Topf;");                 break; /* 𝕋 */
                case 120140: AE("&Uopf;");                 break; /* 𝕌 */
                case 120141: AE("&Vopf;");                 break; /* 𝕍 */
                case 120142: AE("&Wopf;");                 break; /* 𝕎 */
                case 120143: AE("&Xopf;");                 break; /* 𝕏 */
                case 120144: AE("&Yopf;");                 break; /* 𝕐 */
                case 120146: AE("&aopf;");                 break; /* 𝕒 */
                case 120147: AE("&bopf;");                 break; /* 𝕓 */
                case 120148: AE("&copf;");                 break; /* 𝕔 */
                case 120149: AE("&dopf;");                 break; /* 𝕕 */
                case 120150: AE("&eopf;");                 break; /* 𝕖 */
                case 120151: AE("&fopf;");                 break; /* 𝕗 */
                case 120152: AE("&gopf;");                 break; /* 𝕘 */
                case 120153: AE("&hopf;");                 break; /* 𝕙 */
                case 120154: AE("&iopf;");                 break; /* 𝕚 */
                case 120155: AE("&jopf;");                 break; /* 𝕛 */
                case 120156: AE("&kopf;");                 break; /* 𝕜 */
                case 120157: AE("&lopf;");                 break; /* 𝕝 */
                case 120158: AE("&mopf;");                 break; /* 𝕞 */
                case 120159: AE("&nopf;");                 break; /* 𝕟 */
                case 120160: AE("&oopf;");                 break; /* 𝕠 */
                case 120161: AE("&popf;");                 break; /* 𝕡 */
                case 120162: AE("&qopf;");                 break; /* 𝕢 */
                case 120163: AE("&ropf;");                 break; /* 𝕣 */
                case 120164: AE("&sopf;");                 break; /* 𝕤 */
                case 120165: AE("&topf;");                 break; /* 𝕥 */
                case 120166: AE("&uopf;");                 break; /* 𝕦 */
                case 120167: AE("&vopf;");                 break; /* 𝕧 */
                case 120168: AE("&wopf;");                 break; /* 𝕨 */
                case 120169: AE("&xopf;");                 break; /* 𝕩 */
                case 120170: AE("&yopf;");                 break; /* 𝕪 */
                case 120171: AE("&zopf;");                 break; /* 𝕫 */
                default: charDone = 0; 
                }
                if (charDone) {
                    pc += (clen - 1);
                }
            }
            if (!charDone) {
                if ((unsigned char)*pc > 127) {
                    if (!clen) {
                        domPanic("tcldom_AppendEscaped: can only handle "
                                 "UTF-8 chars up to 4 bytes length");
                    }
                    if (clen == 4 || outputFlags & SERIALIZE_ESCAPE_NON_ASCII) {
                        if (clen == 4) {
                            unicode = ((pc[0] & 0x07) << 18) 
                                + ((pc[1] & 0x3F) << 12)
                                + ((pc[2] & 0x3F) <<  6) 
                                + (pc[3] & 0x3F);
                        } else if (clen == 3) {
                            unicode = ((pc[0] & 0x0F) << 12) 
                                + ((pc[1] & 0x3F) << 6)
                                + (pc[2] & 0x3F);
                        } else {
                            unicode = ((pc[0] & 0x1F) << 6) 
                                + (pc[1] & 0x3F);
                        }
                        AP('&') AP('#')
                        sprintf(charRef, "%d", unicode);
                        for (i = 0; i < (int)strlen(charRef); i++) {
                            AP(charRef[i]);
                        }
                        AP(';')
                        pc += (clen - 1);
                    } else {
                        for (i = 0; i < clen; i++) {
                            AP(*pc);
                            pc++;
                        }
                        pc--;
                    }
                } else {
                    AP(*pc);
                }
            }
        }
        if (b >= bLimit) {
            writeChars(xmlString, chan, buf, b - buf);
            b = buf;
        }
        pc++;
    }
    if (b > buf) {
        writeChars(xmlString, chan, buf, b - buf);
    }
}

/*----------------------------------------------------------------------------
|   tcldom_tolower
|
\---------------------------------------------------------------------------*/
void tcldom_tolower (
    const char *str,
    char *str_out,
    int  len
)
{
    char *p;
    int  i;

    len--; i = 0; p = str_out;
    while (*str && (i < len)) {
        *p++ = tolower((unsigned char)*str++);
        i++;
    }
    *p++ = '\0';
}


/*----------------------------------------------------------------------------
|   tcldom_treeAsHTML
|
\---------------------------------------------------------------------------*/
static
void tcldom_treeAsHTML (
    Tcl_Obj     *htmlString,
    domNode     *node,
    Tcl_Channel  chan,
    int          escapeNonASCII,
    int          htmlEntities,
    int          doctypeDeclaration,
    int          noEscaping,
    int          onlyContents,
    int          breakLines
)
{
    int          empty, scriptTag, outputFlags = 0;
    domNode     *child;
    domAttrNode *attrs;
    domDocument *doc;
    char         tag[80], attrName[80];

    if (escapeNonASCII) outputFlags = SERIALIZE_ESCAPE_NON_ASCII;
    if (htmlEntities) outputFlags |= SERIALIZE_HTML_ENTITIES;
    if (node->nodeType == DOCUMENT_NODE) {
        doc = (domDocument*) node;
        if (doctypeDeclaration && doc->documentElement) {
            writeChars(htmlString, chan, "<!DOCTYPE ", 10);
            writeChars(htmlString, chan, doc->documentElement->nodeName, -1);
            if (   doc->doctype 
                && doc->doctype->systemId 
                && doc->doctype->systemId[0] != '\0') {
                if (   doc->doctype->publicId 
                    && doc->doctype->publicId[0] != '\0') {
                    writeChars(htmlString, chan, " PUBLIC \"", 9);
                    writeChars(htmlString, chan, doc->doctype->publicId, -1);
                    writeChars(htmlString, chan, "\" \"", 3);
                    writeChars(htmlString, chan, doc->doctype->systemId, -1);
                    writeChars(htmlString, chan, "\"", 1);
                } else {
                    writeChars(htmlString, chan, " SYSTEM \"", 9);
                    writeChars(htmlString, chan, doc->doctype->systemId, -1);
                    writeChars(htmlString, chan, "\"", 1);
                }
            }
            if (doc->doctype && doc->doctype->internalSubset) {
                writeChars(htmlString, chan, " [", 2);
                writeChars(htmlString, chan, doc->doctype->internalSubset, -1);
                writeChars(htmlString, chan, "]", 1);
            }
            writeChars(htmlString, chan, ">\n", 2);
        }
        child = doc->rootNode->firstChild;
        while (child) {
            tcldom_treeAsHTML(htmlString, child, chan, escapeNonASCII,
                              htmlEntities, doctypeDeclaration, 0, 0,
                              breakLines);
            child = child->nextSibling;
        }
        return;
    }
    
    if (node->nodeType == PROCESSING_INSTRUCTION_NODE) {
        domProcessingInstructionNode *dpn;
        dpn = (domProcessingInstructionNode *)node;
        writeChars(htmlString, chan, "<?", 2);
        writeChars(htmlString, chan, dpn->targetValue, dpn->targetLength);
        writeChars(htmlString, chan, " ", 1);
        writeChars(htmlString, chan, dpn->dataValue, dpn->dataLength);
        writeChars(htmlString, chan, ">", 1);
        return;
    }

    if (node->nodeType == TEXT_NODE) {
        if ((node->nodeFlags & DISABLE_OUTPUT_ESCAPING) 
            || noEscaping) {
            writeChars(htmlString, chan, ((domTextNode*)node)->nodeValue,
                       ((domTextNode*)node)->valueLength);
        } else {
            tcldom_AppendEscaped(htmlString, chan,
                                 ((domTextNode*)node)->nodeValue,
                                 ((domTextNode*)node)->valueLength,
                                 outputFlags);
        }
        return;
    }

    if (node->nodeType == CDATA_SECTION_NODE) {
        if (noEscaping) {
            writeChars(htmlString, chan, ((domTextNode*)node)->nodeValue,
                       ((domTextNode*)node)->valueLength);
        } else {
            tcldom_AppendEscaped(htmlString, chan,
                                 ((domTextNode*)node)->nodeValue,
                                 ((domTextNode*)node)->valueLength,
                                 outputFlags);
        }
        return;
    }

    if (node->nodeType == COMMENT_NODE) {
        writeChars(htmlString, chan, "<!--", 4);
        writeChars(htmlString, chan, ((domTextNode*)node)->nodeValue,
                   ((domTextNode*)node)->valueLength);
        writeChars(htmlString, chan,  "-->", 3);
        return;

    }

    tcldom_tolower(node->nodeName, tag, 80);
    /*-----------------------------------------------------------
    |   check for HTML tags, that must be handled special:
    |   empty tags and script tags (todo: HTML tags with
    |   URI attributes, to do escaping of Non-ASCII chars
    |   in the URI).
    \----------------------------------------------------------*/
    empty = 0;
    scriptTag = 0;
    switch (tag[0]) {
    case 'a':  if (!strcmp(tag,"area"))       {empty = 1;} break;
    case 'b':  if (!strcmp(tag,"br")     ||
                   !strcmp(tag,"base")   ||
                   !strcmp(tag,"basefont"))   {empty = 1;} break;
    case 'c':  if (!strcmp(tag,"col"))        {empty = 1;} break;
    case 'f':  if (!strcmp(tag,"frame"))      {empty = 1;} break;
    case 'h':  if (!strcmp(tag,"hr"))         {empty = 1;} break;
    case 'i':  if (!strcmp(tag,"img")    ||
                   !strcmp(tag,"input")  ||
                   !strcmp(tag,"isindex"))    {empty = 1;} break;
    case 'l':  if (!strcmp(tag,"link"))       {empty = 1;} break;
    case 'm':  if (!strcmp(tag,"meta"))       {empty = 1;} break;
    case 'p':  if (!strcmp(tag,"param"))      {empty = 1;} break;
    case 's':  if (!strcmp(tag,"script") ||     
                   !strcmp(tag,"style"))  {scriptTag = 1;} break;
    }

    if (!onlyContents) {
        writeChars(htmlString, chan, "<", 1);
        writeChars(htmlString, chan, tag, -1);

        attrs = node->firstAttr;
        while (attrs) {
            tcldom_tolower(attrs->nodeName, attrName, 80);
            writeChars(htmlString, chan, " ", 1);
            writeChars (htmlString, chan, attrName, -1);
            writeChars(htmlString, chan, "=\"", 2);
            tcldom_AppendEscaped(htmlString, chan, attrs->nodeValue, -1,
                                 outputFlags | SERIALIZE_FOR_ATTR);
            writeChars(htmlString, chan, "\"", 1);
            attrs = attrs->nextSibling;
        }
        if (breakLines) {
            writeChars(htmlString, chan, "\n>", 2);
        } else {
            writeChars(htmlString, chan, ">", 1);
        }
    }
    
    if (empty) {
        /* strange ! should not happen ! */
        child = node->firstChild;
        while (child != NULL) {
            tcldom_treeAsHTML(htmlString, child, chan, escapeNonASCII,
                              htmlEntities, doctypeDeclaration, scriptTag, 0,
                              breakLines);
            child = child->nextSibling;
        }
        return;
    }

    if (node->nodeType == ELEMENT_NODE) {
        child = node->firstChild;
        if ((child != NULL) && (child != node->lastChild)
            && (child->nodeType != TEXT_NODE)) {
            writeChars(htmlString, chan, "\n", 1);
        }
        while (child != NULL) {
            tcldom_treeAsHTML(htmlString, child, chan, escapeNonASCII,
                              htmlEntities, doctypeDeclaration, scriptTag, 0,
                              breakLines);
            child = child->nextSibling;
        }
        if ((node->firstChild != NULL) && (node->firstChild != node->lastChild)
            && (node->lastChild->nodeType != TEXT_NODE)) {
            writeChars(htmlString, chan, "\n", 1);
        }
    }
    if (!onlyContents) {
        writeChars(htmlString, chan, "</", 2);
        writeChars(htmlString, chan, tag, -1);
        writeChars(htmlString, chan, ">",  1);
    }
}


/*----------------------------------------------------------------------------
|   tcldom_treeAsXML
|
\---------------------------------------------------------------------------*/
static
void tcldom_treeAsXML (
    Tcl_Obj    *xmlString,
    domNode    *node,
    int         indent,
    int         level,
    int         doIndent,
    Tcl_Channel chan,
    Tcl_Obj    *encString,
    int         cdataChild,
    int         outputFlags,
    int         indentAttrs
)
{
    domAttrNode   *attrs;
    domNode       *child;
    domDocument   *doc;
    int            first, hasElements, i;
    char           prefix[MAX_PREFIX_LEN], *start, *p;
    const char    *localName;
    Tcl_HashEntry *h;
    Tcl_DString    dStr;

    if (outputFlags & SERIALIZE_XML_DECLARATION) {
        outputFlags &= ~SERIALIZE_XML_DECLARATION;
        writeChars(xmlString, chan, "<?xml version=\"1.0\"", 19);
        if (encString) {
            writeChars(xmlString, chan, " encoding=\"", 11);
            writeChars(xmlString, chan,
                       Tcl_GetString(encString), -1);
            writeChars(xmlString, chan, "\"", 1);
        } else if (node->nodeType == DOCUMENT_NODE &&
                   ((domDocument*) node)->doctype &&
                   ((domDocument*) node)->doctype->encoding) {
            writeChars(xmlString, chan, " encoding=\"", 11);
            writeChars(xmlString, chan,
                       ((domDocument*) node)->doctype->encoding, -1);
            writeChars(xmlString, chan, "\"", 1);
        }
        writeChars(xmlString, chan, "?>\n", 3);
    }
    if (node->nodeType == DOCUMENT_NODE) {
        doc = (domDocument*) node;
        if (outputFlags & SERIALIZE_DOCTYPE_DECLARATION
            && doc->documentElement) {
            writeChars(xmlString, chan, "<!DOCTYPE ", 10);
            writeChars(xmlString, chan, doc->documentElement->nodeName, -1);
            if (   doc->doctype 
                && doc->doctype->systemId
                && (doc->doctype->systemId[0] != '\0')) {
                if (   doc->doctype->publicId 
                    && doc->doctype->publicId[0] != '\0') {
                    writeChars(xmlString, chan, " PUBLIC \"", 9);
                    writeChars(xmlString, chan, doc->doctype->publicId, -1);
                    writeChars(xmlString, chan, "\" \"", 3);
                    writeChars(xmlString, chan, doc->doctype->systemId, -1);
                    writeChars(xmlString, chan, "\"", 1);
                } else {
                    writeChars(xmlString, chan, " SYSTEM \"", 9);
                    writeChars(xmlString, chan, doc->doctype->systemId, -1);
                    writeChars(xmlString, chan, "\"", 1);
                }
                if (doc->doctype->internalSubset) {
                    writeChars(xmlString, chan, " [", 2);
                    writeChars(xmlString, chan, doc->doctype->internalSubset,
                               -1);
                    writeChars(xmlString, chan, "]", 1);
                }
            }
            writeChars(xmlString, chan, ">\n", 2);
        }
        child = doc->rootNode->firstChild;
        while (child) {
            tcldom_treeAsXML(xmlString, child, indent, level, doIndent, chan,
                             NULL, 0, outputFlags, indentAttrs);
            child = child->nextSibling;
        }
        return;
    }

    if (node->nodeType == TEXT_NODE) {
        if (cdataChild) {
            writeChars(xmlString, chan, "<![CDATA[", 9);
            i = 0;
            start = p = ((domTextNode*)node)->nodeValue;
            while (i < ((domTextNode*)node)->valueLength) {
                if (*p == ']') {
                    p++; i++;
                    if (i >= ((domTextNode*)node)->valueLength) break;
                    if (*p == ']') {
                        p++; i++;
                        if (i >= ((domTextNode*)node)->valueLength) break;
                        if (*p == '>') {
                            writeChars(xmlString, chan, start, p-start);
                            writeChars(xmlString, chan, "]]><![CDATA[>", 13);
                            start = p+1;
                        }
                    }
                }
                p++; i++;
            }
            writeChars(xmlString, chan, start, p-start);
            writeChars(xmlString, chan, "]]>", 3);
        } else {
            if (node->nodeFlags & DISABLE_OUTPUT_ESCAPING) {
                writeChars(xmlString, chan, ((domTextNode*)node)->nodeValue,
                           ((domTextNode*)node)->valueLength);
            } else {
                tcldom_AppendEscaped(xmlString, chan,
                                     ((domTextNode*)node)->nodeValue,
                                     ((domTextNode*)node)->valueLength,
                                     outputFlags);
            }
        }
        return;
    }

    if (node->nodeType == CDATA_SECTION_NODE) {
        writeChars(xmlString, chan, "<![CDATA[", 9);
        writeChars(xmlString, chan, ((domTextNode*)node)->nodeValue,
                                    ((domTextNode*)node)->valueLength);
        writeChars(xmlString, chan, "]]>", 3);
        return;
    }

    if ((indent != -1) && doIndent) {
        if (outputFlags & SERIALIZE_INDENT_WITH_TAB) {
            for(i=0; i<level; i++) {
                writeChars(xmlString, chan, "\t", 1);
            }
        } else {
            for(i=0; i<level; i++) {
                writeChars(xmlString, chan, "        ", indent);
            }
        }
    }

    if (node->nodeType == COMMENT_NODE) {
        writeChars(xmlString, chan, "<!--", 4);
        writeChars(xmlString, chan, ((domTextNode*)node)->nodeValue,
                                    ((domTextNode*)node)->valueLength);
        writeChars(xmlString, chan, "-->", 3);
        if (indent != -1) writeChars (xmlString, chan, "\n", 1);
        return;
    }

    if (node->nodeType == PROCESSING_INSTRUCTION_NODE) {
        writeChars(xmlString, chan, "<?", 2);
        writeChars(xmlString, chan, 
                    ((domProcessingInstructionNode*)node)->targetValue,
                    ((domProcessingInstructionNode*)node)->targetLength);
        writeChars(xmlString, chan, " ", 1);
        writeChars(xmlString, chan, 
                   ((domProcessingInstructionNode*)node)->dataValue,
                   ((domProcessingInstructionNode*)node)->dataLength);
        writeChars(xmlString, chan, "?>", 2);
        if (indent != -1) writeChars (xmlString, chan, "\n", 1);
        return;
    }

    writeChars(xmlString, chan, "<", 1);
    writeChars(xmlString, chan, node->nodeName, -1);

    attrs = node->firstAttr;
    while (attrs) {
        if (indentAttrs > -1) {
            writeChars(xmlString, chan, "\n", 1);
            if ((indent != -1) && doIndent) {
                if (outputFlags & SERIALIZE_INDENT_WITH_TAB) {
                    for(i=0; i<level; i++) {
                        writeChars(xmlString, chan, "\t", 1);
                    }
                } else {
                    for(i=0; i<level; i++) {
                        writeChars(xmlString, chan, "        ", indent);
                    }
                }
                if (outputFlags & SERIALIZE_INDENT_ATTR_WITH_TAB) {
                    writeChars(xmlString, chan, "\t", 1);
                } else {
                    writeChars(xmlString, chan, "        ", indentAttrs);
                }
            }
        } else {
            writeChars(xmlString, chan, " ", 1);
        }
        writeChars(xmlString, chan, attrs->nodeName, -1);
        writeChars(xmlString, chan, "=\"", 2);
        tcldom_AppendEscaped(xmlString, chan, attrs->nodeValue, 
                             attrs->valueLength,
                             outputFlags | SERIALIZE_FOR_ATTR);
        writeChars(xmlString, chan, "\"", 1);
        attrs = attrs->nextSibling;
    }

    hasElements = 0;
    first       = 1;
    doIndent    = 1;

    if (node->nodeType == ELEMENT_NODE) {
        cdataChild = 0;
        if (node->ownerDocument->doctype
            && node->ownerDocument->doctype->cdataSectionElements) {
            if (node->namespace) {
                Tcl_DStringInit (&dStr);
                Tcl_DStringAppend (&dStr, domNamespaceURI(node), -1);
                Tcl_DStringAppend (&dStr, ":", 1);
                domSplitQName (node->nodeName, prefix, &localName);
                Tcl_DStringAppend (&dStr, localName, -1);
                h = Tcl_FindHashEntry (
                    node->ownerDocument->doctype->cdataSectionElements,
                    Tcl_DStringValue (&dStr));
                Tcl_DStringFree (&dStr);
            } else {
                h = Tcl_FindHashEntry (
                    node->ownerDocument->doctype->cdataSectionElements,
                    node->nodeName);
            }
            if (h) {
                cdataChild = 1;
            }
        }
        child = node->firstChild;
        while (child != NULL) {

            if (  (child->nodeType == ELEMENT_NODE)
                ||(child->nodeType == PROCESSING_INSTRUCTION_NODE)
                ||(child->nodeType == COMMENT_NODE) )
            {
                hasElements = 1;
            }
            if (first) {
                writeChars(xmlString, chan, ">", 1);
                if ((indent != -1) && hasElements) {
                    writeChars(xmlString, chan, "\n", 1);
                }
            }
            first = 0;
            tcldom_treeAsXML(xmlString, child, indent, level+1, doIndent,
                             chan, NULL, cdataChild, outputFlags, indentAttrs);
            doIndent = 0;
            if (  (child->nodeType == ELEMENT_NODE)
                ||(child->nodeType == PROCESSING_INSTRUCTION_NODE)
                ||(child->nodeType == COMMENT_NODE) )
            {
               doIndent = 1;
            }
            child = child->nextSibling;
        }
    }

    if (first) {
        if (indent != -1) {
            if (outputFlags & SERIALIZE_NO_EMPTY_ELEMENT_TAG) {
                writeChars (xmlString, chan, "></", 3);
                writeChars(xmlString, chan, node->nodeName, -1);
                writeChars(xmlString, chan, ">\n", 2);
            } else {
                writeChars(xmlString, chan, "/>\n", 3);
            }
        } else {
            if (outputFlags & SERIALIZE_NO_EMPTY_ELEMENT_TAG) {
                writeChars (xmlString, chan, "></", 3);
                writeChars(xmlString, chan, node->nodeName, -1);
                writeChars(xmlString, chan, ">", 1);
            } else {
                writeChars(xmlString, chan, "/>",   2);
            }
        }
    } else {
        if ((indent != -1) && hasElements) {
            if (outputFlags & SERIALIZE_INDENT_WITH_TAB) {
                for(i=0; i<level; i++) {
                    writeChars(xmlString, chan, "\t", 1);
                }
            } else {
                for(i=0; i<level; i++) {
                    writeChars(xmlString, chan, "        ", indent);
                }
            }
        }
        writeChars (xmlString, chan, "</", 2);
        writeChars(xmlString, chan, node->nodeName, -1);
        if (indent != -1) {
            writeChars(xmlString, chan, ">\n", 2);
        } else {
            writeChars(xmlString, chan, ">",   1);
        }
    }
}

/*----------------------------------------------------------------------------
|   tcldom_AppendEscapedJSON
|
\---------------------------------------------------------------------------*/
static
void tcldom_AppendEscapedJSON (
    Tcl_Obj    *jstring,
    Tcl_Channel chan,
    char       *value,
    domLength   value_length
)
{
    char  buf[APESC_BUF_SIZE+80], *b, *bLimit,  *pc, *pEnd;
    int   i;
    int   clen = 0;

    b = buf;
    bLimit = b + APESC_BUF_SIZE;
    pc = pEnd = value;
    if (value_length != -1) {
        pEnd = pc + value_length;
    }
    AP('"');
    while (
        (value_length == -1 && *pc)
        || (value_length != -1 && pc != pEnd)
    ) {
        clen = UTF8_CHAR_LEN(*pc);
        if (!clen) {
            /* This would be invalid utf-8 encoding. */
            clen = 1;
        }
        if (clen == 1) {
            if (*pc == '\\') {
                AP('\\'); AP('\\');
            } else if (*pc == '"') {
                AP('\\'); AP('"');
            } else if (*pc == '\b') {
                AP('\\'); AP('b');
            } else if (*pc == '\f') {
                AP('\\'); AP('f');
            } else if (*pc == '\n') {
                AP('\\'); AP('n');
            } else if (*pc == '\r') {
                AP('\\'); AP('r');
            } else if (*pc == '\t') {
                AP('\\'); AP('t');
            } else if ((unsigned char)*pc < 0x20) {
                AP('\\'); AP('u'); AP('0'); AP('0');
                AP('0' + (*pc>>4));
                AP("0123456789abcdef"[*pc&0xf]);
            } else {
                AP(*pc);
            }
            pc++;
        } else {
            if ((unsigned char)*pc == 0xC0 && (unsigned char)*(pc+1) == 0x80) {
                AP('\\');AP('u');AP('0');AP('0');AP('0');AP('0');
                pc++;pc++;
            } else {
                for (i = 0; i < clen; i++) {
                    AP(*pc);
                    pc++;
                }
            }
        }
        if (b >= bLimit) {
            writeChars(jstring, chan, buf, b - buf);
            b = buf;
        }
    }
    AP('"');
    writeChars(jstring, chan, buf, b - buf);
}

static
void tcldom_childrenAsJSON (
    Tcl_Obj     *jstring,
    domNode     *node, /* Must be an ELEMENT_NODE */
    Tcl_Channel  channel,
    int          indent,
    int          outputFlags,
    int          level,
    int          inside
    )
{
    domNode   *child, *nextChild;
    int i, effectivParentType = 0;
    int first = 1;

    child = node->firstChild;
    while (child
           && child->nodeType != TEXT_NODE
           && child->nodeType != ELEMENT_NODE) {
        child = child->nextSibling;
    }

    if (node->info == JSON_ARRAY || node->info == JSON_OBJECT) {
        effectivParentType = node->info;
    } else if (child == NULL) {
        /* Need 'heuristic rule' to decide, what to do. */
        switch (inside) {
        case JSON_OBJECT:
            /* The children to serialize are the value of an object member. */
            /* No content at all. This could be an empty string,
             * an empty object or an empty array. We default to
             * empty string. */
            writeChars(jstring, channel, "\"\"",2);
            return;
        case JSON_START:
        case JSON_ARRAY:
            /* The children, we serialize are the value of an array
             * element. The node is a container for either a
             * (nested, in case of JSON_ARRAY) array or an object. */
            /* Look, if the name of the container gives a hint.*/
            if (strcmp (node->nodeName, JSON_ARRAY_CONTAINER)==0) {
                effectivParentType = JSON_ARRAY;
                break;
            }
            /* If we here, heuristics didn't helped. We have to
             * default to something. Let's say ... */
            effectivParentType = JSON_OBJECT;
            break;
        }
    } else {
        if (child->nodeType == ELEMENT_NODE) {
            /* The first 'relevant' child node is ELEMENT_NODE */
            effectivParentType = JSON_OBJECT;
            if (inside == JSON_ARRAY) {
                /* Though, if we inside of an array and the node name
                 * of the first 'relevant' child is the array
                 * container element, we assume an array (with a
                 * nested array as first value of that array. */
                if (strcmp (child->nodeName, JSON_ARRAY_CONTAINER))
                    effectivParentType = JSON_ARRAY;
            }
        } else {
            /* If we are here, the first 'relevant' child is a
             * text node. If there is any other 'relevant' child,
             * we assume the value to be an array. Otherwise (only
             * single 'relevant' child is a text node), this is
             * any of string, true, false null. Child may have a
             * type hint. */
            nextChild = child->nextSibling;
            while (nextChild
                   && nextChild->nodeType != TEXT_NODE
                   && nextChild->nodeType != ELEMENT_NODE) {
                nextChild = nextChild->nextSibling;
            }
            if (nextChild) {
                effectivParentType = JSON_ARRAY;
            } else {
                /* Exactly one 'relevant' child node, a text node;
                 * serialize it as simple token value. */
                tcldom_treeAsJSON (jstring, child, channel, indent,
                                   outputFlags, level, JSON_ARRAY);
                return;
            }
        }
    }
        
    switch (effectivParentType) {
    case JSON_ARRAY:
        writeChars(jstring, channel, "[",1);
        while (child) {
            if (first) {
                first = 0;
                level++;
            } else {
                writeChars(jstring, channel, ",", 1);
            }
            if (indent > -1) {
                writeChars(jstring, channel, "\n", 1);
                if (outputFlags & SERIALIZE_INDENT_WITH_TAB) {
                    for (i = 0; i < level; i++) {
                        writeChars(jstring, channel, "\t", 1);
                    }
                } else {
                    for (i = 0; i < level; i++) {
                        writeChars(jstring, channel, "        ", indent);
                    }
                }
            }
            tcldom_treeAsJSON (jstring, child, channel, indent,
                               outputFlags, level, JSON_ARRAY);
            child = child->nextSibling;
            while (child
                   && child->nodeType != TEXT_NODE
                   && child->nodeType != ELEMENT_NODE) {
                child = child->nextSibling;
            }
        }
        if (indent > -1 && first == 0) {
            writeChars(jstring, channel, "\n", 1);
            level--;
            if (outputFlags & SERIALIZE_INDENT_WITH_TAB) {
                for (i = 0; i < level; i++) {
                    writeChars(jstring, channel, "\t", 1);
                }
            } else {
                for (i = 0; i < level; i++) {
                    writeChars(jstring, channel, "        ", indent);
                }
            }
        }
        writeChars(jstring, channel, "]",1);
        break;
    case JSON_OBJECT:
        writeChars(jstring, channel, "{",1);
        while (child) {
            if (first) {
                first = 0;
                level++;
            } else {
                writeChars(jstring, channel, ",", 1);
            }
            if (indent > -1) {
                writeChars(jstring, channel, "\n", 1);
                if (outputFlags & SERIALIZE_INDENT_WITH_TAB) {
                    for (i = 0; i < level; i++) {
                        writeChars(jstring, channel, "\t", 1);
                    }
                } else {
                    for (i = 0; i < level; i++) {
                        writeChars(jstring, channel, "        ", indent);
                    }
                }
            }
            tcldom_treeAsJSON (jstring, child, channel, indent,
                               outputFlags, level, JSON_OBJECT);
            child = child->nextSibling;
            /* Inside of a JSON_OBJECT, only element children make
             * semantically sense. */
            while (child && child->nodeType != ELEMENT_NODE) {
                child = child->nextSibling;
            }
        }
        if (indent > -1 && first == 0) {
            writeChars(jstring, channel, "\n", 1);
            level--;
            if (outputFlags & SERIALIZE_INDENT_WITH_TAB) {
                for (i = 0; i < level; i++) {
                    writeChars(jstring, channel, "\t", 1);
                }
            } else {
                for (i = 0; i < level; i++) {
                    writeChars(jstring, channel, "        ", indent);
                }
            }
        }
        writeChars(jstring, channel, "}",1);
        break;
    default:
        break;
    }
}


/*----------------------------------------------------------------------------
|   tcldom_treeAsJSON
|
\---------------------------------------------------------------------------*/
static
void tcldom_treeAsJSON (
    Tcl_Obj     *jstring,
    domNode     *node,  /* Must not be NULL */
    Tcl_Channel  channel,
    int          indent,
    int          outputFlags,
    int          level,
    int          inside
    )
{
    domTextNode *textNode;
    int i, seenDP, seenE;
    unsigned char c;
    char *num;
    
    switch (node->nodeType) {
    case TEXT_NODE:
        if (inside == JSON_OBJECT) {
            /* We're inside a JSON object. A text node can not be
             * meaningful interpreted as member of an object. Ignore
             * the node */
            return;
        }
        textNode = (domTextNode *) node;
        switch (node->info) {
        case JSON_NUMBER:
            /* Check, if the text value is a JSON number and fall back
             * to string token, if not. This is to ensure, the
             * serialization is always a valid JSON string. */
            if (textNode->valueLength == 0) goto notANumber;
            seenDP = 0;
            seenE = 0;
            i = 0;
            num = textNode->nodeValue;
            c = num[0];
            if (!(c == '-' || (c>='0' && c<='9'))) goto notANumber;
            if (c<='0') {
                i = (c == '-' ? i+1 : i);
                if (i+1 < textNode->valueLength) {
                    if (num[i] == '0' && num[i+1] >= '0' && num[i+1] <= '9') {
                        goto notANumber;
                    }
                }
            }
            i = 1;
            for (; i < textNode->valueLength; i++) {
                c = num[i];
                if (c >= '0' && c <= '9') continue;
                if (c == '.') {
                    if (num[i-1] == '-') goto notANumber;
                    if (seenDP) goto notANumber;
                    seenDP = 1;
                    continue;
                }
                if (c == 'e' || c == 'E') {
                    if (num[i-1] < '0') goto notANumber;
                    if (seenE) goto notANumber;
                    seenDP = seenE = 1;
                    c = num[i+1];
                    if (c == '+' || c == '-') {
                        i++;
                        c = num[i+1];
                    }
                    if (c < '0' || c > '9') goto notANumber;
                    continue;
                }
                break;
            }
            /* Catches a plain '-' without following digits */
            if (num[i-1] < '0') goto notANumber;
            /* Catches trailing chars */
            if (i < textNode->valueLength) goto notANumber;
            writeChars(jstring, channel, textNode->nodeValue,
                       textNode->valueLength);
            break;
            notANumber:
            tcldom_AppendEscapedJSON (jstring, channel,
                                      textNode->nodeValue,
                                      textNode->valueLength);
            break;
        case JSON_NULL:
            writeChars(jstring, channel, "null",4);
            break;
        case JSON_TRUE:
            writeChars(jstring, channel, "true",4);
            break;
        case JSON_FALSE:
            writeChars(jstring, channel, "false",5);
            break;
        case JSON_STRING:
            /* Fall through */
        default:
            tcldom_AppendEscapedJSON (jstring, channel,
                                      textNode->nodeValue,
                                      textNode->valueLength);
            break;
        };
        return;
    case ELEMENT_NODE:
        switch (inside) {
        case JSON_OBJECT:
            /* Write the member name and recurse to the children for
             * the value. */
            tcldom_AppendEscapedJSON (jstring, channel,
                                      node->nodeName, -1);
            writeChars (jstring, channel, ":", 1);
            if (indent > -1 || outputFlags & SERIALIZE_INDENT_WITH_TAB) {
                writeChars (jstring, channel, " ", 1);
            }
            tcldom_childrenAsJSON (jstring, node, channel, indent,
                                 outputFlags, level, inside);
            break;
        case JSON_ARRAY:
            /* Since we're already inside of an array, the element can
               only be interpreted as a container for a nested JSON
               object or array. */
            tcldom_childrenAsJSON (jstring, node, channel, indent,
                                 outputFlags, level, inside);
            break;
        case JSON_START:
            tcldom_childrenAsJSON (jstring, node, channel, indent,
                                 outputFlags, level, inside);            
            break;
        }
        return;
    default:
        /* Any other node types (COMMENT_NODE, CDATA_SECTION_NODE, 
           PROCESSING_INSTRUCTION_NODE) are ignored. */
        return;
    }
}

/*----------------------------------------------------------------------------
|   findBaseURI
|
\---------------------------------------------------------------------------*/
const char *findBaseURI (
    domNode *node
)
{
    const char *baseURI = NULL;
    Tcl_HashEntry *entryPtr;
    domNode       *orgNode;
    
    orgNode = node;
    do {
        if (node->nodeFlags & HAS_BASEURI) {
            entryPtr = Tcl_FindHashEntry(node->ownerDocument->baseURIs,
                                         (char*)node);
            baseURI = (const char *)Tcl_GetHashValue(entryPtr);
            break;
        } else {
            node = node->parentNode;
        }
    } while (node);
    if (!baseURI) {
        node = orgNode->ownerDocument->rootNode;
        if (node->nodeFlags & HAS_BASEURI) {
            entryPtr = Tcl_FindHashEntry(node->ownerDocument->baseURIs,
                                          (char*)node);
            baseURI = (const char *)Tcl_GetHashValue(entryPtr);
        }
    }
    return baseURI;
}

/*----------------------------------------------------------------------------
|   serializeAsXML
|
\---------------------------------------------------------------------------*/
static int serializeAsXML (
    domNode    *node,
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    *const objv[]
)
{
    char          *channelId, prefix[MAX_PREFIX_LEN];
    const char    *localName;
    int            indent, mode, bool;
    int            outputFlags = 0;
    int            optionIndex, cdataChild;
    Tcl_Obj       *resultPtr, *encString = NULL;
    Tcl_Channel    chan = (Tcl_Channel) NULL;
    Tcl_HashEntry *h;
    Tcl_DString    dStr;
    int            indentAttrs = -1;

    static const char *asXMLOptions[] = {
        "-indent", "-channel", "-escapeNonASCII", "-doctypeDeclaration",
        "-xmlDeclaration", "-encString", "-escapeAllQuot", "-indentAttrs",
        "-nogtescape", "-noEmptyElementTag", "-escapeCR", "-escapeTab",
        NULL
    };
    enum asXMLOption {
        m_indent, m_channel, m_escapeNonASCII, m_doctypeDeclaration,
        m_xmlDeclaration, m_encString, m_escapeAllQuot, m_indentAttrs,
        m_nogtescape, m_noEmptyElementTag, m_escapeCR, m_escapeTab
    };
    
    indent = 4;
    while (objc > 2) {
        if (Tcl_GetIndexFromObj(interp, objv[2], asXMLOptions, "option", 0,
                               &optionIndex) != TCL_OK) {
            goto cleanup;
        }
        switch ((enum asXMLOption) optionIndex) {

        case m_indent:
            if (objc < 4) {
                SetResult("-indent must have an argument "
                          "(0..8 or 'no'/'none'/'tabs')");
                goto cleanup;
            }
            if (strcmp("none", Tcl_GetString(objv[3]))==0) {
                indent = -1;
            }
            else if (strcmp("no", Tcl_GetString(objv[3]))==0) {
                indent = -1;
            }
            else if (strcmp("tabs", Tcl_GetString(objv[3]))==0) {
                outputFlags |= SERIALIZE_INDENT_WITH_TAB;
            }
            else if (Tcl_GetIntFromObj(interp, objv[3], &indent) != TCL_OK) {
                SetResult( "indent must be an integer (0..8) or "
                           "'no'/'none'/'tabs'");
                goto cleanup;
            }
            objc -= 2;
            objv += 2;
            break;

        case m_indentAttrs:
            if (objc < 4) {
                SetResult("-indentAttrs must have an argument "
                          "(0..8 or 'no'/'none'/'tabs')");
                goto cleanup;
            }
            if (strcmp("none", Tcl_GetString(objv[3]))==0) {
                indentAttrs = -1;
            }
            else if (strcmp("no", Tcl_GetString(objv[3]))==0) {
                indentAttrs = -1;
            }
            else if (strcmp("tabs", Tcl_GetString(objv[3]))==0) {
                outputFlags |= SERIALIZE_INDENT_ATTR_WITH_TAB;
            }
            else if (Tcl_GetIntFromObj(interp, objv[3], &indentAttrs) != TCL_OK) {
                SetResult( "indentAttrs must be an integer (0..8) or "
                           "'no'/'none'/'tabs'");
                goto cleanup;
            }
            if (indentAttrs > 8) indentAttrs = 8;
            if (indentAttrs < 0) indentAttrs = 0;
            objc -= 2;
            objv += 2;
            break;

        case m_channel:
            if (objc < 4) {
                SetResult("-channel must have a channeldID as argument");
                goto cleanup;
            }
            channelId = Tcl_GetString(objv[3]);
            chan = Tcl_GetChannel(interp, channelId, &mode);
            if (chan == (Tcl_Channel) NULL) {
                SetResult("-channel must have a channeldID as argument");
                goto cleanup;
            }
            if ((mode & TCL_WRITABLE) == 0) {
                Tcl_AppendResult(interp, "channel \"", channelId,
                                "\" is not opened for writing", (char*)NULL);
                goto cleanup;
            }
            objc -= 2;
            objv += 2;
            break;

        case m_escapeNonASCII:
            outputFlags |= SERIALIZE_ESCAPE_NON_ASCII;
            objc--;
            objv++;
            break;
            
        case m_doctypeDeclaration:
            if (node->nodeType != DOCUMENT_NODE) {
                SetResult("-doctypeDeclaration as flag to the method "
                          "'asXML' is only allowed for domDocCmds");
                goto cleanup;
            }
            if (objc < 4) {
                SetResult("-doctypeDeclaration must have a boolean value "
                          "as argument");
                goto cleanup;
            }
            if (Tcl_GetBooleanFromObj(interp, objv[3], &bool)
                != TCL_OK) {
                goto cleanup;
            }
            if (bool) outputFlags |= SERIALIZE_DOCTYPE_DECLARATION;
            objc -= 2;
            objv += 2;
            break;

        case m_xmlDeclaration:
            if (objc < 4) {
                SetResult("-xmlDeclaration must have a boolean value "
                          "as argument");
                goto cleanup;
            }
            if (Tcl_GetBooleanFromObj(interp, objv[3], &bool)
                != TCL_OK) {
                goto cleanup;
            }
            if (bool) outputFlags |= SERIALIZE_XML_DECLARATION;
            objc -= 2;
            objv += 2;
            break;

        case m_encString:
            if (objc < 4) {
                SetResult("-encString must have a string "
                          "as argument");
                goto cleanup;
            }
            if (encString) {
                Tcl_DecrRefCount(encString);
            }
            encString = objv[3];
            Tcl_IncrRefCount(encString);
            objc -= 2;
            objv += 2;
            break;
            
        case m_escapeAllQuot:
            outputFlags |= SERIALIZE_ESCAPE_ALL_QUOT;
            objc -= 1;
            objv += 1;
            break;

        case m_nogtescape:
            outputFlags |= SERIALIZE_NO_GT_ESCAPE;
            objc -= 1;
            objv += 1;
            break;

        case m_noEmptyElementTag:
            outputFlags |= SERIALIZE_NO_EMPTY_ELEMENT_TAG;
            objc -= 1;
            objv += 1;
            break;

        case m_escapeCR:
            outputFlags |= SERIALIZE_ESCAPE_CR;
            objc -= 1;
            objv += 1;
            break;

        case m_escapeTab:
            outputFlags |= SERIALIZE_ESCAPE_TAB;
            objc -= 1;
            objv += 1;
            break;
        }
    }
    if (indent > 8)  indent = 8;
    if (indent < -1) indent = -1;

    resultPtr = Tcl_NewStringObj("", 0);
    cdataChild = 0;
    if (node->nodeType == ELEMENT_NODE
        && node->ownerDocument->doctype 
        && node->ownerDocument->doctype->cdataSectionElements) {
        if (node->namespace) {
            Tcl_DStringInit (&dStr);
            Tcl_DStringAppend (&dStr, domNamespaceURI(node), -1);
            Tcl_DStringAppend (&dStr, ":", 1);
            domSplitQName (node->nodeName, prefix, &localName);
            Tcl_DStringAppend (&dStr, localName, -1);
            h = Tcl_FindHashEntry (
                node->ownerDocument->doctype->cdataSectionElements,
                Tcl_DStringValue (&dStr));
            Tcl_DStringFree (&dStr);
        } else {
            h = Tcl_FindHashEntry (
                node->ownerDocument->doctype->cdataSectionElements,
                node->nodeName);
        }
        if (h) {
            cdataChild = 1;
        }
    }
    tcldom_treeAsXML(resultPtr, node, indent, 0, 1, chan, encString,
                     cdataChild, outputFlags, indentAttrs);
    Tcl_SetObjResult(interp, resultPtr);
    if (encString) {
        Tcl_DecrRefCount(encString);
    }
    return TCL_OK;
cleanup:
    if (encString) {
        Tcl_DecrRefCount(encString);
    }
    return TCL_ERROR;
}

static int compareNSAtts (
    domAttrNode *a,
    domAttrNode *b
    ) 
{
    if (strcmp (a->nodeName, "xmlns") == 0) {
        return -1;
    } else if (strcmp (b->nodeName, "xmlns") == 0) {
        return 1;
    }
    return (strcmp (&a->nodeName[6], &b->nodeName[6]));
}

static int compareAtts (
    domAttrNode *a,
    domAttrNode *b
    ) 
{
    domNS *ans, *bns;
    domDocument *doc;
    const char *alocalname, *blocalname;
    int res;


    if (a->nodeFlags & IS_NS_NODE) {
        if (b->nodeFlags & IS_NS_NODE) {
            return compareNSAtts (a, b);
        } else {
            return -1;
        }
    } else {
        if (a->nodeFlags & IS_NS_NODE) {
            return 1;
        }
    }
    if (a->namespace) {
        if (b->namespace) {
            doc = a->parentNode->ownerDocument;
            ans = domGetNamespaceByIndex (doc, a->namespace);
            bns = domGetNamespaceByIndex (doc, b->namespace);
            res = strcmp (ans->uri, bns->uri);
            if (res == 0) {
                alocalname = domGetLocalName (a->nodeName);
                blocalname = domGetLocalName (b->nodeName);
                return strcmp (alocalname, blocalname);
            } else {
                return res;
            }
        } else {
            return 1;
        }
    } else {
        if (b->namespace) {
            return -1;
        } else {
            alocalname = domGetLocalName (a->nodeName);
            blocalname = domGetLocalName (b->nodeName);
            return strcmp (alocalname, blocalname);
        }
    }
}

static domAttrNode* mergeAtt (
    domAttrNode *a,
    int len_a,
    domAttrNode *b,
    int len_b
    ) 
{
    domAttrNode *start, *head;
    int pos_a = 0;
    int pos_b = 0;

    
    if (!len_a) {
        return b;
    }
    if (!len_b) {
        return a;
    }
    if (compareAtts (a, b) < 0) {
        pos_a++;
        start = a;
        a = a->nextSibling;
    } else {
        pos_b++;
        start = b;
        b = b->nextSibling;
    }
    head = start;
    while (pos_a < len_a && pos_b < len_b) {
        if (compareAtts (a, b) < 0) {
            pos_a++;
            head->nextSibling = a;
            head = a;
            a = a->nextSibling;
        } else {
            pos_b++;
            head->nextSibling = b;
            head = b;
            b = b->nextSibling;
        }
    }
    while (pos_a < len_a) {
        pos_a++;
        head->nextSibling = a;
        head = a;
        a = a->nextSibling;
    }
    while (pos_b < len_b) {
        pos_b++;
        head->nextSibling = b;
        head = b;
        b = b->nextSibling;
    }
    head->nextSibling = NULL;
    return start;
}


static domAttrNode* mergeSortAtt (
    domAttrNode *attr,
    int len
    )
{
    domAttrNode *attr_a, *attr_b;
    int half, count = 1;

    if (len <= 1) {
        return attr;
    }
    half = len/2;
    attr_b = attr->nextSibling;
    while (count < half) {
        attr_b = attr_b->nextSibling;
        count++;
    }
    attr_a = attr;
    return mergeAtt (mergeSortAtt (attr_a, half), half,
                     mergeSortAtt (attr_b, len - half), len - half);
}
    
    
static void
treeAsCanonicalXML (
    Tcl_Obj  *xmlString,
    domNode  *node,
    Tcl_Channel chan,
    int comments,
    domAttrNode **attOrderArray,
    int  *lengthAttOrderArray
    )
{
    domAttrNode   *attr, *thisAtt = NULL, *previousAtt, *attOrder;
    domNode       *child;
    domDocument   *doc;
    domNS         *ns, *ns1;
    int            outputFlags = SERIALIZE_ESCAPE_CR, attNr = 0;

    switch (node->nodeType) {
    case ELEMENT_NODE:
        writeChars(xmlString, chan, "<", 1);
        writeChars(xmlString, chan, node->nodeName, -1);
    restartAttOrder:
        attOrder = *attOrderArray;
        attr = node->firstAttr;
        if (attr) {
            doc = node->ownerDocument;
            /* Attribute sort: It would be possible to sort the
             * attributes linked list in place, but this would mean
             * that two asXML serializations with an asCanonicalXML
             * serialization inbetween may be different, which would
             * be surprsing. */
            while (attr) {
                if(attr->nodeFlags & IS_NS_NODE) {
                    ns = domGetNamespaceByIndex(doc, attr->namespace);
                    ns1 = domLookupPrefix (node->parentNode, ns->prefix);
                    if (ns1) {
                        if (strcmp (ns->uri, ns1->uri) == 0) {
                            /* Namespace declaration already in scope,
                             * suppress it */
                            attr = attr->nextSibling;
                            continue;
                        }
                    } else {
                        if (ns->uri[0] == '\0') {
                            /* This is a superfluous unsetting of the
                             * default namespace because there isn't
                             * default namespace in scope. */
                            attr = attr->nextSibling;
                            continue;
                        }
                    }
                }
                if (attNr >= *lengthAttOrderArray) {
                    FREE (*attOrderArray);
                    *attOrderArray = MALLOC (sizeof (domAttrNode)
                                            * *lengthAttOrderArray * 2);
                    *lengthAttOrderArray *= 2;
                    attNr = 0;
                    goto restartAttOrder;
                }

                thisAtt = memcpy (&attOrder[attNr], attr,
                                  sizeof (domAttrNode));
                if (attNr) {
                    previousAtt = &attOrder[attNr-1];
                    previousAtt->nextSibling = thisAtt;
                }
                attNr++;
                attr = attr->nextSibling;
            }
            if (attNr && thisAtt) {
                thisAtt->nextSibling = NULL;
            }
            attr = mergeSortAtt (attOrderArray[0], attNr) ;
            while (attr) {
                writeChars(xmlString, chan, " ", 1);
                writeChars(xmlString, chan, attr->nodeName, -1);
                writeChars(xmlString, chan, "=\"", 2);
                tcldom_AppendEscaped(xmlString, chan, attr->nodeValue, 
                                     attr->valueLength,
                                     outputFlags
                                     | SERIALIZE_FOR_ATTR
                                     | SERIALIZE_NO_GT_ESCAPE
                                     | SERIALIZE_ESCAPE_TAB);
                writeChars(xmlString, chan, "\"", 1);
                attr = attr->nextSibling;
            }
        }
        writeChars(xmlString, chan, ">", 1);
        child = node->firstChild;
        while (child != NULL) {
            treeAsCanonicalXML (xmlString, child, chan, comments,
                                attOrderArray, lengthAttOrderArray);
            child = child->nextSibling;
        }
        writeChars(xmlString, chan, "</", 2);
        writeChars(xmlString, chan, node->nodeName, -1);
        writeChars(xmlString, chan, ">", 1);
        break;
    case TEXT_NODE:
    case CDATA_SECTION_NODE:
        tcldom_AppendEscaped(xmlString, chan,
                             ((domTextNode*)node)->nodeValue,
                             ((domTextNode*)node)->valueLength,
                             outputFlags);
        break;
    case COMMENT_NODE:
        if (comments) {
            writeChars(xmlString, chan, "<!--", 4);
            writeChars(xmlString, chan, ((domTextNode*)node)->nodeValue,
                       ((domTextNode*)node)->valueLength);
            writeChars(xmlString, chan, "-->", 3);
        }
        break;
    case PROCESSING_INSTRUCTION_NODE:
        writeChars(xmlString, chan, "<?", 2);
        writeChars(xmlString, chan, 
                    ((domProcessingInstructionNode*)node)->targetValue,
                    ((domProcessingInstructionNode*)node)->targetLength);
        if (((domProcessingInstructionNode*)node)->dataLength) {
            writeChars(xmlString, chan, " ", 1);
            writeChars(xmlString, chan, 
                       ((domProcessingInstructionNode*)node)->dataValue,
                       ((domProcessingInstructionNode*)node)->dataLength);
        }
        writeChars(xmlString, chan, "?>", 2);
        break;
    default:
        /* Nothing to output */
        break;
    }
    return;
}


/*----------------------------------------------------------------------------
|   serializeAsCanonicalXML
|
\---------------------------------------------------------------------------*/
static int serializeAsCanonicalXML (
    domNode    *node,
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    *const objv[]
)
{
    int optionIndex, mode, comments = 0, first= 1;
    int lengthAttOrderArray = C14N_ATTR_SORT_SIZE_INIT;
    Tcl_Channel chan = NULL;
    domNode *docChild;
    Tcl_Obj *resultObj;
    domAttrNode *attOrderArray;

    static const char *asCanonicalXMLOptions[] = {
        "-channel", "-comments", NULL
    };
    enum asCanonicalXMLOption {
        m_channel, m_comments
    };
    
    while (objc > 3) {
        if (Tcl_GetIndexFromObj(interp, objv[2], asCanonicalXMLOptions,
                                "option", 0, &optionIndex) != TCL_OK) {
            return TCL_ERROR;
        }
        switch ((enum asCanonicalXMLOption) optionIndex) {
        case m_channel:
            chan = Tcl_GetChannel (interp, Tcl_GetString (objv[3]), &mode);
            if (chan == NULL) {
                SetResult("the -channel option must have a Tcl channel"
                          " argument");
                return TCL_ERROR;
            }
            if ((mode & TCL_WRITABLE) == 0) {
                SetResult ("channel is not opened for writing");
                return TCL_ERROR;
            }
            objc -= 2;
            objv += 2;
            break;
            
        case m_comments:
            if (Tcl_GetBooleanFromObj(interp, objv[3], &comments) != TCL_OK) {
                return TCL_ERROR;
            }
            objc -= 2;
            objv += 2;
            break;
        }
    }
        
    if (objc > 2) {
        SetResult ("unexpected argument(s) after options");
        return TCL_ERROR;
    }
    attOrderArray = (domAttrNode *) MALLOC (sizeof (domAttrNode)
                                            * C14N_ATTR_SORT_SIZE_INIT);
    if (node->nodeType == DOCUMENT_NODE) {
        docChild = ((domDocument*)node)->rootNode->firstChild;
        resultObj = Tcl_GetObjResult (interp);
        while (docChild) {
            if (first) {
                first = 0;
            } else {
                if ((docChild->nodeType != COMMENT_NODE) || comments) {
                    writeChars(resultObj, chan, "\n", 1);
                }
            }
            treeAsCanonicalXML(resultObj, docChild, chan, comments,
                               &attOrderArray, &lengthAttOrderArray);
            docChild = docChild->nextSibling;
        }
    } else {
        treeAsCanonicalXML(Tcl_GetObjResult (interp), node, chan, comments,
                           &attOrderArray, &lengthAttOrderArray);
    }
    FREE (attOrderArray);
    return TCL_OK;
}

/*----------------------------------------------------------------------------
|   serializeAsHTML
|
\---------------------------------------------------------------------------*/
static int serializeAsHTML (
    domNode    *node,
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    *const objv[]
)
{
    char       *channelId;
    int         optionIndex, mode, escapeNonASCII = 0, htmlEntities = 0;
    int         doctypeDeclaration = 0, onlyContents = 0, breakLines = 0;
    Tcl_Obj    *resultPtr;
    Tcl_Channel chan = (Tcl_Channel) NULL;

    static const char *asHTMLOptions[] = {
        "-channel", "-escapeNonASCII", "-htmlEntities", "-doctypeDeclaration",
        "-onlyContents", "-breakLines", NULL        
    };
    enum asHTMLOption {
        m_channel, m_escapeNonASCII, m_htmlEntities, m_doctypeDeclaration,
        m_onlyContents, m_breakLines
    };
    
    if (objc > 10) {
        Tcl_WrongNumArgs(interp, 2, objv,
                         "?-channel <channelId>? ?-escapeNonASCII? "
                         "?-htmlEntities? ?-doctypeDeclaration <boolean>? "
                         "?-onlyContents? ?-breakLines?");
        return TCL_ERROR;
    }
    while (objc > 2) {
        if (Tcl_GetIndexFromObj(interp, objv[2], asHTMLOptions, "option", 
                                0, &optionIndex) != TCL_OK) {
            return TCL_ERROR;
        }
        switch ((enum asHTMLOption) optionIndex) {
            
        case m_channel:
            if (objc < 4) {
                SetResult("-channel must have a channeldID as argument");
                return TCL_ERROR;
            }
            channelId = Tcl_GetString(objv[3]);
            chan = Tcl_GetChannel(interp, channelId, &mode);
            if (chan == (Tcl_Channel) NULL) {
                SetResult("-channel must have a channeldID as argument");
                return TCL_ERROR;
            }
            if ((mode & TCL_WRITABLE) == 0) {
                Tcl_AppendResult(interp, "channel \"", channelId,
                                "\" wasn't opened for writing", (char*)NULL);
                return TCL_ERROR;
            }
            objc -= 2;
            objv += 2;
            break;

        case m_escapeNonASCII:
            escapeNonASCII = 1;
            objc--;
            objv++;
            break;

        case m_htmlEntities:
            htmlEntities = 1;
            objc--;
            objv++;
            break;

        case m_doctypeDeclaration:
            if (node->nodeType != DOCUMENT_NODE) {
                SetResult("-doctypeDeclaration as flag to the method "
                          "'asHTML' is only allowed for domDocCmds");
                return TCL_ERROR;
            }
            if (objc < 4) {
                SetResult("-doctypeDeclaration must have a boolean value "
                          "as argument");
                return TCL_ERROR;
            }
            if (Tcl_GetBooleanFromObj(interp, objv[3], &doctypeDeclaration)
                != TCL_OK) {
                return TCL_ERROR;
            }
            objc -= 2;
            objv += 2;
            break;
 
        case m_onlyContents:
            onlyContents = 1;
            objc--;
            objv++;
            break;

        case m_breakLines:
            breakLines = 1;
            objc--;
            objv++;
            break;
        }
    }
    resultPtr = Tcl_NewStringObj("", 0);
    tcldom_treeAsHTML(resultPtr, node, chan, escapeNonASCII, htmlEntities,
                      doctypeDeclaration, 0, onlyContents, breakLines);
    Tcl_AppendResult(interp, Tcl_GetString(resultPtr), NULL);
    Tcl_DecrRefCount(resultPtr);
    return TCL_OK;
}

/*----------------------------------------------------------------------------
|   serializeAsJSON
|
\---------------------------------------------------------------------------*/
static int serializeAsJSON (
    domNode    *node,
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    *const objv[]
)
{
    char       *channelId;
    int         optionIndex, mode, outputFlags = 0, indent = -1;
    Tcl_Obj    *resultPtr;
    Tcl_Channel chan = (Tcl_Channel) NULL;

    static const char *asJSONOptions[] = {
        "-channel", "-indent",
        NULL
    };
    enum asJSONOption {
        m_channel, m_indent
    };

    if (node->nodeType != ELEMENT_NODE) {
        SetResult("Not an element node.\n");
        return TCL_ERROR;
    }
    
    if (objc > 5) {
        Tcl_WrongNumArgs(interp, 2, objv,
                         "?-channel <channelId>? "
                         "?-indent <none,0..8>?");
        return TCL_ERROR;
    }
    while (objc > 2) {
        if (Tcl_GetIndexFromObj(interp, objv[2], asJSONOptions, "option", 
                                0, &optionIndex) != TCL_OK) {
            return TCL_ERROR;
        }
        switch ((enum asJSONOption) optionIndex) {

        case m_channel:
            if (objc < 4) {
                SetResult("-channel must have a channeldID as argument");
                return TCL_ERROR;
            }
            channelId = Tcl_GetString(objv[3]);
            chan = Tcl_GetChannel(interp, channelId, &mode);
            if (chan == (Tcl_Channel) NULL) {
                SetResult("-channel must have a channeldID as argument");
                return TCL_ERROR;
            }
            if ((mode & TCL_WRITABLE) == 0) {
                Tcl_AppendResult(interp, "channel \"", channelId,
                                "\" wasn't opened for writing", (char*)NULL);
                return TCL_ERROR;
            }
            objc -= 2;
            objv += 2;
            break;

        case m_indent:
            if (objc < 4) {
                SetResult("-indent must have an argument "
                          "(0..8 or 'no'/'none')");
                return TCL_ERROR;
            }
            if (strcmp("none", Tcl_GetString(objv[3]))==0) {
                indent = -1;
            }
            else if (strcmp("no", Tcl_GetString(objv[3]))==0) {
                indent = -1;
            }
            else if (strcmp("tabs", Tcl_GetString(objv[3]))==0) {
                /* User wants indentation */
                indent = 0;
                outputFlags |= SERIALIZE_INDENT_WITH_TAB;
            }
            else if (Tcl_GetIntFromObj(interp, objv[3], &indent) != TCL_OK) {
                SetResult( "indent must be an integer (0..8) or 'no'/'none'");
                return TCL_ERROR;
            } else if (indent < 0 || indent > 8) {
                SetResult( "indent must be an integer (0..8) or 'no'/'none'");
                return TCL_ERROR;
            }
                
            objc -= 2;
            objv += 2;
            break;
        }
    }
    resultPtr = Tcl_NewStringObj("", 0);
    tcldom_treeAsJSON(resultPtr, node, chan, indent, outputFlags, 0,
                      JSON_START);
    Tcl_AppendResult(interp, Tcl_GetString(resultPtr), NULL);
    Tcl_DecrRefCount(resultPtr);
    return TCL_OK;
}

/*----------------------------------------------------------------------------
|   cdataSectionElements
|
\---------------------------------------------------------------------------*/
static int cdataSectionElements (
    domDocument *doc,
    Tcl_Interp  *interp,
    int          objc,
    Tcl_Obj     *const objv[] 
    )
{
    int result, hnew;
    Tcl_Obj *resultPtr,*namePtr;
    Tcl_HashEntry *h;
    Tcl_HashSearch search;
    
    CheckArgs (3,4,0, "<domDoc> cdataSectionElements ?URI:?localname "
               "?boolean?");
    if (objc == 3) {
        if (Tcl_GetString(objv[2])[0] == '*' 
            && Tcl_GetString(objv[2])[1] == '\0') {
            Tcl_ResetResult (interp);
            if (doc->doctype && doc->doctype->cdataSectionElements) {
                resultPtr = Tcl_GetObjResult (interp);
                for (h = Tcl_FirstHashEntry (
                         doc->doctype->cdataSectionElements, &search);
                     h != NULL;
                     h = Tcl_NextHashEntry(&search)) {
                    namePtr = Tcl_NewStringObj (
                        Tcl_GetHashKey (doc->doctype->cdataSectionElements,
                                        h), -1);
                    result = Tcl_ListObjAppendElement (interp, resultPtr, 
                                                       namePtr);
                    if (result != TCL_OK) {
                        Tcl_DecrRefCount(namePtr);
                        return result;
                    }
                }
            }
            return TCL_OK;
        }
        if (!doc->doctype || !doc->doctype->cdataSectionElements) {
            SetBooleanResult (0);
        } else {
            if (Tcl_FindHashEntry (doc->doctype->cdataSectionElements,
                                   Tcl_GetString (objv[2]))) {
                SetBooleanResult (1);
            } else {
                SetBooleanResult (0);
            }
        }
    } else {
        if (Tcl_GetBooleanFromObj (interp, objv[3], &result) 
            != TCL_OK) {
            return TCL_ERROR;
        }
        if (result) {
            if (!doc->doctype) {
                doc->doctype = (domDocInfo *)MALLOC(sizeof(domDocInfo));
                memset(doc->doctype, 0,(sizeof(domDocInfo)));
            }
            if (!doc->doctype->cdataSectionElements) {
                doc->doctype->cdataSectionElements =
                    (Tcl_HashTable *)MALLOC(sizeof(Tcl_HashTable));
                Tcl_InitHashTable (doc->doctype->cdataSectionElements,
                                   TCL_STRING_KEYS);
            }
            Tcl_CreateHashEntry (doc->doctype->cdataSectionElements,
                                 Tcl_GetString (objv[2]), &hnew);
        } else {
            if (doc->doctype && doc->doctype->cdataSectionElements) {
                h = Tcl_FindHashEntry (doc->doctype->cdataSectionElements,
                                       Tcl_GetString (objv[2]));
                if (h) {
                    Tcl_DeleteHashEntry (h);
                    if (!doc->doctype->cdataSectionElements->numEntries) {
                        Tcl_DeleteHashTable (
                            doc->doctype->cdataSectionElements
                            );
                        FREE (doc->doctype->cdataSectionElements);
                        doc->doctype->cdataSectionElements = NULL;
                    }
                }
            }
        }
        SetBooleanResult(result);
    }
    return TCL_OK;
}

/*----------------------------------------------------------------------------
|   selectNodesNamespaces
|
\---------------------------------------------------------------------------*/
int tcldom_prefixNSlist (
    char      ***prefixnsPtr,
    Tcl_Interp  *interp,
    int          objc,
    Tcl_Obj     *const objv[],
    const char  *methodName
    )
{
    char   **prefixns = *prefixnsPtr;
    domLength len, i;
    int result;
    Tcl_Obj *objPtr, *listPtr;

    i = 0;
    if (objc == 1) {
        if (!prefixns) return TCL_OK;
        listPtr = Tcl_NewListObj (0, NULL);
        i = 0;
        while (prefixns[i]) {
            Tcl_ListObjAppendElement (
                interp, listPtr, Tcl_NewStringObj (prefixns[i], -1)
                );
            i++;
        }
        Tcl_SetObjResult (interp, listPtr);
        return TCL_OK;
    }
    result = Tcl_ListObjLength (interp, objv[1], &len);
    if (result != TCL_OK || (len % 2) != 0) {
        SetResult3 ("The optional argument to ", methodName, 
                   " must be a 'prefix namespace' pairs list");
        return TCL_ERROR;
    }
    if (prefixns) {
        while (prefixns[i]) {
            FREE (prefixns[i]);
            i++;
        }
    }
    if (len == 0) {
        FREE (prefixns);
        *prefixnsPtr = NULL;
        return TCL_OK;
    }
    if (i < len + 1) {
        if (prefixns) FREE (prefixns);
        prefixns = MALLOC (sizeof (char*) * (len+1));
        *prefixnsPtr = prefixns;
    }
    for (i = 0; i < len; i++) {
        Tcl_ListObjIndex (interp, objv[1], i, &objPtr);
        prefixns[i] = tdomstrdup (Tcl_GetString (objPtr));
    }
    prefixns[len] = NULL;
    Tcl_SetObjResult (interp, objv[1]);
    return TCL_OK;
}

/*----------------------------------------------------------------------------
|   renameNodes
|
\---------------------------------------------------------------------------*/
static int renameNodes (
    domDocument *doc,
    Tcl_Interp  *interp,
    int          objc,
    Tcl_Obj     *const objv[] 
    )
{
    domLength len, i;
    int hnew;
    Tcl_HashEntry *h;
    Tcl_Obj *objPtr;
    domNode     *node;
    
    CheckArgs (4,4,0, "<domDoc> renameNode nodeList name");
    if (Tcl_ListObjLength (interp, objv[2], &len) != TCL_OK) {
        SetResult ("The first argument to the renameNode method"
                   " must be a list of element nodes.");
        return TCL_ERROR;
    }
    h = Tcl_CreateHashEntry(&HASHTAB(doc,tdom_tagNames), 
                            Tcl_GetString(objv[3]), &hnew);
    for (i = 0; i < len; i++) {
        Tcl_ListObjIndex (interp, objv[2], i, &objPtr);
        node = tcldom_getNodeFromObj (interp, objPtr);
        if (node == NULL) {
            return TCL_ERROR;
        }
        node->nodeName = (char *)&(h->key);
    }
    return TCL_OK;
}

/*----------------------------------------------------------------------------
|   deleteXPathCache
|
\---------------------------------------------------------------------------*/
static int deleteXPathCache (
    domDocument *doc,
    Tcl_Interp  *interp,
    int          objc,
    Tcl_Obj     *const objv[] 
    )
{
    Tcl_HashEntry *h;
    Tcl_HashSearch search;
    
    CheckArgs (2,3,0, "<domDoc> deleteXPathCache ?xpathQuery?");
    if (objc == 3) {
        if (!doc->xpathCache) {
            return TCL_OK;
        }
        h = Tcl_FindHashEntry (doc->xpathCache, Tcl_GetString(objv[2]));
        if (h) {
            xpathFreeAst((ast)Tcl_GetHashValue (h));
            Tcl_DeleteHashEntry (h);
        }
        return TCL_OK;
    }
    if (!doc->xpathCache) {
        return TCL_OK;
    }
    h = Tcl_FirstHashEntry (doc->xpathCache, &search);
    while (h) {
        xpathFreeAst((ast)Tcl_GetHashValue (h));
        h = Tcl_NextHashEntry (&search);
    }
    Tcl_DeleteHashTable (doc->xpathCache);
    FREE (doc->xpathCache);
    doc->xpathCache = NULL;
    return TCL_OK;
}


/*----------------------------------------------------------------------------
|   applyXSLT
|
\---------------------------------------------------------------------------*/
static int applyXSLT (
    domNode     *node,
    Tcl_Interp  *interp,
    void        *clientData,
    int          objc,
    Tcl_Obj     *const objv[]
    )
{
    char          *usage, **parameters = NULL, *errMsg, *option;
    Tcl_Obj       *objPtr, *localListPtr = (Tcl_Obj *)NULL;
    int            result, optionIndex, ignoreUndeclaredParameters = 0;
    int            maxApplyDepth = MAX_XSLT_APPLY_DEPTH;
    domLength      i, length;
    domDocument   *xsltDoc, *xmlDoc, *resultDoc = NULL;
    XsltMsgCBInfo  xsltMsgInfo;

    static char *method_usage = 
        "wrong # args: should be \"nodeObj xslt ?-parameters parameterList? "
        "?-ignoreUndeclaredParameters? ?-maxApplyDepth int? "
        "?-xsltmessagecmd cmd? xsltDocNode ?varname?\"";

    static char *cmd_usage = 
        "wrong # args: should be \"?-parameters parameterList? "
        "?-ignoreUndeclaredParameters? ?-maxApplyDepth int? "
        "?-xsltmessagecmd cmd? <xmlDocObj> ?objVar?\"";

    static const char *xsltOptions[] = {
        "-parameters", "-ignoreUndeclaredParameters",
        "-maxApplyDepth", "-xsltmessagecmd", NULL
    };

    enum xsltOption {
        m_parameters, m_ignoreUndeclaredParameters, m_maxApplyDepth,
        m_xsltmessagecmd
    };

    xsltMsgInfo.interp = interp;
    xsltMsgInfo.msgcmd = NULL;

    if (node)  usage = method_usage;
    else       usage = cmd_usage;
    
    while (objc > 1) {
        option = Tcl_GetString(objv[0]);
        if (option[0] != '-') {
            break;
        }
        if (Tcl_GetIndexFromObj(interp, objv[0], xsltOptions, "option", 0,
                                 &optionIndex) != TCL_OK) {
            goto applyXSLTCleanUP;
        }
    
        switch ((enum xsltOption) optionIndex) {

        case m_parameters:
            if (objc < 3) {SetResult(usage); goto applyXSLTCleanUP;}
            if (Tcl_ListObjLength(interp, objv[1], &length) != TCL_OK) {
                SetResult("ill-formed parameters list: the -parameters "
                          "option needs a list of parameter name and "
                          "parameter value pairs");
                goto applyXSLTCleanUP;
            }
            if (length % 2) {
                SetResult("parameter value missing: the -parameters "
                          "option needs a list of parameter name and "
                          "parameter value pairs");
                goto applyXSLTCleanUP;
            }
            if (parameters) {
                SetResult("only one -parameters option allowed");
                goto applyXSLTCleanUP;
            }
            localListPtr = Tcl_DuplicateObj(objv[1]);
            Tcl_IncrRefCount(localListPtr);
            parameters =  (char **)MALLOC(sizeof(char *)*(length+1));
            for (i = 0; i < length; i ++) {
                Tcl_ListObjIndex(interp, localListPtr, i, &objPtr);
                parameters[i] = Tcl_GetString(objPtr);
            }
            parameters[length] = NULL;
            objc -= 2;
            objv += 2;
            break;

        case m_maxApplyDepth:
            if (objc < 3) {SetResult(usage); goto applyXSLTCleanUP;}
            if (Tcl_GetIntFromObj(interp, objv[1], &maxApplyDepth)
                != TCL_OK) {
                SetResult("-maxApplyDepth requires a positive integer "
                          "as argument");
                goto applyXSLTCleanUP;
            }
            if (maxApplyDepth < 1) {
                SetResult("-maxApplyDepth requires a positive integer "
                          "as argument");
                goto applyXSLTCleanUP;
            }
            objc -= 2;
            objv += 2;
            break;
        
        case m_ignoreUndeclaredParameters:
            if (objc < 2) {SetResult(usage); goto applyXSLTCleanUP;}
            ignoreUndeclaredParameters = 1;
            objc--; objv++;
            break;
            
        case m_xsltmessagecmd:
            if (objc < 3) {SetResult(usage); goto applyXSLTCleanUP;}
            if (xsltMsgInfo.msgcmd) {
                Tcl_DecrRefCount(xsltMsgInfo.msgcmd);
            }
            xsltMsgInfo.msgcmd = objv[1];
            Tcl_IncrRefCount(xsltMsgInfo.msgcmd);
            objc -= 2;
            objv += 2;
            break;
        }
    }
    if (objc > 2 || objc < 1) {SetResult(usage); goto applyXSLTCleanUP;}
    if (node) {
        xsltDoc = tcldom_getDocumentFromName(interp, Tcl_GetString(objv[0]),
                                             &errMsg);
        if (xsltDoc == NULL) {
            SetResult( errMsg );
            goto applyXSLTCleanUP;
        }
    } else {
        xmlDoc = tcldom_getDocumentFromName(interp,Tcl_GetString(objv[0]),
                                            &errMsg);
        if (xmlDoc == NULL) {
            SetResult( errMsg );
            goto applyXSLTCleanUP;
        }
        node = (domNode *) xmlDoc;
        xsltDoc = NULL;
    }
    result = xsltProcess(xsltDoc, node, clientData, parameters, 
                         ignoreUndeclaredParameters,
                         maxApplyDepth,
                         tcldom_xpathFuncCallBack,  interp,
                         tcldom_xsltMsgCB, &xsltMsgInfo,
                         &errMsg, &resultDoc);

    if (result < 0) {
        SetResult( errMsg );
        FREE(errMsg);
        if (objc == 2) {
            Tcl_SetVar (interp, Tcl_GetString(objv[1]), "", 0);
        }
        goto applyXSLTCleanUP;
    }
    if (parameters) {
        Tcl_DecrRefCount(localListPtr);
        FREE((char *) parameters);
    }
    if (xsltMsgInfo.msgcmd) {
        Tcl_DecrRefCount(xsltMsgInfo.msgcmd);
    }
    return tcldom_returnDocumentObj(interp, resultDoc, (objc == 2),
                                    (objc == 2) ? objv[1] : NULL, 1, 0);
            
 applyXSLTCleanUP:
    if (localListPtr) {
        Tcl_DecrRefCount(localListPtr);
        FREE((char *) parameters);
    }
    if (xsltMsgInfo.msgcmd) {
        Tcl_DecrRefCount(xsltMsgInfo.msgcmd);
    }
    return TCL_ERROR;
}

/*----------------------------------------------------------------------------
|   tcldom_XSLTObjCmd
|
\---------------------------------------------------------------------------*/
static int tcldom_XSLTObjCmd (
    ClientData  clientData,
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    *const objv[]
)
{
    int          index;
    char        *errMsg = NULL;
    
    static const char *options[] = {
        "transform", "delete", NULL
    };
    enum option {
        m_transform, m_delete
    };
    

    /* Longest possible call currently is:
       xsltCmd transform -parameters parameterList \
                         -ignoreUndeclaredParameters
                         -xsltmessagecmd cmd <xmlDocObj> objVar */
    CheckArgs(2,9,1,"option ?arg ...?");

    /* This is not optimal, because we do the
       tcldom_getDocumentFromName call here and again in
       applyXSLT. This is only transitional, until <domNode xslt ..>
       will be deprecated */
    if ((tcldom_getDocumentFromName (interp, Tcl_GetString(objv[1]), &errMsg) 
         != NULL)
        || (Tcl_GetString (objv[1])[0] == '-')) {
        /* Method obmitted, may default to "transform", try this */
        objv++;
        objc--;
        return applyXSLT(NULL, interp, (void *) clientData, objc, objv);
    }

    if (Tcl_GetIndexFromObj (interp, objv[1], options, "option", 0, &index)
        != TCL_OK) {
        return TCL_ERROR;
    }
    switch ((enum option) index) {
    case m_transform:
        objv++;objv++;
        objc--;objc--;
        return applyXSLT(NULL, interp, (void *) clientData, objc, objv);
    case m_delete:
        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, "");
            return TCL_ERROR;
        }
        Tcl_DeleteCommand(interp, Tcl_GetString(objv[0]));
    }
    return TCL_OK;
}

/*----------------------------------------------------------------------------
|   convertToXSLTCmd
|
\---------------------------------------------------------------------------*/
static int convertToXSLTCmd (
    domDocument *doc,
    Tcl_Interp  *interp,
    int          setVariable,
    Tcl_Obj     *var_name
    )
{
    char *errMsg, *objVar, objCmdName[80];
    ClientData *clientData;

    doc->nodeFlags |= DONT_FREE;
    clientData = (ClientData *) xsltCompileStylesheet(doc,
                                                      tcldom_xpathFuncCallBack,
                                                      interp, 0, &errMsg);
    if (!clientData) {
        doc->nodeFlags &= ~DONT_FREE;
        SetResult(errMsg);
        if (setVariable) {
            objVar = Tcl_GetString(var_name);
            Tcl_UnsetVar(interp, objVar, 0);
            Tcl_SetVar   (interp, objVar, "", 0);
        }
        FREE(errMsg);
        return TCL_ERROR;
    }
    DOC_CMD(objCmdName, doc);
    Tcl_DeleteCommand( interp, objCmdName );
    XSLT_CMD(objCmdName, doc);
    Tcl_CreateObjCommand(interp, objCmdName, tcldom_XSLTObjCmd, clientData,
                          xsltFreeStateWrapper);
    if (setVariable) {
        objVar = Tcl_GetString(var_name);
        Tcl_UnsetVar (interp, objVar, 0);
        Tcl_SetVar   (interp, objVar, objCmdName, 0);
    }
    SetResult(objCmdName);
    return TCL_OK;
}

/*----------------------------------------------------------------------------
|   tcldom_NodeObjCmd
|
\---------------------------------------------------------------------------*/
int tcldom_NodeObjCmd (
    ClientData  clientData,
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    *const objv[]
)
{
    GetTcldomDATA;

    domNode     *node, *child, *refChild, *oldChild, *refNode;
    domNS       *ns;
    domAttrNode *attrs;
    domException exception;
    char         tmp[200], prefix[MAX_PREFIX_LEN], *method, *nodeName,
                 *str, *attr_name, *attr_val, *filter;
    const char  *localName, *uri, *nsStr;
    int          result, methodIndex, i;
    domLength    length;
    long         line, column, byteIndex;
    int          nsIndex, bool, hnew, legacy, jsonType;
    Tcl_Obj     *namePtr, *resultPtr;
    Tcl_Obj     *mobjv[MAX_REWRITE_ARGS];
    Tcl_CmdInfo  cmdInfo;
    Tcl_HashEntry *h;

    static const char *nodeMethods[] = {
        "firstChild",      "nextSibling",    "getAttribute",    "nodeName",
        "nodeValue",       "nodeType",       "attributes",      "asList",
        "find",            "setAttribute",   "removeAttribute", "parentNode",
        "previousSibling", "lastChild",      "appendChild",     "removeChild",
        "hasChildNodes",   "localName",      "childNodes",      "ownerDocument",
        "insertBefore",    "replaceChild",   "getLine",         "getColumn",
        "asXML",           "appendFromList", "child",           "fsibling",
        "psibling",        "descendant",     "ancestor",        "text",
        "root",            "hasAttribute",   "cloneNode",       "appendXML",
        "target",          "data",           "selectNodes",     "namespaceURI",
        "getAttributeNS",  "setAttributeNS", "hasAttributeNS",  "removeAttributeNS",
        "asHTML",          "prefix",         "getBaseURI",      "appendFromScript",
        "xslt",            "toXPath",        "delete",          "getElementById",
        "getElementsByTagName",              "getElementsByTagNameNS",
        "disableOutputEscaping",             "precedes",         "asText",
        "insertBeforeFromScript",            "normalize",        "baseURI",
        "asJSON",          "jsonType",       "attributeNames",   "asCanonicalXML",
        "getByteIndex",
#ifdef TCL_THREADS
        "readlock",        "writelock",
#endif
        NULL
    };
    enum nodeMethod {
        m_firstChild,      m_nextSibling,    m_getAttribute,    m_nodeName,
        m_nodeValue,       m_nodeType,       m_attributes,      m_asList,
        m_find,            m_setAttribute,   m_removeAttribute, m_parentNode,
        m_previousSibling, m_lastChild,      m_appendChild,     m_removeChild,
        m_hasChildNodes,   m_localName,      m_childNodes,      m_ownerDocument,
        m_insertBefore,    m_replaceChild,   m_getLine,         m_getColumn,
        m_asXML,           m_appendFromList, m_child,           m_fsibling,
        m_psibling,        m_descendant,     m_ancestor,        m_text,
        m_root,            m_hasAttribute,   m_cloneNode,       m_appendXML,
        m_target,          m_data,           m_selectNodes,     m_namespaceURI,
        m_getAttributeNS,  m_setAttributeNS, m_hasAttributeNS,  m_removeAttributeNS,
        m_asHTML,          m_prefix,         m_getBaseURI,      m_appendFromScript,
        m_xslt,            m_toXPath,        m_delete,          m_getElementById,
        m_getElementsByTagName,              m_getElementsByTagNameNS,
        m_disableOutputEscaping,             m_precedes,        m_asText,
        m_insertBeforeFromScript,            m_normalize,       m_baseURI,
        m_asJSON,          m_jsonType,       m_attributeNames,  m_asCanonicalXML,
        m_getByteIndex
#ifdef TCL_THREADS
        ,m_readlock,       m_writelock
#endif
    };

    node = (domNode*) clientData;
    if (TcldomDATA(domCreateCmdMode) == DOM_CREATECMDMODE_AUTO) {
        TcldomDATA(dontCreateObjCommands) = 0;
    }
    if (node == NULL) {
        if (objc < 3) {
            SetResult(node_usage);
            return TCL_ERROR;
        }
        if (TcldomDATA(domCreateCmdMode) == DOM_CREATECMDMODE_AUTO) {
            TcldomDATA(dontCreateObjCommands) = 1;
        }
        node = tcldom_getNodeFromObj(interp, objv[1]);
        if (node == NULL) {
            return TCL_ERROR;
        }
        objc--;
        objv++;
    }
    if (objc < 2) {
        SetResult(node_usage);
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(NULL, objv[1], nodeMethods, "method", 0,
                            &methodIndex) != TCL_OK) {

        method = Tcl_GetString(objv[1]);
        if (*method != '@') {
            /*--------------------------------------------------------
            |   not a getAttribute short cut:
            |   try to find method implemented as normal Tcl proc
            \-------------------------------------------------------*/
            result = 0;
            if (node->nodeType == ELEMENT_NODE) {
                /*----------------------------------------------------
                |   try to find Tcl level node specific method proc
                |
                |       ::dom::domNode::<nodeName>::<method>
                |
                \---------------------------------------------------*/
                sprintf(tmp, "::dom::domNode::%s::%s", (char*)node->nodeName,
                        method);
                DBG(fprintf(stderr, "testing %s\n", tmp));
                result = Tcl_GetCommandInfo(interp, tmp, &cmdInfo);
            }
            if (!result) {
                /*----------------------------------------------------
                |   try to find Tcl level general method proc
                |
                |       ::dom::domNode::<method>
                |
                \---------------------------------------------------*/
                sprintf(tmp, "::dom::domNode::%s", method);
                DBG(fprintf(stderr, "testing %s\n", tmp));
                result = Tcl_GetCommandInfo(interp, tmp, &cmdInfo);
            }
            if (!result) {
                SetResult(node_usage);
                return TCL_ERROR;
            }
            if (!cmdInfo.isNativeObjectProc) {
                SetResult("can't access Tcl level method!");
                return TCL_ERROR;
            }
            if (objc >= MAX_REWRITE_ARGS) {
                SetResult("too many args to call Tcl level method!");
                return TCL_ERROR;
            }
            mobjv[0] = objv[1];
            mobjv[1] = objv[0];
            for (i=2; i<objc; i++) mobjv[i] = objv[i];
            return cmdInfo.objProc(cmdInfo.objClientData, interp, objc, mobjv);
        }

        /*--------------------------------------------------------
        |   @<attributeName>: try to look up attribute
        \-------------------------------------------------------*/
        Tcl_ResetResult(interp);
        CheckArgs(2,3,1,"@<attributeName> ?defaultvalue?");
        if (node->nodeType != ELEMENT_NODE) {
            SetResult("NOT_AN_ELEMENT : there are no attributes");
            return TCL_ERROR;
        }
        attrs = node->firstAttr;
        while (attrs && strcmp(attrs->nodeName, &(method[1]))) {
            attrs = attrs->nextSibling;
        }
        if (attrs) {
            SetResult(attrs->nodeValue);
        } else {
            if (objc == 3) {
                SetResult(Tcl_GetString(objv[2]));
            } else {
                Tcl_ResetResult(interp);
                Tcl_AppendResult(interp, "Attribute \"", &(method[1]),
                                 "\" not found!", NULL);
                return TCL_ERROR;
            }
        }
        return TCL_OK;
    }

    /*----------------------------------------------------------------------
    |   node may have been deleted in the meantime by some other 
    |   thread operating on the tree, so check this fact before.
    |
    \---------------------------------------------------------------------*/

    if (node->nodeFlags & IS_DELETED) {
        SetResult("node has been deleted");
        return TCL_ERROR;
    }

    /*----------------------------------------------------------------------
    |   dispatch the node object method
    |
    \---------------------------------------------------------------------*/
    switch ((enum nodeMethod)methodIndex) {

        case m_toXPath:
            CheckArgs(2,3,2,"?-legacy?");
            legacy = 0;
            if (objc == 3) {
                if (!strcmp(Tcl_GetString(objv[2]), "-legacy")) {
                    legacy = 1;
                } else {
                    SetResult("unknown option! Options: ?-legacy?");
                    return TCL_ERROR;
                }
            }
            str = xpathNodeToXPath(node, legacy);
            SetResult (str);
            FREE (str);
            return TCL_OK;

        case m_xslt:
            CheckArgs(3,9,2, "?-parameters parameterList? "
                      "?-ignoreUndeclaredParameters? ?-xsltmessagecmd cmd? "
                      "<xsltDocNode> ?objVar?");
            objv += 2; objc -= 2;
            return applyXSLT(node, interp, NULL, objc, objv);

        case m_selectNodes:
            return tcldom_selectNodes (interp, node, --objc, ++objv);

        case m_find:
            CheckArgs(4,5,2,"attrName attrVal ?nodeObjVar?");
            attr_name = Tcl_GetStringFromObj(objv[2], NULL);
            attr_val  = Tcl_GetStringFromObj(objv[3], &length);
            return tcldom_setInterpAndReturnVar
                (interp, tcldom_find(node, attr_name, attr_val, length),
                 (objc == 5), (objc == 5) ? objv[4] : NULL);

        case m_child:
            CheckArgs(3,6,2,"instance|all ?type? ?attr value?");
            return tcldom_xpointerSearch(interp, XP_CHILD, node, objc, objv);

        case m_descendant:
            CheckArgs(3,6,2,"instance|all ?type? ?attr value?");
            return tcldom_xpointerSearch(interp, XP_DESCENDANT,node,objc,objv);

        case m_ancestor:
            CheckArgs(3,6,2,"instance|all ?type? ?attr value?");
            return tcldom_xpointerSearch(interp, XP_ANCESTOR, node,objc,objv);

        case m_fsibling:
            CheckArgs(3,6,2,"instance|all ?type? ?attr value?");
            return tcldom_xpointerSearch(interp, XP_FSIBLING, node,objc,objv);

        case m_psibling:
            CheckArgs(3,6,2,"instance|all ?type? ?attr value?");
            return tcldom_xpointerSearch(interp, XP_PSIBLING, node,objc,objv);

        case m_root:
            CheckArgs(2,3,2,"?nodeObjVar?");
            while (node->parentNode) {
                node = node->parentNode;
            }
            return tcldom_setInterpAndReturnVar(interp, node, (objc == 3),
                                        (objc == 3) ? objv[2] : NULL);

        case m_text:
            CheckArgs(2,2,2,"");
            if (node->nodeType != ELEMENT_NODE) {
                SetResult("NOT_AN_ELEMENT");
                return TCL_ERROR;
            }
            Tcl_ResetResult(interp);
            child = node->firstChild;
            while (child) {
                if ((child->nodeType == TEXT_NODE) ||
                    (child->nodeType == CDATA_SECTION_NODE)) {
                    Tcl_AppendToObj(Tcl_GetObjResult(interp),
                                     ((domTextNode*)child)->nodeValue,
                                     ((domTextNode*)child)->valueLength);
                }
                child = child->nextSibling;
            }
            return TCL_OK;

        case m_attributes:
            CheckArgs(2,3,2,"?nameFilter?");
            if (node->nodeType != ELEMENT_NODE) {
                SetResult("");
                return TCL_OK;
            }
            filter = NULL;
            if (objc == 3) {
                filter = Tcl_GetString(objv[2]);
            }
            Tcl_ResetResult(interp);
            resultPtr = Tcl_GetObjResult(interp);

            attrs = node->firstAttr;
            while (attrs != NULL) {
                if (!filter || Tcl_StringMatch((char*)attrs->nodeName, filter)) {
                    if (attrs->namespace == 0) {
                        namePtr = Tcl_NewStringObj((char*)attrs->nodeName, -1);
                    } else {
                        domSplitQName((char*)attrs->nodeName, prefix, 
                                      &localName);
                        mobjv[0] = Tcl_NewStringObj((char*)localName, -1);
                        mobjv[1] = Tcl_NewStringObj(
                            domNamespacePrefix((domNode*)attrs), -1
                            );
                        mobjv[2] = Tcl_NewStringObj(
                            domNamespaceURI((domNode*)attrs), -1
                            );
                        namePtr  = Tcl_NewListObj(3, mobjv);
                    }
                    result = Tcl_ListObjAppendElement(interp, resultPtr, 
                                                      namePtr);
                    if (result != TCL_OK) {
                        Tcl_DecrRefCount(namePtr);
                        return result;
                    }
                }
                attrs = attrs->nextSibling;
            }
            break;

        case m_attributeNames:
            CheckArgs(2,3,2,"?nameFilter?");
            if (node->nodeType != ELEMENT_NODE) {
                SetResult("");
                return TCL_OK;
            }
            filter = NULL;
            if (objc == 3) {
                filter = Tcl_GetString(objv[2]);
            }
            resultPtr = Tcl_GetObjResult(interp);

            attrs = node->firstAttr;
            while (attrs != NULL) {
                if (!filter || Tcl_StringMatch((char*)attrs->nodeName, filter)) {
                    namePtr = Tcl_NewStringObj((char*)attrs->nodeName, -1);
                    result = Tcl_ListObjAppendElement(interp, resultPtr, 
                                                      namePtr);
                    if (result != TCL_OK) {
                        Tcl_DecrRefCount(namePtr);
                        return result;
                    }
                }
                attrs = attrs->nextSibling;
            }
            break;

        case m_asList:
            CheckArgs(2,2,2,"");
            Tcl_SetObjResult(interp, tcldom_treeAsTclList(interp, node));
            break;

        case m_asXML:
            Tcl_ResetResult(interp);
            if (serializeAsXML(node, interp, objc, objv) != TCL_OK) {
                return TCL_ERROR;
            }
            break;

        case m_asCanonicalXML:
            Tcl_ResetResult(interp);
            if (serializeAsCanonicalXML(node, interp, objc, objv) != TCL_OK) {
                return TCL_ERROR;
            }
            break;
        case m_asHTML:
            Tcl_ResetResult(interp);
            if (serializeAsHTML(node, interp, objc, objv) != TCL_OK) {
                return TCL_ERROR;
            }
            break;

        case m_asJSON:
            if (serializeAsJSON(node, interp, objc, objv) != TCL_OK) {
                return TCL_ERROR;
            }
            break;
            
        case m_getAttribute:
            CheckArgs(3,4,2,"attrName ?defaultValue?");
            if (node->nodeType != ELEMENT_NODE) {
                SetResult("NOT_AN_ELEMENT : there are no attributes");
                return TCL_ERROR;
            }
            attr_name = Tcl_GetString(objv[2]);
            attrs = node->firstAttr;
            while(attrs && strcmp(attrs->nodeName, attr_name)) {
                attrs = attrs->nextSibling;
            }
            if (attrs) {
                SetResult(attrs->nodeValue);
                return TCL_OK;
            }
            if (objc == 4) {
                SetResult(Tcl_GetString(objv[3]));
                return TCL_OK;
            } else {
                Tcl_ResetResult(interp);
                Tcl_AppendResult(interp, "Attribute \"", attr_name,
                                 "\" not found!", NULL);
                return TCL_ERROR;
            }
            break;

        case m_getAttributeNS:
            CheckArgs(4,5,2,"uri localName ?defaultValue?");
            if (node->nodeType != ELEMENT_NODE) {
                SetResult("NOT_AN_ELEMENT : there are no attributes");
                return TCL_ERROR;
            }
            uri = Tcl_GetString(objv[2]);
            localName = Tcl_GetString(objv[3]);
            attrs = domGetAttributeNodeNS(node, uri, localName);
            if (attrs) {
                SetResult(attrs->nodeValue);
                return TCL_OK;
            } 
            if (objc == 5) {
                SetResult(Tcl_GetString(objv[4]));
                return TCL_OK;
            }
            Tcl_ResetResult (interp);
            Tcl_AppendResult (interp, "Attribute \"", localName, "\" in "
                              "namespace \"", uri, "\" not found!", NULL);
            return TCL_ERROR;

        case m_setAttribute:
            if (node->nodeType != ELEMENT_NODE) {
                SetResult("NOT_AN_ELEMENT : there are no attributes");
                return TCL_ERROR;
            }
            if ((objc < 4) || ((objc % 2)!=0)) {
                SetResult("attrName value pairs expected");
                return TCL_ERROR;
            }
            for ( i = 2;  i < objc; ) {
                attr_name = Tcl_GetString(objv[i++]);
                CheckName (interp, attr_name, "attribute", 0);
                attr_val  = Tcl_GetString(objv[i++]);
                CheckText (interp, attr_val, "attribute");
                domSetAttribute(node, attr_name, attr_val);
            }
            return tcldom_setInterpAndReturnVar(interp, node, 0, NULL);

        case m_setAttributeNS:
            if (node->nodeType != ELEMENT_NODE) {
                SetResult("NOT_AN_ELEMENT : there are no attributes");
                return TCL_ERROR;
            }
            if ((objc < 5) || (((objc - 2) % 3) != 0)) {
                SetResult("uri attrName value triples expected");
                return TCL_ERROR;
            }
            for (i = 2; i < objc;) {
                uri       = Tcl_GetString(objv[i++]);
                attr_name = Tcl_GetString(objv[i++]);
                CheckName (interp, attr_name, "full qualified attribute", 1);
                attr_val  = Tcl_GetString(objv[i++]);
                CheckText (interp, attr_val, "attribute");
                attrs = domSetAttributeNS(node, attr_name, attr_val, uri, 0);
                if (!attrs) {
                    if (uri[0]) {
                        SetResult("An attribute in a namespace "
                                  "must have a prefix");
                    } else {
                        SetResult("For all prefixed attributes with prefixes "
                                  "other than 'xml' or 'xmlns' "
                                  "you have to provide a namespace URI");
                    }
                    return TCL_ERROR;
                }
            }
            return tcldom_setInterpAndReturnVar(interp, node, 0, NULL);

        case m_hasAttribute:
            CheckArgs(3,3,2,"attrName");
            if (node->nodeType != ELEMENT_NODE) {		
                SetResult("NOT_AN_ELEMENT : there are no attributes");
                return TCL_ERROR;
            }
            attr_name = Tcl_GetString(objv[2]);
            attrs = node->firstAttr;
            while (attrs && strcmp(attrs->nodeName, attr_name)) {
                attrs = attrs->nextSibling;
            }
            if (attrs) {
                SetResult("1");
                return TCL_OK;
            }
            SetResult("0");
            return TCL_OK;

        case m_hasAttributeNS:
            CheckArgs(4,4,2,"uri localName");
            if (node->nodeType != ELEMENT_NODE) {		
                SetResult("NOT_AN_ELEMENT : there are no attributes");
                return TCL_ERROR;
            }
            uri = Tcl_GetString(objv[2]);
            localName = Tcl_GetString(objv[3]);
            attrs = node->firstAttr;
            while (attrs) {
                domSplitQName(attrs->nodeName, prefix, &nsStr);
                if (!strcmp(localName,nsStr)) {
                    ns = domGetNamespaceByIndex(node->ownerDocument, 
                                                attrs->namespace);
                    if (ns && !strcmp(ns->uri, uri)) {
                        SetResult("1");
                        return TCL_OK;
                    }
                }
                attrs = attrs->nextSibling;
            }
            SetResult("0");
            return TCL_OK;

        case m_removeAttribute:
            CheckArgs(3,3,2,"attrName");
            if (node->nodeType != ELEMENT_NODE) {		
                SetResult("NOT_AN_ELEMENT : there are no attributes");
                return TCL_ERROR;
            }
            attr_name = Tcl_GetString(objv[2]);
            result = domRemoveAttribute(node, attr_name);
            if (result) {
                SetResult("can't remove attribute '");
                AppendResult(attr_name);
                AppendResult("'");
                return TCL_ERROR;
            }
            return tcldom_setInterpAndReturnVar(interp, node, 0, NULL);

        case m_removeAttributeNS:
            CheckArgs(4,4,2,"uri attrName");
            if (node->nodeType != ELEMENT_NODE) {		
                SetResult("NOT_AN_ELEMENT : there are no attributes");
                return TCL_ERROR;
            }
            uri = Tcl_GetString(objv[2]);
            localName = Tcl_GetString(objv[3]);
            result = domRemoveAttributeNS(node, uri, localName);
            if (result < 0) {
                SetResult("can't remove attribute with localName '");
                AppendResult(localName);
                AppendResult("'");
                return TCL_ERROR;
            }
            return tcldom_setInterpAndReturnVar(interp, node, 0, NULL);

        case m_nextSibling:
            CheckArgs(2,3,2,"?nodeObjVar?");
            return tcldom_setInterpAndReturnVar(interp, node->nextSibling,
                                        (objc == 3), 
                                        (objc == 3) ? objv[2] : NULL);
        case m_previousSibling:
            CheckArgs(2,3,2,"?nodeObjVar?");
            return tcldom_setInterpAndReturnVar(interp, node->previousSibling,
                                        (objc == 3),
                                        (objc == 3) ? objv[2] : NULL);
        case m_firstChild:
            CheckArgs(2,3,2,"?nodeObjVar?");
            if (node->nodeType == ELEMENT_NODE) {
                return tcldom_setInterpAndReturnVar(interp, node->firstChild,
                                            (objc == 3),
                                            (objc == 3) ? objv[2] : NULL);
            }
            return tcldom_setInterpAndReturnVar(interp, NULL, (objc == 3),
                                        (objc == 3) ? objv[2] : NULL);
        case m_lastChild:
            CheckArgs(2,3,2,"?nodeObjVar?");
            if (node->nodeType == ELEMENT_NODE) {
                return tcldom_setInterpAndReturnVar(interp, node->lastChild,
                                            (objc == 3),
                                            (objc == 3) ? objv[2] : NULL);
            }
            return tcldom_setInterpAndReturnVar(interp, NULL, (objc == 3),
                                        (objc == 3) ? objv[2] : NULL);
        case m_parentNode:
            CheckArgs(2,3,2,"?nodeObjVar?");
            return tcldom_setInterpAndReturnVar(interp, node->parentNode, (objc == 3),
                                        (objc == 3) ? objv[2] : NULL);
        case m_appendFromList:
            CheckArgs(3,3,2,"list");
            return tcldom_appendFromTclList(interp, node, objv[2]);

        case m_appendFromScript:
            CheckArgs(3,3,2,"script");
            result = nodecmd_appendFromScript(interp, node, objv[2]);
            if (result == TCL_BREAK) {
                SetResult("");
                return TCL_OK;
            }
            if (result != TCL_OK) {            
                return TCL_ERROR;
            }
            return tcldom_setInterpAndReturnVar(interp, node, 0, NULL);

        case m_insertBeforeFromScript:
            CheckArgs(4,4,2, "script refChild");
            if (objv[3]->typePtr == &tdomNodeType) {
                refChild = objv[3]->internalRep.otherValuePtr;
            } else {
                nodeName = Tcl_GetString (objv[3]);
                if (nodeName[0] == '\0') {
                    refChild = NULL;
                } else {
                    refChild = tcldom_getNodeFromObj (interp, objv[3]);
                    if (refChild == NULL) {
                        return TCL_ERROR;
                    }
                }
            }
            result = nodecmd_insertBeforeFromScript(interp, node, objv[2],
                                                    refChild);
            if (result == TCL_BREAK) {
                SetResult("");
                return TCL_OK;
            }
            if (result != TCL_OK) {
                return TCL_ERROR;
            }
            return tcldom_setInterpAndReturnVar (interp, node, 0, NULL);
            
        case m_appendXML:
            CheckArgs(3,3,2,"xmlString");
            return tcldom_appendXML(interp, node, objv[2]);

        case m_appendChild:
            CheckArgs(3,3,2,"nodeToAppend");
            child = tcldom_getNodeFromObj(interp, objv[2]);
            if (child == NULL) {
                return TCL_ERROR;
            }
            exception = domAppendChild (node, child);
            if (exception != OK) {
                SetResult(domException2String(exception));
                return TCL_ERROR;
            }
            return tcldom_setInterpAndReturnVar(interp, child, 0, NULL);

        case m_cloneNode:
            CheckArgs(2,3,2,"?-deep?");
            if (objc == 3) {
                if (!strcmp(Tcl_GetString(objv[2]), "-deep")) {
                    return tcldom_setInterpAndReturnVar(interp, domCloneNode(node, 1),
                                                0, NULL);
                }
                SetResult("unknown option! Options: ?-deep? ");
                return TCL_ERROR;
            }
            return tcldom_setInterpAndReturnVar(interp, domCloneNode(node, 0), 0, NULL);

        case m_removeChild:
            CheckArgs(3,3,2,"childToRemove");
            child = tcldom_getNodeFromObj(interp, objv[2]);
            if (child == NULL) {
                return TCL_ERROR;
            }
            exception = domRemoveChild (node, child);
            if (exception != OK) {
                SetResult (domException2String (exception));
                return TCL_ERROR;
            }
            return tcldom_setInterpAndReturnVar(interp, child, 0, NULL);

        case m_insertBefore:
            CheckArgs(4,4,2,"childToInsert refChild");
            child = tcldom_getNodeFromObj(interp, objv[2]);
            if (child == NULL) {
                return TCL_ERROR;
            }

            if (objv[3]->typePtr == &tdomNodeType) {
                refChild = objv[3]->internalRep.otherValuePtr;
            } else {
                nodeName = Tcl_GetString (objv[3]);
                if (nodeName[0] == '\0') {
                    refChild = NULL;
                } else {
                    refChild = tcldom_getNodeFromObj (interp, objv[3]);
                    if (refChild == NULL) {
                        return TCL_ERROR;
                    }
                }
            }
            exception = domInsertBefore(node, child, refChild);
            if (exception != OK) {
                SetResult(domException2String(exception));
                return TCL_ERROR;
            }
            return tcldom_setInterpAndReturnVar(interp, child, 0, NULL);

        case m_replaceChild:
            CheckArgs(4,4,2,"new old");
            child = tcldom_getNodeFromObj(interp, objv[2]);
            if (child == NULL) {
                return TCL_ERROR;
            }
            oldChild = tcldom_getNodeFromObj(interp, objv[3]);
            if (oldChild == NULL) {
                return TCL_ERROR;
            }
            exception = domReplaceChild(node, child, oldChild);
            if (exception != OK) {
                SetResult(domException2String(exception));
                return TCL_ERROR;
            }
            return tcldom_setInterpAndReturnVar(interp, oldChild, 0, NULL);

        case m_hasChildNodes:
            CheckArgs(2,2,2,"");
            if (node->nodeType == ELEMENT_NODE) {
                SetIntResult(node->firstChild ? 1 : 0);
            } else {
                SetIntResult(0);
            }
            break;

        case m_childNodes:
            CheckArgs(2,2,2,"");
            resultPtr = Tcl_GetObjResult(interp);
            if (node->nodeType == ELEMENT_NODE) {
                child = node->firstChild;
                while (child != NULL) {
                    namePtr = tcldom_returnNodeObj(interp, child);
                    result  = Tcl_ListObjAppendElement(interp, resultPtr,
                                                       namePtr);
                    if (result != TCL_OK) {
                        Tcl_DecrRefCount(namePtr);
                        return result;
                    }
                    child = child->nextSibling;
                }
            }
            break;

        case m_getElementsByTagName:
            CheckArgs(3,3,2,"elementName");
            if (node->nodeType != ELEMENT_NODE) {
                SetResult("Node must be an element node.");
                return TCL_ERROR;
            }
            Tcl_ResetResult(interp);
            return tcldom_getElementsByTagName(interp, Tcl_GetString(objv[2]),
                                               node->firstChild, -1, NULL);

        case m_getElementsByTagNameNS:
            CheckArgs(4,4,2,"uri localname");
            if (node->nodeType != ELEMENT_NODE) {
                SetResult("Node must be an element node.");
                return TCL_ERROR;
            }
            uri = Tcl_GetString(objv[2]);
            str = Tcl_GetString(objv[3]);
            nsIndex = -1;
            if (uri[0] == '*' && uri[1] == '\0') {
                nsIndex = -3;
            } else if (uri[0] == '\0') {
                /* all elements not in a namespace */
                nsIndex = -4;
            } else {
                for (i = 0; i <= node->ownerDocument->nsptr; i++) {
                    if (strcmp (node->ownerDocument->namespaces[i]->uri,
                                uri)==0) {
                        if (nsIndex != -1) {
                            /* OK, this is one of the 'degenerated' (though
                               legal) documents, which bind the same URI
                               to different prefixes. */
                            nsIndex = -2;
                            break;
                        }
                        nsIndex = node->ownerDocument->namespaces[i]->index;
                    }
                }
            }
            if (nsIndex == -1) {
                /* There isn't such a namespace declared in this document.
                   Since getElementsByTagNameNS doesn't raise an exception
                   short cut: return empty result */
                Tcl_ResetResult(interp);
                return TCL_OK;
            }
            return tcldom_getElementsByTagName(interp, str, node->firstChild,
                                                nsIndex, uri);
            
        case m_getElementById:
            CheckArgs(3,3,2,"id");
            if (node->ownerDocument->ids) {
                str = Tcl_GetString(objv[2]);
                h = Tcl_FindHashEntry(node->ownerDocument->ids, str);
                if (h) {
                    domNode *node = Tcl_GetHashValue(h);
                    return tcldom_setInterpAndReturnVar(interp, node, 0, NULL);
                }
            }
            SetResult("");
            return TCL_OK;

        case m_nodeName:
            CheckArgs(2,2,2,"");
            if (node->nodeType == ELEMENT_NODE) {
                SetResult((char*)node->nodeName);
            } else
            if (node->nodeType == TEXT_NODE) {
                SetResult("#text");
            } else
            if (node->nodeType == PROCESSING_INSTRUCTION_NODE) {
                domProcessingInstructionNode *dpn;
                dpn = (domProcessingInstructionNode *)node;
                Tcl_SetStringObj(Tcl_GetObjResult(interp),
                                 dpn->targetValue, dpn->targetLength);
            } else 
            if (node->nodeType == COMMENT_NODE) {
                SetResult("#comment");
            } else 
            if (node->nodeType == CDATA_SECTION_NODE) {
                SetResult("#cdata-section");
            } else {
                SetResult("");
            }
            break;

        case m_nodeValue:
            CheckArgs(2,3,2,"?newValue?");
            if (node->nodeType == ELEMENT_NODE) {
                Tcl_SetStringObj(Tcl_GetObjResult(interp), "", 0);
            } else if (node->nodeType == PROCESSING_INSTRUCTION_NODE) {
                domProcessingInstructionNode *dpn;
                dpn = (domProcessingInstructionNode *)node;
                Tcl_SetStringObj(Tcl_GetObjResult(interp),
                                 dpn->dataValue, dpn->dataLength);
            } else {
                domTextNode *dtn;
                dtn = (domTextNode*)node;
                Tcl_SetStringObj(Tcl_GetObjResult(interp),
                                 dtn->nodeValue, dtn->valueLength);
            }
            if (objc == 3) {
                str = Tcl_GetStringFromObj(objv[2], &length);
                switch (node->nodeType) {
                case TEXT_NODE: CheckText (interp, str, "text"); break;
                case COMMENT_NODE: CheckComment (interp, str); break;
                case CDATA_SECTION_NODE: CheckCDATA (interp, str); break;
                default: break; /* Do nothing */
                }
                exception = domSetNodeValue(node, str, length);
                if (exception != OK) {
                    SetResult(domException2String(exception));
                    return TCL_ERROR;
                }
            }
            break;

        case m_nodeType:
            CheckArgs(2,2,2,"");
            switch (node->nodeType) {
               case ELEMENT_NODE:
                    SetResult("ELEMENT_NODE");
                    break;
               case ATTRIBUTE_NODE:
                    SetResult("ATTRIBUTE_NODE");
                    break;
               case TEXT_NODE:
                    SetResult("TEXT_NODE");
                    break;
               case CDATA_SECTION_NODE:
                    SetResult("CDATA_SECTION_NODE");
                    break;
               case COMMENT_NODE:
                    SetResult("COMMENT_NODE");
                    break;
	       case PROCESSING_INSTRUCTION_NODE:
                    SetResult("PROCESSING_INSTRUCTION_NODE");
                    break;
               default:
                    SetResult("unknown nodeType!");
                    return TCL_ERROR;
            }
            break;

        case m_prefix:
            CheckArgs(2,2,2,"");
            nsStr = domNamespacePrefix(node);
            if (nsStr) {
                SetResult(nsStr);
            } else {
                SetResult("");
            }
            return TCL_OK;

        case m_namespaceURI:
            CheckArgs(2,2,2,"");
            nsStr = domNamespaceURI(node);
            if (nsStr) {
                SetResult(nsStr);
            } else {
                SetResult("");
            }
            return TCL_OK;

        case m_localName:
            CheckArgs(2,2,2,"");
            if (node->nodeType == ELEMENT_NODE) {
                if (node->namespace != 0) {
                    SetResult(domGetLocalName((char*)node->nodeName));
                    break;
                }
            }
            SetResult("");
            break;

        case m_ownerDocument:
            CheckArgs(2,3,2,"?docObjVar?");
            return tcldom_returnDocumentObj(interp, node->ownerDocument,
                                            (objc == 3),
                                            (objc == 3) ? objv[2] : NULL, 0, 
                                            1);
        case m_target:
            CheckArgs(2,2,2,"");
            if (node->nodeType != PROCESSING_INSTRUCTION_NODE) {
                SetResult("not a PROCESSING_INSTRUCTION_NODE!");
                return TCL_ERROR;
            } else {
                domProcessingInstructionNode *dpn;
                dpn = (domProcessingInstructionNode *)node;
                Tcl_SetStringObj(Tcl_GetObjResult(interp), 
                                 dpn->targetValue, dpn->targetLength);
            }
            break;

        case m_delete:
            CheckArgs(2,2,2,"");
            domDeleteNode(node, tcldom_deleteNode, interp);
            break;

        case m_data:
            CheckArgs(2,2,2,"");
            if (node->nodeType == PROCESSING_INSTRUCTION_NODE) {
                domProcessingInstructionNode *dpn;
                dpn = (domProcessingInstructionNode*)node;
                Tcl_SetStringObj(Tcl_GetObjResult(interp),
                                 dpn->dataValue, dpn->dataLength);
            } else
            if (   node->nodeType == TEXT_NODE 
                || node->nodeType == CDATA_SECTION_NODE
                || node->nodeType == COMMENT_NODE) {
                domTextNode *dtn;
                dtn = (domTextNode*)node;
                Tcl_SetStringObj(Tcl_GetObjResult(interp),
                                 dtn->nodeValue, dtn->valueLength);
            } else {
                SetResult("not a "
                          "TEXT_NODE / "
                          "CDATA_SECTION_NODE / "
                          "COMMENT_NODE / "
                          "PROCESSING_INSTRUCTION_NODE !");
                return TCL_ERROR;
            }
            break;

        case m_getLine:
            CheckArgs(2,2,2,"");
            if (domGetLineColumn(node, &line, &column, &byteIndex) < 0) {
                SetResult("no line/column information available!");
                return TCL_ERROR;
            }
            SetLongResult(line);
            break;

        case m_getColumn:
            CheckArgs(2,2,2,"");
            if (domGetLineColumn (node, &line, &column, &byteIndex) < 0) {
                SetResult("no line/column information available!");
                return TCL_ERROR;
            }
            SetLongResult(column);
            break;

        case m_getByteIndex:
            CheckArgs(2,2,2,"");
            if (domGetLineColumn (node, &line, &column, &byteIndex) < 0) {
                SetResult("no position information available!");
                return TCL_ERROR;
            }
            SetLongResult(byteIndex);
            break;

        case m_getBaseURI:
            CheckArgs(2,2,2,"");
            /* fall through */

        case m_baseURI:    
            CheckArgs(2,3,2,"?URI?");
            if (objc == 3) {
                h = Tcl_CreateHashEntry (node->ownerDocument->baseURIs, 
                                         (char *) node, &hnew);
                if (!hnew) {
                    FREE (Tcl_GetHashValue (h));
                }
                Tcl_SetHashValue (h, tdomstrdup (Tcl_GetString (objv[2])));
                node->nodeFlags |= HAS_BASEURI;
                SetResult (Tcl_GetString (objv[2]));
            } else {
                nsStr = findBaseURI(node);
                if (!nsStr) {
                    SetResult("");
                } else {
                    SetResult(nsStr);
                }
            }
            break;

        case m_disableOutputEscaping:
            CheckArgs(2,3,2,"?boolean?");
            if (node->nodeType != TEXT_NODE) {
                SetResult("not a TEXT_NODE!");
                return TCL_ERROR;
            }
            SetIntResult(
                (((node->nodeFlags & DISABLE_OUTPUT_ESCAPING) == 0) ? 0 : 1));
            if (objc == 3) {
                if (Tcl_GetBooleanFromObj(interp, objv[2], &bool) != TCL_OK) {
                    return TCL_ERROR;
                }
                if (bool) {
                    node->nodeFlags |= DISABLE_OUTPUT_ESCAPING;
                } else {
                    node->nodeFlags &= (~DISABLE_OUTPUT_ESCAPING);
                }
            }
            break;

        case m_precedes:
            CheckArgs(3,3,2, "node");
            refNode = tcldom_getNodeFromObj(interp, objv[2]);
            if (refNode == NULL) {
                return TCL_ERROR;
            }
            if (node->ownerDocument != refNode->ownerDocument) {
                SetResult("Cannot compare the relative order of nodes "
                          "out of different documents.");
                return TCL_ERROR;
            }
            if (((node->parentNode == NULL) 
                 && (node != node->ownerDocument->documentElement)
                 && (node != node->ownerDocument->rootNode))
                || 
                ((refNode->parentNode == NULL)
                 && (refNode != refNode->ownerDocument->documentElement)
                 && (refNode != refNode->ownerDocument->rootNode))) {
                SetResult("Cannot compare the relative order of a node "
                          "with a node out of the fragment list.");
                return TCL_ERROR;
            }
            SetBooleanResult (domPrecedes (node, refNode));
            break;

        case m_asText:
            CheckArgs (2,2,2, "");
            str = xpathGetStringValue(node, &length);
            Tcl_SetStringObj(Tcl_GetObjResult(interp), str, length);
            FREE (str);
            return TCL_OK;

        case m_normalize:
            CheckArgs (2,3,2, "?-forXPath?");
            bool = 0;
            if (objc == 3) {
                if (strcmp (Tcl_GetString(objv[2]), "-forXPath") == 0) {
                    bool = 1;
                } else {
                    SetResult("unknown option! Options: ?-forXPath?");
                    return TCL_ERROR;
                }
            }
            domNormalize (node, bool, tcldom_deleteNode, interp);
            return TCL_OK;

        case m_jsonType:
            CheckArgs (2,3,2, "?jsonType?");
            if (node->nodeType != ELEMENT_NODE
                && node->nodeType != TEXT_NODE) {
                SetResult("Only element and text nodes may have a JSON type.");
                return TCL_ERROR;
            }
            if (objc == 3) {
                if (Tcl_GetIndexFromObj (interp, objv[2], jsonTypes,
                                         "jsonType", 0, &jsonType)
                    != TCL_OK) {
                    return TCL_ERROR;
                }
                if (node->nodeType == ELEMENT_NODE) {
                    if (jsonType > 2) {
                        SetResult("For an element node the jsonType argument "
                                  "must be one out of this list: ARRAY OBJECT NONE.");
                        return TCL_ERROR;
                    }
                } else {
                    /* Text nodes */
                    if (jsonType < 3 && jsonType > 0) {
                        SetResult("For a text node the jsonType argument must be "
                                  "one out of this list: TRUE FALSE NULL NUMBER "
                                  "STRING NONE");
                        return TCL_ERROR;
                    }
                }
                node->info = jsonType;
                SetIntResult(jsonType);
                return TCL_OK;
            }
            if (node->info > 7) {
                SetResult(jsonTypes[0]);
            } else {
                SetResult(jsonTypes[node->info]);
            }
            return TCL_OK;
            
        TDomThreaded(
        case m_writelock:
            CheckArgs(3,3,2,"script");
            return tcldom_EvalLocked(interp, (Tcl_Obj**)objv, 
                                     node->ownerDocument, LOCK_WRITE);
        case m_readlock:
            CheckArgs(3,3,2,"script");
            return tcldom_EvalLocked(interp, (Tcl_Obj**)objv, 
                                     node->ownerDocument, LOCK_READ);
        )
    }
    return TCL_OK;}



/*----------------------------------------------------------------------------
|   tcldom_DocObjCmd
|
\---------------------------------------------------------------------------*/
int tcldom_DocObjCmd (
    ClientData  clientData,
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    *const objv[]
)
{
    GetTcldomDATA;

    domDeleteInfo       * dinfo;
    domDocument         * doc;
    char                * method, *tag, *data, *target, *uri, tmp[100];
    char                * str, *docName, *errMsg;
    int                   methodIndex, result, i, nsIndex, forXPath, bool;
    int                   setDocumentElement = 0, restoreDomCreateCmdMode = 0;
    domLength             data_length, target_length;
    domNode             * n;
    Tcl_CmdInfo           cmdInfo;
    Tcl_Obj             * mobjv[MAX_REWRITE_ARGS];

    static const char *docMethods[] = {
        "documentElement", "getElementsByTagName",       "delete",
        "createElement",   "createCDATASection",         "createTextNode",
        "createComment",   "createProcessingInstruction",
        "createElementNS", "getDefaultOutputMethod",     "asXML",
        "asHTML",          "getElementsByTagNameNS",     "xslt", 
        "publicId",        "systemId",                   "internalSubset",
        "toXSLTcmd",       "asText",                     "normalize",
        "indent",          "omit-xml-declaration",       "encoding",
        "standalone",      "mediaType",                  "nodeType",
        "cdataSectionElements",
        "selectNodesNamespaces",
        "renameNode",      "deleteXPathCache",           "asCanonicalXML",
        /* The following methods will be dispatched to tcldom_NodeObjCmd */
        "getElementById",  "firstChild",                 "lastChild",
        "appendChild",     "removeChild",                "hasChildNodes",
        "childNodes",      "ownerDocument",              "insertBefore",
        "replaceChild",    "appendFromList",             "appendXML",
        "selectNodes",     "baseURI",                    "appendFromScript",
        "insertBeforeFromScript",                        "asJSON",
        "jsonType",        
#ifdef TCL_THREADS
        "readlock",        "writelock",                  "renumber",
#endif
        NULL
    };
    enum docMethod {
        m_documentElement,  m_getElementsByTagName,       m_delete,
        m_createElement,    m_createCDATASection,         m_createTextNode,
        m_createComment,    m_createProcessingInstruction,
        m_createElementNS,  m_getdefaultoutputmethod,     m_asXML,
        m_asHTML,           m_getElementsByTagNameNS,     m_xslt,
        m_publicId,         m_systemId,                   m_internalSubset,
        m_toXSLTcmd,        m_asText,                     m_normalize,
        m_indent,           m_omitXMLDeclaration,         m_encoding,
        m_standalone,       m_mediaType,                  m_nodeType,
        m_cdataSectionElements,
        m_selectNodesNamespaces,
        m_renameNode,       m_deleteXPathCache,           m_asCanonicalXML,
        /* The following methods will be dispatched to tcldom_NodeObjCmd */
        m_getElementById,   m_firstChild,                 m_lastChild,
        m_appendChild,      m_removeChild,                m_hasChildNodes,
        m_childNodes,       m_ownerDocument,              m_insertBefore,
        m_replaceChild,     m_appendFromList,             m_appendXML,
        m_selectNodes,      m_baseURI,                    m_appendFromScript,
        m_insertBeforeFromScript,                         m_asJSON,
        m_jsonType
#ifdef TCL_THREADS
       ,m_readlock,         m_writelock,                  m_renumber
#endif
    };

    dinfo = (domDeleteInfo*)clientData;
    if (TcldomDATA(domCreateCmdMode) == DOM_CREATECMDMODE_AUTO) {
        TcldomDATA(dontCreateObjCommands) = 0;
    }
    if (dinfo == NULL) {
        if (objc < 3) {
            SetResult(doc_usage);
            return TCL_ERROR;
        }
        if (TcldomDATA(domCreateCmdMode) == DOM_CREATECMDMODE_AUTO) {
            TcldomDATA(dontCreateObjCommands) = 1;
        }
        docName = Tcl_GetString(objv[1]);
        doc = tcldom_getDocumentFromName(interp, docName, &errMsg);
        if (doc == NULL) {
            SetResult(errMsg);
            return TCL_ERROR;
        }
        objc--;
        objv++;
    } else {
        doc = dinfo->document;
    }

    if (objc < 2) {
        SetResult(doc_usage);
        return TCL_ERROR;
    }
    method = Tcl_GetString(objv[1]);
    if (Tcl_GetIndexFromObj(NULL, objv[1], docMethods, "method", 0,
                            &methodIndex) != TCL_OK)
    {
        /*--------------------------------------------------------
        |   try to find method implemented as normal Tcl proc
        \-------------------------------------------------------*/
        sprintf(tmp, "::dom::domDoc::%s", method);
        DBG(fprintf(stderr, "testing %s\n", tmp));
        result = Tcl_GetCommandInfo(interp, tmp, &cmdInfo);
        if (!result) {
            SetResult(doc_usage);
            return TCL_ERROR;
        }
        if (!cmdInfo.isNativeObjectProc) {
            SetResult( "can't access Tcl level method!");
            return TCL_ERROR;
        }
        if (objc >= MAX_REWRITE_ARGS) {
            SetResult("too many args to call Tcl level method!");
            return TCL_ERROR;
        }
        mobjv[0] = objv[1];
        mobjv[1] = objv[0];
        for (i = 2; i < objc; i++) {
            mobjv[i] = objv[i];
        }
        return cmdInfo.objProc(cmdInfo.objClientData, interp, objc, mobjv);
    }

    CheckArgs (2,10,1,doc_usage);
    Tcl_ResetResult (interp);

    /*----------------------------------------------------------------------
    |   dispatch the doc object method
    |
    \---------------------------------------------------------------------*/

    switch ((enum docMethod) methodIndex ) {

        case m_documentElement:
            CheckArgs(2,3,2,"");
            return tcldom_setInterpAndReturnVar(interp, doc->documentElement,
                                        (objc == 3), 
                                        (objc == 3) ? objv[2] : NULL);
        case m_getElementsByTagName:
            CheckArgs(3,3,2,"elementName");
            return tcldom_getElementsByTagName(interp, Tcl_GetString(objv[2]),
                                               doc->documentElement, -1, NULL);
        case m_getElementsByTagNameNS:
            CheckArgs(4,4,2,"uri localname");
            uri = Tcl_GetString(objv[2]);
            str = Tcl_GetString(objv[3]);
            nsIndex = -1;
            if (uri[0] == '*' && uri[1] == '\0') {
                nsIndex = -3;
            } else if (uri[0] == '\0') {
                /* all elements not in a namespace i.e. */
                nsIndex = -4;
            } else {
                for (i = 0; i <= doc->nsptr; i++) {
                    if (strcmp(doc->namespaces[i]->uri, uri)==0) {
                        if (nsIndex != -1) {
                            /* OK, this is one of the 'degenerated' (though
                               legal) documents, which bind the same URI
                               to different prefixes. */
                            nsIndex = -2;
                            break;
                        }
                        nsIndex = doc->namespaces[i]->index;
                    }
                }
            }
            if (nsIndex == -1) {
                /* There isn't such a namespace declared in this document.
                   Since getElementsByTagNameNS doesn't raise an exception
                   short cut: return empty result */
                return TCL_OK;
            }
            return tcldom_getElementsByTagName(interp, str,
                                               doc->documentElement, nsIndex,
                                               uri);
        case m_createElement:
            CheckArgs(3,4,2,"elementName ?newObjVar?");
            tag = Tcl_GetString(objv[2]);
            CheckName (interp, tag, "tag", 0);
            n = domNewElementNode(doc, tag);
            return tcldom_setInterpAndReturnVar(interp, n, (objc == 4),
                                        (objc == 4) ? objv[3] : NULL);

        case m_createElementNS:
            CheckArgs(4,5,2,"elementName uri ?newObjVar?");
            uri = Tcl_GetString(objv[2]);
            tag = Tcl_GetString(objv[3]);
            CheckName (interp, tag, "full qualified tag", 1);
            n = domNewElementNodeNS(doc, tag, uri);
            if (n == NULL) {
                SetResult("Missing URI in Namespace declaration");
                return TCL_ERROR;
            }
            return tcldom_setInterpAndReturnVar(interp, n, (objc == 5),
                                        (objc == 5) ? objv[4] : NULL);

        case m_createTextNode:
            CheckArgs(3,4,2,"data ?newObjVar?");
            data = Tcl_GetStringFromObj(objv[2], &data_length);
            CheckText (interp, data, "text");
            n = (domNode*)domNewTextNode(doc, data, data_length, TEXT_NODE);
            return tcldom_setInterpAndReturnVar(interp, n, (objc == 4),
                                        (objc == 4) ? objv[3] : NULL);

        case m_createCDATASection:
            CheckArgs(3,4,2,"data ?newObjVar?");
            data = Tcl_GetStringFromObj(objv[2], &data_length);
            CheckCDATA (interp, data);
            n = (domNode*)domNewTextNode(doc, data, data_length, 
                                         CDATA_SECTION_NODE);
            return tcldom_setInterpAndReturnVar(interp, n, (objc == 4),
                                        (objc == 4) ? objv[3] : NULL);

        case m_createComment:
            CheckArgs(3,4,2,"data ?newObjVar?");
            data = Tcl_GetStringFromObj(objv[2], &data_length);
            CheckComment(interp, data);
            n = (domNode*)domNewTextNode(doc, data, data_length, COMMENT_NODE);
            return tcldom_setInterpAndReturnVar(interp, n, (objc == 4),
                                        (objc == 4) ? objv[3] : NULL);

        case m_createProcessingInstruction:
            CheckArgs(4,5,2,"target data ?newObjVar?");
            target = Tcl_GetStringFromObj(objv[2], &target_length);
            CheckPIName (interp, target);
            data   = Tcl_GetStringFromObj(objv[3], &data_length);
            CheckPIValue (interp, data);
            n = (domNode*)domNewProcessingInstructionNode(doc, target, 
                                                          target_length, data, 
                                                          data_length);
            return tcldom_setInterpAndReturnVar(interp, n, (objc == 5),
                                        (objc == 5) ? objv[4] : NULL);

        case m_delete:
            CheckArgs(2,2,2,"");
            if (clientData != NULL || doc->nodeFlags & DOCUMENT_CMD) {
                Tcl_DeleteCommand(interp, Tcl_GetString (objv[0]));
            } else {
                tcldom_deleteDoc(interp, doc);
            }
            SetResult("");
            return TCL_OK;

        case m_getdefaultoutputmethod:
            CheckArgs(2,2,2,"");
            if (doc->doctype && doc->doctype->method) {
                SetResult (doc->doctype->method);
            } else {
                SetResult("xml");
            }
            return TCL_OK;

        case m_asXML:
            if (serializeAsXML((domNode*)doc, interp, objc, objv) != TCL_OK) {
                return TCL_ERROR;
            }
            return TCL_OK;

        case m_asCanonicalXML:
            if (serializeAsCanonicalXML((domNode*)doc, interp, objc, objv)
                != TCL_OK) {
                return TCL_ERROR;
            }
            return TCL_OK;

        case m_asHTML:
            if (serializeAsHTML((domNode*)doc, interp, objc, objv)
                != TCL_OK) {
                return TCL_ERROR;
            }
            return TCL_OK;

        case m_xslt:
            CheckArgs(3,9,2, "?-parameters parameterList? "
                      "?-ignoreUndeclaredParameters? "
                      "?-xsltmessagecmd cmd? <xsltDocNode> ?objVar?");
            objv += 2; objc -= 2;
            return applyXSLT((domNode *) doc, interp, NULL, objc, objv);


        case m_toXSLTcmd:
            CheckArgs(2,3,2, "?objVar?");
            return convertToXSLTCmd(doc, interp, (objc == 3),
                                    (objc == 3) ? objv[2] : NULL);
            
        case m_publicId:
            CheckArgs(2,3,2, "?publicID?");
            if (doc->doctype && doc->doctype->publicId) {
                SetResult(doc->doctype->publicId);
            } else {
                SetResult("");
            }
            if (objc == 3) {
                if (!doc->doctype) {
                    doc->doctype = (domDocInfo *)MALLOC(sizeof(domDocInfo));
                    memset(doc->doctype, 0,(sizeof(domDocInfo)));
                } else if (doc->doctype->publicId) {
                    FREE(doc->doctype->publicId);
                }
                doc->doctype->publicId = tdomstrdup(Tcl_GetString(objv[2]));
            }
            return TCL_OK;
            
        case m_systemId:
            CheckArgs(2,3,2, "?systemID?");
            if (doc->doctype && doc->doctype->systemId) {
                SetResult(doc->doctype->systemId);
            } else {
                SetResult("");
            }
            if (objc == 3) {
                if (!doc->doctype) {
                    doc->doctype = (domDocInfo *)MALLOC(sizeof(domDocInfo));
                    memset(doc->doctype, 0,(sizeof(domDocInfo)));
                } else if (doc->doctype->systemId) {
                    FREE(doc->doctype->systemId);
                }
                doc->doctype->systemId = 
                    tdomstrdup(Tcl_GetString(objv[2]));
            }
            return TCL_OK;

        case m_internalSubset:
            CheckArgs(2,3,2, "?internalSubset?");
            if (doc->doctype && doc->doctype->internalSubset) {
                SetResult(doc->doctype->internalSubset);
            } else {
                SetResult("");
            }
            if (objc == 3) {
                if (!doc->doctype) {
                    doc->doctype = (domDocInfo *)MALLOC(sizeof(domDocInfo));
                    memset(doc->doctype, 0,(sizeof(domDocInfo)));
                } else if (doc->doctype->systemId) {
                    FREE(doc->doctype->systemId);
                }
                doc->doctype->internalSubset = 
                    tdomstrdup(Tcl_GetString(objv[2]));
            }
            return TCL_OK;
            
        case m_indent:
            CheckArgs(2,3,2, "?boolean?");
            if (doc->nodeFlags & OUTPUT_DEFAULT_INDENT) {
                SetBooleanResult (1);
            } else {
                SetBooleanResult(0);
            }
            if (objc == 3) {
                if (Tcl_GetBooleanFromObj (interp, objv[2], &bool) != TCL_OK) {
                    return TCL_ERROR;
                }
                if (bool) {
                    doc->nodeFlags |= OUTPUT_DEFAULT_INDENT;
                } else {
                    doc->nodeFlags &= ~OUTPUT_DEFAULT_INDENT;
                }
            }
            return TCL_OK;
            
        case m_omitXMLDeclaration:
            CheckArgs(2,3,2, "?boolean?");
            if (doc->doctype) {
                SetBooleanResult (doc->doctype->omitXMLDeclaration);
            } else {
                SetBooleanResult (1);
            }
            if (objc == 3) {
                if (!doc->doctype) {
                    doc->doctype = (domDocInfo *)MALLOC(sizeof(domDocInfo));
                    memset(doc->doctype, 0,(sizeof(domDocInfo)));
                }
                if (Tcl_GetBooleanFromObj (
                        interp, objv[2], &(doc->doctype->omitXMLDeclaration)
                        ) != TCL_OK) {
                    return TCL_ERROR;
                }
            }
            return TCL_OK;

        case m_encoding:
            CheckArgs(2,3,2, "?value?");
            if (doc->doctype && doc->doctype->encoding) {
                SetResult (doc->doctype->encoding);
            } else {
                SetResult ("");
            }
            if (objc == 3) {
                if (!doc->doctype) {
                    doc->doctype = (domDocInfo *)MALLOC(sizeof(domDocInfo));
                    memset(doc->doctype, 0,(sizeof(domDocInfo)));
                } else {
                    if (doc->doctype->encoding) FREE (doc->doctype->encoding);
                }
                doc->doctype->encoding = tdomstrdup (Tcl_GetString (objv[2]));
            }
            return TCL_OK;
                    
        case m_standalone:
            CheckArgs(2,3,2, "?boolean?");
            if (doc->doctype) {
                SetBooleanResult (doc->doctype->standalone);
            } else {
                SetBooleanResult (0);
            }
            if (objc == 3) {
                if (!doc->doctype) {
                    doc->doctype = (domDocInfo *)MALLOC(sizeof(domDocInfo));
                    memset(doc->doctype, 0,(sizeof(domDocInfo)));
                }
                if (Tcl_GetBooleanFromObj (
                        interp, objv[2], &(doc->doctype->standalone)
                        ) != TCL_OK) {
                    return TCL_ERROR;
                }
            }
            return TCL_OK;

        case m_mediaType:
            CheckArgs(2,3,2, "?value?");
            if (doc->doctype && doc->doctype->mediaType) {
                SetResult (doc->doctype->mediaType);
            } else {
                SetResult ("");
            }
            if (objc == 3) {
                if (!doc->doctype) {
                    doc->doctype = (domDocInfo *)MALLOC(sizeof(domDocInfo));
                    memset(doc->doctype, 0,(sizeof(domDocInfo)));
                } else {
                    if (doc->doctype->mediaType) FREE(doc->doctype->mediaType);
                }
                doc->doctype->mediaType = tdomstrdup (Tcl_GetString (objv[2]));
            }
            return TCL_OK;
                    
        case m_asText:
            CheckArgs (2,2,2,"");
            data = xpathGetStringValue (doc->rootNode, &data_length);
            Tcl_SetStringObj (Tcl_GetObjResult (interp), data, data_length);
            FREE (data);
            return TCL_OK;

        case m_normalize:
            CheckArgs (2,3,2, "?-forXPath?");
            forXPath = 0;
            if (objc == 3) {
                if (strcmp (Tcl_GetString (objv[2]), "-forXPath") == 0) {
                    forXPath = 1;
                } else {
                    SetResult("unknown option! Options: ?-forXPath?");
                    return TCL_ERROR;
                }
            }
            domNormalize(doc->rootNode, forXPath, tcldom_deleteNode, interp);
            return TCL_OK;

        case m_nodeType:
            CheckArgs (2,2,2, "");
            SetResult("DOCUMENT_NODE");
            return TCL_OK;

        case m_cdataSectionElements:
            return cdataSectionElements (doc, interp, objc, objv);

        case m_selectNodesNamespaces:
            CheckArgs (2,3,2, "?prefixUriList?");
            return tcldom_prefixNSlist (&(doc->prefixNSMappings), interp,
                                        --objc, ++objv,
                                        "selectNodesNamespaces");
            
        case m_renameNode:
            return renameNodes (doc, interp, objc, objv);
            
        case m_deleteXPathCache:
            return deleteXPathCache (doc, interp, objc, objv);

        case m_appendChild:
        case m_removeChild:
        case m_insertBefore:
        case m_replaceChild:
        case m_appendFromList:
        case m_appendXML:
        case m_appendFromScript:
        case m_insertBeforeFromScript:
            setDocumentElement = 1;
            /* fall through */
        case m_firstChild:
        case m_lastChild:
        case m_hasChildNodes:
        case m_childNodes:
        case m_ownerDocument:
        case m_selectNodes:
        case m_baseURI:
        case m_asJSON:
        case m_jsonType:
        case m_getElementById:
            /* We dispatch the method call to tcldom_NodeObjCmd */
            if (TcldomDATA(domCreateCmdMode) == DOM_CREATECMDMODE_AUTO) {
                if (dinfo == NULL) {
                    /* tcldom_DocObjCmd was called with a doc token.
                       Since the domCreateCmdMode is 'automatic'
                       and we call tcldom_DocObjCmd with the root node
                       as 'clientData', we temporarily set domCreateCmdMode
                       to 'token', to get token results from that call
                       and later to set it back. */
                    TcldomDATA(domCreateCmdMode) = DOM_CREATECMDMODE_TOKENS;
                    restoreDomCreateCmdMode = 1;
                }
            }
            if (tcldom_NodeObjCmd (doc->rootNode, interp, objc, objv) !=
                TCL_OK) {
                if (restoreDomCreateCmdMode) {
                    TcldomDATA(domCreateCmdMode) = DOM_CREATECMDMODE_AUTO;
                    TcldomDATA(dontCreateObjCommands) = 0;
                }
                return TCL_ERROR;
            }
            if (setDocumentElement) {
                /* The method call may have altered the documentElement. */
                /* There may be even no node anymore */
                domSetDocumentElement (doc);
            }
            if (restoreDomCreateCmdMode) {
                TcldomDATA(domCreateCmdMode) = DOM_CREATECMDMODE_AUTO;
                TcldomDATA(dontCreateObjCommands) = 0;
            }
            return TCL_OK;
            
        TDomThreaded(
        case m_writelock:
            CheckArgs(3,3,2,"script");
            return tcldom_EvalLocked(interp, (Tcl_Obj**)objv, doc, LOCK_WRITE);

        case m_readlock:
            CheckArgs(3,3,2,"script");
            return tcldom_EvalLocked(interp, (Tcl_Obj**)objv, doc, LOCK_READ);

        case m_renumber:
            CheckArgs(2,2,2,"");
            if (doc->nodeFlags & NEEDS_RENUMBERING) {
                domRenumberTree (doc->rootNode);
                doc->nodeFlags &= ~NEEDS_RENUMBERING;
            }
            return TCL_OK;
        )
    }

    SetResult(doc_usage);
    return TCL_ERROR;
}

/*----------------------------------------------------------------------------
|   tDOM_fsnewNodeCmd
|
\---------------------------------------------------------------------------*/
int
tDOM_fsnewNodeCmd (
    ClientData      UNUSED(clientData),
    Tcl_Interp    * interp,
    int             objc,
    Tcl_Obj *const  objv[]
) {
    domNode *parent, *newNode = NULL;
    int index, jsonType, haveJsonType = 0, type, ret;
    int checkName, checkCharData;
    domLength len;
    Tcl_Obj *cmdObj;
    char *namespace = NULL, *option, *tag;

    GetTcldomDATA;

    static const char *options[] = {
        "-jsonType", "-namespace", "--", NULL
    };

    enum option {
        o_jsonType, o_namespace, o_Last
    };

    static const char *jsonTypes[] = {
        "NONE",
        "ARRAY",
        "OBJECT",
        "NULL",
        "TRUE",
        "FALSE",
        "STRING",
        "NUMBER"
    };

    Tcl_ResetResult (interp);

    /*------------------------------------------------------------------------
    |   Need parent node to get the owner document and to append new 
    |   child tag to it. The current parent node is stored on the stack.
    |
    \-----------------------------------------------------------------------*/

    parent = nodecmd_currentNode(interp);
    if (parent == NULL) {
        Tcl_AppendResult(interp, "called outside domNode context", NULL);
        return TCL_ERROR;
    }

    if (objc < 2) {
        Tcl_AppendResult(interp, "::tdom::fsnewNode \n"
                         "\t?-jsonType <jsonType>?\n"
                         "\t?-namespace <namespace>?\n"
                         " tagName ?attributes? ?script?", NULL);
        return TCL_ERROR;
    }
    while (objc > 2) {
        option = Tcl_GetString (objv[1]);
        if (option[0] != '-') {
            break;
        }
        if (Tcl_GetIndexFromObj (interp, objv[1], options, "option",
                                 0, &index) != TCL_OK) {
            return TCL_ERROR;
        }
        switch ((enum option) index) {
        case o_jsonType:
            if (Tcl_GetIndexFromObj (interp, objv[2], jsonTypes, "jsonType",
                                     1, &jsonType) != TCL_OK) {
                return TCL_ERROR;
            }
            haveJsonType = 1;
            objc -= 2;
            objv += 2;
            break;
            
        case o_namespace:
            namespace = Tcl_GetString (objv[2]);
            objc -= 2;
            objv += 2;
            break;
            
        case o_Last:
            objv++;  objc--; break;

        }
    }
    if (objc < 2) {
        Tcl_AppendResult(interp, "::tdom::fsnewNode \n"
                         "\t?-jsonType <jsonType>?\n"
                         "\t?-namespace <namespace>?\n"
                         " tagName ?attributes? ?script?", NULL);
        return TCL_ERROR;
    }
    tag = Tcl_GetStringFromObj(objv[1], &len);
    objv++;  objc--;

    newNode = domAppendNewElementNode (parent, tag, namespace);
    if (haveJsonType) {
        newNode->info = jsonType;
    }

    cmdObj = NULL;
    checkName = !TcldomDATA(dontCheckName);
    checkCharData = !TcldomDATA(dontCheckCharData);
    
    if (objc > 1) {
        if (haveJsonType) {
            type = ELEMENT_NODE;
        } else {
            if (checkName && checkCharData) {
                type = ELEMENT_NODE_CHK;
            } else if (checkName) {
                type = ELEMENT_NODE_ANAME_CHK;
            } else if (checkCharData) {
                type = ELEMENT_NODE_AVALUE_CHK;
            } else {
                type = ELEMENT_NODE;
            }
        }
        if (nodecmd_processAttributes (interp, newNode, type, objc, objv,
                                         &cmdObj) != TCL_OK) {
            return TCL_ERROR;
        }
        if (cmdObj) {
            ret = nodecmd_appendFromScript(interp, newNode, cmdObj);
            if (ret == TCL_OK) {
                parent->ownerDocument->nodeFlags |= NEEDS_RENUMBERING;
            }
            return ret;
        }
    }
    return TCL_OK;
}

/*----------------------------------------------------------------------------
|   tDOM_fsinsertNodeCmd
|
\---------------------------------------------------------------------------*/
int
tDOM_fsinsertNodeCmd (
    ClientData      UNUSED(clientData),
    Tcl_Interp    * interp,
    int             objc,
    Tcl_Obj *const  objv[]
) {
    domNode *parent, *newNode = NULL;
    domException exception;

    Tcl_ResetResult (interp);

    if (objc != 2) {
        Tcl_AppendResult (interp, "::tdom::fsinsertNode <node>", NULL);
        return TCL_ERROR;
    }

    /*------------------------------------------------------------------------
    |   Need parent node to get the owner document and to append new 
    |   child tag to it. The current parent node is stored on the stack.
    |
    \-----------------------------------------------------------------------*/

    parent = nodecmd_currentNode(interp);
    if (parent == NULL) {
        Tcl_AppendResult(interp, "called outside domNode context", NULL);
        return TCL_ERROR;
    }

    newNode = tcldom_getNodeFromObj (interp, objv[1]);
    if (!newNode) {
        return TCL_ERROR;
    }
    exception = domAppendChild (parent, newNode);
    if (exception != OK) {
        Tcl_AppendResult (interp, domException2String(exception), NULL);
        return TCL_ERROR;
    }
    tcldom_setInterpAndReturnVar (interp, newNode, 0, NULL);
    return TCL_OK;
}

/*----------------------------------------------------------------------------
|   tcldom_createDocument
|
\---------------------------------------------------------------------------*/
static
int tcldom_createDocument (
    ClientData  UNUSED(clientData),
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    * const objv[]
)
{
    int          setVariable = 0;
    domDocument *doc;
    Tcl_Obj     *newObjName = NULL;

    GetTcldomDATA;

    CheckArgs(2,3,1,"docElemName ?newObjVar?");

    if (objc == 3) {
        newObjName = objv[2];
        setVariable = 1;
    }

    CheckName(interp, Tcl_GetString(objv[1]), "root element", 0);
    doc = domCreateDocument(NULL, Tcl_GetString(objv[1]));
    return tcldom_returnDocumentObj(interp, doc, setVariable, newObjName, 1,
                                    0);
}

/*----------------------------------------------------------------------------
|   tcldom_createDocumentNode
|
\---------------------------------------------------------------------------*/
static
int tcldom_createDocumentNode (
    ClientData  UNUSED(clientData),
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    * const objv[]
)
{
    int          setVariable = 0, jsonType = 0, index;
    domDocument *doc;
    Tcl_Obj     *newObjName = NULL;

    static const char *options[] = {"-jsonType", NULL};
    
    CheckArgs(1,4,1,"?-jsonType jsonType? ?newObjVar?");

    if (objc == 2) {
        newObjName = objv[1];
        setVariable = 1;
    }
    if (objc > 2) {
        if (Tcl_GetIndexFromObj(interp, objv[1], options, "option",
                                0, &index) != TCL_OK) {
            return TCL_ERROR;
        }
        Tcl_ResetResult(interp);
            if (Tcl_GetIndexFromObj(interp, objv[2], jsonTypes, "jsonType",
                                0, &jsonType) != TCL_OK) {
            return TCL_ERROR;
        }
        if (objc == 4) {
            newObjName = objv[3];
            setVariable = 1;
        }
    }

    doc = domCreateDoc(NULL, 0);
    doc->rootNode->info = jsonType;
    
    return tcldom_returnDocumentObj(interp, doc, setVariable, newObjName, 1,
                                    0);
}

/*----------------------------------------------------------------------------
|   tcldom_createDocumentNS
|
\---------------------------------------------------------------------------*/
static
int tcldom_createDocumentNS (
    ClientData  UNUSED(clientData),
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    * const objv[]
)
{
    int          setVariable = 0;
    domLength    len;
    char        *uri;
    domDocument *doc;
    Tcl_Obj     *newObjName = NULL;

    GetTcldomDATA;

    CheckArgs(3,4,1,"uri docElemName ?newObjVar?");

    if (objc == 4) {
        newObjName = objv[3];
        setVariable = 1;
    }

    CheckName(interp, Tcl_GetString(objv[2]), "root element", 1);
    uri = Tcl_GetStringFromObj (objv[1], &len);
    if (len == 0) {
        if (!TcldomDATA(dontCheckName)) {
            if (!domIsNCNAME (Tcl_GetString(objv[2]))) {
                SetResult ("Missing URI in Namespace declaration");
                return TCL_ERROR;
            }
        }
        doc = domCreateDocument (NULL, Tcl_GetString(objv[2]));
    } else {
        doc = domCreateDocument (uri, Tcl_GetString(objv[2]));
    }
    return tcldom_returnDocumentObj (interp, doc, setVariable, newObjName, 1,
                                    0);
}

/* Helper function to build up the error string message in a central
 * place. Caller must provide byteIndex; line is expected to be > 0 if
 * line/column information is given. */
void tcldom_reportErrorLocation (
    Tcl_Interp *interp,
    int before,
    int after,
    domLength line,
    domLength column,
    char *xmlstring,
    const char *entity,
    domLength byteIndex,
    const char *errStr
    )
{
    char s[200], sb[25], sl[25], sc[25];
    char *d = NULL, *buf;
    domLength i, ind;

    if (before > 197 || after > 197) {
        d = MALLOC (sizeof (char) * ((before > after ? before + 3 : after + 1)));
        buf = d;
    } else {
        buf = s;
    }
    Tcl_ResetResult(interp);
    Tcl_AppendResult (interp, "error \"", errStr, "\"", NULL);
    if (entity) {
        Tcl_AppendResult (interp, " in entity \"", entity, "\"", NULL);
    }
    if (line) {
        sprintf(sl, "%" TCL_SIZE_MODIFIER "d", line);
        sprintf(sc, domLengthConversion, column);
        Tcl_AppendResult (interp, " at line ", sl, " character ", sc, NULL);
        
    } else {
        sprintf(sb, domLengthConversion, byteIndex);
        Tcl_AppendResult (interp, " at position ", sb, NULL);
    }
    if (xmlstring) {
        Tcl_AppendResult (interp, "\n\"", NULL);
        ind = 0;
        buf[0] = '\0';
        for (i = (byteIndex < before ? 0 : byteIndex - before);
             i <= byteIndex;
             i++) {
            buf[ind] = xmlstring[i];
            ind++;
        }
        buf[ind] = '\0';
        Tcl_AppendResult(interp, buf, " <--Error-- ", NULL);
        ind = 0;
        buf[0] = '\0';
        if (xmlstring[byteIndex]) {
            for (i = byteIndex + 1; i < byteIndex + after; i++) {
                if (!xmlstring[i]) {
                    break;
                }
                buf[ind] = xmlstring[i];
                ind++;
            }
            buf[ind] = '\0';
            Tcl_AppendResult(interp, buf, NULL);
        }
        Tcl_AppendResult(interp, "\"",NULL);
    }
    if (d) {
        FREE (d);
    }
}

/*----------------------------------------------------------------------------
|   tcldom_parse
|
\---------------------------------------------------------------------------*/
static
int tcldom_parse (
    ClientData  UNUSED(clientData),
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    * const objv[]
)
{
    GetTcldomDATA;

    char        *xml_string, *option, *errStr, *channelId, *baseURI = NULL;
    char        *jsonRoot = NULL;
    Tcl_Obj     *extResolver = NULL;
    Tcl_Obj     *feedbackCmd = NULL;
    const char  *interpResult;
    int          optionIndex, value, mode;
    domLength    xml_string_len;
    int          jsonmaxnesting = JSON_MAX_NESTING;
    int          ignoreWhiteSpaces   = 1;
    int          takeJSONParser      = 0;
    int          takeSimpleParser    = 0;
    int          takeHTMLParser      = 0;
    int          takeGUMBOParser     = 0;
    int          setVariable         = 0;
    int          ignorexmlns         = 0;
    int          feedbackAfter       = 0;
    int          useForeignDTD       = 0;
    int          paramEntityParsing  = (int)XML_PARAM_ENTITY_PARSING_ALWAYS;
    int          keepCDATA           = 0;
    int          forest              = 0;
    int          status              = 0;
    double       maximumAmplification = 0.0;
    long         activationThreshold = 0;
    domParseForestErrorData forestError;
    domDocument *doc;
    Tcl_Obj     *newObjName = NULL;
    XML_Parser   parser;
    Tcl_Channel  chan = (Tcl_Channel) NULL;
    Tcl_CmdInfo  cmdInfo;
#ifndef TDOM_NO_SCHEMA
    SchemaData  *sdata = NULL;
#endif
    static const char *parseOptions[] = {
        "-keepEmpties",           "-simple",        "-html",
        "-feedbackAfter",         "-channel",       "-baseurl",
        "-externalentitycommand", "-useForeignDTD", "-paramentityparsing",
        "-feedbackcmd",           "-json",          "-jsonroot",
#ifdef TDOM_HAVE_GUMBO
        "-html5",
#endif
#ifndef TDOM_NO_SCHEMA
        "-validateCmd",
#endif
        "-jsonmaxnesting",        "-ignorexmlns",   "--",
        "-keepCDATA",
        "-billionLaughsAttackProtectionMaximumAmplification",
        "-billionLaughsAttackProtectionActivationThreshold",
        "-forest",
        NULL
    };
    enum parseOption {
        o_keepEmpties,            o_simple,         o_html,
        o_feedbackAfter,          o_channel,        o_baseurl,
        o_externalentitycommand,  o_useForeignDTD,  o_paramentityparsing,
        o_feedbackcmd,            o_json,           o_jsonroot,
#ifdef TDOM_HAVE_GUMBO
        o_htmlfive,
#endif
#ifndef TDOM_NO_SCHEMA
        o_validateCmd,
#endif
        o_jsonmaxnesting,         o_ignorexmlns,    o_LAST,
        o_keepCDATA,
        o_billionLaughsAttackProtectionMaximumAmplification,
        o_billionLaughsAttackProtectionActivationThreshold,
        o_forest
    };

    static const char *paramEntityParsingValues[] = {
        "always",
        "never",
        "notstandalone",
        (char *) NULL
    };
    enum paramEntityParsingValue {
        EXPAT_PARAMENTITYPARSINGALWAYS,
        EXPAT_PARAMENTITYPARSINGNEVER,
        EXPAT_PARAMENTITYPARSINGNOTSTANDALONE
    };

    while (objc > 1) {
        option = Tcl_GetString(objv[1]);
        if (option[0] != '-') {
            break;
        }
        if (Tcl_GetIndexFromObj(interp, objv[1], parseOptions, "option", 0,
                                 &optionIndex) != TCL_OK) {
            return TCL_ERROR;
        }

        switch ((enum parseOption) optionIndex) {

        case o_keepEmpties:
            ignoreWhiteSpaces = 0;
            objv++;  objc--; continue;

        case o_json:
            if (takeGUMBOParser || takeHTMLParser) {
                SetResult("The options -html, -html5 and -json are "
                          "mutually exclusive.");
                return TCL_ERROR;
            }
            takeJSONParser = 1;
            objv++;  objc--; continue;
            
        case o_jsonroot:
            objv++; objc--;
            if (objc > 1) {
                jsonRoot = Tcl_GetString(objv[1]);
            } else {
                SetResult("The \"dom parse\" option \"-jsonroot\" "
                          "expects the document element name of the "
                          "DOM tree to create as argument.");
                return TCL_ERROR;
            }
            objv++; objc--; continue;
            
        case o_simple:
            takeSimpleParser = 1;
            objv++;  objc--; continue;

        case o_html:
            if (takeGUMBOParser || takeJSONParser) {
                SetResult("The options -html, -html5 and -json are "
                          "mutually exclusive.");
                return TCL_ERROR;
            }
            takeSimpleParser = 1;
            takeHTMLParser = 1;
            objv++;  objc--; continue;

#ifdef TDOM_HAVE_GUMBO
        case o_htmlfive:
            if (takeHTMLParser || takeJSONParser) {
                SetResult("The options -html, -html5 and -json are "
                          "mutually exclusive.");
                return TCL_ERROR;
            }
            takeGUMBOParser = 1;
            objv++;  objc--; continue;
#endif
            
        case o_feedbackAfter:
            objv++; objc--;
            if (objc > 1) {
                if (Tcl_GetIntFromObj(interp, objv[1], &feedbackAfter)
                    != TCL_OK) {
                    SetResult("-feedbackAfter must have an integer argument");
                    return TCL_ERROR;
                }
            } else {
                SetResult("The \"dom parse\" option \"-feedbackAfter\" requires"
                          " a positive integer as argument.");
                return TCL_ERROR;
            }
            if (feedbackAfter <= 0) {
                SetResult("The \"dom parse\" option \"-feedbackAfter\" requires"
                          " a positive integer as argument.");
                return TCL_ERROR;
            }                
            objv++; objc--;
            continue;

        case o_channel:
            objv++; objc--;
            if (objc > 1) {
                channelId = Tcl_GetString(objv[1]);
            } else {
                SetResult("The \"dom parse\" option \"-channel\" "
                          "requires a Tcl channel as argument.");
                return TCL_ERROR;
            }
            chan = Tcl_GetChannel(interp, channelId, &mode);
            if (chan == (Tcl_Channel) NULL) {
                return TCL_ERROR;
            }
            if ((mode & TCL_READABLE) == 0) {
                Tcl_AppendResult(interp, "channel \"", channelId,
                                "\" wasn't opened for reading", (char *) NULL);
                return TCL_ERROR;
            }
            objv++; objc--;
            continue;

        case o_baseurl:
            objv++; objc--;
            if (objc > 1) {
                baseURI = Tcl_GetString(objv[1]);
            } else {
                SetResult("The \"dom parse\" option \"-baseurl\" "
                          "requires the base URL of the document "
                          "to parse as argument.");
                return TCL_ERROR;
            }
            objv++; objc--;
            continue;

        case o_externalentitycommand:
            objv++; objc--;
            if (objc > 1) {
                extResolver = objv[1];
            } else {
                SetResult("The \"dom parse\" option \"-externalentitycommand\" "
                          "requires a script as argument.");
                return TCL_ERROR;
            }
            objv++; objc--;
            continue;

        case o_useForeignDTD:
            objv++; objc--;
            if (objc > 1) {
                if (Tcl_GetBooleanFromObj(interp, objv[1], &useForeignDTD)
                    != TCL_OK) {
                    return TCL_ERROR;
                }
            } else {
                SetResult(dom_usage);
                return TCL_ERROR;
            }
            objv++; objc--;
            continue;

        case o_paramentityparsing:
            if (objc > 2) {
                if (Tcl_GetIndexFromObj(interp, objv[2], 
                                        paramEntityParsingValues, "value", 0, 
                                        &value) != TCL_OK) {
                    return TCL_ERROR;
                }
                switch ((enum paramEntityParsingValue) value) {
                case EXPAT_PARAMENTITYPARSINGALWAYS:
                    paramEntityParsing = (int) XML_PARAM_ENTITY_PARSING_ALWAYS;
                    break;
                case EXPAT_PARAMENTITYPARSINGNEVER:
                    paramEntityParsing = (int) XML_PARAM_ENTITY_PARSING_NEVER;
                    break;
                case EXPAT_PARAMENTITYPARSINGNOTSTANDALONE:
                    paramEntityParsing = 
                        (int) XML_PARAM_ENTITY_PARSING_UNLESS_STANDALONE;
                    break;
                }
            } else {
                SetResult("-paramEntityParsing requires 'always', 'never' "
                          "or 'notstandalone' as argument");
                return TCL_ERROR;
            }
            objv++; objc--;
            objv++; objc--;
            continue;

        case o_feedbackcmd:
            objv++; objc--;
            if (objc > 1) {
                feedbackCmd = objv[1];
            } else {
                SetResult("The \"dom parse\" option \"-feedbackcmd\" "
                          "requires a script as argument.");
                return TCL_ERROR;
            }
            objv++; objc--;
            continue;
            
        case o_ignorexmlns:
            ignorexmlns = 1;
            objv++;  objc--; continue;

        case o_jsonmaxnesting:
            objv++; objc--;
            if (objc < 2) {
                SetResult("The \"dom parse\" option \"-jsonmaxnesting\" "
                          "requires an integer as argument.");
                return TCL_ERROR;
            }
            if (Tcl_GetIntFromObj(interp, objv[1], &jsonmaxnesting)
                != TCL_OK) {
                SetResult("-jsonmaxnesting must have an integer argument");
                return TCL_ERROR;
            }
            if (jsonmaxnesting < 0) {
                SetResult("The value of -jsonmaxnesting cannot be negativ");
                return TCL_ERROR;
            }
            objv++;  objc--; continue;
                        
        case o_LAST:
            objv++;  objc--; break;
            
        case o_keepCDATA:
            keepCDATA = 1;
            objv++;  objc--; continue;

        case o_billionLaughsAttackProtectionMaximumAmplification:
            objv++; objc--;
            if (objc < 2) {
                SetResult("The \"dom parse\" option \""
                          "-billionLaughsAttackProtectionMaximumAmplification"
                          "\" requires a float >= 1.0 as argument.");
                return TCL_ERROR;
            }
            if (Tcl_GetDoubleFromObj (interp, objv[1], &maximumAmplification)
                != TCL_OK) {
                SetResult("The \"dom parse\" option \""
                          "-billionLaughsAttackProtectionMaximumAmplification"
                          "\" requires a float >= 1.0 as argument.");
                return TCL_ERROR;
            }
            if (maximumAmplification > (double)FLT_MAX || maximumAmplification < 1.0) {
                SetResult("The \"dom parse\" option \""
                          "-billionLaughsAttackProtectionMaximumAmplification"
                          "\" requires a float >= 1.0 as argument.");
                return TCL_ERROR;
            }
            objv++;  objc--; continue;

        case o_billionLaughsAttackProtectionActivationThreshold:
            objv++; objc--;
            if (objc < 2) {
                SetResult("The \"dom parse\" option \""
                          "-billionLaughsAttackProtectionActivationThreshold"
                          "\" requires a long > 0 as argument.");
                return TCL_ERROR;
            }
            if (Tcl_GetLongFromObj (interp, objv[1], &activationThreshold)
                != TCL_OK) {
                SetResult("The \"dom parse\" option \""
                          "-billionLaughsAttackProtectionActivationThreshold"
                          "\" requires a long > 0 as argument.");
                return TCL_ERROR;
            }
            if (activationThreshold < 1) {
                SetResult("The \"dom parse\" option \""
                          "-billionLaughsAttackProtectionActivationThreshold"
                          "\" requires a long > 0 as argument.");
                return TCL_ERROR;
            }
            objv++;  objc--; continue;
            
#ifndef TDOM_NO_SCHEMA
        case o_validateCmd:
            objv++; objc--;
            if (objc < 2) {
                SetResult("The \"dom parse\" option \"-validateCmd\" "
                          "requires a tDOM validation command as argument.");
                return TCL_ERROR;
            }
            if (!Tcl_GetCommandInfo(interp, Tcl_GetString(objv[1]),
                                    &cmdInfo)) {
                SetResult3("The \"-validateCmd\" argument \"",
                           Tcl_GetString(objv[1]),
                           "\" is not a tDOM validation command.");
                return TCL_ERROR;
            }
            if (cmdInfo.objProc != tDOM_schemaInstanceCmd) {
                SetResult3("The \"-validateCmd\" argument \"",
                           Tcl_GetString(objv[1]),
                           "\" is not a tDOM validation command.");
                return TCL_ERROR;
            }
            sdata = (SchemaData *) cmdInfo.objClientData;
            objv++;  objc--; continue;
#endif
        case o_forest:
            forest = 1;
            forestError.errorLine = 0;
            forestError.errorColumn = 0;
            forestError.byteIndex = 0;
            forestError.errorCode = 0;
            objv++;  objc--; continue;
            
        }
        if ((enum parseOption) optionIndex == o_LAST) break;
    }

    if (feedbackAfter && !feedbackCmd) {
        if (!Tcl_GetCommandInfo(interp, "::dom::domParseFeedback", 
                                &cmdInfo)) {
            SetResult("If -feedbackAfter is used, "
                      "-feedbackcmd must also be used.");
            return TCL_ERROR;
        }
    }
    if (chan == NULL) {
        if (objc < 2) {
            SetResult(dom_usage);
            return TCL_ERROR;
        }
        xml_string = Tcl_GetStringFromObj( objv[1], &xml_string_len);
        if (objc == 3) {
            newObjName = objv[2];
            setVariable = 1;
        }
    } else {
        if (objc > 2) {
            SetResult(dom_usage);
            return TCL_ERROR;
        }
        xml_string = NULL;
        xml_string_len = 0;
        if (takeSimpleParser || takeHTMLParser || takeJSONParser
#ifdef TDOM_HAVE_GUMBO
                || takeGUMBOParser
#endif
            ) {
            Tcl_AppendResult(interp, "simple, JSON and HTML parser "
                             " don't support channel reading", NULL);
            return TCL_ERROR;
        }
        if (objc == 2) {
            newObjName = objv[1];
            setVariable = 1;
        }
    }

#ifdef TDOM_HAVE_GUMBO
    if (takeGUMBOParser) {
        if (xml_string_len > UINT_MAX) {
            SetResult ("The Gumbo library doesn't support strings longer than"
                       " 4 gigabytes.");
            return TCL_ERROR;
        }
        doc = HTML_GumboParseDocument(xml_string, ignoreWhiteSpaces,
                                      ignorexmlns);
        return tcldom_returnDocumentObj (interp, doc, setVariable, newObjName,
                                         1, 0);
    }
#endif
    
    if (takeJSONParser) {
        domLength byteIndex;

        errStr = NULL;

        doc = JSON_Parse (xml_string, jsonRoot, jsonmaxnesting, &errStr,
                          &byteIndex);
        if (doc) {
            return tcldom_returnDocumentObj (interp, doc, setVariable,
                                             newObjName, 1, 0);
        } else {
            tcldom_reportErrorLocation(interp, 20, 20, 0, 0, xml_string, NULL,
                                       byteIndex, errStr);
            return TCL_ERROR;
        }
    }
    
    if (takeSimpleParser) {
        domLength byteIndex;

        errStr = NULL;

        if (takeHTMLParser) {
            doc = HTML_SimpleParseDocument(xml_string, ignoreWhiteSpaces,
                                           forest, &byteIndex, &errStr);
        } else {
            doc = XML_SimpleParseDocument(xml_string, ignoreWhiteSpaces,
                                          keepCDATA, forest,
                                          baseURI, extResolver,
                                          &byteIndex, &errStr);
        }
        if (errStr != NULL) {
            domFreeDocument (doc, NULL, interp);
            tcldom_reportErrorLocation(interp, 80, 80, 0, 0, xml_string, NULL,
                                       byteIndex, errStr);
            if (takeHTMLParser) {
                FREE(errStr);
            }
            return TCL_ERROR;
        }
        return tcldom_returnDocumentObj (interp, doc, setVariable, newObjName,
                                         1, 0);
    }

#ifdef TDOM_NO_EXPAT
    Tcl_AppendResult(interp, "tDOM was compiled without Expat!", NULL);
    return TCL_ERROR;
#else
    parser = XML_ParserCreate_MM(NULL, MEM_SUITE, NULL);
#ifndef TDOM_NO_SCHEMA
    if (sdata) {
        if (sdata->validationState != VALIDATION_READY) {
            XML_ParserFree(parser);
            SetResult ("The configured schema command is busy");
            return TCL_ERROR;
        }
        sdata->inuse++;
        sdata->parser = parser;
    }
#endif
#ifdef XML_DTD
    if (maximumAmplification >= 1.0f) {
        if (XML_SetBillionLaughsAttackProtectionMaximumAmplification (
                parser, (float)maximumAmplification) == XML_FALSE) {
            SetResult("The \"dom parse\" option \""
                      "-billionLaughsAttackProtectionMaximumAmplification"
                      "\" requires a float >= 1.0 as argument.");
            XML_ParserFree(parser);
            return TCL_ERROR;
        }
    }
    if (activationThreshold > 0) {
        if (XML_SetBillionLaughsAttackProtectionActivationThreshold (
                parser, activationThreshold) == XML_FALSE) {
            XML_ParserFree(parser);
            SetResult("The \"dom parse\" option \""
                      "-billionLaughsAttackProtectionActivationThreshold"
                      "\" requires a long > 0 as argument.");
            return TCL_ERROR;
        }
    }
#endif

    Tcl_ResetResult(interp);
    doc = domReadDocument(parser, xml_string,
                          xml_string_len,
                          ignoreWhiteSpaces,
                          keepCDATA,
                          TcldomDATA(storeLineColumn),
                          ignorexmlns,
                          feedbackAfter,
                          feedbackCmd,
                          chan,
                          baseURI,
                          extResolver,
                          useForeignDTD,
                          forest,
                          paramEntityParsing,
#ifndef TDOM_NO_SCHEMA
                          sdata,
#endif
                          interp,
                          &forestError,
                          &status);
#ifndef TDOM_NO_SCHEMA
    if (sdata) {
        sdata->inuse--;
        tDOM_schemaReset (sdata);
    }
#endif
    if (doc == NULL) {
        char sl[50];
        char sc[50];
        
        switch (status) {
        case TCL_BREAK:
            /* Abort of parsing by the application */
            Tcl_ResetResult(interp);
            XML_ParserFree(parser);
            return TCL_OK;
        default:
            interpResult = Tcl_GetStringResult(interp);
            if (interpResult[0] == '\0') {
                /* If the interp result isn't empty, then there was an error
                   in an enternal entity and the interp result has already the
                   error msg. If we don't got a document, but interp result is
                   empty, the error occurred in the main document and we
                   build the error msg as follows. */
                tcldom_reportErrorLocation (
                    interp, 20, 40,
                    (forest ? forestError.errorLine : XML_GetCurrentLineNumber(parser)),
                    (forest ? forestError.errorColumn : XML_GetCurrentColumnNumber(parser)),
                    xml_string, NULL,
                    (forest ? forestError.byteIndex : XML_GetCurrentByteIndex(parser)),
                    XML_ErrorString((forest ? forestError.errorCode : XML_GetErrorCode(parser))));
            } else {
                if (status == TCL_OK) {
                    /* For Tcl errors (in -externalentitycommand or
                     * feedback callback) we leave the error msg in
                     * the interpreter alone. If there wasn't a Tcl
                     * error, there was a parsing error. Because the
                     * interp has already an error msg, that parsing
                     * error was in an external entity. Therefore, we
                     * just add the place of the referencing entity in
                     * the main document.*/
                    sprintf(sl, "%ld", XML_GetCurrentLineNumber(parser));
                    Tcl_AppendResult(interp, ", referenced at line ", sl,
                                     NULL);
                    sprintf(sc, "%ld", XML_GetCurrentColumnNumber(parser));
                    Tcl_AppendResult(interp, " character ", sc, NULL);
                }
            }
            XML_ParserFree(parser);
            return TCL_ERROR;
        }
    }
    XML_ParserFree(parser);

    return tcldom_returnDocumentObj (interp, doc, setVariable, newObjName, 1,
                                     0);
#endif

}

/*----------------------------------------------------------------------------
|   tcldom_featureinfo
|
\---------------------------------------------------------------------------*/
static
int tcldom_featureinfo (
    ClientData  UNUSED(clientData),
    Tcl_Interp *interp,
    int         UNUSED(objc),
    Tcl_Obj    * const objv[]
)
{
    int featureIndex, result;
    
    static const char *features[] = {
        "expatversion",      "expatmajorversion",  "expatminorversion",
        "expatmicroversion", "dtd",                "ns",
        "unknown",           "tdomalloc",          "lessns",
        "html5",             "jsonmaxnesting",     "versionhash",
        "pullparser",        "TCL_UTF_MAX",        "schema",
        NULL
    };
    enum feature {
        o_expatversion,      o_expatmajorversion,  o_expatminorversion,
        o_expatmicroversion, o_dtd,                o_ns,
        o_unknown,           o_tdomalloc,          o_lessns,
        o_html5,             o_jsonmaxnesting,     o_versionhash,
        o_pullparser,        o_TCL_UTF_MAX,        o_schema
    };

    /* objc is already checked by caller */
    if (Tcl_GetIndexFromObj(interp, objv[1], features, "feature", 0,
                            &featureIndex) != TCL_OK) {
        return TCL_ERROR;
    }

    switch ((enum feature) featureIndex) {
    case o_expatversion:
        SetResult(XML_ExpatVersion());
        break;
    case o_expatmajorversion:
        SetIntResult(XML_MAJOR_VERSION);
        break;
    case o_expatminorversion:
        SetIntResult(XML_MINOR_VERSION);
        break;
    case o_expatmicroversion:
        SetIntResult(XML_MICRO_VERSION);
        break;
    case o_dtd:
#ifdef XML_DTD
        result = 1;
#else
        result = 0;
#endif
        SetBooleanResult(result);
        break;
    case o_ns:
#ifdef XML_NS
        result = 1;
#else
        result = 0;
#endif
        SetBooleanResult(result);
        break;
    case o_unknown:       
#ifdef TDOM_NO_UNKNOWN_CMD
        result = 0;
#else
        result = 1;
#endif
        SetBooleanResult(result);
        break;
    case o_tdomalloc:
#ifdef USE_NORMAL_ALLOCATOR
        result = 0;
#else
        result = 1;
#endif
        SetBooleanResult(result);
        break;
    case o_lessns:
#ifdef TDOM_LESS_NS
        result = 1;
#else
        result = 0;
#endif
        SetBooleanResult(result);
        break;
    case o_html5:
#ifdef TDOM_HAVE_GUMBO
        result = 1;
#else
        result = 0;
#endif
        SetBooleanResult(result);
        break;
    case o_jsonmaxnesting:
        SetIntResult(JSON_MAX_NESTING);
        break;

    case o_versionhash:
        SetResult(FOSSIL_HASH);
        break;
    case o_pullparser:
#ifndef TDOM_NO_PULL
        result = 1;
#else
        result = 0;
#endif
        SetBooleanResult(result);
        break;
    case o_schema:
#ifndef TDOM_NO_SCHEMA
        result = 1;
#else
        result = 0;
#endif
        SetBooleanResult(result);
        break;
    case o_TCL_UTF_MAX:
        SetIntResult(TCL_UTF_MAX);
        break;
    }
    return TCL_OK;
}

/*----------------------------------------------------------------------------
|   tcldom_DomObjCmd
|
\---------------------------------------------------------------------------*/
int tcldom_DomObjCmd (
    ClientData   clientData,
    Tcl_Interp * interp,
    int          objc,
    Tcl_Obj    * const objv[]
)
{
    GetTcldomDATA;

    char        * method, tmp[300], *string, *option,
                 *replacement;
    int           methodIndex, result, i, bool, changed;
    domLength     repllen;
    Tcl_CmdInfo   cmdInfo;
    Tcl_Obj     * mobjv[MAX_REWRITE_ARGS], *newObj;
    Tcl_DString   cleardString;

    static const char *domMethods[] = {
        "createDocument",  "createDocumentNS",   "createNodeCmd",
        "parse",                                 "setStoreLineColumn",
        "isCharData",      "isName",             "isPIName",
        "isQName",         "isComment",          "isCDATA",
        "isPIValue",       "isNCName",           "createDocumentNode",
        "setNameCheck",    "setTextCheck",       "setObjectCommands",
        "featureinfo",     "isBMPCharData",      "clearString",
#ifdef TCL_THREADS
        "attachDocument",  "detachDocument",
#endif
        NULL
    };
    enum domMethod {
        m_createDocument,    m_createDocumentNS,   m_createNodeCmd,
        m_parse,                                   m_setStoreLineColumn,
        m_isCharData,        m_isName,             m_isPIName,
        m_isQName,           m_isComment,          m_isCDATA,
        m_isPIValue,         m_isNCName,           m_createDocumentNode,
        m_setNameCheck,      m_setTextCheck,       m_setObjectCommands,
        m_featureinfo,       m_isBMPCharData,      m_clearString
#ifdef TCL_THREADS
        ,m_attachDocument,   m_detachDocument
#endif
    };

    static const char *nodeModeValues[] = {
        "automatic", "command", "token", NULL
    };
    enum nodeModeValue {
        v_automatic, v_command, v_token
    };

    static const char *clearStringOptions[] = {
        "-replace", NULL
    };
    enum clearStringOption {
        o_replace
    };
    
    if (objc < 2) {
        SetResult(dom_usage);
        return TCL_ERROR;
    }
    if (TcldomDATA(domCreateCmdMode) == DOM_CREATECMDMODE_AUTO) {
        TcldomDATA(dontCreateObjCommands) = 0;
    }
    method = Tcl_GetString(objv[1]);
    if (Tcl_GetIndexFromObj(NULL, objv[1], domMethods, "method", 0,
                            &methodIndex) != TCL_OK) {
        /*--------------------------------------------------------
        |   try to find method implemented as normal Tcl proc
        \-------------------------------------------------------*/
        if ((strlen(method)-1) >= 270) {
            SetResult("method name too long!");
            return TCL_ERROR;
        }
        sprintf(tmp, "::dom::DOMImplementation::%s", method);
        DBG(fprintf(stderr, "testing %s\n", tmp));
        result = Tcl_GetCommandInfo(interp, tmp, &cmdInfo);
        if (!result) {
            SetResult(dom_usage);
            return TCL_ERROR;
        }
        if (!cmdInfo.isNativeObjectProc) {
            SetResult("can't access Tcl level method!");
            return TCL_ERROR;
        }
        if (objc >= MAX_REWRITE_ARGS) {
            SetResult("too many args to call Tcl level method!");
            return TCL_ERROR;
        }
        mobjv[0] = objv[1];
        mobjv[1] = objv[0];
        for (i=2; i<objc; i++) mobjv[i] = objv[i];
        return cmdInfo.objProc(cmdInfo.objClientData, interp, objc, mobjv);
    }
    CheckArgs(2,12,1,dom_usage);
    switch ((enum domMethod) methodIndex) {

        case m_createDocument:
            return tcldom_createDocument(clientData, interp, --objc, objv+1);

        case m_createDocumentNS:
            return tcldom_createDocumentNS(clientData, interp, --objc, objv+1);

        case m_createDocumentNode:
            return tcldom_createDocumentNode (clientData, interp, --objc,
                                              objv+1);
        case m_createNodeCmd:
            return nodecmd_createNodeCmd(interp, --objc, objv+1,
                                         !TcldomDATA(dontCheckName),
                                         !TcldomDATA(dontCheckCharData));
        case m_parse:
            return tcldom_parse(clientData, interp, --objc, objv+1);

#ifdef TCL_THREADS
        case m_attachDocument:
            {
                char *cmdName, *errMsg;
                domDocument *doc;
                if (objc < 3) {
                    SetResult(dom_usage);
                    return TCL_ERROR;
                }
                cmdName = Tcl_GetString(objv[2]);
                doc = tcldom_getDocumentFromName(interp, cmdName, &errMsg);
                if (doc == NULL) {
                    SetResult(errMsg);
                    return TCL_ERROR;
                }
                return tcldom_returnDocumentObj(interp, doc, (objc == 4),
                                                (objc==4) ? objv[3] : NULL,
                                                1, 0);
            }
            break;
        case m_detachDocument:
            {
                char objCmdName[80], *cmdName, *errMsg;
                Tcl_CmdInfo cmdInfo;
                domDocument *doc;
                if (objc < 3) {
                    SetResult(dom_usage);
                    return TCL_ERROR;
                }
                cmdName = Tcl_GetString(objv[2]);
                doc = tcldom_getDocumentFromName(interp, cmdName, &errMsg);
                if (doc == NULL) {
                    SetResult(errMsg);
                    return TCL_ERROR;
                }
                DOC_CMD(objCmdName, doc);
                if (Tcl_GetCommandInfo(interp, objCmdName, &cmdInfo)) {
                    Tcl_DeleteCommand(interp, objCmdName);
                } else {
                    tcldom_deleteDoc(interp, doc);
                }
                SetResult("");
                return TCL_OK;
            }
            break;
#endif

        case m_setStoreLineColumn:
            if (objc == 3) {
                if (Tcl_GetBooleanFromObj(interp, objv[2], &bool) != TCL_OK) {
                    return TCL_ERROR;
                }
                TcldomDATA(storeLineColumn) = bool;
            }
            SetBooleanResult(TcldomDATA(storeLineColumn));
            return TCL_OK;

        case m_setNameCheck:
            if (objc == 3) {
                if (Tcl_GetBooleanFromObj(interp, objv[2], &bool) != TCL_OK) {
                    return TCL_ERROR;
                }
                TcldomDATA(dontCheckName) = !bool;
            }
            SetBooleanResult(!TcldomDATA(dontCheckName));
            return TCL_OK;
            
        case m_setTextCheck:
            if (objc == 3) {
                if (Tcl_GetBooleanFromObj(interp, objv[2], &bool) != TCL_OK) {
                    return TCL_ERROR;
                }
                TcldomDATA(dontCheckCharData) = !bool;
            }
            SetBooleanResult(!TcldomDATA(dontCheckCharData));
            return TCL_OK;
            
        case m_setObjectCommands:
            if (objc == 3) {
                if (Tcl_GetIndexFromObj (interp, objv[2], nodeModeValues,
                                         "mode value", 0, &i) != TCL_OK) {
                    return TCL_ERROR;
                }
                switch ((enum nodeModeValue) i) {
                case v_automatic:
                    TcldomDATA(domCreateCmdMode) = DOM_CREATECMDMODE_AUTO;
                    TcldomDATA(dontCreateObjCommands) = 0;
                    break;
                case v_command:
                    TcldomDATA(domCreateCmdMode) = DOM_CREATECMDMODE_CMDS;
                    TcldomDATA(dontCreateObjCommands) = 0;
                    break;
                case v_token:
                    TcldomDATA(domCreateCmdMode) = DOM_CREATECMDMODE_TOKENS;
                    TcldomDATA(dontCreateObjCommands) = 1;
                    break;
                }
            }
            switch (TcldomDATA(domCreateCmdMode)) {
            case DOM_CREATECMDMODE_AUTO:
                SetResult("automatic");
                break;
            case DOM_CREATECMDMODE_CMDS:
                SetResult("command");
                break;
            case DOM_CREATECMDMODE_TOKENS:
                SetResult("token");
                break;
            default:
                domPanic("Impossible node creation mode.");
            }
            return TCL_OK;

        case m_isCharData:
            CheckArgs(3,3,2,"string");
            SetBooleanResult(domIsChar(Tcl_GetString(objv[2])));
            return TCL_OK;
            
        case m_isName:
            CheckArgs(3,3,2,"string");
            SetBooleanResult(domIsNAME(Tcl_GetString(objv[2])));
            return TCL_OK;
            
        case m_isPIName:
            CheckArgs(3,3,2,"string");
            SetBooleanResult(domIsPINAME(Tcl_GetString(objv[2])));
            return TCL_OK;
            
        case m_isQName:
            CheckArgs(3,3,2,"string");
            SetBooleanResult(domIsQNAME(Tcl_GetString(objv[2])));
            return TCL_OK;
            
        case m_isComment:
            CheckArgs(3,3,2,"string");
            SetBooleanResult(domIsComment(Tcl_GetString(objv[2])));
            return TCL_OK;
            
        case m_isCDATA:
            CheckArgs(3,3,2,"string");
            SetBooleanResult(domIsCDATA(Tcl_GetString(objv[2])));
            return TCL_OK;

        case m_isPIValue:
            CheckArgs(3,3,2,"string");
            SetBooleanResult(domIsPIValue(Tcl_GetString(objv[2])));
            return TCL_OK;
            
        case m_isNCName:
            CheckArgs(3,3,2,"string");
            SetBooleanResult(domIsNCNAME(Tcl_GetString(objv[2])));
            return TCL_OK;

        case m_featureinfo:
            CheckArgs(3,3,2,"feature")
            return tcldom_featureinfo(clientData, interp, --objc, objv+1);

        case m_isBMPCharData:
            CheckArgs(3,3,2,"string");
            SetBooleanResult(domIsBMPChar(Tcl_GetString(objv[2])));
            return TCL_OK;

        case m_clearString:
            CheckArgs(3,5,2,"?-replace ?replacement?? string");
            if (objc >= 4) {
                option = Tcl_GetString (objv[2]);
                if (option[0] == '-' && option[1] == 'r') {
                    if (Tcl_GetIndexFromObj (interp, objv[2],
                                             clearStringOptions, "option",
                                             0, &i) != TCL_OK) {
                        return TCL_ERROR;
                    }
                } else {
                    SetResult("expected: clearString ?-replace ?replacement?"
                              " string");
                    return TCL_ERROR;
                }
                objc--;
                objv++;
                if (objc == 4) {
                    replacement = Tcl_GetStringFromObj (objv[2], &repllen);
                    objc--;
                    objv++;
                } else {
                    replacement = "\xEF\xBF\xBD\0";
                    repllen = 3;
                }
            } else {
                replacement = NULL;
                repllen = 0;
            }
            string = Tcl_GetString (objv[2]);
            domClearString (string, replacement, repllen, &cleardString,
                            &changed);
            if (changed) {
                newObj = Tcl_NewStringObj (
                    Tcl_DStringValue (&cleardString),
                    Tcl_DStringLength (&cleardString));
                Tcl_DStringFree (&cleardString);
                Tcl_SetObjResult (interp, newObj);
            } else {
                Tcl_SetObjResult (interp, objv[2]);
            }
            return TCL_OK;
    }
    SetResult( dom_usage);
    return TCL_ERROR;
}

#ifdef TCL_THREADS

/*----------------------------------------------------------------------------
|   tcldom_EvalLocked
|
\---------------------------------------------------------------------------*/

static
int tcldom_EvalLocked (
    Tcl_Interp  * interp, 
    Tcl_Obj    ** objv, 
    domDocument * doc,
    int          flag
)
{
    int ret;
    domlock *dl = doc->lock;

    domLocksLock(dl, flag);

    Tcl_AllowExceptions(interp);
    ret = Tcl_EvalObjEx(interp, objv[2], 0);
    if (ret == TCL_ERROR) {
        char msg[64 + TCL_INTEGER_SPACE];
        sprintf(msg, "\n    (\"%s %s\" body line %d)", Tcl_GetString(objv[0]),
                Tcl_GetString(objv[1]), Tcl_GetErrorLine(interp));
        Tcl_AddErrorInfo(interp, msg);
    }

    domLocksUnlock(dl);

    return (ret == TCL_BREAK) ? TCL_OK : ret;
}

/*----------------------------------------------------------------------------
|   tcldom_RegisterDocShared
|
\---------------------------------------------------------------------------*/

static
int tcldom_RegisterDocShared (
    domDocument * doc
)
{
    Tcl_HashEntry *entryPtr;
    int newEntry = 0;
#ifdef DEBUG    
    int refCount;
#endif

    Tcl_MutexLock(&tableMutex);
#ifdef DEBUG    
    refCount = ++doc->refCount;
#else
    ++doc->refCount;
#endif
    entryPtr = Tcl_CreateHashEntry(&sharedDocs, (char*)doc, &newEntry);
    if (newEntry) {
        Tcl_SetHashValue(entryPtr, (ClientData)doc);
    }
    Tcl_MutexUnlock(&tableMutex);

    DBG(fprintf(stderr, "--> tcldom_RegisterDocShared: doc %p %s "
                "shared table now with refcount of %d\n", doc,
                newEntry ? "entered into" : "already in", refCount));
    return 0;
}

/*----------------------------------------------------------------------------
|   tcldom_UnregisterDocShared
|
\---------------------------------------------------------------------------*/

static
int tcldom_UnregisterDocShared (
    Tcl_Interp  * interp,
    domDocument * doc
)
{
    int deleted;

    Tcl_MutexLock(&tableMutex);
    if (doc->refCount > 1) {
        tcldom_deleteNode(doc->rootNode, interp);
        domFreeNode(doc->rootNode, tcldom_deleteNode, interp, 1);
        doc->refCount--;
        deleted = 0;
    } else {
        if (tcldomInitialized) {
            Tcl_HashEntry *entryPtr = Tcl_FindHashEntry(&sharedDocs, (char*)doc);
            if (entryPtr) {
                Tcl_DeleteHashEntry(entryPtr);
                deleted = 1;
            } else {
                deleted = 0;
            }
        } else {
            deleted = 0;
        }
    }
    Tcl_MutexUnlock(&tableMutex);

    DBG(fprintf(stderr, "--> tcldom_UnregisterDocShared: doc %p %s "
                "shared table\n", doc, deleted ? "deleted from" : "left in"));

    return deleted;
}

/*----------------------------------------------------------------------------
|   tcldom_CheckDocShared
|
\---------------------------------------------------------------------------*/

static
int tcldom_CheckDocShared (
    domDocument * doc
)
{
    Tcl_HashEntry *entryPtr;
    domDocument *tabDoc = NULL;
    int found = 0;

    Tcl_MutexLock(&tableMutex);
    if (tcldomInitialized) {
        entryPtr = Tcl_FindHashEntry(&sharedDocs, (char*)doc);
        if (entryPtr == NULL) {
            found = 0;
        } else {
            tabDoc = (domDocument*)Tcl_GetHashValue(entryPtr);
            found  = tabDoc ? 1 : 0;
        }
    }
    Tcl_MutexUnlock(&tableMutex);

    if (found && doc != tabDoc) {
        Tcl_Panic("document mismatch; doc=%p, in table=%p\n", (void *)doc,
                  (void *)tabDoc);
    }

    return found;
}

#endif /* TCL_THREADS */

#ifndef TDOM_NO_UNKNOWN_CMD

/*----------------------------------------------------------------------------
|   tcldom_unknownCmd
|
\---------------------------------------------------------------------------*/
int tcldom_unknownCmd (
    ClientData   clientData,
    Tcl_Interp * interp,
    int          objc,
    Tcl_Obj    * const objv[]
)
{
    int          i, rc, openedParen, count, args;
    domLength    len;
    char        *cmd, *dot, *paren, *arg[MAX_REWRITE_ARGS], *object, *method;
    Tcl_DString  callString;
    Tcl_CmdInfo  cmdInfo;
    Tcl_Obj     *vector[2+MAX_REWRITE_ARGS];
    Tcl_Obj     **objvCall;


    cmd = Tcl_GetStringFromObj(objv[1], &len);

    DBG(fprintf(stderr, "tcldom_unknownCmd: cmd=-%s- \n", cmd));

    dot = strchr(cmd,'.');
    if ((dot != NULL) && (dot != cmd)) {

        object = cmd;
        cmd    = dot+1;
        *dot   = '\0';
        dot    = strchr(cmd,'.');

        while (dot != NULL) {

            method = cmd;
            paren = strchr(cmd,'(');
            args = 0;
            if (paren && (paren < dot)) {
                *paren = '\0';
                paren++;
                arg[args] = paren;
                openedParen = 1;
                while (*paren) {
                    if (*paren == '\\') {
                        (void) Tcl_Backslash(paren, &count);
                        paren += count;
                    } else if (*paren == ')') {
                        openedParen--;
                        if (openedParen==0) {
                            *paren = '\0';
                            args++;
                            break;
                        }
                    } else if (*paren == '(') {
                        openedParen++;
                        paren++;
                    } else if (*paren == ',') {
                        *paren = '\0';
                        arg[++args] = paren+1;
                        if (args >= MAX_REWRITE_ARGS) {
                            SetResult( "too many args");
                            return TCL_ERROR;
                        }
                        paren++;
                    } else {
                        paren++;
                    }
                }
                if (openedParen!=0) {
                    SetResult( "mismatched (");
                    return TCL_ERROR;
                }
            }
            cmd    = dot+1;
            *dot   = '\0';

            DBG(fprintf(stderr, "method=-%s- \n", method);
                fprintf(stderr, "rest=-%s- \n", cmd);
                for(i=0; i<args; i++) {
                    fprintf(stderr, "args %d =-%s- \n", i, arg[i]);
                }
            )

            /*---------------------------------------------------------
            |   intermediate call
            \--------------------------------------------------------*/
            rc = Tcl_GetCommandInfo(interp, object, &cmdInfo);
            if (rc && cmdInfo.isNativeObjectProc) {
                vector[0] = Tcl_NewStringObj(object, -1);
                vector[1] = Tcl_NewStringObj(method, -1);
                for(i=0; i<args; i++) {
                    vector[2+i] = Tcl_NewStringObj(arg[i], -1);
                }
                rc = cmdInfo.objProc(cmdInfo.objClientData, interp, 2+args,
                                     vector);
                if (rc != TCL_OK) {
                   return rc;
                }
                for(i=args+1; i >= 0; i--) {
                    Tcl_DecrRefCount(vector[i]);
                }
            } else {
                Tcl_DStringInit(&callString);
                Tcl_DStringAppendElement(&callString, object);
                Tcl_DStringAppendElement(&callString, method);
                for(i=0; i<args; i++) {
                    Tcl_DStringAppendElement(&callString, arg[i] );
                }
                rc = Tcl_Eval(interp, Tcl_DStringValue(&callString));
                Tcl_DStringFree(&callString);
                if (rc != TCL_OK) {
                   return rc;
                }
            }
            /* get the new object returned from above call */
            object = Tcl_GetStringResult(interp);
            dot = strchr(cmd,'.');
        }

        method = cmd;
            paren = strchr(cmd,'(');
            args = 0;
            if (paren) {
                *paren = '\0';
                paren++;
                arg[args] = paren;
                openedParen = 1;
                while (*paren) {
                    if (*paren == '\\') {
                        (void) Tcl_Backslash(paren, &count);
                        paren += count;
                    } else if (*paren == ')') {
                        openedParen--;
                        if (openedParen==0) {
                            *paren = '\0';
                            args++;
                            break;
                        }
                    } else if (*paren == '(') {
                        openedParen++;
                        paren++;
                    } else if (*paren == ',') {
                        *paren = '\0';
                        arg[++args] = paren+1;
                        if (args >= MAX_REWRITE_ARGS) {
                            SetResult( "too many args");
                            return TCL_ERROR;
                        }
                        paren++;
                    } else {
                        paren++;
                    }
                }
                if (openedParen!=0) {
                    SetResult( "mismatched (");
                    return TCL_ERROR;
                }
            }
            DBG(fprintf(stderr, "method=-%s- \n", method);
                fprintf(stderr, "rest=-%s- \n", cmd);
                for(i=0; i<args; i++) {
                    fprintf(stderr, "args %d =-%s- \n", i, arg[i]);
                }
            )

        /*----------------------------------------------------------------
        |   final call
        \---------------------------------------------------------------*/
        rc = Tcl_GetCommandInfo(interp, object, &cmdInfo);
        if (rc && cmdInfo.isNativeObjectProc) {

            objvCall = (Tcl_Obj**)MALLOC(sizeof(Tcl_Obj*) * (objc+args));

            objvCall[0] = Tcl_NewStringObj(object, -1);
            objvCall[1] = Tcl_NewStringObj(method, -1);
            for(i=0; i<args; i++) {
                objvCall[2+i] = Tcl_NewStringObj(arg[i], -1);
            }
            for (i=2; i<objc; i++) {
                objvCall[i+args] = objv[i];
            }
            rc = cmdInfo.objProc(cmdInfo.objClientData, interp, objc + args,
                                 objvCall);
            for(i=objc+args-1; i >= 0; i--) {
                Tcl_DecrRefCount(objvCall[i]);
            }
            FREE((void*)objvCall);

        } else {
            Tcl_DStringInit(&callString);
            Tcl_DStringAppendElement(&callString, object);
            Tcl_DStringAppendElement(&callString, method);
            for(i=2; i<objc; i++) {
                Tcl_DStringAppendElement(&callString, Tcl_GetString(objv[i]));
            }
            rc = Tcl_Eval(interp, Tcl_DStringValue(&callString));
            Tcl_DStringFree(&callString);
        }
        return rc;

    } else {

        /*----------------------------------------------------------------
        |   call the original unknown function
        |
        \---------------------------------------------------------------*/
        Tcl_DStringInit(&callString);
        Tcl_DStringAppendElement(&callString, "unknown_tdom");
        for(i=1; i<objc; i++) {
            Tcl_DStringAppendElement(&callString, Tcl_GetString(objv[i]));
        }
        rc = Tcl_Eval(interp, Tcl_DStringValue(&callString));
        Tcl_DStringFree(&callString);
        return rc;
    }
}

#endif
