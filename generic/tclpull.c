
#include <tdom.h>
#include <fcntl.h>
#ifdef _MSC_VER
#include <io.h>
#else
#include <unistd.h>
#endif

#ifndef O_BINARY
#ifdef _O_BINARY
#define O_BINARY _O_BINARY
#else
#define O_BINARY 0
#endif
#endif

#ifndef TDOM_EXPAT_READ_SIZE
# define TDOM_EXPAT_READ_SIZE (1024*8)
#endif

typedef enum {
    PULLPARSERSTATE_READY,
    PULLPARSERSTATE_START_DOCUMENT,
    PULLPARSERSTATE_END_DOCUMENT,
    PULLPARSERSTATE_START_TAG,
    PULLPARSERSTATE_END_TAG,
    PULLPARSERSTATE_TEXT,
    PULLPARSERSTATE_PARSE_ERROR
} PullParserState;
    

typedef struct tDOM_PullParserInfo 
{
    XML_Parser      parser;
    Tcl_Interp     *interp;
    Tcl_Obj        *inputString;
    Tcl_Channel     inputChannel;
    int             inputfd;
    PullParserState state;
    PullParserState nextState;
    Tcl_DString    *cdata;
    const char     *elmname;
    const char    **atts;
    Tcl_Obj        *channelReadBuf;
    Tcl_Obj        *start_tag;
    Tcl_Obj        *end_tag;
    Tcl_Obj        *text;
} tDOM_PullParserInfo;

#define SetResult(str) Tcl_ResetResult(interp); \
                     Tcl_SetStringObj(Tcl_GetObjResult(interp), (str), -1)

static void
startElement(
    void         *userData,
    const char   *name,
    const char  **atts
)
{
    tDOM_PullParserInfo *pullInfo = userData;

    if (Tcl_DStringLength (pullInfo->cdata) > 0) {
        pullInfo->state = PULLPARSERSTATE_TEXT;
        pullInfo->nextState = PULLPARSERSTATE_START_TAG;
    } else {
        pullInfo->state = PULLPARSERSTATE_START_TAG;
    }
    pullInfo->elmname = name;
    pullInfo->atts = atts;

    XML_StopParser(pullInfo->parser, 1);
}

static void
endElement (
    void        *userData,
    const char  *name
)
{
    tDOM_PullParserInfo *pullInfo = userData;
    XML_ParsingStatus status;
    
    if (Tcl_DStringLength (pullInfo->cdata) > 0) {
        pullInfo->state = PULLPARSERSTATE_TEXT;
        pullInfo->nextState = PULLPARSERSTATE_END_TAG;
    } else {
        XML_GetParsingStatus (pullInfo->parser, &status);
        if (status.parsing == XML_SUSPENDED) {
            pullInfo->state = PULLPARSERSTATE_START_TAG;
            pullInfo->nextState = PULLPARSERSTATE_END_TAG;
        } else {
            pullInfo->state = PULLPARSERSTATE_END_TAG;
        }
    }
    pullInfo->elmname = name;
    XML_StopParser(pullInfo->parser, 1);
}

static void
characterDataHandler (
    void        *userData,
    const char  *s,
    int          len
)
{
    tDOM_PullParserInfo *pullInfo = userData;

    Tcl_DStringAppend (pullInfo->cdata, s, len);    
}

static void
tDOM_PullParserDeleteCmd (
    ClientData clientdata
    )
{
    tDOM_PullParserInfo *pullInfo = clientdata;

    XML_ParserFree (pullInfo->parser);
    if (pullInfo->inputString) {
        Tcl_DecrRefCount (pullInfo->inputString);
    }
    if (pullInfo->inputfd) {
        close (pullInfo->inputfd);
    }
    Tcl_DStringFree (pullInfo->cdata);
    FREE (pullInfo->cdata);
    if (pullInfo->channelReadBuf) {
        Tcl_DecrRefCount (pullInfo->channelReadBuf);
    }
    Tcl_DecrRefCount(pullInfo->start_tag);
    Tcl_DecrRefCount(pullInfo->end_tag);
    Tcl_DecrRefCount(pullInfo->text);
    FREE (pullInfo);
}

static void
tDOM_ReportXMLError (
    Tcl_Interp *interp,
    tDOM_PullParserInfo *pullInfo
    )
{
    char s[255];

    Tcl_ResetResult (interp);
    sprintf(s, "%ld", XML_GetCurrentLineNumber(pullInfo->parser));
    Tcl_AppendResult(interp, "error \"",
                     XML_ErrorString(
                         XML_GetErrorCode(pullInfo->parser)),
                     "\" at line ", s, " character ", NULL);
    sprintf(s, "%ld", XML_GetCurrentColumnNumber(pullInfo->parser));
    Tcl_AppendResult(interp, s, NULL);
}

static void
tDOM_CleanupInputSource (
    tDOM_PullParserInfo *pullInfo
    )
{
    if (pullInfo->inputString) {
        Tcl_DecrRefCount (pullInfo->inputString);
        pullInfo->inputString = NULL;
    }
    pullInfo->inputChannel = NULL;
    if (pullInfo->inputfd) {
        close (pullInfo->inputfd);
        pullInfo->inputfd = 0;
    }
}

static int
tDOM_PullParserInstanceCmd (
    ClientData  clientdata,
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    *CONST objv[]
    )
{
    tDOM_PullParserInfo *pullInfo = clientdata;
    int methodIndex, len, result, status, mode, fd, done;
    char *data;
    const char **atts;
    Tcl_Obj *resultPtr;
    Tcl_Channel channel;
    XML_ParsingStatus pstatus;
    
    static const char *const methods[] = {
        "input", "inputchannel", "inputfile",
        "next", "state", "tag", "attributes",
        "text", "delete", "reset", NULL
    };

    enum method {
        m_input, m_inputchannel, m_inputfile,
        m_next, m_state, m_tag, m_attributes,
        m_text, m_delete, m_reset
    };

    if (objc == 1) {
        /* Default method call is next */
        methodIndex = m_next;
    } else {
        if (Tcl_GetIndexFromObj (interp, objv[1], methods, "method", 0,
                                 &methodIndex) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    switch ((enum method) methodIndex) {

    case m_input:
        if (objc != 3) {
            Tcl_WrongNumArgs (interp, 2, objv, "<xml>");
            return TCL_ERROR;
        }
        if (pullInfo->state != PULLPARSERSTATE_READY) {
            SetResult ("Can't change input while already parsing.");
            return TCL_ERROR;
        }
        Tcl_IncrRefCount (objv[2]);
        pullInfo->inputString = objv[2];
        pullInfo->state = PULLPARSERSTATE_START_DOCUMENT;
        break;

    case m_inputchannel:
        if (objc != 3) {
            Tcl_WrongNumArgs (interp, 2, objv, "<channel>");
            return TCL_ERROR;
        }
        if (pullInfo->state != PULLPARSERSTATE_READY) {
            SetResult ("Can't change input while already parsing.");
            return TCL_ERROR;
        }
        channel = Tcl_GetChannel (interp, Tcl_GetString(objv[2]), &mode);
        if (channel == NULL) {
            Tcl_ResetResult (interp);
            Tcl_AppendResult (interp, "\"", Tcl_GetString(objv[2]),
                              "\" isn't a Tcl channel in this interpreter", 
                              NULL);
            return TCL_ERROR;
        }
        if (!(mode & TCL_READABLE)) {
            Tcl_ResetResult (interp);
            Tcl_AppendResult (interp, "channel \"", Tcl_GetString(objv[2]),
                              "wasn't opened for reading", NULL);
            return TCL_ERROR;
        }
        pullInfo->inputChannel = channel;
        if (pullInfo->channelReadBuf == NULL) {
            pullInfo->channelReadBuf = Tcl_NewObj ();
            Tcl_IncrRefCount (pullInfo->channelReadBuf);
            Tcl_SetObjLength (pullInfo->channelReadBuf, 6144);
        }
        pullInfo->state = PULLPARSERSTATE_START_DOCUMENT;
        break;

    case m_inputfile:
        if (objc != 3) {
            Tcl_WrongNumArgs (interp, 2, objv, "<filename>");
            return TCL_ERROR;
        }
        if (pullInfo->state != PULLPARSERSTATE_READY) {
            SetResult ("Can't change input while already parsing.");
            return TCL_ERROR;
        }
        fd = open(Tcl_GetString(objv[2]), O_BINARY|O_RDONLY);
        if (fd < 0) {
            Tcl_ResetResult (interp);
            Tcl_AppendResult (interp, "error opening file \"",
                              Tcl_GetString(objv[2]), "\"", NULL);
            return TCL_ERROR;
        }
        pullInfo->inputfd = fd;
        pullInfo->state = PULLPARSERSTATE_START_DOCUMENT;
        break;

    case m_next:
        if (objc != 2) {
            Tcl_WrongNumArgs (interp, 2, objv, "");
            return TCL_ERROR;
        }
        if (pullInfo->state == PULLPARSERSTATE_TEXT) {
            Tcl_DStringSetLength (pullInfo->cdata, 0);
        }
        if (pullInfo->nextState) {
            pullInfo->state = pullInfo->nextState;
            pullInfo->nextState = 0;
        } else {
            switch (pullInfo->state) {
            case PULLPARSERSTATE_READY:
                SetResult ("No input");
                return TCL_ERROR;
            case PULLPARSERSTATE_PARSE_ERROR:
                SetResult ("Parsing stoped with XML parsing error.");
                return TCL_ERROR;
            case PULLPARSERSTATE_END_DOCUMENT:
                SetResult ("No next event after END_DOCUMENT");
                return TCL_ERROR;
            case PULLPARSERSTATE_TEXT:
                /* Since PULLPARSERSTATE_TEXT always has nextState set
                 * this case is handled in the if part of this if else
                 * and this is never reached. It's just here to eat up
                 * this case in the switch. */
                break;
            case PULLPARSERSTATE_START_DOCUMENT:
                if (pullInfo->inputfd) {
                    do {
                        char *fbuf =
                            XML_GetBuffer (pullInfo->parser,
                                           TDOM_EXPAT_READ_SIZE);
                        len = read(pullInfo->inputfd, fbuf,
                                   TDOM_EXPAT_READ_SIZE);
                        result = XML_ParseBuffer (pullInfo->parser,
                                                  len, len == 0);
                    } while (result == XML_STATUS_OK);
                } else if (pullInfo->inputChannel) {
                    do {
                        len = Tcl_ReadChars (pullInfo->inputChannel,
                                             pullInfo->channelReadBuf,
                                             1024, 0);
                        data = Tcl_GetString (pullInfo->channelReadBuf);
                        result = XML_Parse (pullInfo->parser, data, len,
                                            len == 0);
                    } while (result == XML_STATUS_OK);
                } else if (pullInfo->inputString) {
                    data = Tcl_GetStringFromObj(pullInfo->inputString, &len);
                    result = XML_Parse (pullInfo->parser, data, len, 1);
                }
                switch (result) {
                case XML_STATUS_OK:
                    tDOM_CleanupInputSource (pullInfo);
                    pullInfo->state = PULLPARSERSTATE_END_DOCUMENT;
                    break;
                case XML_STATUS_ERROR:
                    tDOM_CleanupInputSource (pullInfo);
                    tDOM_ReportXMLError (interp, pullInfo);
                    pullInfo->state = PULLPARSERSTATE_PARSE_ERROR;
                    return TCL_ERROR;
                case XML_STATUS_SUSPENDED:
                    /* Nothing to do here, state was set in handler, just
                     * take care to report */
                    break;
                }
                break;
            default:
                status = XML_ResumeParser (pullInfo->parser);
                switch (status) {
                case XML_STATUS_OK:
                    if (pullInfo->inputString) {
                        Tcl_DecrRefCount (pullInfo->inputString);
                        pullInfo->inputString = NULL;
                        pullInfo->state = PULLPARSERSTATE_END_DOCUMENT;
                        break;
                    }
                    XML_GetParsingStatus (pullInfo->parser, &pstatus);
                    if (pstatus.parsing == XML_FINISHED) {
                        tDOM_CleanupInputSource (pullInfo);
                        pullInfo->state = PULLPARSERSTATE_END_DOCUMENT;
                        break;
                    }
                    if (pullInfo->inputChannel) {
                        do {
                            len = Tcl_ReadChars (pullInfo->inputChannel,
                                                 pullInfo->channelReadBuf,
                                                 1024, 0);
                            done = (len < 1024);
                            data = Tcl_GetStringFromObj (
                                pullInfo->channelReadBuf, &len
                                );
                            result = XML_Parse (pullInfo->parser, data,
                                                len, done);
                        } while (result == XML_STATUS_OK && !done);
                    } else {
                        /* inputfile */
                        do {
                            char *fbuf = 
                                XML_GetBuffer (pullInfo->parser,
                                               TDOM_EXPAT_READ_SIZE);
                            len = read (pullInfo->inputfd, fbuf,
                                        TDOM_EXPAT_READ_SIZE);
                            done = (len < TDOM_EXPAT_READ_SIZE);
                            result = XML_ParseBuffer (pullInfo->parser,
                                                      len, done);
                        } while (result == XML_STATUS_OK && !done);
                    }
                    if (result == XML_STATUS_ERROR) {
                        tDOM_CleanupInputSource (pullInfo);
                        tDOM_ReportXMLError (interp, pullInfo);
                        pullInfo->state = PULLPARSERSTATE_PARSE_ERROR;
                        return TCL_ERROR;
                    }
                    if (done && result == XML_STATUS_OK) {
                        tDOM_CleanupInputSource (pullInfo);
                        pullInfo->state = PULLPARSERSTATE_END_DOCUMENT;
                    }
                    /* If here result == XML_STATUS_SUSPENDED,
                     * state was set in handler, just take care to
                     * report */
                            
                    break;
                case XML_STATUS_ERROR:
                    tDOM_CleanupInputSource (pullInfo);
                    tDOM_ReportXMLError (interp, pullInfo);
                    pullInfo->state = PULLPARSERSTATE_PARSE_ERROR;
                    return TCL_ERROR;
                case XML_STATUS_SUSPENDED:
                    /* Nothing to do here, state was set in handler, just
                     * take care to report */
                    break;
                }
            }
        }
        /* Fall throu to reporting state */
    case m_state:
        if (objc != 2) {
            Tcl_WrongNumArgs (interp, 2, objv, "");
            return TCL_ERROR;
        }
        switch (pullInfo->state) {
        case PULLPARSERSTATE_READY:
            SetResult("READY");
            break;
        case PULLPARSERSTATE_PARSE_ERROR:
            SetResult("PARSE_ERROR");
            break;
        case PULLPARSERSTATE_START_DOCUMENT:
            SetResult("START_DOCUMENT");
            break;
        case PULLPARSERSTATE_END_DOCUMENT:
            SetResult("END_DOCUMENT");
            break;
        case PULLPARSERSTATE_START_TAG:
            Tcl_SetObjResult (interp, pullInfo->start_tag);
            break;
        case PULLPARSERSTATE_END_TAG:
            Tcl_SetObjResult (interp, pullInfo->end_tag);
            break;
        case PULLPARSERSTATE_TEXT:
            Tcl_SetObjResult (interp, pullInfo->text);
            break;
        }
        break;

    case m_tag:
        if (objc != 2) {
            Tcl_WrongNumArgs (interp, 2, objv, "");
            return TCL_ERROR;
        }
        if (pullInfo->state != PULLPARSERSTATE_START_TAG
            && pullInfo->state != PULLPARSERSTATE_END_TAG) {
            SetResult("Invalid state");
            return TCL_ERROR;
        }
        SetResult(pullInfo->elmname);
        break;

    case m_attributes:
        if (objc != 2) {
            Tcl_WrongNumArgs (interp, 2, objv, "");
            return TCL_ERROR;
        }
        if (pullInfo->state != PULLPARSERSTATE_START_TAG) {
            return TCL_ERROR;
        }
        Tcl_ResetResult(interp);
        resultPtr = Tcl_GetObjResult(interp);
        atts = pullInfo->atts;
        while (atts[0] != NULL) {
            Tcl_ListObjAppendElement (interp, resultPtr,
                                      Tcl_NewStringObj(atts[0], -1));
            atts++;
        }
        break;

    case m_text:
        if (objc != 2) {
            Tcl_WrongNumArgs (interp, 2, objv, "");
            return TCL_ERROR;
        }
        Tcl_ResetResult (interp);
        Tcl_SetStringObj (
            Tcl_GetObjResult (interp),
            Tcl_DStringValue (pullInfo->cdata),
            Tcl_DStringLength (pullInfo->cdata)
            );
        break;
        
    case m_delete:
        if (objc != 2) {
            Tcl_WrongNumArgs (interp, 2, objv, "");
            return TCL_ERROR;
        }
        Tcl_DeleteCommand (interp, Tcl_GetString (objv[0]));
        break;

    case m_reset:
        if (objc != 2) {
            Tcl_WrongNumArgs (interp, 2, objv, "");
            return TCL_ERROR;
        }
        tDOM_CleanupInputSource (pullInfo);
        pullInfo->state = PULLPARSERSTATE_READY;
        pullInfo->nextState = 0;
        Tcl_DStringSetLength (pullInfo->cdata, 0);
        if (XML_ParserReset (pullInfo->parser, NULL) != XML_TRUE) {
            SetResult ("Parser reset failed!");
            return TCL_ERROR;
        }
        XML_SetElementHandler (pullInfo->parser, startElement, endElement);
        XML_SetUserData (pullInfo->parser, pullInfo);
        break;
        
    }

    return TCL_OK;
}


int
tDOM_PullParserCmd (
    ClientData  dummy,
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj    *CONST objv[]
    )
{
    tDOM_PullParserInfo *pullInfo;

    if (objc != 2) {
        Tcl_WrongNumArgs (interp, 1, objv, "cmdName");
        return TCL_ERROR;
    }
    
    pullInfo = (tDOM_PullParserInfo *) MALLOC(sizeof(tDOM_PullParserInfo));
    memset (pullInfo, 0, sizeof (tDOM_PullParserInfo));

    pullInfo->parser = XML_ParserCreate_MM(NULL, MEM_SUITE, NULL);
    XML_SetUserData (pullInfo->parser, pullInfo);
    XML_SetElementHandler (pullInfo->parser, startElement, endElement);
    XML_SetCharacterDataHandler (pullInfo->parser, characterDataHandler);
    pullInfo->interp = interp;
    pullInfo->cdata = (Tcl_DString*) MALLOC (sizeof (Tcl_DString));
    Tcl_DStringInit (pullInfo->cdata);
    pullInfo->state = PULLPARSERSTATE_READY;
    pullInfo->start_tag = Tcl_NewStringObj("START_TAG", 9);
    Tcl_IncrRefCount (pullInfo->start_tag);
    pullInfo->end_tag = Tcl_NewStringObj("END_TAG", 7);
    Tcl_IncrRefCount (pullInfo->end_tag);
    pullInfo->text = Tcl_NewStringObj("TEXT", 4);
    Tcl_IncrRefCount (pullInfo->text);
    
    Tcl_CreateObjCommand (interp, Tcl_GetString(objv[1]),
                          tDOM_PullParserInstanceCmd, (ClientData) pullInfo,
                          tDOM_PullParserDeleteCmd);
    Tcl_SetObjResult(interp, objv[1]);
    return TCL_OK;
}

