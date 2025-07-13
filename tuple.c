// tuple.c ... functions on tuples
// part of Multi-attribute Linear-hashed Files
// Credt: John Shepherd
// Last modified by Xiangjun Zai, 24/03/2024

#include "defs.h"
#include "tuple.h"
#include "reln.h"
#include "hash.h"
#include "chvec.h"
#include "bits.h"
#include "util.h"



/* NEW STATIC FUNCS*/


static Bool strMatch(char* p, char* s);







// return number of bytes/chars in a tuple

int tupLength(Tuple t)
{
	return strlen(t);
}

// reads/parses next tuple in input

Tuple readTuple(Reln r, FILE *in)
{
	char line[MAXTUPLEN];
	if (fgets(line, MAXTUPLEN-1, in) == NULL)
		return NULL;
	line[strlen(line)-1] = '\0';
	// count fields
	// cheap'n'nasty parsing
	char *c; int nf = 1;
	for (c = line; *c != '\0'; c++)
		if (*c == ',') nf++;
	// invalid tuple
	if (nf != nattrs(r)) return NULL;
	return copyString(line); // needs to be free'd sometime
}

// extract values into an array of strings

void tupleVals(Tuple t, char **vals)
{
	char *c = t, *c0 = t;
	int i = 0;
	for (;;) {
		while (*c != ',' && *c != '\0') c++;
		if (*c == '\0') {
			// end of tuple; add last field to vals
			vals[i++] = copyString(c0);
			break;
		}
		else {
			// end of next field; add to vals
			*c = '\0';
			vals[i++] = copyString(c0);
			*c = ',';
			c++; c0 = c;
		}
	}
}

// release memory used for separate attribute values

void freeVals(char **vals, int nattrs)
{
	int i;
    // release memory used for each attribute
	for (i = 0; i < nattrs; i++) free(vals[i]);
    // release memory used for pointer array
    free(vals);
}


/*************************
EDITTED 
**************************/
// hash a tuple using the choice vector
// TODO: actually use the choice vector to make the hash

Bits tupleHash(Reln r, Tuple t)
{	
	Bits hash = 0;
	
	
	
	Count nvals = nattrs(r);
    char **vals = malloc(nvals*sizeof(char *));
    assert(vals != NULL);
    tupleVals(t, vals);
	ChVecItem * cv = chvec(r);

	// NEW
	// CALCULATE MULTI_ATTRIBUTE HASH
	// because 1 attributes might contributes >= 0 bit	
	// loop through attributes first
	for (int i = 0; i < nvals; i ++) {
		Bits attr_hash = 0;
		//loop through the choice vector
		//only calculate attr_hash if it is needed for at least 1 bit 
		for (int j = 0; j < MAXCHVEC; j ++) { 
			if (cv[j].att == i) {
				attr_hash = hash_any((unsigned char *)vals[i],strlen(vals[i]));
				break;
			}
		}
		// get all the bits from attribute i
		for (int j = 0; j < MAXCHVEC; j++) {
			if ((cv[j].att == i) && bitIsSet(attr_hash, cv[j].bit)) hash = setBit(hash, j);
		}
	}

	//char buf[MAXBITS+5];  //*** for debug
	//bitsString(hash,buf);  //*** for debug
	//printf("hash(%s) = %s\n", t, buf);  //*** for debug

    freeVals(vals,nvals);
	return hash;
}





/******************************************************
NEW FUNC
- Given a pattern string and a string (attribute value) 
- Check whether string match pattern 
********************************************************/

static Bool strMatch(char* p, char* s) {
		
	int result;   //0 if match, non-0 if not match

	// count the number of known sections in pattern
	//have to also check if there is known parts at the beginning and end of the pattern string
	//cause they have to occur at the exact position for tuple to be a match (non-flexible positions)

	int plen = strlen(p);
	int slen = strlen(s);
	int n_kn = 0;
	int last = -1;
	int knownStart = -1;
	int knownEnd = -1;

	// get the number of known parts
	for (int i = 0; i < plen; i++)  {
		if (p[i]  == '%')  {
			//to account for >= 2 consecutive '%'
			if (i > last + 1) n_kn += 1;
			last = i;
		} 
	} 

	if (plen > last + 1) {
		if (slen - plen + last + 1 < 0 ) return FALSE;
		n_kn += 1;
		knownEnd = slen - plen + last + 1; 
	}  

	if (p[0] != '%') knownStart = 0;


	

	// if there is no unknown section
	if (last == -1) 
		result = strcmp(p,s);
	else if (n_kn == 0) 
		return TRUE;
	else {
		// if there is at least 1 unknown section
		// have to gather all known sections in pattern in order
		// put them in an array of string (pat)
		char** pat =  malloc(n_kn*sizeof(char *));
		int j = 0;
		last = -1;
		for (int i = 0; i <= plen; i ++) {
			if ((p[i] == '%')||(p[i] == '\0')) {
				// if there is space between last and current '%'
				if (i > last + 1) {
					// add the known section to the pat array
					int slen = i - last - 1;
					pat[j] = (char *)malloc(sizeof(char) * (slen + 1));
					memcpy(pat[j], p + last + 1, slen);
					pat[j][slen] = '\0';
					j++;
				}
				//update last
				last = i;
			}
		}

				
		// now look for all substring occurence in s
		//have to also check if there is known parts at the beginning and end of the pattern string
		//cause they have to occur at the exact position for tuple to be a match (non-flexible positions)

		result = 0;
		j = 0;
		char temp[MAXTUPLEN];

		for (int i = 0; i < n_kn; i ++) {
			//look for first occurence of pat[i] in s
			Bool found = FALSE;
			int sublen = strlen(pat[i]);
			while ((found == FALSE) && (sublen <= slen - j)) {
				//copy the next block of s into temp
				memcpy(temp, s + j, sizeof(char)*sublen);
				temp[sublen] = '\0';
				// if there is a match
				if (strcmp(temp, pat[i]) == 0) {
					// check if the subpattern is an "inflexible" known section at the beginning of pattern
					// or at the end of the pattern
					if ((i == 0) && (knownStart == 0) && (j != 0)) 
						return FALSE;
					else if ((i== n_kn - 1) && (knownEnd >= 0) && (j != knownEnd))
						return FALSE;
					else {
						found = TRUE;
						j += sublen;
					} 
				} else
				// if no match yet 
					j += 1;
			}
	
			// if cannot see any
			// means the string cannot match
			if (found == FALSE) {
				result = -1; 
				break;
			}		
		}
		//free everything that needs to be freed
		freeVals(pat, n_kn);
	}

	// now return
	if (result == 0) 
		return TRUE;
	else
		return FALSE;

}



/**************************************************************
NEW FUNC
- given a tuple, and an array of pattern value strings (char *)
- return the matching result
- can be used by other file
- declared in tuple.h file
*****************************************************************/
Bool tupValMatch(Count nAttr, char **ptv, Tuple t) {
	
	char **v = malloc(nAttr*sizeof(char *));
	tupleVals(t, v);
	Bool match = TRUE;

	for (int i = 0; i < nAttr; i++) {
		if ((ptv[i][0] == '?') && (ptv[i][1] == '\0')) 
			continue;
		else {
			match = strMatch(ptv[i], v[i]);
			if (match != TRUE) break;
		}
	}

	freeVals(v, nAttr);

	return match;

}




/************************
EDITED
*************************/
// compare two tuples (allowing for "unknown" values)
// TODO: actually compare values
Bool tupleMatch(Reln r, Tuple pt, Tuple t)
{
	Count na = nattrs(r);
	char **ptv = malloc(na*sizeof(char *));
	tupleVals(pt, ptv);


	Bool match = tupValMatch(na, ptv, t);

	freeVals(ptv,na); 
	
	//if (match == TRUE) printf("tuple.c tupleMatch FOUND A MATCH: query = '%s' + tup = '%s' \n", pt, t); //for debug

	return match;

}




// puts printable version of tuple in user-supplied buffer

void tupleString(Tuple t, char *buf)
{
	strcpy(buf,t);
}

// release memory used for tuple
void freeTuple(Tuple t)
{
    if (t != NULL) {
        free(t);
    }
}
