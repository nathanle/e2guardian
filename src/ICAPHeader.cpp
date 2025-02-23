//ll support, instructions and copyright go to:
// http://e2guardian.org/
// Released under the GPL v2, with the OpenSSL exception described in the README file.

// INCLUDES

#ifdef HAVE_CONFIG_H
#include "e2config.h"
#endif
#include "ICAPHeader.hpp"
#include "Socket.hpp"
#include "OptionContainer.hpp"
#include "FDTunnel.hpp"
#include "Logger.hpp"

#include <unistd.h>
#include <sys/socket.h>
#include <exception>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <cerrno>
#include <zlib.h>

// GLOBALS
extern OptionContainer o;

// IMPLEMENTATION

// set timeout for socket operations
void ICAPHeader::setTimeout(int t)
{
    timeout = t;
}

bool ICAPHeader::setEncapRecs() {
    String t = *pencapsulated;
    DEBUG_icap("pencapsulated is ", t);
    t = t.after(": ");
    while ( t.length() ) {
        String t1 = t.before(",");
        if (t1 == "")
            t1 = t;
        struct encap_rec erec;
        erec.name = t1.before("=");
        String t2 = t1.after("=");
        if (erec.name ==  "" || t2 == "")
            return false;
        erec.value = t2.toInteger();
        encap_recs.push_back(erec);
        t = t.after(", ");
    }
    return true;
}

// reset header object for future use
void ICAPHeader::reset()
{
    HTTPresponse.setType(__HEADER_RESPONSE);
    HTTPresponse.icap = true;
    HTTPrequest.icap = true;
    if (dirty) {
        header.clear();
        encap_recs.clear();
        waspersistent = false;
        ispersistent = false;

        pproxyconnection = NULL;
        pencapsulated= NULL;
        pauthorization = NULL;
        pallow= NULL;
        allow_204 = false;
        pfrom= NULL;
        phost = NULL;
        preferer = NULL;
        puseragent = NULL;
        pxforwardedfor = NULL;
        pkeepalive = NULL;
        pupgrade = NULL;
        pencapsulated = NULL;

        pproxyauthorization = NULL;
        pproxyauthenticate = NULL;
        pcontentdisposition = NULL;
        pclientip= NULL;
        pclientuser= NULL;

        req_hdr_flag = false;
        res_hdr_flag = false;
        out_req_hdr_flag = false;
        out_res_hdr_flag = false;
        out_req_body_flag = false;
        out_res_body_flag = false;
        req_body_flag = false;
        res_body_flag = false;
        opt_body_flag = false;
        null_body_flag = false;
        service_reqmod = false;
        service_resmod = false;
        service_options = false;
        icap_reqmod_service = false;
        icap_resmod_service = false;

        icap_com.filtergroup = -1;
        username = "";

        dirty = false;

    }
}

// *
// *
// * header value and type checks
// *
// *

// grab request type (REQMOD, RESPMOD, OPTIONS)
String ICAPHeader::requestType()
{
    return header.front().before(" ");
}

// grab return code
int ICAPHeader::returnCode()    // does not apply to ICAP ?  May do if we use for ICAP client
{
    if (header.size() > 0) {
        return header.front().after(" ").before(" ").toInteger();
    }else {
        return 0;
    }
}

// *
// *
// * detailed header checks & fixes
// *
// *

void ICAPHeader::checkheader(bool allowpersistent)
{
    if (header.size() > 1) {
    for (std::deque<String>::iterator i = header.begin() + 1; i != header.end(); i++) { // check each line in the headers
        // index headers - try to perform the checks in the order the average browser sends the headers.
        // also only do the necessary checks for the header type (sent/received).
        // Sequencial if else
        if ((phost == NULL) && i->startsWithLower("host:")) {
            phost = &(*i);
        } else if ((pauthorization == NULL) && i->startsWithLower("authorization:")) {
            pauthorization = &(*i);
        } else if ((pallow == NULL) && i->startsWithLower("allow:")) {
            pallow = &(*i);
            allow_204 = pallow->contains("204");
            allow_206 = pallow->contains("206");
        } else if ((pfrom == NULL) && i->startsWithLower("from:")) {
            pfrom = &(*i);
        } else if ((phost == NULL) && i->startsWithLower("host:")) {
            phost = &(*i);
        } else if ((ppreview == NULL) && i->startsWithLower("preview:")) {
            ppreview = &(*i);
            allow_204 = true;
        } else if ((pkeepalive == NULL) && i->startsWithLower("keep-alive:")) {
            pkeepalive = &(*i);
        } else if (i->startsWithLower("encapsulated:")) {
            pencapsulated = &(*i);
            setEncapRecs();
            //i->assign("X-E2G-IgnoreMe: encapuslated always regenerated\r");
        } else if (i->startsWithLower("x-client-ip:")) {
            pclientip = &(*i);
            String t = pclientip->after(": ");
            t.chop();
            setClientIP(t);
        } else if (i->startsWithLower("x-client-username:")) {
            pclientuser = &(*i);
            username = pclientuser->after(": ");
            username.chop();
        } else if (i->startsWithLower("x-icap-e2g:")) {
            String t = *i;
            t.chop();   // remove '\r'
            t = t.after(":");
            icap_com.user = t.before(",");
            t  = t.after(",");
            icap_com.EBG = t.before(",");
            t  = t.after(",");
            icap_com.filtergroup = t.before(",").toInteger();
            t  = t.after(",");
            icap_com.mess_no = t.before(",").toInteger();
            t  = t.after(",");
            icap_com.log_mess_no = t.before(",").toInteger();
            icap_com.mess_string = t.after(",");
        }

        String t2 = *i;
        DEBUG_icap("Header value from ICAP client: ", t2,  "allow_204 is ", allow_204, " allow_206 is ", allow_206 );

        }
    }
}

#ifdef NOTDEF
String ICAPHeader::getUrl()
{
    // Version of URL *with* port is not cached,
    // as vast majority of our code doesn't like
    // port numbers in URLs.
    port = 80;
    bool https = false;
    String hostname;
    String userpassword;
    String answer(header.front().after(" "));
    answer.removeMultiChar(' ');
    if (answer.after(" ").startsWith("ICAP/")) {
        answer = answer.before(" ICAP/");
    } else {
        answer = answer.before(" icap/"); // just in case!
    }
    if (answer.length()) {
        if (answer[0] == '/') { // must be the latter above
            if (phost != NULL) {
                hostname = phost->after(" ");
                hostname.removeWhiteSpace();
                if (hostname.contains(":")) {
                    port = hostname.after(":").toInteger();
                    hostname = hostname.before(":");
                }
                while (hostname.endsWith("."))
                    hostname.chop();
                hostname = "icap://" + hostname;
                answer = hostname + answer;
            }
            header.front() = requestType() + " " + answer + " ICAP/" + header.front().after(" ICAP/");
        } else { // must be in the form GET http://foo.bar:80/ HTML/1.0
            if (!answer.after("://").contains("/")) {
                answer += "/"; // needed later on so correct host is extracted
            }
            String protocol(answer.before("://"));
            hostname = answer.after("://");
            String url(hostname.after("/"));
            url.removeWhiteSpace(); // remove rubbish like ^M and blanks
            if (hostname.endsWith(".")) {
                hostname.chop();
            }
            if (url.length() > 0) {
                url = "/" + url;
            }
            hostname = hostname.before("/"); // extra / was added 4 here
            if (hostname.contains("@")) { // Contains a username:password combo
                userpassword = hostname.before("@");
                hostname = hostname.after("@");
            }
            if (hostname.contains(":")) {
                port = hostname.after(":").toInteger();
                if (port == 0 || port > 65535) {
                    port = (https ? 443 : 80);
                }
                hostname = hostname.before(":"); // chop off the port bit
            }
            while (hostname.endsWith("."))
                hostname.chop();
            if (userpassword.length())
                answer = protocol + "://" + userpassword + "@" + hostname + url;
            else
                answer = protocol + "://" + hostname + url;
        }
    }

	DEBUG_icap("from header url:", answer);

    return answer;
}

String ICAPHeader::url()
{
    return getUrl();
}
#endif

// *
// *
// * URL and Base64 decoding funcs
// *
// *


// turn %xx back into original character
String ICAPHeader::hexToChar(const String &n, bool all)
{
    if (n.length() < 2) {
        return String(n);
    }
    static char buf[2];
    unsigned int a, b;
    unsigned char c;
    a = n[0];
    b = n[1];
    if (a >= 'a' && a <= 'f') {
        a -= 87;
    } else if (a >= 'A' && a <= 'F') {
        a -= 55;
    } else if (a >= '0' && a <= '9') {
        a -= 48;
    } else {
        return String("%") + n;
    }
    if (b >= 'a' && b <= 'f') {
        b -= 87;
    } else if (b >= 'A' && b <= 'F') {
        b -= 55;
    } else if (b >= '0' && b <= '9') {
        b -= 48;
    } else {
        return String("%") + n;
    }
    c = a * 16 + b;
    if (all || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '-')) {
        buf[0] = c;
        buf[1] = '\0';
        return String(buf);
    } else {
        return String("%") + n;
    }
}



// *
// *
// * network send/receive funcs
// *
// *

// send headers out over the given socket
bool ICAPHeader::respond(Socket &sock, String res_code, bool echo, bool encap)
{
	DEBUG_icap("ICAP response starting - RCode ", res_code, " echo is ", echo);
    String l; // for amalgamating to avoid conflict with the Nagel algorithm
    if(echo) {
        if (service_reqmod && !(out_res_hdr_flag || out_req_hdr_flag)) {
            out_req_header = HTTPrequest.stringHeader();
            DEBUG_icap("out_req_header copied from HTTPrequest :", out_req_header);
            out_req_hdr_flag = true;
            out_req_body_flag = req_body_flag;
            if (req_body > 0)
                size_req_body = req_body; // TODO Check this!
        }

        if (service_resmod && !(out_res_hdr_flag)) {
            out_res_header = HTTPresponse.stringHeader();
            DEBUG_icap("out_res_header is ", out_res_header);
            out_res_hdr_flag = true;
            out_res_body_flag = res_body_flag;
            if (res_body > 0) {
                size_res_body = res_body;// TODO Check this!
            }
        }
    }


    l = "ICAP/1.0 " + res_code + "\r\n";
    l += "ISTag:";
    l += ISTag;
    l += "\r\n";

    // add ICAP communication header
    if (service_reqmod && icap_com.filtergroup > -1) {
        l += "X-ICAP-E2G:";
        l += icap_com.user;
        l += ",";
        l += icap_com.EBG;
        l += ",";
        l += std::to_string(icap_com.filtergroup);
        l += ",";
        l += std::to_string(icap_com.mess_no);
        l += ",";
        l += std::to_string(icap_com.log_mess_no);
        l += ",";
        l += icap_com.mess_string;
        l += "\r\n";
    }

    if (encap) {
        // add Encapsulated header logic
        int offset = 0;
        String soffset(offset);
        String sep = " ";
        l += "Encapsulated:";
        if (out_req_hdr_flag && (out_req_header.size() > 0)) {
            l += sep;
            sep = ", ";
            l += "req-hdr=";
            l += soffset;
            offset += out_req_header.size();
            soffset = offset;
        }
        if (out_res_hdr_flag && out_res_header.size() > 0) {
            l += sep;
            sep = ", ";
            l += "res-hdr=";
            l += soffset;
            offset += out_res_header.size();
            soffset = offset;
        }
        if (out_req_body_flag) {
            l += sep;
            l += "req-body=";
            l += soffset;
        } else if (out_res_body_flag) {
            l += sep;
            l += "res-body=";
            l += soffset;
        } else {
            l += sep;
            l += "null-body=";
            l += soffset;
        }
        l += "\r\n";
    }
        l += "\r\n";

    if(encap) {
        // send headers to the output stream
        // need exception for bad write
        if (out_req_hdr_flag) {
            String temp = out_req_header.toCharArray();
            l += temp;
        }

        if (out_res_hdr_flag) {
            String temp = out_res_header.toCharArray();
            l += temp;
        }
    }

    DEBUG_icap("Icap response header is: ", l);
    if (!sock.writeToSocket(l.toCharArray(), l.length(), 0, timeout)) {
        return false;
    }

	DEBUG_icap("Returning from icapheader:respond");

    return true;
}

bool  ICAPHeader::errorResponse(Socket &peerconn, String &res_header, String &res_body) {
    // set IAP outs and then output ICAP header and res_header/body
    out_res_header = res_header;
    out_res_hdr_flag = true;
    out_req_body_flag = false;
    if (res_body.length() > 0) {
        out_res_body = res_body;
        out_res_body_flag = true;
    }

	DEBUG_icap("out_res_header: ",out_res_header);
    DEBUG_icap("out_res_body: ", out_res_body);



    if (!respond(peerconn))
        return false;
    if(out_res_body_flag) {
        if (!peerconn.writeChunk((char *) res_body.toCharArray(), res_body.length(), timeout))
            return false;
        String n;
        if (!peerconn.writeChunk((char*)  n.toCharArray(),0, timeout))
            return false;
    }
    return true;
}

void ICAPHeader::setClientIP(String &ip) {
    clientip = ip;
    HTTPrequest.setClientIP(ip);
}

bool ICAPHeader::in(Socket *sock, bool allowpersistent)
{
    if (dirty)
        reset();
    dirty = true;

#ifdef DEBUG_HIGH
    if(is_response) {
        DEBUG_icap("Start of response ICAPheader:in");
    } else {
        DEBUG_icap("Start of request ICAPheader:in");
    }
#endif

    // the RFCs don't specify a max header line length so this should be
    // dynamic really.  Pointed out (well reminded actually) by Daniel Robbins
    char buff[32768]; // setup a buffer to hold the incomming ICAP line
    String line; // temp store to hold the line after processing
    line = "----"; // so we get past the first while
    bool firsttime = true;
    bool discard = false;
    while (line.length() > 3 || discard) { // loop until the stream is
        // failed or we get to the end of the header (a line by itself)

        // get a line of header from the stream
        bool truncated = false;
        int rc;
        if (firsttime) {

            DEBUG_icap("ICAPheader:in before getLine - timeout: ", timeout);

            rc = sock->getLine(buff, 32768, timeout, NULL, &truncated);
            DEBUG_icap("firstime: ICAPheader:in after getLine ");

            if (rc == 0) return false;
            if (rc < 0 || truncated) {
                ispersistent = false;
                DEBUG_icap("firstime: ICAPheader:in after getLine: rc: ", rc, " truncated: ", truncated );
                return false;
            }
        } else {
            rc = sock->getLine(buff, 32768, timeout, NULL,
                               &truncated);   // timeout reduced to 100ms for lines after first
            if (rc == 0) return false;
            if (rc < 0 || truncated) {
                ispersistent = false;
                DEBUG_icap("not firstime: ICAPheader:in after getLine: rc: ", rc, " truncated: ", truncated );
                return false;        // do not allow non-terminated headers
            }

        }

        if (header.size() > o.header.max_header_lines) {
            DEBUG_icap("ICAP header:size too big =  ", header.size() );
	        E2LOGGER_info(" header:size too big: ", header.size(),  ", see maxheaderlines");
            ispersistent = false;
            return false;
        }

        //       throw std::exception();

        // getline will throw an exception if there is an error which will
        // only be caught by HandleConnection()       ?????????????????????

        if (rc > 0) line = buff;
        else line = "";// convert the line to a String

        if (firsttime) {
            // check first line header
            if (is_response) {
                if (!(line.length() > 11 && line.startsWith("ICAP/") &&
                      (line.after(" ").before(" ").toInteger() > 99))) {
                    if (o.conn.logconerror) {
                        E2LOGGER_error("Server did not respond with ICAP");
                    }
                    DEBUG_icap("Returning from header:in Server did not respond with ICAP length: ", line.length(), " content: ", line );
                    return false;
                }
            } else {
                method = line.before(" ");
                DEBUG_icap("Returning from header:in client requests with ICAP length: ", line.length(), " content: ", line);
                String t = line.after(" ").before(" ");
                DEBUG_icap("Request is ", t, " size: ", line.length(), " content: ", line);
                if (t.startsWith("icap://")) {
                    // valid protocol
                } else {
                    icap_error = "400 Bad Request";
                    DEBUG_icap("Request error is: ", icap_error, " Line: ", t);
                    return false;
                }
                t = t.after("//").after("/");
                if (t == o.icap.icap_reqmod_url) {
                    icap_reqmod_service = true;
                } else if (t == o.icap.icap_resmod_url) {
                    icap_resmod_service = true;
                } else {
                    icap_error = "404 ICAP Service not found";
                }
                if (method == "OPTIONS") {
                    service_options = true;
                } else if (icap_reqmod_service && method == "REQMOD") {
                    service_reqmod = true;
                } else if (icap_resmod_service && method == "RESPMOD") {
                    service_resmod = true;
                } else {
                    icap_error = "405 Method not allowed for service";
                }

                DEBUG_icap("Request method is: ", method, " error?: ", icap_error, " url value: ", t);
            }
        }
            // ignore crap left in buffer from old pconns (in particular, the IE "extra CRLF after POST" bug)
            discard = false;
            if (not(firsttime && line.length() <= 3)) {
                header.push_back(line); // stick the line in the deque that holds the header
            } else {
                discard = true;
                DEBUG_icap("Discarding unwanted bytes at head of request (pconn closed or IE multipart POST bug)");
            }
            firsttime = false;
// End of while
        }


    if (header.size() == 0) {
        DEBUG_icap("ICAP header:size = 0 ");
        return false;
    }

    header.pop_back(); // remove the final blank line of a header
    checkheader(allowpersistent); // sort out a few bits in the header
    DEBUG_icap("checkheader done- ", encap_recs.size(), " encap_recs");
    //now need to get http req and res headers - if present
    HTTPrequest.reset();
    HTTPresponse.reset();
    if(encap_recs.size() > 0) {
        for (std::deque<encap_rec>::iterator i = encap_recs.begin(); i < encap_recs.end(); i++) {
            if (i->name == "req-hdr") {
                req_hdr_flag = HTTPrequest.in(sock);
                if (!req_hdr_flag)
                    return false;
                req_hdr = i->value;
            } else if (i->name == "res-hdr") {
                res_hdr_flag = HTTPresponse.in(sock);
                if (!res_hdr_flag)
                    return false;
                res_hdr = i->value;
            } else if (i->name == "req-body") {
                req_body_flag = true;
                req_body = i->value;
            } else if (i->name == "res-body") {
                res_body_flag = true;
                res_body = i->value;
            } else if (i->name == "null-body") {
                null_body_flag = true;
                null_body = i->value;
            } else if (i->name == "opt-body") {      // may not need this as only sent in respone
                opt_body_flag = true;
                opt_body = i->value;
            }
            // add further checking in here for REQMOD, RESPMOD and OPTIONS
        }
    }


    return true;
}

void ICAPHeader::set_icap_com(std::string &user, String EBG, int &filtergroup, int &mess_no, int &log_mess_no, std::string &mess) {
    icap_com.user = user;
    icap_com.EBG = EBG;
    icap_com.filtergroup = filtergroup;
    icap_com.mess_no = mess_no;
    icap_com.log_mess_no = log_mess_no;
    icap_com.mess_string = mess;
}
