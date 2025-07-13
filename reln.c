// reln.c ... functions on Relations
// part of Multi-attribute Linear-hashed Files
// Credit: John Shepherd
// Last modified by Xiangjun Zai, Mar 2025


#include <math.h>
#include <stdbool.h>


#include "defs.h"
#include "reln.h"
#include "page.h"
#include "tuple.h"
#include "chvec.h"
#include "bits.h"
#include "hash.h"

#define HEADERSIZE (3*sizeof(Count)+sizeof(Offset))


/* NEW FUNCS*/

static void flushToBuck(Reln r, PageID bid, Page buf);
static void lh_split(Reln r);



struct RelnRep {
	Count  nattrs; // number of attributes
	Count  depth;  // depth of main data file
	Offset sp;     // split pointer
    Count  npages; // number of main data pages
    Count  ntups;  // total number of tuples
	ChVec  cv;     // choice vector
	char   mode;   // open for read/write
	FILE  *info;   // handle on info file
	FILE  *data;   // handle on data file
	FILE  *ovflow; // handle on ovflow file
};

// create a new relation (three files)

Status newRelation(char *name, Count nattrs, Count npages, Count d, char *cv)
{
    char fname[MAXFILENAME];
	Reln r = malloc(sizeof(struct RelnRep));
	r->nattrs = nattrs; r->depth = d; r->sp = 0;
	r->npages = npages; r->ntups = 0; r->mode = 'w';
	assert(r != NULL);
	if (parseChVec(r, cv, r->cv) != OK) return ~OK;
	sprintf(fname,"%s.info",name);
	r->info = fopen(fname,"w");
	assert(r->info != NULL);
	sprintf(fname,"%s.data",name);
	r->data = fopen(fname,"w");
	assert(r->data != NULL);
	sprintf(fname,"%s.ovflow",name);
	r->ovflow = fopen(fname,"w");
	assert(r->ovflow != NULL);
	int i;
	for (i = 0; i < npages; i++) addPage(r->data);
	closeRelation(r);
	return 0;
}

// check whether a relation already exists

Bool existsRelation(char *name)
{
	char fname[MAXFILENAME];
	sprintf(fname,"%s.info",name);
	FILE *f = fopen(fname,"r");
	if (f == NULL)
		return FALSE;
	else {
		fclose(f);
		return TRUE;
	}
}

// set up a relation descriptor from relation name
// open files, reads information from rel.info

Reln openRelation(char *name, char *mode)
{
	Reln r;
	r = malloc(sizeof(struct RelnRep));
	assert(r != NULL);
	char fname[MAXFILENAME];
	sprintf(fname,"%s.info",name);
	r->info = fopen(fname,mode);
	assert(r->info != NULL);
	sprintf(fname,"%s.data",name);
	r->data = fopen(fname,mode);
	assert(r->data != NULL);
	sprintf(fname,"%s.ovflow",name);
	r->ovflow = fopen(fname,mode);
	assert(r->ovflow != NULL);
	// Naughty: assumes Count and Offset are the same size
	int n = fread(r, sizeof(Count), 5, r->info);
	assert(n == 5);
	n = fread(r->cv, sizeof(ChVecItem), MAXCHVEC, r->info);
	assert(n == MAXCHVEC);
	r->mode = (mode[0] == 'w' || mode[1] =='+') ? 'w' : 'r';
	return r;
}

// release files and descriptor for an open relation
// copy latest information to .info file

void closeRelation(Reln r)
{
	// make sure updated global data is put in info
	// Naughty: assumes Count and Offset are the same size
	if (r->mode == 'w') {
		fseek(r->info, 0, SEEK_SET);
		// write out core relation info (#attr,#pages,d,sp)
		int n = fwrite(r, sizeof(Count), 5, r->info);
		assert(n == 5);
		// write out choice vector
		n = fwrite(r->cv, sizeof(ChVecItem), MAXCHVEC, r->info);
		assert(n == MAXCHVEC);
	}
	fclose(r->info);
	fclose(r->data);
	fclose(r->ovflow);
	free(r);
}




/******************************************************
NEW FUNC 
// flush a temporary page AS A WHOLE to an existing bucket
// for linear hash, temp page will be full of tuples
// to the appropriate position in bucket (bid)
*******************************************************/
static void flushToBuck(Reln r, PageID bid, Page buf) {

	//first look for primary page 
	//and check if it is empty or not
	//if empty: flush to primary data file
	//if not, flush to the next empty overflow if any
	// or flush to the end of the overflow chain if there is no empty overflow (have to record new overflow info)
	Page 	p = getPage(r->data, bid);
	PageID 	pid = bid; 
	PageID  ovf;
	Bool 	isOvf = FALSE;
	Bool 	done = FALSE;


	while (done == FALSE) {
		// if encounter an empty page in the bucket, flush
		if (p->ntuples == 0) {
			buf->ovflow = p->ovflow;
			if (isOvf == FALSE) 
				putPage(r->data, pid, buf);
			else 
				putPage(r->ovflow, pid, buf);
			done = TRUE;
			free(p);

		} else {
		// move to next page (overflow) in bucket
			ovf = pageOvflow(p);
			if (ovf == NO_PAGE) 
				break;
			else {
				free(p);
				p = getPage(r->ovflow, ovf);
				pid = ovf;
				isOvf = TRUE;
			}
		}
	}

	// if after the loop done == FALSE
	// means reaching the end of bucket without flushing
	// have to create new overflow 
	// record the overflow information in previous page
	if (done == FALSE) {
		ovf = addPage(r->ovflow);
		p->ovflow = ovf;
		if (isOvf == FALSE) 
			putPage(r->data, pid, p);
		else 
			putPage(r->ovflow, pid, p);
		putPage(r->ovflow, ovf, buf);
	}
	
}



/**************************
NEW FUNC - LINEAR HASHING
 - splitting page 
 - relocate tuples
***************************/

static void lh_split(Reln r) {


	//create new splitted page/bucket
	PageID 		newBid = addPage(r->data);
	//have to manually update number of primary pages
	r->npages += 1;


	//for debug
	// printf("    create a new primary page/bucket id = %u \n", newBid);
	// printf("    now relation have %u pages \n", r->npages);

	//then, check and redistribute tuples in bucket pointed to by split pointer (if any)

	char	tup[MAXTUPLEN];
	Bool 	finished = FALSE;
	Bool 	isOvf = FALSE;
	PageID	curPid = r->sp;
	PageID  ovf;
	Page	move = newPage();
	Page 	stay = newPage();
	Page 	p;
	Bits    tupHash;
	Status 	try;

	// parsing through all pages in the split bucket
	while (finished != TRUE) {
		// get the next page
		if (isOvf != TRUE) 
			p = getPage(r->data, curPid);
		else
			p = getPage(r->ovflow, curPid);

		char* c = p->data;
		char* start = p->data;

		// check all tup one by one
		while (c < p->data + p->free) {
			if (*c == '\0') {
				memcpy(tup, start, sizeof(char)*(c - start + 1));
				assert(tup[c-start] == '\0');
				//hash tup and get (depth + 1) lower bit
				//then place tup in either stay or move buffer
				//flush buffer to appropriate bucket when full
				tupHash = tupleHash(r, tup);
				if (bitIsSet(tupHash, r->depth) == 0) {
					try = addToPage(stay, tup);
					// if the buffer for "stay" tuples is full
					if (try != OK) {
						flushToBuck(r, r->sp, stay);
						stay = newPage();
						addToPage(stay, tup);
					}

				} else {
					try = addToPage(move, tup);
					// if the buffer for moved tuples is full
					if (try != OK) {
						flushToBuck(r, newBid, move);  //moved will also get freed here
						move = newPage();
						addToPage(move, tup);
					}
				}
				
				start = c + 1;
			}
			c ++;
		}
		


		// get the next overflow id
		ovf = pageOvflow(p);

		// "empty" the page and flush back to old spot in split bucket 
		// retain the overflow information
		// (will be refilled later when stay buffer is full)

		p->free = 0;
		p->ntuples = 0;
		int dataSize = PAGESIZE - 2*sizeof(Offset) - sizeof(Count);
		memset(p->data, 0, dataSize);
		
		if (isOvf != TRUE) 
			putPage(r->data, curPid, p);
		else
			putPage(r->ovflow, curPid, p);

		
		if (ovf == NO_PAGE) 
			finished = TRUE; 
		else {
			curPid = ovf;
			isOvf = TRUE;
		}
			
	}
	
	// after finish parsing
	//if there is any remaining tup in "move" and "stay" buffers-> flush it out
	
	if (stay != NULL) {
		if (stay->ntuples > 0) flushToBuck(r, r->sp, stay); else free(stay);
	}
	if (move != NULL) {
		if (move->ntuples > 0) flushToBuck(r, newBid, move); else free(move);
	}
	
	
	//printf("reln.c lh_split finishing linear split\n\n");  // for debug

}
	





/************************
EDITED
************************ */

// insert a new tuple into a relation
// returns index of bucket where inserted
// - index always refers to a primary data page
// - the actual insertion page may be either a data page or an overflow page
// returns NO_PAGE if insert fails completely
// TODO: include splitting and file expansion
PageID addToRelation(Reln r, Tuple t)
{
	Bits h, p;

	// NEW 
	// check if need to split
	Count Pcap = floor(102.4/r->nattrs);
	assert(Pcap > 0);

	if ( (r->ntups >0) &&((r->ntups % Pcap) == 0)) {
		
		//printf("reln.c addToRelation time to split at ntups = %u and Pcap = %u \n", r->ntups, Pcap);  //for debug
		
		lh_split(r);

		//printf("reln.c addToRelation successfully split at ntups = %u and Pcap = %u \n", r->ntups, Pcap);  //for debug

		if (r->sp < pow(2, r->depth) - 1) 
			r->sp +=1;
		else {
			r->sp = 0;
			r->depth += 1;
		}
	}



	// hash + insert
	
	h = tupleHash(r,t);
	if (r->depth == 0)
		p = 0;
	else {
		p = getLower(h, r->depth);
		if (p < r->sp) p = getLower(h, r->depth+1);
	}
	
	//char buf[MAXBITS+5]; //*** for debug
	//bitsString(h,buf); printf("hash %s = %s\n",t, buf); //*** for debug
	//bitsString(p,buf); printf("page = %s\n",buf); //*** for debug



	Page pg = getPage(r->data,p);
	if (addToPage(pg,t) == OK) {
		putPage(r->data,p,pg);
		r->ntups++;
		return p;
	}
	// primary data page full
	if (pageOvflow(pg) == NO_PAGE) {
		// add first overflow page in chain
		PageID newp = addPage(r->ovflow);
		pageSetOvflow(pg,newp);
		putPage(r->data,p,pg);
		Page newpg = getPage(r->ovflow,newp);
		// can't add to a new page; we have a problem
		if (addToPage(newpg,t) != OK) return NO_PAGE;
		putPage(r->ovflow,newp,newpg);
		r->ntups++;
		return p;
	}
	else {
		// scan overflow chain until we find space
		// worst case: add new ovflow page at end of chain
		Page ovpg, prevpg = NULL;
		PageID ovp, prevp = NO_PAGE;
		ovp = pageOvflow(pg);
		free(pg);
		while (ovp != NO_PAGE) {
			ovpg = getPage(r->ovflow, ovp);
			if (addToPage(ovpg,t) != OK) {
			    if (prevpg != NULL) free(prevpg);
				prevp = ovp; prevpg = ovpg;
				ovp = pageOvflow(ovpg);
			}
			else {
				if (prevpg != NULL) free(prevpg);
				putPage(r->ovflow,ovp,ovpg);
				r->ntups++;
				return p;
			}
		}
		// all overflow pages are full; add another to chain
		// at this point, there *must* be a prevpg
		assert(prevpg != NULL);
		// make new ovflow page
		PageID newp = addPage(r->ovflow);
		// insert tuple into new page
		Page newpg = getPage(r->ovflow,newp);
        if (addToPage(newpg,t) != OK) return NO_PAGE;
        putPage(r->ovflow,newp,newpg);
		// link to existing overflow chain
		pageSetOvflow(prevpg,newp);
		putPage(r->ovflow,prevp,prevpg);
        r->ntups++;
		return p;
	}
	return NO_PAGE;
}

// external interfaces for Reln data

FILE *dataFile(Reln r) { return r->data; }
FILE *ovflowFile(Reln r) { return r->ovflow; }
Count nattrs(Reln r) { return r->nattrs; }
Count npages(Reln r) { return r->npages; }
Count ntuples(Reln r) { return r->ntups; }
Count depth(Reln r)  { return r->depth; }
Count splitp(Reln r) { return r->sp; }
ChVecItem *chvec(Reln r)  { return r->cv; }


// displays info about open Reln

void relationStats(Reln r)
{
	printf("Global Info:\n");
	printf("#attrs:%d  #pages:%d  #tuples:%d  d:%d  sp:%d\n",
	       r->nattrs, r->npages, r->ntups, r->depth, r->sp);
	printf("Choice vector\n");
	printChVec(r->cv);
	printf("Bucket Info:\n");
	printf("%-4s %s\n","#","Info on pages in bucket");
	printf("%-4s %s\n","","(pageID,#tuples,freebytes,ovflow)");
	for (Offset pid = 0; pid < r->npages; pid++) {
		printf("[%2d]  ",pid);
		Page p = getPage(r->data, pid);
		Count ntups = pageNTuples(p);
		Count space = pageFreeSpace(p);
		Offset ovid = pageOvflow(p);
		printf("(d%d,%d,%d,%d)",pid,ntups,space,ovid);
		free(p);
		while (ovid != NO_PAGE) {
			Offset curid = ovid;
			p = getPage(r->ovflow, ovid);
			ntups = pageNTuples(p);
			space = pageFreeSpace(p);
			ovid = pageOvflow(p);
			printf(" -> (ov%d,%d,%d,%d)",curid,ntups,space,ovid);
			free(p);
		}
		putchar('\n');
	}
}
