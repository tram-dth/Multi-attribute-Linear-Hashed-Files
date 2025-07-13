// project.c ... project scan functions
// part of Multi-attribute Linear-hashed Files
// Manage creating and using Projection objects
// Last modified by Xiangjun Zai, Mar 2025

#include <stdbool.h>
#include "defs.h"
#include "project.h"
#include "reln.h"
#include "tuple.h"
#include "util.h"



/*NEW*/
struct ProjectionRep {
	Reln        rel;          // need to remember Relation info
    Count       nPA;          //     number of projected attributes
    char**      projected;    // record the projected attributes
                              // in an array of string
                              // use string to save memory
};




/*EDIT*/
// take a string of 1-based attribute indexes (e.g. "1,3,4")
// set up a ProjectionRep object for the Projection
Projection startProjection(Reln r, char *attrstr)
{

    Projection new = malloc(sizeof(struct ProjectionRep));
    assert(new != NULL);

    new->rel = r;

    //get the list of attribute
    // if it is '*' - project all
    if ((attrstr[0] == '*')&&(attrstr[1] == '\0')) {
        new->projected = NULL;
        new->nPA = nattrs(r);
    } else {    
    // if only projected a subset of attributes

        //get number of projected attributes
        Count nPA = 0;
        for (char* c = attrstr; *c != '\0'; c++) { if (*c == ',') nPA += 1; }
        nPA += 1;

        //extract attribute indexes (string)
        new->projected = (char**) malloc(nPA * sizeof(char *));
        assert(new->projected != NULL);
        tupleVals(attrstr, new->projected);

        //number of projected attributes
        new->nPA = nPA; 
                     
    }
   
    return new;
}



// DONE: Implement projection of tuple 't' according to 'p' and store result in 'buf'
void projectTuple(Projection p, Tuple t, char *buf)
{
    memset(buf, '\0', sizeof(char)*MAXTUPLEN);

    // IF PROJECT ALL
    if (p->projected == NULL) {
        memcpy(buf, t, sizeof(char)*strlen(t));
    } else {

    // IF PROJECT SUBSET ONLY
        // get all attributes in tuple 
        Count nvals = nattrs(p->rel);
        char **vals = malloc(nvals*sizeof(char *));
        tupleVals(t, vals);

        //copy projected attrs into buffer string 
        //in query order
        Offset  bfree = 0;
        for (int i = 0; i < p->nPA; i ++) {
            Count j = atoi(p->projected[i]) - 1;
            Count lenA = strlen(vals[j]);
            memcpy(buf + bfree, vals[j], lenA);
            buf[bfree + lenA] = ',';
            bfree = bfree + lenA +1;
        }
        buf[bfree - 1] = '\0';
        freeVals(vals, nvals);
    }    
    
    if (t!= NULL) free(t);

}


//DONE
void closeProjection(Projection p)
{
    if (p->projected != NULL) freeVals(p->projected, p->nPA);
    free(p);
}
