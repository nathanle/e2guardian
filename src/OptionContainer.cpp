// For all support, instructions and copyright go to:
// http://e2guardian.org/
// Released under the GPL v2, with the OpenSSL exception described in the README file.

// INCLUDES

#ifdef HAVE_CONFIG_H
#include "e2config.h"
#endif

#include "LOptionContainer.hpp"
#include "OptionContainer.hpp"
#include "RegExp.hpp"
#include "ConfigVar.hpp"
#include "Logger.hpp"
#include "LoggerConfigurator.hpp"

//#include <iostream>
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <cstdlib>
#include <unistd.h>
#include <grp.h>
#include <pwd.h>
#include <fcntl.h>
// GLOBALS


// IMPLEMENTATION

OptionContainer::OptionContainer() {
    log.log_Q = new Queue<std::string>;
    log.RQlog_Q = new Queue<std::string>;
}


OptionContainer::~OptionContainer() {
    reset();
}

void OptionContainer::reset() {
    deletePlugins(dmplugins);
    deletePlugins(csplugins);
    deletePlugins(authplugins);
    language_list.reset();
    net.filter_ip.clear();
    net.filter_ports.clear();
    auth_map.clear();
}

// Purpose: reads all options from the main configuration file (e2guardian.conf)
bool OptionContainer::read_config(std::string &filename, bool readFullConfig) {
    ConfigReader          cr;

    config.conffilename = filename;

    // all sorts of exceptions could occur reading conf files
    try {
        String list_pwd = __CONFDIR;
        list_pwd += "/lists/common";
        if (!cr.readConfig(filename.c_str(), list_pwd))
            return false;

        if (!findProcOptions(cr)) return false;
        if (!findLoggerOptions(cr)) return false;
        if (!findAccessLogOptions(cr)) return false;        

        if (!readFullConfig) {     // pid_filename is the only thing needed for type 0 in order to send signals
            return true;
        }

        if (!findConfigOptions(cr)) return false;
        if (!findDStatOptions(cr)) return false;
        if (!findCertificateOptions(cr)) return false;
        if (!findNetworkOptions(cr)) return false;
        if (!findConnectionHandlerOptions(cr)) return false;
        if (!findContentScannerOptions(cr)) return false;
        if (!findBlockPageOptions(cr)) return false;
        if (!findFilterGroupOptions(cr)) return false;
        if (!findHeaderOptions(cr)) return false;
        if (!findListsOptions(cr)) return false;
        if (!findNaughtyOptions(cr)) return false;

        // soft_restart = (findoptionS("softrestart") == "on"); // Unused

#ifdef ENABLE_EMAIL
        // Email notification patch by J. Gauthier
        mailer = cr.findoptionS("mailer");
#endif

        // to remove in v5.5
        // monitor_helper = findoptionS("monitorhelper");
        // if (monitor_helper == "") {
        //     monitor_helper_flag = false;
        // } else {
        //     monitor_helper_flag = true;
        // }

        monitor_flag_prefix = cr.findoptionS("monitorflagprefix");
        if (monitor_flag_prefix == "") {
            monitor_flag_flag = false;
        } else {
            monitor_flag_flag = true;
        }




        if (findoptionS("usecustombannedimage") == "off") {
            use_custom_banned_image = false;
        } else {
            use_custom_banned_image = true;
            custom_banned_image_file = findoptionS("custombannedimagefile");
            if (custom_banned_image_file.empty()) {
                custom_banned_image_file = __DATADIR;
                custom_banned_image_file += "/transparent1x1.gif";
            }
            banned_image.read(custom_banned_image_file.c_str());
        }

        if (findoptionS("usecustombannedflash") == "off") {
            use_custom_banned_flash = false;
        } else {
            use_custom_banned_flash = true;
            custom_banned_flash_file = findoptionS("custombannedflashfile");

            if (custom_banned_flash_file.empty()) {
                custom_banned_flash_file = __DATADIR;
                custom_banned_flash_file += "/blockedflash.swf";
            }
            banned_flash.read(custom_banned_flash_file.c_str());
        }


        if (!no_proxy) {
            proxy_port = findoptionI("proxyport");
            if (proxy_port == 0) proxy_port = 3128;
            if (!realitycheck(proxy_port, 1, 65535, "proxyport")) {
                return false;
            } // etc
        }

        // multiple listen IP support
        filter_ip = findoptionMD("filterip",":");
        if (filter_ip.empty()) filter_ip.push_back("");
        if (filter_ip.size() > 127) {
            E2LOGGER_error("Can not listen on more than 127 IPs");
            return false;
        }
        // multiple check IP support - used for loop checking
        check_ip = findoptionMD("checkip",":");
        if (check_ip.size() > 127) {
            E2LOGGER_error("Can not check on more than 127 IPs");
            return false;
        }
        if (check_ip.empty()) {
            String t = "127.0.0.1";
            check_ip.push_back(t);
        }

        filter_ports = findoptionMD("filterports",":");
        if (filter_ports.empty())
            filter_ports.push_back("8080");
        if (map_ports_to_ips and filter_ports.size() != filter_ip.size()) {
            E2LOGGER_error("filterports (", filter_ports.size(), ") must match number of filterips (", filter_ip.size(),
                           ")");
            return false;
        }
        filter_port = filter_ports[0].toInteger();
        if (!realitycheck(filter_port, 1, 65535, "filterport[0]")) {
            return false;
        }

        TLS_filter_ports = findoptionMD("tlsfilterports",":");
        TLSproxyCN = findoptionS("tlsproxycn");
        if (TLSproxyCN.empty())
            TLSproxyCN = server_name;
        {
            String temp = TLSproxyCN;
            int tno = temp.before(".").toInteger();
            if ( tno > 0 && tno < 256 ) {
                TLSproxyCN_is_ip = true;
            }
        }

        transparenthttps_port = findoptionI("transparenthttpsport");
        if (!realitycheck(transparenthttps_port, 0, 65535, "transparenthttpsport")) {
            return false;
        }

        icap_port = findoptionI("icapport");
        if (!realitycheck(filter_port, 0, 65535, "icapport")) {
            return false;
        }

        if (icap_port > 0) {   // add non-plugin auth for ICAP
            SB_entry_map sen;
            sen.entry_function = "auth_icap";
            sen.entry_id = ENT_STORYA_AUTH_ICAP;
            auth_entry_dq.push_back(sen);
        }

        icap_reqmod_url = findoptionS("icapreqmodurl");
        if (icap_reqmod_url == "")
            icap_reqmod_url = "request";

        icap_resmod_url = cr.findoptionS("icapresmodurl");
        if (icap_resmod_url == "")
            icap_resmod_url = "response";


#ifdef SG_LOGFORMAT
        prod_id.assign(cr.findoptionS("productid"));
        if (prod_id.empty())
            // SG '08
            prod_id.assign("2");
#endif

        log_ssl_errors = cr.findoptionB("logsslerrors");
        reverse_lookups = cr.findoptionB("reverseaddresslookups");
        recheck_replaced_urls = cr.findoptionB("recheckreplacedurls");
        use_xforwardedfor = cr.findoptionB("usexforwardedfor");

        storyboard_location = cr.findoptionS("preauthstoryboard");
        if (storyboard_location.empty()) {
            storyboard_location = __CONFDIR;
            storyboard_location += "/preauth.story";
        }

        per_room_directory_location = cr.findoptionS("perroomdirectory");

        if (!loadDMPlugins(cr)) {
            E2LOGGER_error("Error loading DM plugins");
            return false;
        }

        if (content.contentscanning) {
            if (!loadCSPlugins(cr)) {
                E2LOGGER_error("Error loading CS plugins");
                return false;
            }
        }

        if (!loadAuthPlugins(cr)) {
            E2LOGGER_error("Error loading auth plugins");
            return false;
        }

        // check if same number of auth-plugin as ports if in
        //     authmaptoport mode
        if (net.map_auth_to_ports && (net.filter_ports.size() > 1)
            && (net.filter_ports.size() != authplugins.size())) {
            E2LOGGER_error("In mapauthtoports mode you need to setup one port per auth plugin");
            return false;
        }

        // map port numbers to auth plugin names
        for (unsigned int i = 0; i < authplugins.size(); i++) {
            AuthPlugin *tmpPlugin = (AuthPlugin *) authplugins[i];
            String tmpStr = tmpPlugin->getPluginName();

            if ((!net.map_auth_to_ports) || net.filter_ports.size() == 1)
                auth_map[i] = tmpStr;
            else
                auth_map[net.filter_ports[i].toInteger()] = tmpStr;
        }

        // if the more than one port is being used, validate the combination of auth plugins
        if (authplugins.size() > 1 and net.filter_ports.size() > 1 and net.map_auth_to_ports) {
            std::deque<Plugin *>::iterator it = authplugins.begin();
            String firstPlugin;
            while (it != authplugins.end()) {
                AuthPlugin *tmp = (AuthPlugin *) *it;
                if (tmp->getPluginName().startsWith("proxy-basic")) {
                    E2LOGGER_error("Proxy auth is not possible with multiple ports");
                    return false;
                }
                if (tmp->getPluginName().startsWith("proxy-ntlm") && (tmp->isTransparent() == false)) {
                    E2LOGGER_error("Non-transparent NTLM is not possible with multiple ports");
                    return false;
                }
                if (it == authplugins.begin())
                    firstPlugin = tmp->getPluginName();
                else {
                    if ((firstPlugin == tmp->getPluginName()) and (!tmp->getPluginName().startsWith("ssl-core"))) {
                        E2LOGGER_error("Auth plugins can not be the same");
                        return false;
                    }
                }
                *it++;
            }
        }


        if ( cr.findoptionB("authrequiresuserandgroup") && (authplugins.size() > 1))
            auth_requires_user_and_group = true;


        if (cert.enable_ssl) {
            if (!cert.generate_ca_certificate()) return false;
        }

    } catch (std::exception &e) {
        E2LOGGER_error(e.what());
        return false;
    }
    DEBUG_config("Done: read Configfile: ", filename);
    return true;
}


bool OptionContainer::readinStdin() {
    DEBUG_trace("");

    if (!std::cin.good()) {
        E2LOGGER_error("Error reading stdin");
        return false;
    }
    std::string linebuffer;
    String temp;
    while (!std::cin.eof()) {
        getline(std::cin, linebuffer);
        DEBUG_debug("Line in: ", linebuffer);
        if (linebuffer.length() < 2)
            continue; // its jibberish

        temp = linebuffer.c_str();
        bool site_list = false;
        bool url_list = false;
        if (linebuffer[0] == '#') {
            if (temp.startsWith("#SITELIST:"))
                site_list = true;
            else if (temp.startsWith("#URLLIST:"))
                url_list = true;
            else
                continue;
            String param = temp.after(":");
            String nm, fpath;
            String t = param;
            bool startswith;
            t.removeWhiteSpace();
            t += ",";
            while (t.length() > 0) {
                if (t.startsWith("name=")) {
                    nm = t.after("=").before(",");
                } else if (t.startsWith("path=")) {
                    fpath = t.after("=").before(",");
                }
                t = t.after(",");
            }
            if (!fpath.startsWith("memory:")) {
                // syntax error
                return false;
            }
            if (nm.length() == 0) {
                // syntax error
                return false;
            }
            if (site_list)
                startswith = false;
            else
                startswith = true;

            int rc = lm.newItemList(fpath.c_str(), "", startswith, 1, true);
            if (rc < 0)
                return false;
            lm.l[rc]->doSort(url_list);
            if (site_list)
                lists.sitelist_dq.push_back(param);
            else
                lists.urllist_dq.push_back(param);
        }
    }
    return true;
}

bool OptionContainer::findAccessLogOptions(ConfigReader &cr)
{

    log.dns_user_logging_domain = cr.findoptionS("dnsuserloggingdomain");
    log.log_header_value = cr.findoptionS("logheadervalue");

    // default of unlimited no longer allowed as could cause buffer overflow
    log.max_logitem_length = cr.findoptionIWithDefault("maxlogitemlength", 10, 32000, 2000);

    log.log_level = cr.findoptionIWithDefault("loglevel", 0, 3, 3);
    log.log_file_format = cr.findoptionIWithDefault("logfileformat", 1, 8, 1);

    log.anonymise_logs = cr.findoptionB("anonymizelogs");
    log.log_ad_blocks = cr.findoptionB("logadblocks");
    log.log_timestamp = cr.findoptionB("logtimestamp");
    log.log_user_agent = cr.findoptionB("loguseragent");
    log.use_dash_for_blanks = cr.findoptionB("usedashforblank");
    log.log_client_host_and_ip = cr.findoptionB("logclientnameandip");

    log.log_exception_hits = cr.findoptionIWithDefault("logexceptionhits", 0, 2, 2);

    log.log_client_hostnames = cr.findoptionB("logclienthostnames");
    conn.reverse_client_ip_lookups = log.log_client_hostnames;  // TODO: reverse_client_ip_lookups could be done in log thread

    log.logid_1 = cr.findoptionS("logid1");
    if (log.logid_1.empty())
        log.logid_1 = "-";
    log.logid_2 = cr.findoptionS("logid2");
    if (log.logid_2.empty())
        log.logid_2 = "-";

    return true;
}

bool OptionContainer::findBlockPageOptions(ConfigReader &cr)
{

    block.reporting_level = cr.findoptionIWithDefault("reportinglevel", -1, 3, 3);

    if ( !cr.findoptionB("usecustombannedimage") ) {
        block.use_custom_banned_image = false;
    } else {
        block.use_custom_banned_image = true;
        block.custom_banned_image_file = cr.findoptionS("custombannedimagefile");
        if (block.custom_banned_image_file.empty()) {
            block.custom_banned_image_file = __DATADIR;
            block.custom_banned_image_file += "/transparent1x1.gif";
        }
        block.banned_image.read(block.custom_banned_image_file.c_str());
    }

    if ( !cr.findoptionB("usecustombannedflash") ) {
        block.use_custom_banned_flash = false;
    } else {
        block.use_custom_banned_flash = true;
        block.custom_banned_flash_file = cr.findoptionS("custombannedflashfile");

        if (block.custom_banned_flash_file.empty()) {
            block.custom_banned_flash_file = __DATADIR;
            block.custom_banned_flash_file += "/blockedflash.swf";
        }
        block.banned_flash.read(block.custom_banned_flash_file.c_str());
    }
    return true;
}

bool OptionContainer::findCertificateOptions(ConfigReader &cr)
{
    cert.ssl_certificate_path = cr.findoptionS("sslcertificatepath") + "/";
    if (cert.ssl_certificate_path == "/") {
        cert.ssl_certificate_path = ""; // "" will enable default openssl certs
    }

    cert.enable_ssl = cr.findoptionB("enablessl");

    if (cert.enable_ssl) {
        bool ret = true;
        if (cr.findoptionB("useopensslconf")) {
            cert.use_openssl_conf = true;
            cert.openssl_conf_path = cr.findoptionS("opensslconffile");
            cert.have_openssl_conf = (cert.openssl_conf_path == "");
        } else {
            cert.use_openssl_conf = false;
        };

        cert.ca_certificate_path = cr.findoptionS("cacertificatepath");
        if (cert.ca_certificate_path == "") {
            E2LOGGER_error("cacertificatepath is required when ssl is enabled");
            ret = false;
        }

        cert.ca_private_key_path = cr.findoptionS("caprivatekeypath");
        if (cert.ca_private_key_path == "") {
            E2LOGGER_error("caprivatekeypath is required when ssl is enabled");
            ret = false;
        }

        cert.cert_private_key_path = cr.findoptionS("certprivatekeypath");
        if (cert.cert_private_key_path == "") {
            E2LOGGER_error("certprivatekeypath is required when ssl is enabled");
            ret = false;
        }

        cert.generated_cert_path = cr.findoptionS("generatedcertpath") + "/";
        if (cert.generated_cert_path == "/") {
            E2LOGGER_error("generatedcertpath is required when ssl is enabled");
            ret = false;
        }

        time_t def_start = 1417872951; // 6th Dec 2014
        time_t ten_years = 315532800;
        cert.gen_cert_start = cr.findoptionI("generatedcertstart");
        if (cert.gen_cert_start < def_start)
            cert.gen_cert_start = def_start;
        cert.gen_cert_end = cr.findoptionI("generatedcertend");
        if (cert.gen_cert_end < cert.gen_cert_start)
            cert.gen_cert_end = cert.gen_cert_start + ten_years;

        cert.set_cipher_list = cr.findoptionS("setcipherlist");
        if (cert.set_cipher_list == "")
            cert.set_cipher_list = "HIGH:!ADH:!MD5:!RC4:!SRP:!PSK:!DSS";

        if (ret) {
    #ifdef NODEF
            cert.ca = new CertificateAuthority(cert.ca_certificate_path.c_str(),
                                cert.ca_private_key_path.c_str(),
                                cert.cert_private_key_path.c_str(),
                                cert.generated_cert_path.c_str(),
                                cert.gen_cert_start, cert.gen_cert_end);
    #endif
            return true;
        } else {
            return false;
        }
    }
    return true;
}

bool OptionContainer::findConfigOptions(ConfigReader &cr)
{
    String t = cr.findoptionS("languagedir") + "/";
    if (t == "/") {
        t = __DATADIR;
        t += "/languages";
    }
    config.languagepath = t + "/" + cr.findoptionS("language") + "/";

    std::string language_list_location(config.languagepath + "messages");
    if (!language_list.readLanguageList(language_list_location.c_str())) {
        return false;
    } // messages language file

    return true;
}

bool OptionContainer::findConnectionHandlerOptions(ConfigReader &cr)
{
    conn.logconerror = cr.findoptionB("logconnectionhandlingerrors");

    conn.use_original_ip_port = cr.findoptionB("useoriginalip");
    conn.reverse_client_ip_lookups = cr.findoptionB("reverseclientiplookups");

    if ((conn.internal_test_url = cr.findoptionS("internaltesturl")).empty()) {
        conn.internal_test_url = "internal.test.e2guardian.org";
    }

    if ((conn.internal_status_url = cr.findoptionS("internalstatusurl")).empty()) {
        conn.internal_status_url = "internal.status.e2guardian.org";
    }

    return true;
}

bool OptionContainer::findContentScannerOptions(ConfigReader &cr)
{

    content.max_content_filecache_scan_size = cr.findoptionIWithDefault("maxcontentfilecachescansize", 0, 0, 20000);
    content.max_content_filecache_scan_size *= 1024;

    content.max_content_ramcache_scan_size = cr.findoptionIWithDefault("maxcontentramcachescansize", 0, 0, 2000);
    content.max_content_ramcache_scan_size *= 1024;
    if (content.max_content_ramcache_scan_size == 0) {
        content.max_content_ramcache_scan_size = content.max_content_filecache_scan_size;
    }

    content.max_content_filter_size = cr.findoptionIWithDefault("maxcontentfiltersize", 0, 0, 2048);
    content.max_content_filter_size *= 1024;

    content.contentscanning = cr.findoptionM("contentscanner")->size() > 0;
    if (content.contentscanning) {

        if (content.max_content_filter_size > content.max_content_ramcache_scan_size) {
            E2LOGGER_error("maxcontentfiltersize can not be greater than maxcontentramcachescansize");
            return false;
        }
        if (content.max_content_ramcache_scan_size > content.max_content_filecache_scan_size) {
            E2LOGGER_error("maxcontentramcachescansize can not be greater than maxcontentfilecachescansize");
            return false;
        }

        content.trickle_delay = cr.findoptionIWithDefault("trickledelay", 1, 0, 10);
        content.initial_trickle_delay = cr.findoptionIWithDefault("initialtrickledelay", 1, 0, 20);

        content.content_scanner_timeout_sec = cr.findoptionIWithDefault("contentscannertimeout", 1, 0, 60);
        if (content.content_scanner_timeout_sec > 0)
            content.content_scanner_timeout = content.content_scanner_timeout_sec * 1000;
        else {
            content.content_scanner_timeout = net.pcon_timeout;
            content.content_scanner_timeout_sec = net.pcon_timeout_sec;
        }

    }

    // this needs to be known before loading CS plugins,
    // because ClamAV plugin makes use of it during init()
    content.download_dir = cr.findoptionS("filecachedir");
    if (content.download_dir.empty()) {
        content.download_dir = "/tmp";
    }
    content.delete_downloaded_temp_files = cr.findoptionB("deletedownloadedtempfiles");

    return true;

}


bool OptionContainer::findFilterGroupOptions(ConfigReader &cr)
{
    filter.filter_groups = cr.findoptionI("filtergroups");
    if (filter.filter_groups == 0) filter.filter_groups = 1;

    filter.numfg = filter.filter_groups; 

    filter.default_fg = cr.findoptionIWithDefault("defaultfiltergroup", 1, filter.filter_groups, 1);
    filter.default_fg--;    // zero based index

    filter.default_trans_fg = cr.findoptionIWithDefault("defaulttransparentfiltergroup", 1, filter.filter_groups, 1);
    filter.default_trans_fg--;

    filter.default_icap_fg = cr.findoptionIWithDefault("defaulticapfiltergroup", 1, filter.filter_groups, 1);
    filter.default_icap_fg--;

    return true;
}

bool OptionContainer::findHeaderOptions(ConfigReader &cr)
{
    header.forwarded_for = cr.findoptionB("forwardedfor");
    if (cr.findoptionB("addforwardedfor")) {
        header.forwarded_for = true;
    }

    header.max_header_lines = cr.findoptionIWithDefault("maxheaderlines", 10, 250, 50);

    return true;
}

bool OptionContainer::findListsOptions(ConfigReader &cr)
{
    lists.force_quick_search = cr.findoptionB("forcequicksearch");
    lists.abort_on_missing_list = cr.findoptionB("abortiflistmissing");
    lists.search_sitelist_for_ip = cr.findoptionB("searchsitelistforip");

    lists.iplist_dq = *cr.findoptionM("iplist");
    lists.sitelist_dq = *cr.findoptionM("sitelist");
    lists.ipsitelist_dq = *cr.findoptionM("ipsitelist");
    lists.urllist_dq = *cr.findoptionM("urllist");
    lists.regexpboollist_dq = *cr.findoptionM("regexpboollist");
    lists.maplist_dq = *cr.findoptionM("maplist");
    lists.ipmaplist_dq = *cr.findoptionM("ipmaplist");

    return true;
}


bool OptionContainer::findLoggerOptions(ConfigReader &cr)
{
    LoggerConfigurator loggerConf(&e2logger);

    {
        std::string temp = cr.findoptionS("set_info");
        if (!temp.empty()) {
            if (!loggerConf.configure(LoggerSource::info, temp))
                return false;
        }
    }

    {
        std::string temp = cr.findoptionS("set_error");
        if (!temp.empty()) {
            if (!loggerConf.configure(LoggerSource::error, temp))
                return false;
        }
    }

    {
        std::string temp = cr.findoptionS("set_warning");
        if (!temp.empty()) {
            if (!loggerConf.configure(LoggerSource::warning, temp))
                return false;
        }
    }

    {
        if (cr.findoptionB("logsyslog")) {
            if ((log.name_suffix = cr.findoptionS("namesuffix")) == "") {
                log.name_suffix = "";
            }
            e2logger.setSyslogName(config.prog_name + log.name_suffix);
        }     
    }

    {
        String temp = cr.findoptionS("set_accesslog");
        if (!temp.empty()) {
            if (!loggerConf.configure(LoggerSource::accesslog, temp))
                return false;
        } else {
                log_location = cr.findoptionS("loglocation");
                if (log_location.empty()) {
                    log_location = __LOGLOCATION;
                    log_location += "/access.log";
                }
                if (!e2logger.setLogOutput(LoggerSource::accesslog, LoggerDestination::file, log_location))
                    return false;
            }
    }

    log.debug_format = cr.findoptionIWithDefault("debugformat", 1, 6, 1);
    loggerConf.debugformat(log.debug_format);

    if (cr.findoptionB("tag_logs")) {
        e2logger.setFormat(LoggerSource::accesslog, false, true, false, false, false);
        e2logger.setFormat(LoggerSource::requestlog, false, true, false, false, false);
    }

    {
        String temp = cr.findoptionS("set_requestlog");
        if (!temp.empty()) {
            if (!loggerConf.configure(LoggerSource::requestlog, temp))
                return false;
            log_requests = true;
        } else {
            if ((RQlog_location = cr.findoptionS("rqloglocation")) == "") {
                log_requests = false;
            } else {
                log_requests = true;
                if (!e2logger.setLogOutput(LoggerSource::requestlog, LoggerDestination::file, RQlog_location))
                    return false;
            }
        }
    }

    {
        dstat.dstat_log_flag = false;
        String temp = cr.findoptionS("set_dstatslog");
        if (!temp.empty()) {
            if (!loggerConf.configure(LoggerSource::dstatslog, temp))
                return false;
            dstat.dstat_log_flag = true;
        } else {
            if ((dstat.dstat_location = cr.findoptionS("dstatlocation")) == "") {
                dstat.dstat_log_flag = false;
            } else {
                dstat.dstat_log_flag = true;
                if (!e2logger.setLogOutput(LoggerSource::dstatslog, LoggerDestination::file, dstat.dstat_location))
                    return false;
            }
        }
    }

    {
        std::string temp = cr.findoptionS("set_storytrace");
        if (!temp.empty()) {
            if (!loggerConf.configure(LoggerSource::storytrace, temp))
                return false;
        }
    }

    {
        if (cr.findoptionB("storyboardtrace")) {
            log.SB_trace = true;
            e2logger.enable(LoggerSource::storytrace);
        } else {
            log.SB_trace = false;
        }
    }

    if ( log.SB_trace ) {
        DEBUG_config("Enable Storyboard tracing !!");
        e2logger.enable(LoggerSource::story);
    }


    {
        std::deque<String> *temp = cr.findoptionM("debuglevel");
        if ( temp && !temp->empty()) {
            for (std::deque<String>::iterator i = temp->begin(); i != temp->end(); i++) {
                loggerConf.debuglevel(*i);
            }
        }
    }

    return true;

}

bool OptionContainer::findNaughtyOptions(ConfigReader &cr)
{
    if (cr.findoptionS("weightedphrasemode").empty()) {
        naughty.weighted_phrase_mode = 2;
    } else {
        naughty.weighted_phrase_mode = cr.findoptionIWithDefault("weightedphrasemode", 0, 2, 2);
    }

    if (cr.findoptionS("phrasefiltermode").empty()) {
        naughty.phrase_filter_mode = 2;
    } else {
        naughty.phrase_filter_mode = cr.findoptionIWithDefault("phrasefiltermode", 0, 3, 2);
    }

    naughty.preserve_case = cr.findoptionIWithDefault("preservecase", 0, 2, 0);

    naughty.hex_decode_content = (cr.findoptionB("hexdecodecontent"));

    naughty.show_weighted_found = (cr.findoptionB("showweightedfound"));
    naughty.show_all_weighted_found =  (cr.findoptionB("showallweightedfound"));
    if (naughty.show_all_weighted_found)
        naughty.show_weighted_found = true;

    return true;
}

bool OptionContainer::findNetworkOptions(ConfigReader &cr)
{
    net.server_name = cr.findoptionS("servername");
    if (net.server_name == "") {
        char sysname[256];
        int r;
        r = gethostname(sysname, 256);
        if (r == 0) {
            net.server_name = sysname;
        }
    }

    net.connect_timeout_sec = cr.findoptionIWithDefault("connecttimeout", 1, 100, 5);
    net.connect_timeout = net.connect_timeout_sec * 1000;

    net.connect_retries = cr.findoptionIWithDefault("connectretries", 1, 100, 1);

    net.proxy_ip = cr.findoptionI("proxyip");
    if (!net.no_proxy) {
        net.proxy_port = cr.findoptionIWithDefault("proxyport", 1, 65535, 3128);
    }

    net.proxy_timeout_sec = cr.findoptionIWithDefault("proxytimeout", 5, 100, 55);
    net.proxy_timeout = net.proxy_timeout_sec * 1000;

    net.pcon_timeout_sec = cr.findoptionIWithDefault("pcontimeout", 5, 300, 55);
    net.pcon_timeout = net.pcon_timeout_sec * 1000;

    net.exchange_timeout_sec = cr.findoptionIWithDefault("proxyexchange", 5, 300, 61);
    net.exchange_timeout = net.exchange_timeout_sec * 1000;

    net.map_ports_to_ips = cr.findoptionB("mapportstoips");    // to be removed in v5.5
    net.map_auth_to_ports = cr.findoptionB("mapauthtoports");  // to be removed in v5.5

    // multiple listen IP support
    net.filter_ip = *cr.findoptionM("filterip");
    if (net.filter_ip.empty()) 
        net.filter_ip.push_back("");
    if (net.filter_ip.size() > 127) {
        E2LOGGER_error("Can not listen on more than 127 IPs");
        return false;
    }
    // multiple check IP support - used for loop checking
    net.check_ip = *cr.findoptionM("checkip");
    if (net.check_ip.size() > 127) {
        E2LOGGER_error("Can not check on more than 127 IPs");
        return false;
    }
    if (net.check_ip.empty()) {
        net.check_ip.push_back("127.0.0.1");
    }

    net.filter_ports = *cr.findoptionM("filterports");
    if (net.filter_ports.empty())
        net.filter_ports.push_back("8080");
    if (net.map_ports_to_ips and net.filter_ports.size() != net.filter_ip.size()) {
        E2LOGGER_error("filterports (", net.filter_ports.size(), ") must match number of filterips (", net.filter_ip.size(),
                        ")");
        return false;
    }
    net.filter_port = net.filter_ports[0].toInteger();
    if (!realitycheck(net.filter_port, 1, 65535, "filterport[0]")) {
        return false;
    }

    net.TLS_filter_ports = *cr.findoptionM("tlsfilterports");
    net.TLSproxyCN = cr.findoptionS("tlsproxycn");
    if (net.TLSproxyCN.empty())
        net.TLSproxyCN = net.server_name;
    {
        String temp = net.TLSproxyCN;
        int tno = temp.before(".").toInteger();
        if ( tno > 0 && tno < 256 ) {
            net.TLSproxyCN_is_ip = true;
        }
    }


    net.transparenthttps_port = cr.findoptionI("transparenthttpsport");
    if (!realitycheck(net.transparenthttps_port, 0, 65535, "transparenthttpsport")) {
        return false;
    }

    net.icap_port = cr.findoptionI("icapport");
    if (!realitycheck(net.icap_port, 0, 65535, "icapport")) {
        return false;
    }

    if (net.icap_port > 0) {   // add non-plugin auth for ICAP
        SB_entry_map sen;
        sen.entry_function = "auth_icap";
        sen.entry_id = ENT_STORYA_AUTH_ICAP;
        auth_entry_dq.push_back(sen);
    }


    net.xforwardedfor_filter_ip = *cr.findoptionM("xforwardedforfilterip");

    return true;

}

bool OptionContainer::findDStatOptions(ConfigReader &cr)
{
    dstat.dstat_interval = cr.findoptionI("dstatinterval");
    if (dstat.dstat_interval == 0) {
        dstat.dstat_interval = 300; // 5 mins
    }

    dstat.stats_human_readable = cr.findoptionB("statshumanreadable");

    if (cr.findoptionB("tag_dstatlog")) {
        e2logger.setFormat(LoggerSource::dstatslog, false, true, false, false, false);
    }

    return true;
}

bool OptionContainer::findProcOptions(ConfigReader &cr)
{

    proc.no_daemon = cr.findoptionB("nodaemon");

    if (cr.findoptionB("dockermode")) {
        proc.no_daemon = true;
        e2logger.setDockerMode();
    }

    if ((proc.pid_filename = cr.findoptionS("pidfilename")) == "") {
        proc.pid_filename = std::string(__PIDDIR) + "/e2guardian.pid";
    }

    if ((proc.daemon_user_name = cr.findoptionS("daemonuser")) == "") {
        proc.daemon_user_name = __PROXYUSER;
    }

    if ((proc.daemon_group_name = cr.findoptionS("daemongroup")) == "") {
        proc.daemon_group_name = __PROXYGROUP;
    }

    proc.http_workers = cr.findoptionIWithDefault("httpworkers", 20, 20000, 500);

    return true;

}


std::string OptionContainer::findoptionS(const char *option) {
    // findoptionS returns a found option stored in the deque
    String temp;
    String temp2;
    String o(option);

    for (std::deque<std::string>::iterator i = conffile.begin(); i != conffile.end(); i++) {
        if ((*i).empty())
            continue;
        temp = (*i).c_str();
        temp2 = temp.before("=");
        while (temp2.endsWith(" ")) { // get rid of tailing spaces before =
            temp2.chop();
        }
        if (o == temp2) {
            temp = temp.after("=");
            while (temp.startsWith(" ")) { // get rid of heading spaces
                temp.lop();
            }
            if (temp.startsWith("'")) { // inverted commas
                temp.lop();
            }
            while (temp.endsWith(" ")) { // get rid of tailing spaces
                temp.chop();
            }
            if (temp.endsWith("'")) { // inverted commas
                temp.chop();
            }
            DEBUG_config(o, "=", temp);
            return temp.toCharArray();
        }
    }
    return "";
}

std::deque<String> OptionContainer::findoptionM(const char *option) {
    // findoptionS returns all the matching options
    return findoptionMD(option, nullptr);
}

std::deque<String> OptionContainer::findoptionMD(const char *option, const char *delim) {
    // findoptionMD returns all instances of an option & allows multiple entries on a line separated by delim
    String temp;
    String temp2;
    String o(option);
    std::deque<String> results;

    for (std::deque<std::string>::iterator i = conffile.begin(); i != conffile.end(); i++) {
        if ((*i).empty())
            continue;
        temp = (*i).c_str();
        temp2 = temp.before("=");
        while (temp2.endsWith(" ")) { // get rid of tailing spaces before =
            temp2.chop();
        }
        if (o == temp2) {
            temp = temp.after("=");
            while (temp.startsWith(" ")) { // get rid of heading spaces
                temp.lop();
            }
            if (temp.startsWith("'")) { // inverted commas
                temp.lop();
            }
            while (temp.endsWith(" ")) { // get rid of tailing spaces
                temp.chop();
            }
            if (temp.endsWith("'")) { // inverted commas
                temp.chop();
            }
            if (delim != nullptr) {
                while (temp.contains(delim)) {
                    String t = temp.before(delim);
                    DEBUG_config(o, "=", t);
                    results.push_back(t);
                    temp = temp.after(delim);
                }
            }
            DEBUG_config(o, "=", temp);
            results.push_back(temp);
        }
    }
    return results;
}

bool OptionContainer::realitycheck(long int l, long int minl, long int maxl, const char *emessage) {
    // realitycheck checks an amount for certain expected criteria
    // so we can spot problems in the conf files easier
    if ((l < minl) || ((maxl > 0) && (l > maxl))) {
        E2LOGGER_error("Config problem; check allowed values for ", emessage, "( ", l, " should be >= ", minl, " <=",
                       maxl, ")");
        return false;
    }
    return true;
}

// realitycheckWithDefault gets an option value, checks for minl and maxl bounds and defaults to defaultl if no value was found
long int OptionContainer::realitycheckWithDefault(const char *option, long int minl, long int maxl, long int defaultl) {
    std::string s = findoptionS(option);
    if ( s == "" ) return defaultl;
    long int value = String(s).toLong();

    if ((value < minl) || ((maxl > 0) && (value > maxl))) {
        E2LOGGER_error("Config problem; check allowed values for ", option, "( ", value , " should be >= ", minl, " <=", maxl, ")",
                    "we are using default value:", defaultl);        
        return defaultl;
    }
    return value;
}

#pragma region Plugins
bool OptionContainer::loadDMPlugins(ConfigReader &cr) {
    DEBUG_config("load Download manager plugins");
    std::deque<String> dq = *cr.findoptionM("downloadmanager");
    unsigned int numplugins = dq.size();
    if (numplugins < 1) {
        E2LOGGER_error("There must be at least one download manager option");
        return false;
    }
    String config;
    for (unsigned int i = 0; i < numplugins; i++) {
        config = dq[i];
        DEBUG_debug("loading download manager config: ", config);
        DMPlugin *dmpp = dm_plugin_load(config.toCharArray());
        if (dmpp == NULL) {
            E2LOGGER_error("dm_plugin_load() returned NULL pointer with config file: ", config);
            return false;
        }
        bool lastplugin = (i == (numplugins - 1));
        int rc = dmpp->init(&lastplugin);
        if (rc < 0) {
            E2LOGGER_error("Download manager plugin init returned error value: ", rc);
            return false;
        } else if (rc > 0) {
            E2LOGGER_error("Download manager plugin init returned warning value: ", rc);
        }
        dmplugins.push_back(dmpp);
    }
    // cache reusable iterators
    dmplugins_begin = dmplugins.begin();
    dmplugins_end = dmplugins.end();
    return true;
}

bool OptionContainer::loadCSPlugins(ConfigReader &cr) {
    DEBUG_config("load Content scanner plugins");
    std::deque<String> dq = *cr.findoptionM("contentscanner");
    unsigned int numplugins = dq.size();
    if (numplugins < 1) {
        return true; // to have one is optional
    }
    String config;
    for (unsigned int i = 0; i < numplugins; i++) {
        config = dq[i];
        // worth adding some input checking on config
        DEBUG_debug("loading content scanner config: ", config);
        CSPlugin *cspp = cs_plugin_load(config.toCharArray());
        if (cspp == NULL) {
            E2LOGGER_error("cs_plugin_load() returned NULL pointer with config file: ", config);
            return false;
        }
        DEBUG_debug("Content scanner plugin is good, calling init...");
        int rc = cspp->init(NULL);
        if (rc < 0) {
            E2LOGGER_error("Content scanner plugin init returned error value: ", rc);
            return false;
        } else if (rc > 0) {
            E2LOGGER_error("Content scanner plugin init returned warning value: ", rc);
        }
        csplugins.push_back(cspp);
    }
    // cache reusable iterators
    csplugins_begin = csplugins.begin();
    csplugins_end = csplugins.end();
    return true;
}

bool OptionContainer::loadAuthPlugins(ConfigReader &cr) {
    DEBUG_config("load Auth plugins");
    // Assume no auth plugins need an upstream proxy query (NTLM, BASIC) until told otherwise
    auth_needs_proxy_query = false;

    std::deque<String> dq = *cr.findoptionM("authplugin");
    unsigned int numplugins = dq.size();
    if (numplugins < 1) {
        return true; // to have one is optional
    }
    String config;
    for (unsigned int i = 0; i < numplugins; i++) {
        config = dq[i];
        // worth adding some input checking on config
        DEBUG_debug("loading auth plugin config: ", config);
        AuthPlugin *app = auth_plugin_load(config.toCharArray());
        if (app == NULL) {
            E2LOGGER_error("auth_plugin_load() returned NULL pointer with config file: ", config);
            return false;
        }
        DEBUG_debug("Auth plugin is good, calling init...");
        int rc = app->init(NULL);
        if (rc < 0) {
            E2LOGGER_error("Auth plugin init returned error value:", rc);
            return false;
        } else if (rc > 0) {
            E2LOGGER_error("Auth plugin init returned warning value: ", rc);
        }

        if (app->needs_proxy_query) {
            auth_needs_proxy_query = true;
            DEBUG_debug("Auth plugin relies on querying parent proxy");
        }
        if (app->needs_proxy_access_in_plugin) {
            auth_needs_proxy_in_plugin = true;
            DEBUG_debug("Auth plugin relies on querying parent proxy within plugin");
        }
        authplugins.push_back(app);
    }
    // cache reusable iterators
    authplugins_begin = authplugins.begin();
    authplugins_end = authplugins.end();
    return true;
}

void OptionContainer::deletePlugins(std::deque<Plugin *> &list) {
    for (std::deque<Plugin *>::iterator i = list.begin(); i != list.end(); i++) {
        if ((*i) != NULL) {
            (*i)->quit();
            delete (*i);
        }
    }
    list.clear();
}
#pragma endregion

bool OptionContainer::createLists(int load_id) {
    DEBUG_config("create Lists: ", load_id);
    std::shared_ptr <LOptionContainer> temp(new LOptionContainer(load_id));
    if (temp->loaded_ok) {
        current_LOC = temp;
        return true;
    }
    return false;
}

#pragma region ProcessOptions
bool ProcessOptions::find_user_ids()
{

    struct passwd *st;
    struct group *sg;
    int rc;

    root_user = geteuid();

    // This is an important feature because we need to be able to create temp
    // files with suitable permissions for scanning by AV daemons - we do this
    // by becoming a member of a specified AV group and setting group read perms
    if ((sg = getgrnam(daemon_group_name.c_str())) != 0) {
        proxy_group = sg->gr_gid;
    } else {
        E2LOGGER_error( "Unable to getgrnam(): ", strerror(errno));
        E2LOGGER_error("Check the group that e2guardian runs as (", daemon_group_name, ")");
        return 1;
    }

    if ((st = getpwnam(daemon_user_name.c_str())) != 0) { // find uid for proxy user
        proxy_user = st->pw_uid;

        rc = setgid(proxy_group); // change to rights of proxy user group
        // i.e. low - for security
        if (rc == -1) {
            E2LOGGER_error("Unable to setgid()");
            return false; // setgid failed for some reason so exit with error
        }


    } else {
        E2LOGGER_error("Unable to getpwnam() - does the proxy user exist?");
        E2LOGGER_error("Proxy user looking for is '", daemon_user_name, "'" );
        return false;   // was unable to lockup the user id from passwd
                        // for some reason, so exit with error
    }
    return true;
}

bool ProcessOptions::become_root_user()
{
    int rc;
#ifdef HAVE_SETREUID
    rc = setreuid((uid_t)-1, root_user);
#else
    rc = seteuid(root_user);
#endif
    if (rc == -1) {
        E2LOGGER_error("Unable to seteuid() to become root user");
        return false;
    }
    return true;
}

bool ProcessOptions::become_proxy_user()
{
    int rc;
#ifdef HAVE_SETREUID
    rc = setreuid((uid_t)-1, proxy_user);
#else
    rc = seteuid(proxy_user); // become low priv again
#endif
    if (rc == -1) {
        E2LOGGER_error("Unable to re-seteuid() to become proxy user");
        return false;
    }
    return true;
}

// Fork ourselves off into the background
bool ProcessOptions::daemonise()
{
    if (no_daemon) {
        return true;
    }
#ifdef E2DEBUG
    return true; // if debug mode is enabled we don't want to detach
#endif

    if (is_daemonised) {
        return true; // we are already daemonised so this must be a
        // reload caused by a HUP
    }

    int nullfd = -1;
    if ((nullfd = open("/dev/null", O_WRONLY, 0)) == -1) {
        E2LOGGER_error("Couldn't open /dev/null");
        return false;
    }

    pid_t pid;
    if ((pid = fork()) < 0) {    // Error!!
        close(nullfd);
        return false;
    } else if (pid != 0) {      // parent goes...
        if (nullfd != -1) {
            close(nullfd);
        }
        
        exit(0);    // bye-bye
    }

    // child continues
    dup2(nullfd, 0); // stdin
    dup2(nullfd, 1); // stdout
    dup2(nullfd, 2); // stderr
    close(nullfd);

    setsid(); // become session leader
    if (chdir("/") != 0) {// change working directory
	    E2LOGGER_error(" Can't change / directory !");
	    return false;
    }
    umask(0); // clear our file mode creation mask
    umask(S_IWGRP | S_IWOTH); // set to mor sensible setting??

    is_daemonised = true;

    return true;
}
#pragma endregion

bool CertificateOptions::generate_ca_certificate()
{
    if (!enable_ssl) return true;

    DEBUG_config("enable SSL");
    if (ca_certificate_path != "") {
        ca = new CertificateAuthority(ca_certificate_path.c_str(),
                                      ca_private_key_path.c_str(),
                                      cert_private_key_path.c_str(),
                                      generated_cert_path.c_str(),
                                      gen_cert_start, gen_cert_end);
    } else {
        E2LOGGER_error(
                "Error - Valid cacertificatepath, caprivatekeypath and generatedcertpath must given when using MITM.");
        return false;
    }
    return true;
}

std::shared_ptr <LOptionContainer> OptionContainer::currentLists() {
    return current_LOC;
}

bool OptionContainer::realitycheck(long int l, long int minl, long int maxl, const char *emessage) {
    // realitycheck checks an amount for certain expected criteria
    // so we can spot problems in the conf files easier
    if ((l < minl) || ((maxl > 0) && (l > maxl))) {
        E2LOGGER_error("Config problem; check allowed values for ", emessage, "( ", l, " should be >= ", minl, " <=",
                       maxl, ")");
        return false;
    }
    return true;
}
