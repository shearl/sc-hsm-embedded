/*
 *  ---------
 * |.**> <**.|  CardContact Software & System Consulting
 * |*       *|  32429 Minden, Germany (www.cardcontact.de)
 * |*       *|  Copyright (c) 1999-2003. All rights reserved
 * |'**> <**'|  See file COPYING for details on licensing
 *  --------- 
 *
 * The Smart Card Development Platform (SCDP) provides a basic framework to
 * implement smartcard aware applications.
 *
 * Abstract :       PKCS#11 functions for object management
 *
 * Author :         Frank Thater (FTH)
 *
 *****************************************************************************/

#include <stdio.h>
#include <memory.h>

#include <pkcs11/cryptoki.h>
#include <pkcs11/p11generic.h>
#include <pkcs11/slotpool.h>
#include <pkcs11/slot.h>
#include <pkcs11/token.h>
#include <pkcs11/object.h>
#include <pkcs11/secretkeyobject.h>
#include <pkcs11/dataobject.h>
#include <pkcs11/session.h>

#ifdef DEBUG
#include <pkcs11/debug.h>
#endif

extern struct p11Context_t *context;

/*  C_CreateObject creates a new object. */

CK_DECLARE_FUNCTION(CK_RV, C_CreateObject)(
    CK_SESSION_HANDLE hSession,
    CK_ATTRIBUTE_PTR pTemplate,
    CK_ULONG ulCount,
    CK_OBJECT_HANDLE_PTR phObject
)
{
    int rv = 0;
    struct p11Object_t *pObject;
    struct p11Session_t *session;
    struct p11Slot_t *slot;
    int pos;

#ifdef DEBUG
    debug("[C_CreateObject] called\n");
#endif

    rv = findSessionByHandle(context->sessionPool, hSession, &session);
    
    if (rv < 0) {
        return CKR_SESSION_HANDLE_INVALID;
    }

    pObject = (struct p11Object_t *) malloc(sizeof(struct p11Object_t));

    if (pObject == NULL) {       
        return CKR_HOST_MEMORY;
    }
    
    memset(pObject, 0x00, sizeof(struct p11Object_t));

    pos = findAttributeInTemplate(CKA_CLASS, pTemplate, ulCount);
    
    if (pos == -1) {
        free(pObject);
        return CKR_TEMPLATE_INCOMPLETE;
    }

    switch (*(CK_LONG *)pTemplate[pos].pValue) {
        case CKO_DATA:
            rv = createDataObject(pTemplate, ulCount, pObject);
            break;

        case CKO_SECRET_KEY:
            rv = createSecretKeyObject(pTemplate, ulCount, pObject);
            break;
        
        default:
            rv = CKR_FUNCTION_FAILED;
    }

    if (rv != CKR_OK) {
        free(pObject);
        return rv;
    }

    rv = findSlot(context->slotPool, session->slotID, &slot);
    
    if (rv < 0) {
        return CKR_FUNCTION_FAILED;        
    }
    
    /* Check if this is a session or a token object */
    
    /* Token object */
    if ((session->state == CKS_RW_USER_FUNCTIONS) && pObject->tokenObj) {    
                
        addObject(slot->token, pObject, pObject->publicObj);
        
        rv = synchronizeTokenToDisk(slot, slot->token);

        if (rv < 0) {
            removeObject(slot->token, pObject->handle, pObject->publicObj);
            return CKR_FUNCTION_FAILED;        
        }        
        
        
       
    } else {

        if (pObject->tokenObj) {
            removeAllAttributes(pObject);
            free(pObject);
            return CKR_SESSION_READ_ONLY;

        }
        
        addSessionObject(session, pObject);    
    
    }
    
    *phObject = pObject->handle;

    return rv;
}


/*  C_CopyObject copies an object. */

CK_DECLARE_FUNCTION(CK_RV, C_CopyObject)(
    CK_SESSION_HANDLE hSession,
    CK_OBJECT_HANDLE hObject,
    CK_ATTRIBUTE_PTR pTemplate,
    CK_ULONG ulCount,
    CK_OBJECT_HANDLE_PTR phNewObject
)
{
    CK_RV rv = CKR_FUNCTION_NOT_SUPPORTED;

#ifdef DEBUG
    debug("[C_CopyObject] called\n");
#endif

    return rv;
}


/*  C_DestroyObject destroys an object. */

CK_DECLARE_FUNCTION(CK_RV, C_DestroyObject)(
    CK_SESSION_HANDLE hSession,
    CK_OBJECT_HANDLE hObject
)
{
    int rv;
    struct p11Session_t *session = NULL;
    struct p11Slot_t *slot = NULL;
    struct p11Object_t *pObject = NULL;

#ifdef DEBUG
    debug("[C_DestroyObject] called\n");
#endif

    rv = findSessionByHandle(context->sessionPool, hSession, &session);
    
    if (rv < 0) {
        return CKR_SESSION_HANDLE_INVALID;
    }

    rv = findSlot(context->slotPool, session->slotID, &slot);
    
    if (rv < 0) {
        return CKR_FUNCTION_FAILED;        
    }
   
    rv = findSessionObject(session, hObject, &pObject);

    if (rv < 0) {
        
        rv = findObject(slot->token, hObject, &pObject, TRUE);
        
        if (rv < 0) {
                  
            if (session->state == CKS_RW_USER_FUNCTIONS) {
                rv = findObject(slot->token, hObject, &pObject, FALSE);      

                if (rv < 0) {
                    return CKR_OBJECT_HANDLE_INVALID;
                }

            } else {
                return CKR_OBJECT_HANDLE_INVALID;
            }        
        }

        /* remove the object from the storage media */
        destroyObject(slot, slot->token, pObject);

        /* remove the object from the list */
        removeObject(slot->token, hObject, pObject->publicObj);

        rv = synchronizeTokenToDisk(slot, slot->token);

        if (rv < 0) {
            return CKR_FUNCTION_FAILED;        
        }

    } else {
    
        removeSessionObject(session, hObject);
    
    }
    
    return CKR_OK;
}


/*  C_GetObjectSize gets the size of an object. */

CK_DECLARE_FUNCTION(CK_RV, C_GetObjectSize)(
    CK_SESSION_HANDLE hSession,
    CK_OBJECT_HANDLE hObject,
    CK_ULONG_PTR pulSize
)
{
    int rv;
    struct p11Object_t *pObject;
    struct p11Session_t *session;
    struct p11Slot_t *slot;
    unsigned int size;
    unsigned char *tmp;

#ifdef DEBUG
    debug("[C_GetObjectSize] called\n");
#endif

    rv = findSessionByHandle(context->sessionPool, hSession, &session);
    
    if (rv < 0) {
        return CKR_SESSION_HANDLE_INVALID;
    }
    
    rv = findSlot(context->slotPool, session->slotID, &slot);
    
    if (rv < 0) {
        return CKR_FUNCTION_FAILED;        
    }
    
    rv = findSessionObject(session, hObject, &pObject);

    if (rv < 0) {
        
        rv = findObject(slot->token, hObject, &pObject, TRUE);
        
        if (rv < 0) {
                  
            if (session->state == CKS_RW_USER_FUNCTIONS) {
                rv = findObject(slot->token, hObject, &pObject, FALSE);      

                if (rv < 0) {
                    return CKR_OBJECT_HANDLE_INVALID;
                }    
            } else {
                return CKR_OBJECT_HANDLE_INVALID;
            }
        
        }
    }
    
    serializeObject(pObject, &tmp, &size);
    free(tmp);

    *pulSize = size;

    return CKR_OK;
}


/*  C_GetAttributeValue obtains the value of one or more attributes of an object. */

CK_DECLARE_FUNCTION(CK_RV, C_GetAttributeValue)(
    CK_SESSION_HANDLE hSession,
    CK_OBJECT_HANDLE hObject,
    CK_ATTRIBUTE_PTR pTemplate,
    CK_ULONG ulCount
)
{
    int rv;
    CK_ULONG i;
    struct p11Object_t *pObject;
    struct p11Session_t *session;
    struct p11Slot_t *slot;
    struct p11Attribute_t *attribute;

#ifdef DEBUG
    debug("[C_GetAttributeValue] called\n");    
#endif

    rv = findSessionByHandle(context->sessionPool, hSession, &session);
    
    if (rv < 0) {
        return CKR_SESSION_HANDLE_INVALID;
    }
    
    rv = findSlot(context->slotPool, session->slotID, &slot);
    
    if (rv < 0) {
        return CKR_FUNCTION_FAILED;        
    }
    
    
    rv = findSessionObject(session, hObject, &pObject);

    if (rv < 0) {
        
        rv = findObject(slot->token, hObject, &pObject, TRUE);
        
        if (rv < 0) {
                  
            if (session->state == CKS_RW_USER_FUNCTIONS) {
                rv = findObject(slot->token, hObject, &pObject, FALSE);      

                if (rv < 0) {
                    return CKR_OBJECT_HANDLE_INVALID;
                }
            } else {
                return CKR_OBJECT_HANDLE_INVALID;
            }        
        }
    }

#ifdef DEBUG
    debug("[C_GetAttributeValue] Trying to get %u attributes ...\n", ulCount);
#endif

    rv = CKR_OK;

    for (i = 0; i < ulCount; i++) {
        
        attribute = pObject->attrList;

        while (attribute && (attribute->attrData.type != pTemplate[i].type)) {
            attribute = attribute->next;       
        }

        if (!attribute) {
            pTemplate[i].ulValueLen = (CK_LONG) -1;
            rv = CKR_ATTRIBUTE_TYPE_INVALID;
            continue;
        }

        if ((attribute->attrData.type == CKA_VALUE) && (pObject->sensitiveObj)) {
            pTemplate[i].ulValueLen = (CK_LONG) -1;
            rv = CKR_ATTRIBUTE_SENSITIVE;
            continue;
        }

        if (pTemplate[i].pValue == NULL_PTR) {
            pTemplate[i].ulValueLen = attribute->attrData.ulValueLen;
            continue;
        }

        if (pTemplate[i].ulValueLen >= attribute->attrData.ulValueLen) {
            memcpy(pTemplate[i].pValue, attribute->attrData.pValue, attribute->attrData.ulValueLen); 
            pTemplate[i].ulValueLen = attribute->attrData.ulValueLen;
        } else {
            pTemplate[i].ulValueLen = attribute->attrData.ulValueLen;
            rv = CKR_BUFFER_TOO_SMALL;
        }            
    }

    return rv;
}


/*  C_SetAttributeValue modifies the value of one or more attributes of an object. */

CK_DECLARE_FUNCTION(CK_RV, C_SetAttributeValue)(
    CK_SESSION_HANDLE hSession,
    CK_OBJECT_HANDLE hObject,
    CK_ATTRIBUTE_PTR pTemplate,
    CK_ULONG ulCount
)
{
    int rv;
    CK_ULONG i;
    struct p11Object_t *pObject, *tmp;
    struct p11Session_t *session;
    struct p11Slot_t *slot;
    struct p11Attribute_t *attribute;
    
#ifdef DEBUG
    debug("[C_SetAttributeValue] called\n");
#endif
    
    rv = findSessionByHandle(context->sessionPool, hSession, &session);
    
    if (rv < 0) {
        return CKR_SESSION_HANDLE_INVALID;
    }
    
    rv = findSlot(context->slotPool, session->slotID, &slot);
    
    if (rv < 0) {
        return CKR_FUNCTION_FAILED;        
    }
        
    rv = findSessionObject(session, hObject, &pObject);

    /* only session objects can be modified without user authentication */

    if ((rv < 0) && (session->state == CKS_RW_USER_FUNCTIONS)) {
        
        rv = findObject(slot->token, hObject, &pObject, TRUE);
        
        if (rv < 0) {
                  
            rv = findObject(slot->token, hObject, &pObject, FALSE);      

            if (rv < 0) {
                return CKR_OBJECT_HANDLE_INVALID;
            }
        } 
    } 
        
    for (i = 0; i < ulCount; i++) {
        
        attribute = pObject->attrList;

        while (attribute && (attribute->attrData.type != pTemplate[i].type)) {
            attribute = attribute->next;       
        }

        if (!attribute) {
            return CKR_TEMPLATE_INCOMPLETE; /* we do not allow manufacturer specific attributes ! */
        }
        
        /* Check if the value of CKA_PRIVATE changes */        
        if (pTemplate[i].type == CKA_PRIVATE) {
        
            /* changed from TRUE to FALSE */
            if ((*(CK_BBOOL *)pTemplate[i].pValue == CK_FALSE) && (*(CK_BBOOL *)attribute->attrData.pValue == CK_TRUE)) {
               return CKR_TEMPLATE_INCONSISTENT;
            }
            
            /* changed from FALSE to TRUE */
            if ((*(CK_BBOOL *)pTemplate[i].pValue == CK_TRUE) && (*(CK_BBOOL *)attribute->attrData.pValue == CK_FALSE)) {
            
                memcpy(attribute->attrData.pValue, pTemplate[i].pValue, pTemplate[i].ulValueLen); 

                tmp = (struct p11Object_t *) malloc(sizeof(struct p11Object_t));
                memset(tmp, 0x00, sizeof(*tmp));
                
                memcpy(tmp, pObject, sizeof(*pObject));

                tmp->next = NULL;
                tmp->publicObj = FALSE;
                tmp->dirtyFlag = 1;
                
                /* remove the public object */
                destroyObject(slot, slot->token, pObject);         
                removeObjectLeavingAttributes(slot->token, pObject->handle, TRUE);
                
                /* insert new private object */
                addObject(slot->token, tmp, FALSE);

                rv = synchronizeTokenToDisk(slot, slot->token);
            
                if (rv < 0) {
                    return rv;
                }
            }

        } else {
            
            if (pTemplate[i].ulValueLen > attribute->attrData.ulValueLen) {
                
                free(attribute->attrData.pValue);
                
                attribute->attrData.pValue = malloc(pTemplate[i].ulValueLen);
            }

            attribute->attrData.ulValueLen = pTemplate[i].ulValueLen;
            memcpy(attribute->attrData.pValue, pTemplate[i].pValue, pTemplate[i].ulValueLen);
            
            pObject->dirtyFlag = 1;    
            
            rv = synchronizeTokenToDisk(slot, slot->token);
            
            if (rv < 0) {
                return rv;
            }
        }
    }

    return rv;
}


/*  C_FindObjectsInit initializes a search for token and session objects 
    that match a template. */

CK_DECLARE_FUNCTION(CK_RV, C_FindObjectsInit)(
    CK_SESSION_HANDLE hSession,
    CK_ATTRIBUTE_PTR pTemplate,
    CK_ULONG ulCount
)
{
    int rv;
    // int i,j;
    struct p11Object_t *pObject;
    // , *pNewSearchObject, *pTempObject;
    struct p11Session_t *session;
    struct p11Slot_t *slot;
    struct p11Attribute_t *pAttribute;
    
#ifdef DEBUG
    debug("[C_FindObjectsInit] called\n");
#endif
    
    rv = findSessionByHandle(context->sessionPool, hSession, &session);
    
    if (rv < 0) {
        return CKR_SESSION_HANDLE_INVALID;
    }

    rv = findSlot(context->slotPool, session->slotID, &slot);
    
    if (rv < 0) {
        return CKR_FUNCTION_FAILED;        
    }
     
    session->searchObj.searchList = NULL;
    session->searchObj.searchNumOfObjects = 0;
    session->searchObj.objectsCollected = 0;

    if (ulCount == 0) {
       /* return all objects in the token */
        
        /* session objects */
        pObject = session->sessionObjList;

        while (pObject != NULL) {
        
            addObjectToSearchList(session, pObject);
            pObject = pObject->next;
        }
        
        /* public token objects */
        pObject = slot->token->tokenObjList;

        while (pObject != NULL) {
        
            addObjectToSearchList(session, pObject);
            pObject = pObject->next;
        }
        
        /* private token objects */
        if (session->state == CKS_RW_USER_FUNCTIONS) {
         
            pObject = slot->token->tokenPrivObjList;

            while (pObject != NULL) {
        
                addObjectToSearchList(session, pObject);
                pObject = pObject->next;
            }
        }
        
    } else if ((ulCount == 1) && (pTemplate[0].type == CKA_LABEL)) {
       /* return only objects that match a specific template - we only support the label! */
        
        /* session objects */
        pObject = session->sessionObjList;

        while (pObject != NULL) {
            
            rv = findAttribute(pObject, pTemplate, &pAttribute);

            if (rv >= 0) {                
                if (!memcmp(pAttribute->attrData.pValue, pTemplate[0].pValue, pAttribute->attrData.ulValueLen)) {
                    addObjectToSearchList(session, pObject);            
                }
            }
            pObject = pObject->next;
        
        }
        
        /* public token objects */
        pObject = slot->token->tokenObjList;

        while (pObject != NULL) {
        
            rv = findAttribute(pObject, pTemplate, &pAttribute);

            if (rv >= 0) {                
                if (!memcmp(pAttribute->attrData.pValue, pTemplate[0].pValue, pAttribute->attrData.ulValueLen)) {
                    addObjectToSearchList(session, pObject);            
                }
            }
            pObject = pObject->next;
        }
        
        /* private token objects */
        if (session->state == CKS_RW_USER_FUNCTIONS) {
         
            pObject = slot->token->tokenPrivObjList;

            while (pObject != NULL) {
        
                rv = findAttribute(pObject, pTemplate, &pAttribute);

                if (rv >= 0) {                
                    if (!memcmp(pAttribute->attrData.pValue, pTemplate[0].pValue, pAttribute->attrData.ulValueLen)) {
                        addObjectToSearchList(session, pObject);            
                    }
                }
                pObject = pObject->next;
            }
        }
        
    } else {
        return CKR_FUNCTION_NOT_SUPPORTED;
    }

    return CKR_OK;
}


/*  C_FindObjects continues a search for token and session objects that match a template, */

CK_DECLARE_FUNCTION(CK_RV, C_FindObjects)(
    CK_SESSION_HANDLE hSession,
    CK_OBJECT_HANDLE_PTR phObject,
    CK_ULONG ulMaxObjectCount,
    CK_ULONG_PTR pulObjectCount
)
{
    int rv;
    struct p11Session_t *session;
    struct p11Object_t *pObject;
    int i = 0;
    
#ifdef DEBUG
    debug("[C_FindObjects] called\n");
#endif

    rv = findSessionByHandle(context->sessionPool, hSession, &session);
    
    if (rv < 0) {
        return CKR_SESSION_HANDLE_INVALID;
    }

    if (session->searchObj.objectsCollected == session->searchObj.searchNumOfObjects) {
        *pulObjectCount = 0;
        return CKR_OK;
    }

    pObject = session->searchObj.searchList;

    i = session->searchObj.objectsCollected;

    while (i > 0) {      
        pObject = pObject->next;
        i--;    
    }

    *phObject = pObject->handle;
    *pulObjectCount = 1;

    session->searchObj.objectsCollected++;

    return CKR_OK;
}


/*  C_FindObjectsFinal terminates a search for token and session objects. */

CK_DECLARE_FUNCTION(CK_RV, C_FindObjectsFinal)(
    CK_SESSION_HANDLE hSession
)
{
    int rv;
    struct p11Object_t *pObject, *pTempObject;
    struct p11Session_t *session;
 
#ifdef DEBUG
    debug("[C_FindObjectsFinal] called\n");
#endif

    rv = findSessionByHandle(context->sessionPool, hSession, &session);
    
    if (rv < 0) {
        return CKR_SESSION_HANDLE_INVALID;
    }

    pObject = session->searchObj.searchList;

    while (pObject) {      
        pTempObject = pObject->next;
        free(pObject);
        pObject = pTempObject;
    }

    session->searchObj.searchNumOfObjects = 0;
    session->searchObj.objectsCollected = 0;
    session->searchObj.searchList = NULL;

    return CKR_OK;
}


