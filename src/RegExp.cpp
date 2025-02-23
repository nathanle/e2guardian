// RegExp class - search text using regular expressions

// For all support, instructions and copyright go to:
// http://e2guardian.org/
// Released under the GPL v2, with the OpenSSL exception described in the README file.

// INCLUDES

#ifdef HAVE_CONFIG_H
#include "e2config.h"
#endif
#include "RegExp.hpp"
#include "Logger.hpp"

#include <cstring>
#include <iostream>


RegResult::RegResult()
    : imatched(false)
{
}


// destructor - free regex if compiled
RegResult::~RegResult()
{
}

// return the i'th match result
std::string RegResult::result(int i)
{
    if (i >= (signed)results.size() || i < 0) { // reality check
        return ""; // maybe exception?
    }
    return results[i];
}

// get the position of the i'th match result in the overall text
unsigned int RegResult::offset(int i)
{
    if (i >= (signed)offsets.size() || i < 0) { // reality check
        return 0; // maybe exception?
    }
    return offsets[i];
}

// get the length of the i'th match
unsigned int RegResult::length(int i)
{
    if (i >= (signed)lengths.size() || i < 0) { // reality check
        return 0; // maybe exception?
    }
    return lengths[i];
}

// how many matches did the last run generate?
int RegResult::numberOfMatches()
{
    int i = (signed)results.size();
    return i;
}

// did it, in fact, generate any?
bool RegResult::matched()
{
    return imatched; // regexp matches only - not search/replace
}

// constructor - set defaults
RegExp::RegExp()
    : reg(),  wascompiled(false)
{
}

// copy constructor
RegExp::RegExp(const RegExp &r)
{
//    rs.results.clear();
    //nrs.offsets.clear();
    //nrs.lengths.clear();
    //unsigned int i;
    //for (i = 0; i < rs.results.size(); i++) {
        //nrs.results.push_back(rs.results[i]);
    //}
    //for (i = 0; i < rs.offsets.size(); i++) {
        //nrs.offsets.push_back(rs.offsets[i]);
    //}
    //for (i = 0; i < rs.lengths.size(); i++) {
        //nrs.lengths.push_back(rs.lengths[i]);
    //}

    //nrs.imatched = rs.imatched;
    wascompiled = r.wascompiled;
    searchstring = r.searchstring;
    if (wascompiled == true) {
#ifdef HAVE_PCRE
        if (regcomp(&reg, searchstring.c_str(), REG_ICASE | REG_EXTENDED | REG_DOTALL) != 0) {
#else
        if (regcomp(&reg, searchstring.c_str(), REG_ICASE | REG_EXTENDED) != 0) {
#endif
            regfree(&reg);
            //rs.imatched = false;
            wascompiled = false;
        }
    }
}

// destructor - free regex if compiled
RegExp::~RegExp()
{
    if (wascompiled) {
        regfree(&reg);
    }
}

// compile the given regular expression
bool RegExp::comp(const char *exp)
{
    if (wascompiled) {
        regfree(&reg);
        wascompiled = false;
    }
    DEBUG_regexp("Compiling ", exp);
#ifdef HAVE_PCRE
    DEBUG_regexp("...with PCRE ");
    if (regcomp(&reg, exp, REG_ICASE | REG_EXTENDED | REG_DOTALL) != 0) { // compile regex
#else
    DEBUG_regexp("...without PCRE ");
    if (regcomp(&reg, exp, REG_ICASE | REG_EXTENDED) != 0) {
#endif
        regfree(&reg);

        return false; // need exception?
    }
    wascompiled = true;
    searchstring = exp;
    return true;
}

// match the given text against the pre-compiled expression
bool RegExp::match(const char *text, RegResult &rs)
{
    rs.results.clear();
    rs.offsets.clear();
    rs.lengths.clear();
    rs.imatched = false;

    if (!wascompiled) {
        return false; // need exception?
    }

    if( text == NULL || *text == 0)   // false if text is empty
        return false;

    char *pos = (char *)text;
    int i;
    unsigned int num_sub_expressions = MAX_SUB_EXPRESSIONS;
    if (reg.re_nsub < num_sub_expressions)
        num_sub_expressions = reg.re_nsub;
    regmatch_t *pmatch = new regmatch_t[num_sub_expressions + 1]; // to hold result
    if (!pmatch) { // if it failed
        delete[] pmatch;
        rs.imatched = false;
        return false;
        // exception?
    }
    if (regexec(&reg, pos, num_sub_expressions + 1, pmatch, 0)) { // run regexdelete[]pmatch;
        delete[] pmatch;
        rs.imatched = false;
        DEBUG_regexp("no match for:", searchstring);
        return false; // if no match
    }
    size_t matchlen;
    char *submatch;
    unsigned int largestoffset;
    int error = 0;
    while (error == 0) {
        largestoffset = 0;
        for (i = 0; i <= (signed)num_sub_expressions; i++) {
            if (pmatch[i].rm_so != -1) {
                matchlen = pmatch[i].rm_eo - pmatch[i].rm_so;
                submatch = new char[matchlen + 1];
                strncpy(submatch, pos + pmatch[i].rm_so, matchlen);
                submatch[matchlen] = '\0';
                rs.results.push_back(std::string(submatch));
                rs.offsets.push_back(pmatch[i].rm_so + (pos - text));
                rs.lengths.push_back(matchlen);
                delete[] submatch;
                if ((pmatch[i].rm_so + matchlen) > largestoffset) {
                    largestoffset = pmatch[i].rm_so + matchlen;
                }
            }
        }
        if (largestoffset > 0) {
            pos += largestoffset;
            error = regexec(&reg, pos, num_sub_expressions + 1, pmatch, REG_NOTBOL);
        } else {
            error = -1;
        }
    }
    rs.imatched = true;
    delete[] pmatch;
    DEBUG_regexp("match(s) for:", searchstring);
    return true; // match(s) found
}

// My own version of STL::search() which seems to be 5-6 times faster
char *RegExp::search(char *file, char *fileend, char *phrase, char *phraseend)
{

    int j, l; // counters
    int p; // to hold precalcuated value for speed
    bool match; // flag
    int qsBc[256]; // Quick Search Boyer Moore shift table (256 alphabet)
    char *k; // pointer used in matching

    int pl = phraseend - phrase; // phrase length
    int fl = (int)(fileend - file) - pl; // file length that could match

    if (fl < pl)
        return fileend; // reality checking
    if (pl > 126)
        return fileend; // reality checking

    // For speed we append the phrase to the end of the memory block so it
    // is always found, thus eliminating some checking.  This is possible as
    // we know an extra 127 bytes have been provided by NaughtyFilter.cpp
    // and also the OptionContainer does not allow phrase lengths greater
    // than 126 chars

    for (j = 0; j < pl; j++) {
        fileend[j] = phrase[j];
    }

    // Next we need to make the Quick Search Boyer Moore shift table

    p = pl + 1;
    for (j = 0; j < 256; j++) { // Preprocessing
        qsBc[j] = p;
    }
    for (j = 0; j < pl; j++) { // Preprocessing
        qsBc[(unsigned char)phrase[j]] = pl - j;
    }

    // Now do the searching!

    for (j = 0;;) {
        k = file + j;
        match = true;
        for (l = 0; l < pl; l++) { // quiv, but faster, memcmp()
            if (k[l] != phrase[l]) {
                match = false;
                break;
            }
        }
        if (match) {
            return (j + file); // match found at offset j (but could be the
            // copy put at fileend)
        }
        j += qsBc[(unsigned char)file[j + pl]]; // shift
    }
    return fileend; // should never get here as it should always match
}
