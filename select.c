// select.c ... select scan functions
// part of Multi-attribute Linear-hashed Files
// Manage creating and using Selection objects
// Credit: John Shepherd
// Last modified by Xiangjun Zai, Mar 2025

#include "defs.h"
#include "select.h"
#include "reln.h"
#include "tuple.h"
#include "bits.h"
#include "hash.h"

/**************************************
NEW FUNCS
***************************************/
static Bool known_attr(char* s);
static void setup(Reln r, char* q, Selection new);
static Status moveToNextPage(Selection s);
static Status nextMatchTup(Selection s, char **t);


/********************************************************************************
EDIT - add:
- qHash: hash value of query string (masked: all unknown bits = 0)
- known: record which bits are from known attributes (= 1), which are from unknown (0)
- curBid - current Bucket/primary page
- maxBid - maximum Page ID possible given the query hash
- qvals  - array of substrings of query (to avoid repeated malloc of same stuff)

Note: is_ovflow is kind of redundant but whatever!

**********************************************************************************/
struct SelectionRep {
	Reln        rel;          // need to remember Relation info
	Bits        qHash;        // the hash value of query 
	Bits        known;        // the known bits = 1, unknown bits = 0
    Page        curPage;          // current page in scan
	Bool        is_ovflow;        // are we in the overflow pages?
	Offset      curtupOffset;     // offset of current tuple within page      
    Bits        curBid; 
    Bits        maxBid;
    char**       qvals;           //query values  
};



/****************************************
NEW FUNCTION 
 - check if an attribute from query string
 - if known or unknown attribute
*****************************************/
static Bool known_attr(char* s) {
	if (((s[0] == '?') && (s[1] == '\0')) || (strchr(s, '%') != NULL)) 
		return FALSE;
	else return TRUE;
}



/******************************************
NEW FUNC 
- given a new, blank selection
- set the relation pointer
- calculate and assign: 
    - query hash
    - known bits
    - array of query attribute strings
********************************************/
static void setup(Reln r, char * q, Selection new) {

    //char buf[MAXBITS+50];  //*** for debug

    new->rel = r;

    
    Count nvals = nattrs(r);
    new->qvals = (char **) malloc(sizeof(char *) * nvals);
    tupleVals(q, new->qvals);

    ChVecItem * cv = chvec(r);
    
	// get known and unknown bits
    Bits unknown = 0;
	for (int i = 0; i < nvals; i ++) {
		// if the attribute is unknown
        // set unknown bits to 1 (in unknown)
		if (known_attr(new->qvals[i]) == FALSE) {
			for (int j = 0; j <= MAXCHVEC; j ++)  {
                if (cv[j].att == i) unknown = setBit(unknown, j);
            }
		}
	}

    // reverse to get known bits
    new->known = ~unknown;

    // get query hash (with all unknown bits set =  0)
    new->qHash = tupleHash(r, q) &(new->known);

}




/*****************************************************
NEW FUNC
    - Move to selection object to the next MATCHING page 
    - (Given current bucket and current Page)
    - return OK if move successfully
    - return -1 if cannot move anymore (reach the end)

Note: make sure curPage != NULL
******************************************************/
static Status moveToNextPage(Selection s) {

    Status succeed = -1;
    PageID next_ovf = pageOvflow(s->curPage);

    // if there is at least one more overflow page in the same bucket
    if (next_ovf!= NO_PAGE) {
        free(s->curPage);
        s->curPage = getPage(ovflowFile(s->rel), next_ovf);
        s->curtupOffset = 0;
        s->is_ovflow = TRUE;
        succeed = OK;
    } else {
    // if there is no overflow 
    // then move to the next MATCHING BUCKET
        for (int bid = s->curBid + 1; bid <= s->maxBid; bid++) {
            /*  masked bid:
                    s->known: all bits = 1 except unknown bits = 0
                    bid & known: all bits remain the same, but bits at unknown position turn to 0
                Then: compare with queryHash: use mask_bid XOR queryHash (0 = matched)
                IF (1) all (depth + 1) bits matched 
                OR (2) only lowest (depth) bits match, but the page is split pointer or after
                    => grab the page
            */
            Bits masked = ((s->known) & bid)^(s->qHash);
            Count d = depth(s->rel);

            if ( (getLower(masked, d +1 ) == 0) || ( (bid >= splitp(s->rel)) &&  (getLower(masked, d) == 0) ) ) {
                if (s->curPage != NULL) free(s->curPage);
                s->curBid = bid;
                s->curPage = getPage(dataFile(s->rel), bid);
                s->curtupOffset = 0;
                s->is_ovflow = FALSE;
                succeed = OK;
                break;
           }
        }
    }
               
    return succeed;

}



/*************************************************************************
NEW FUNC
- given a query string, a page and a tuple offset
- get the next matching tuple in the page
- and update the offset within in the page
- IF there is a matching tuple within the page: copy it to t + return OK
- if there is no matching tup within the page: *t == NULL +  return -1;
***************************************************************************/
static Status nextMatchTup(Selection s, char **t) {

    Page        p = s->curPage;
    Count       nAttr = nattrs(s->rel);


    if (p->ntuples == 0) return -1;


    char*       c0 = p->data + s->curtupOffset;
    char*       c = p->data + s->curtupOffset + 1;
    char        temp[MAXTUPLEN];
    
   
    Status succeed = -1;

    while ((c < p->data + p->free) && (succeed != OK)){

        if (*c == '\0') {
            memcpy(temp, c0, c - c0 + 1);   //this will also copy *c = '\0' at the end
            //printf("select.c nextMatchTup get tup = "); puts(temp);  //for debug
            //if it is a match, get the result and stop
            if (tupValMatch(nAttr, s->qvals, temp) == TRUE) {
                //copy the result
                *t = (char*) malloc(sizeof(char)* (c - c0 + 1));
                memcpy(*t, temp, c - c0 + 1);  //this will also copy *c = '\0' at the end
                succeed = OK;
            } 
            // move the current offset to next tuple          
            c ++;
            c0 = c;
            s->curtupOffset = c0 - p->data;
        }

        c++;
             
    }
    
    return succeed;

}



/*EDIT */
// take a query string (e.g. "1234,?,abc,?")
// set up a SelectionRep object for the scan

Selection startSelection(Reln r, char *q)
{        
    Selection new = malloc(sizeof(struct SelectionRep));
    assert(new != NULL);

    //set up - record relation
    // and get query hash and known bits (lowest depth+1 bits only)
    // and query values
    setup(r, q, new)
;    
    // get the first page
    if (depth(r) == 0) {
        new->curBid = 0;
        new->maxBid = 0;
    } else {
        // get the minimum matching page ID (set all unknown bits within lowest depth bits in query hash to 0)
        new->curBid = getLower(new->qHash, depth(r));
        //change all unknown bits in query hash to 1 to get maximum (possible) Pid (within lowest depth + 1 bits)
        new->maxBid = getLower((new->qHash|~(new->known)), 1 + depth(r));  
        if (new->maxBid >= npages(r)) new->maxBid = npages(r) - 1;
    }
    new->curPage = getPage(dataFile(r), new->curBid);
    new->curtupOffset = 0; 
    new->is_ovflow = FALSE;


    /*
    printf("\n select.c startSelection NEW SELECTION OBJJECT CREATED \n");    //for debug
    char buf[MAXBITS+50];  
    bitsString(new->qHash, buf); printf("   query hash = %s \n", buf);   //for debug
    bitsString(new->known, buf); printf("    known bit = %s \n", buf);    //for debug
    printf("   depth = %u\n", depth(r));    //for debug
    printf("   minBid = %u\n", new->curBid);    //for debug
    printf("   max possible Bid = %u\n", getLower((new->qHash|~(new->known)), 1 + depth(r))); //for debug
    printf("   maxBid = %u\n", new->maxBid);    //for debug
    */
    

    return new;
}






/********************************************
EDIT - return next matching tuple during a scan
- if there is no matching tuple in s->curPage
- move to another page
- if cannot move anymore 
=> return NULL
**********************************************/

Tuple getNextTuple(Selection s)
{
    char* t = NULL;
    Status try = nextMatchTup(s, &t);
    
    //if (try == OK) printf("select.c getNextTuple found a match tup = '%s' \n", t); //for debug

    while (try != OK) {
        
        Status move = moveToNextPage(s);

        if (move == OK) {
            // printf("select.c getNextTuple move successfully to bid = %u \n", s->curBid);  // for debug
            try = nextMatchTup(s, &t);
        } else {
            // printf("select.c getNextTuple cannot move from bid = %u \n", s->curBid); // for debug
            break;
        }

    }
    
    return t;
}



//EDIT
// clean up a SelectionRep object and associated data
void closeSelection(Selection s)
{
    if (s->curPage != NULL) free(s->curPage);
    if (s->qvals != NULL) freeVals(s->qvals, nattrs(s->rel));
    free(s);
}
