
#include <tdom.h>

typedef enum {
    PULLPARSERSTATE_READY,
    PULLPARSERSTATE_START_DOKUMENT,
    PULLPARSERSTATE_END_DOKUMENT,
    PULLPARSERSTATE_START_TAG,
    PULLPARSERSTATE_END_TAG,
    PULLPARSERSTATE_TEXT
} PullParserState;
    

typedef struct tDOM_PullParserInfo 
{
    XML_Parser      parser;
    Tcl_Interp     *interp;
    Tcl_Obj        *name;
    Tcl_Obj        *inputString;
    Tcl_Channel    *inputChannel;
    int             inputfd;
    PullParserState state;
    Tcl_DString    *cdata;
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

}

static void
endElement (
    void        *userData,
    const char  *name
)
{
    tDOM_PullParserInfo *pullInfo = userData;

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
    
    static const char *const methods[] = {
        "input", "inputchannel", "inputfile",
        "next", "state", NULL
    };

    enum method {
        m_input, m_inputchannel, m_inputfile,
        m_next, m_state
    };

    if (objc == 1) {
        /* Default method call is next */

    }

    if (Tcl_GetIndexFromObj (interp, objv[1], methods, "method", 0,
                             &methodIndex) != TCL_OK) {
        return TCL_ERROR;
    }
    switch ((enum method) methodIndex) {
    case m_input:
        if (objc != 3) {
            Tcl_WrongNumArgs (interp, 2, objv, "<xml>");
            return TCL_ERROR;
        }
        Tcl_IncrRefCount (objv[2]);
        pullInfo->inputString = objv[2];
        pullInfo->state = PULLPARSERSTATE_START_DOKUMENT;
        break;
    case m_inputchannel:
        break;
    case m_inputfile:
        break;
    case m_next:
        switch (pullInfo->state) {
        case PULLPARSERSTATE_READY:
            SetResult ("No input");
            return TCL_ERROR;
        case PULLPARSERSTATE_END_DOKUMENT:
            SetResult ("No next event after END_DOKUMENT");
            return TCL_ERROR;
        case PULLPARSERSTATE_START_DOKUMENT:
            if (pullInfo->inputfd) {
                
            } else if (pullInfo->inputChannel) {

            } else if (pullInfo->inputString) {
                data = Tcl_GetStringFromObj(pullInfo->inputString, &len);
                result = XML_Parse (pullInfo->parser, data, len, 1);
            }
            switch (result) {
            case XML_STATUS_OK:
                pullInfo->state = PULLPARSERSTATE_END_DOKUMENT;
                break;
            case XML_STATUS_ERROR:
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
        }
        /* Fall throu to reporting state */
    case m_state:
        switch (pullInfo->state) {
        case PULLPARSERSTATE_READY:
            SetResult("READY");
            break;
        case PULLPARSERSTATE_START_DOKUMENT:
            SetResult("START_DOKUMENT");
            break;
        case PULLPARSERSTATE_END_DOKUMENT:
            SetResult("END_DOKUMENT");
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
    XML_SetElementHandler (pullInfo->parser, startElement, endElement);
    XML_SetCharacterDataHandler (pullInfo->parser, characterDataHandler);
    pullInfo->interp = interp;
    pullInfo->name = objv[1];
    pullInfo->cdata = (Tcl_DString*) MALLOC (sizeof (Tcl_DString));
    Tcl_DStringInit (pullInfo->cdata);
    pullInfo->state = PULLPARSERSTATE_READY;

    Tcl_CreateObjCommand (interp, Tcl_GetString(pullInfo->name),
                          tDOM_PullParserInstanceCmd, (ClientData) pullInfo,
                          tDOM_PullParserDeleteCmd);
    Tcl_SetObjResult(interp, pullInfo->name);
    return TCL_OK;
}

