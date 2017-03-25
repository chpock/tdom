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
|   March 2017
|
\---------------------------------------------------------------------------*/

#ifdef TDOM_HAVE_GUMBO

/*----------------------------------------------------------------------------
|   Includes
|
\---------------------------------------------------------------------------*/
#include <tcl.h>
#include <dom.h>
#include "gumbo.h"
#include <assert.h>

static void
convertGumboToDom (
    domNode *parent,
    GumboNode *gumboParent,
    int ignoreWhiteSpaces
    ) 
{
    int i, j, hnew;
    GumboVector *children = &gumboParent->v.element.children;
    GumboNode *child;
    GumboElement *gumboElm;
    GumboAttribute *gumboAtt;
    domNode *node;
    domNodeType nodeType = ALL_NODES;
    domAttrNode *attr;
    Tcl_HashEntry *h;
    
    for (i = 0; i < children->length; ++i) {
        child = (GumboNode*) (children->data[i]);
        switch (child->type) {
        case GUMBO_NODE_DOCUMENT:
            assert(false &&
                   "This gumbo node type can't happen here.");
            break;
        case GUMBO_NODE_ELEMENT:
        case GUMBO_NODE_TEMPLATE:
            gumboElm = &child->v.element;
            
            /* Ignore namespaces, for now. That's probably what most
             * users want, anyway. Otherwise, XPath selecting nodes
             * inside of svg or math elements would need namespaced
             * expressions. */
            
            node = domNewElementNode (parent->ownerDocument,
                                      gumbo_normalized_tagname(gumboElm->tag),
                                      ELEMENT_NODE);
            for (j = 0; j < gumboElm->attributes.length; ++j) {
                gumboAtt = gumboElm->attributes.data[j];
                /* This is to ensure the same behavior as the -html
                 * parser: if there is just the attribute name given
                 * in the source (as 'selected' on a checkbox) then do
                 * it the XHTML style (att value is the att name,
                 * selected="selected"). If there is any value given
                 * in the source, including the empty string, use
                 * that. See gumbo.h for the details why/how this
                 * works.*/
                if (gumboAtt->original_value.data[0] != '"'
                    && gumboAtt->original_value.data[0] != '\'') {
                    attr = domSetAttribute (node, gumboAtt->name,
                                            gumboAtt->name);
                } else {
                    attr = domSetAttribute (node, gumboAtt->name,
                                            gumboAtt->value);
                }
                if (!strcmp(gumboAtt->name, "id")) {
                    if (!node->ownerDocument->ids) {
                        node->ownerDocument->ids = (Tcl_HashTable *)
                            MALLOC (sizeof (Tcl_HashTable));
                        Tcl_InitHashTable (
                            node->ownerDocument->ids,
                            TCL_STRING_KEYS);
                    }
                    h = Tcl_CreateHashEntry (
                        node->ownerDocument->ids,
                        gumboAtt->value,
                        &hnew);
                    /* How to resolve in case of dublicates?  We
                       follow, what the core dom building code does:
                       the first value in document order wins. */
                    if (hnew) {
                        Tcl_SetHashValue (h, node);
                        attr->nodeFlags |= IS_ID_ATTRIBUTE;
                    }
                }
            }
            domAppendChild(parent, node);
            convertGumboToDom(node, child, ignoreWhiteSpaces);
            break;
        case GUMBO_NODE_WHITESPACE:
            if (ignoreWhiteSpaces) {
                continue;
            }
            /* fall thru */;
        case GUMBO_NODE_CDATA:
        case GUMBO_NODE_TEXT:
            nodeType = TEXT_NODE
            /* fall thru */;
        case GUMBO_NODE_COMMENT:
            if (nodeType == ALL_NODES) nodeType = COMMENT_NODE;
            node = (domNode*)domNewTextNode(parent->ownerDocument,
                                            child->v.text.text,
                                            strlen(child->v.text.text),
                                            nodeType);
            domAppendChild(parent, node);
            break;
        default:
            assert(false && "unknown node type");
        }
    }
}

domDocument *
HTML_GumboParseDocument (
    char   *html,              /* Complete text of the file being parsed.  */
    int     ignoreWhiteSpaces,
    int    *pos,
    char  **errStr
) {
    domDocument *doc = domCreateDoc(NULL, 0);
    GumboOutput *output = gumbo_parse(html);
    convertGumboToDom (doc->rootNode, output->document, ignoreWhiteSpaces);
    domSetDocumentElement (doc);
    gumbo_destroy_output(&kGumboDefaultOptions, output);    
    return doc;
}

#endif
