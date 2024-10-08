
#ifndef __TCL_EXPAT_H__
#define __TCL_EXPAT_H__

#include <tcl.h>
#include <expat.h>
#include "dom.h"
#include "schema.h"

struct TclGenExpatInfo;

typedef void (*CHandlerSet_userDataReset)(Tcl_Interp *interp, void *userData);
typedef void (*CHandlerSet_userDataFree)(Tcl_Interp *interp, void *userData);
typedef void (*CHandlerSet_parserReset)(XML_Parser parser, void *userData);
typedef void (*CHandlerSet_initParse)(Tcl_Interp *interp, void *userData);

typedef void(XMLCALL *tdom_CharacterDataHandler)(void *userData,
                                                  const XML_Char *s, domLength len);

typedef struct CHandlerSet {
    struct CHandlerSet *nextHandlerSet;
    char *name;                     /* refname of the handler set */
    int ignoreWhiteCDATAs;          /* ignore 'white' CDATA sections */

    void *userData;                 /* Handler set specific Data Structure;
                                       the C handler set extension has to
                                       malloc the needed structure in his
                                       init func and has to provide a
                                       cleanup func (to free it). */

    CHandlerSet_userDataReset        resetProc;
    CHandlerSet_userDataFree         freeProc;
    CHandlerSet_parserReset          parserResetProc;
    CHandlerSet_initParse            initParseProc;

    /* C func for element start */
    XML_StartElementHandler          elementstartcommand;
    /* C func for element end */
    XML_EndElementHandler            elementendcommand;
    /* C func for character data */
    tdom_CharacterDataHandler        datacommand;
    /* C func for namespace decl start */
    XML_StartNamespaceDeclHandler    startnsdeclcommand;
    /* C func for namespace decl end */
    XML_EndNamespaceDeclHandler      endnsdeclcommand;
    /* C func for processing instruction */
    XML_ProcessingInstructionHandler picommand;
    /* C func for default data */
    XML_DefaultHandler               defaultcommand;
    /* C func for unparsed entity declaration */
    XML_NotationDeclHandler          notationcommand;
    /* C func for external entity */
    XML_ExternalEntityRefHandler     externalentitycommand;
    /* C func for unknown encoding */
    XML_UnknownEncodingHandler       unknownencodingcommand;
    /* C func for comments */
    XML_CommentHandler               commentCommand;
    /* C func for "not standalone" docs */
    XML_NotStandaloneHandler         notStandaloneCommand;
    /* C func for CDATA section start */
    XML_StartCdataSectionHandler     startCdataSectionCommand;
    /* C func for CDATA section end */
    XML_EndCdataSectionHandler       endCdataSectionCommand;
    /* C func for <!ELEMENT decl's */
    XML_ElementDeclHandler           elementDeclCommand;
    /* C func for <!ATTLIST decl's */
    XML_AttlistDeclHandler           attlistDeclCommand;
    /* C func for <!DOCTYPE decl's */
    XML_StartDoctypeDeclHandler      startDoctypeDeclCommand;
    /* C func for <!DOCTYPE decl ends */
    XML_EndDoctypeDeclHandler        endDoctypeDeclCommand;
    /* C func for <?XML decl's */
    XML_XmlDeclHandler               xmlDeclCommand;
    /* C func for <!ENTITY decls's */
    XML_EntityDeclHandler            entityDeclCommand;
} CHandlerSet;

/*----------------------------------------------------------------------------
|   The structure below is used to refer to an event handler set
|   of Tcl scripts.
\---------------------------------------------------------------------------*/

typedef struct TclHandlerSet {
    struct TclHandlerSet *nextHandlerSet;
    char *name;                     /* refname of the handler set */
    int status;                     /* handler set status */
    int continueCount;		    /* reference count for continue */
    int ignoreWhiteCDATAs;          /* ignore 'white' CDATA sections */
    int fastCall;                   /* do fast ("Welch") handler calls */
    
    Tcl_Obj *elementstartcommand;      /* Script for element start */
    Tcl_ObjCmdProc *elementstartObjProc;
    ClientData      elementstartclientData;
    Tcl_Obj *elementendcommand;        /* Script for element end */
    Tcl_ObjCmdProc *elementendObjProc;
    ClientData      elementendclientData;
    Tcl_Obj *datacommand;	       /* Script for character data */
    Tcl_ObjCmdProc *datacommandObjProc;
    ClientData      datacommandclientData;
    Tcl_Obj *startnsdeclcommand;       /* Script for namespace decl start */
    Tcl_Obj *endnsdeclcommand;         /* Script for namespace decl end */
    Tcl_Obj *picommand;		       /* Script for processing instruction */
    Tcl_Obj *defaultcommand;	       /* Script for default data */
    Tcl_Obj *notationcommand;	       /* Script for notation declaration */
    Tcl_Obj *externalentitycommand;    /* Script for external entity */
    Tcl_Obj *unknownencodingcommand;   /* Script for unknown encoding */
    Tcl_Obj *commentCommand;           /* Script for comments */
    Tcl_Obj *notStandaloneCommand;     /* Script for "not standalone" docs */
    Tcl_Obj *startCdataSectionCommand; /* Script for CDATA section start */
    Tcl_Obj *endCdataSectionCommand;   /* Script for CDATA section end */
    Tcl_Obj *elementDeclCommand;       /* Script for <!ELEMENT decl's */
    Tcl_Obj *attlistDeclCommand;       /* Script for <!ATTLIST decl's */
    Tcl_Obj *startDoctypeDeclCommand;  /* Script for <!DOCTYPE decl's */
    Tcl_Obj *endDoctypeDeclCommand;    /* Script for <!DOCTYPE decl ends */
    Tcl_Obj *xmlDeclCommand;           /* Script for <?XML decl's */
    Tcl_Obj *entityDeclCommand;        /* Script for <!ENTITY decl's */
} TclHandlerSet;

typedef struct expatElemContent {

    XML_Content             *content;
    struct expatElemContent *next;

} ExpatElemContent;

typedef struct TclGenExpatInfo {
    XML_Parser  parser;		/* The expat parser structure */
    Tcl_Interp *interp;		/* Interpreter for this instance */
    Tcl_Obj    *name;		/* name of this instance */
    int final;			/* input data complete? */
    int needWSCheck;            /* Any handler set has ignoreWhiteCDATAs==1? */
    int status;			/* application status */
    Tcl_Obj *result;		/* application return result */
    const char *context;        /* reference to the context pointer */  
    Tcl_Obj *cdata;             /* Accumulates character data */ 
    domLength cdataStartLine;   /* Line number of the start of cdata */
    domLength cdataStartColumn; /* Column number of the start of cdata */
    domLength cdataStartByteIndex; /* Byte index of the start of cdata */
    int keepTextStart;          /* Keep the cdata start line/column/byteIndex */
    ExpatElemContent *eContents;/* The reported XML_Contents as linked list */
    int ns_mode;                /* namespace mode */
    Tcl_Obj *baseURI;
    int finished;
    int parsingState;           /* 0 == freshly (re-)initialized
                                   1 == initParserProcs called
                                   2 == parsing an input chunk */
    XML_Char nsSeparator;
    float maximumAmplification; /* Billion Laughts Attack Protection 1 */
    long activationThreshold;   /* Billion Laughts Attack Protection 2 */
    int paramentityparsing;     
    int noexpand;
    int useForeignDTD;
    const char *currentmarkup;  /* Used to transfer data for method */
    int currentmarkuplen;       /* currentmarkup */
#ifndef TDOM_NO_SCHEMA
    SchemaData *sdata;          /* Validation / Schema data */
#endif
 
    TclHandlerSet *firstTclHandlerSet;
    CHandlerSet *firstCHandlerSet;
} TclGenExpatInfo;

/*--------------------------------------------------------------------------
|   Function prototypes
|
\-------------------------------------------------------------------------*/

#ifndef __TDOM_H
Tcl_ObjCmdProc TclExpatObjCmd;

int CheckExpatParserObj (Tcl_Interp *interp,
 			Tcl_Obj *const nameObj);
int CHandlerSetInstall (Tcl_Interp *interp,
 		       Tcl_Obj *const expatObj,
 		       CHandlerSet *handlerSet);
int CHandlerSetRemove (Tcl_Interp *interp,
 		      Tcl_Obj *const expatObj,
 		      char *handlerSetName);
CHandlerSet * CHandlerSetCreate (char *name);
CHandlerSet * CHandlerSetGet (Tcl_Interp *interp,
                              Tcl_Obj *const expatObj,
 			     char *handlerSetName);
void * CHandlerSetGetUserData (Tcl_Interp *interp,
                               Tcl_Obj *const expatObj,
 			      char *handlerSetName);

TclGenExpatInfo * GetExpatInfo (Tcl_Interp *interp,
				       Tcl_Obj *const expatObj);
#endif

#endif 
