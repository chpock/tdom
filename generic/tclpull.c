
#include <tdom.h>

typedef enum {
    PULLPARSERSTATE_READY,
    PULLPARSERSTATE_START_DOCUMENT,
    PULLPARSERSTATE_END_DOCUMENT,
    PULLPARSERSTATE_START_TAG,
    PULLPARSERSTATE_END_TAG,
    PULLPARSERSTATE_TEXT
} PullParserState;
    

typedef struct tDOM_PullParserInfo 
{
    XML_Parser      parser;
    Tcl_Interp     *interp;
    Tcl_Obj        *inputString;
    Tcl_Channel    *inputChannel;
    int             inputfd;
    PullParserState state;
    PullParserState nextState;
    Tcl_DString    *cdata;
    const char     *elmname;
    const char    **atts;
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
    Tcl_DStringFree (pullInfo->cdata);
    FREE (pullInfo->cdata);
    FREE (pullInfo);
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
    int methodIndex, len, result, status;
    char *data, s[255];
    const char **atts;
    Tcl_Obj *resultPtr;
    
    static const char *const methods[] = {
        "input", "inputchannel", "inputfile",
        "next", "state", "tag", "attributes",
        "text", "delete", NULL
    };

    enum method {
        m_input, m_inputchannel, m_inputfile,
        m_next, m_state, m_tag, m_attributes,
        m_text, m_delete
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
        Tcl_IncrRefCount (objv[2]);
        pullInfo->inputString = objv[2];
        pullInfo->state = PULLPARSERSTATE_START_DOCUMENT;
        break;

    case m_inputchannel:
        break;

    case m_inputfile:
        break;

    case m_next:
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
                    
                } else if (pullInfo->inputChannel) {

                } else if (pullInfo->inputString) {
                    data = Tcl_GetStringFromObj(pullInfo->inputString, &len);
                    result = XML_Parse (pullInfo->parser, data, len, 1);
                }
                switch (result) {
                case XML_STATUS_OK:
                    if (pullInfo->inputString) {
                        Tcl_DecrRefCount (pullInfo->inputString);
                        pullInfo->inputString = NULL;
                    }
                    pullInfo->state = PULLPARSERSTATE_END_DOCUMENT;
                    break;
                case XML_STATUS_ERROR:
                    if (pullInfo->inputString) {
                        Tcl_DecrRefCount (pullInfo->inputString);
                        pullInfo->inputString = NULL;
                    }
                    Tcl_ResetResult (interp);
                    sprintf(s, "%ld", XML_GetCurrentLineNumber(pullInfo->parser));
                    Tcl_AppendResult(interp, "error \"",
                                     XML_ErrorString(
                                         XML_GetErrorCode(pullInfo->parser)),
                                     "\" at line ", s, " character ", NULL);
                    sprintf(s, "%ld", XML_GetCurrentColumnNumber(pullInfo->parser));
                    Tcl_AppendResult(interp, s, NULL);
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
                    }
                    pullInfo->state = PULLPARSERSTATE_END_DOCUMENT;
                    break;
                case XML_STATUS_ERROR:
                    if (pullInfo->inputString) {
                        Tcl_DecrRefCount (pullInfo->inputString);
                        pullInfo->inputString = NULL;
                    }
                    sprintf(s, "%ld", XML_GetCurrentLineNumber(pullInfo->parser));
                    Tcl_AppendResult(interp, "error \"",
                                     XML_ErrorString(
                                         XML_GetErrorCode(pullInfo->parser)),
                                     "\" at line ", s, " character ", NULL);
                    sprintf(s, "%ld", XML_GetCurrentColumnNumber(pullInfo->parser));
                    Tcl_AppendResult(interp, s, NULL);
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
        switch (pullInfo->state) {
        case PULLPARSERSTATE_READY:
            SetResult("READY");
            break;
        case PULLPARSERSTATE_START_DOCUMENT:
            SetResult("START_DOCUMENT");
            break;
        case PULLPARSERSTATE_END_DOCUMENT:
            SetResult("END_DOCUMENT");
            break;
        case PULLPARSERSTATE_START_TAG:
            SetResult("START_TAG");
            break;
        case PULLPARSERSTATE_END_TAG:
            SetResult("END_TAG");
            break;
        case PULLPARSERSTATE_TEXT:
            SetResult("TEXT");
            break;
        }
        break;

    case m_tag:
        if (pullInfo->state != PULLPARSERSTATE_START_TAG
            && pullInfo->state != PULLPARSERSTATE_END_TAG) {
            SetResult("Invalid state");
            return TCL_ERROR;
        }
        SetResult(pullInfo->elmname);
        break;

    case m_attributes:
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
        Tcl_ResetResult (interp);
        Tcl_SetStringObj (
            Tcl_GetObjResult (interp),
            Tcl_DStringValue (pullInfo->cdata),
            Tcl_DStringLength (pullInfo->cdata)
            );
        break;
        
    case m_delete:
        Tcl_DeleteCommand (interp, Tcl_GetString (objv[0]));
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

    if (objc < 2) {
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

    Tcl_CreateObjCommand (interp, Tcl_GetString(objv[1]),
                          tDOM_PullParserInstanceCmd, (ClientData) pullInfo,
                          tDOM_PullParserDeleteCmd);
    Tcl_SetObjResult(interp, objv[1]);
    return TCL_OK;
}

