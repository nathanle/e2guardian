// For all support, instructions and copyright go to:
// http://e2guardian.org/
// Released under the GPL v2, with the OpenSSL exception described in the README file.

// INCLUDES

#ifdef HAVE_CONFIG_H
#include "e2config.h"
#endif
#include "FatController.hpp"
#include "SysV.hpp"
#include "Queue.hpp"
#include "Logger.hpp"
#include "LoggerConfigurator.hpp"

#include <cstdlib>
#include <iostream>
#include <cstdio>
#include <ctime>
#include <unistd.h>
#include <cerrno>
#include <pwd.h>
#include <grp.h>
#include <fstream>
#include <fcntl.h>
#include <locale.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/times.h>
#include <sys/resource.h>

#ifdef __BENCHMARK
#include <sys/times.h>
#include "NaughtyFilter.hpp"
#endif

// GLOBALS

OptionContainer o;
thread_local std::string thread_id;
std::atomic<bool> g_is_starting;

LoggerConfigurator loggerConfig(&e2logger);
bool is_daemonised;

// regexp used during URL decoding by HTTPHeader
// we want it compiled once, not every time it's used, so do so on startup
RegExp urldecode_re;

#ifdef HAVE_PCRE
// regexes used for embedded URL extraction by NaughtyFilter
RegExp absurl_re, relurl_re;
#endif

// DECLARATIONS
int readCommandlineOptions(int argc, char *argv[]);
int runBenchmarks();
void prepareRegExp();
int startDaemon();

// get the OptionContainer to read in the given configuration file
void read_config(std::string& configfile, int type);

// IMPLEMENTATION

// get the OptionContainer to read in the given configuration file
//void read_config(const char *configfile, int type)
void read_config(std::string& configfile, int type)
{
    int rc = open(configfile.c_str(), 0, O_RDONLY);
    if (rc < 0) {
        E2LOGGER_error("Error opening ", configfile);
        exit(1); // could not open conf file for reading, exit with error
    }
    close(rc);

    if (!o.read(configfile, type)) {
        E2LOGGER_error( "Error parsing the e2guardian.conf file or other e2guardian configuration files");
        exit(1); // OptionContainer class had an error reading the conf or other files so exit with error
    }
}

int main(int argc,char *argv[])
{
    g_is_starting = true;
    thread_id = "";

    o.config.prog_name = PACKAGE;
    o.config.configfile = __CONFFILE;

    srand(time(NULL));

    // Set current locale for proper character conversion
    setlocale(LC_ALL, "");

    e2logger.setSyslogName(o.config.prog_name);
#ifdef DEBUG_LOW
    e2logger.enable(LoggerSource::debug);
#endif

//    E2LOGGER_info("Start ", prog_name );  // No we are not starting here - we may be stopping, reloading etc
    DEBUG_debug("Running in debug_low mode...");

    DEBUG_trace("read CommandLineOptions");
    readCommandlineOptions(argc, argv);
    
    DEBUG_trace("read Configfile: ", o.config.configfile);
    read_config(o.config.configfile, 2);

    if ( o.log.SB_trace ) {
        DEBUG_config("Enable Storyboard tracing !!");
        e2logger.enable(LoggerSource::story);
    }

    if (o.config.total_block_list && !o.readinStdin()) {
        E2LOGGER_error("Error on reading total_block_list");
//		return 1;
        DEBUG_debug("Total block lists read OK from stdin.");
    }

    DEBUG_trace("create Lists");
    if(!o.createLists(0))  {
        E2LOGGER_error("Error reading filter group conf file(s).");
        return 1;
    }

    prepareRegExp();

    return startDaemon();

}


int readCommandlineOptions(int argc, char *argv[])
{
    bool needreset = false;
    std::string debugoptions;

    DEBUG_trace("parse Options");
    for (int i = 1; i < argc; i++) {  // first check for config file
        if (argv[i][0] == '-') {
            for (unsigned int j = 1; j < strlen(argv[i]); j++) {
                char option = argv[i][j];
                bool dobreak = false;
                switch (option) {
                    case 'c':
                        if ((i + 1) < argc) {
                            o.config.configfile = argv[i + 1];
                            i++;
                            dobreak = true;
                        } else {
                            std::cerr << "No config file specified!" << std::endl;
                            return 1;
                        }
                        break;
                }
                if (dobreak) break;
            }
        }
    }

    for (int i = 1; i < argc; i++) {    // then rest of args
        bool skip_next = false;
        if (argv[i][0] == '-') {
            for (unsigned int j = 1; j < strlen(argv[i]); j++) {
                char option = argv[i][j];
                bool dobreak = false;
                switch (option) {
                case 'q':
                    read_config(o.config.configfile, 0);
                    return sysv_kill(o.proc.pid_filename,true);
                case 'Q':
                    read_config(o.config.configfile, 0);
                    sysv_kill(o.proc.pid_filename, false);
                    // give the old process time to die
                    while (sysv_amirunning(o.proc.pid_filename))
                        sleep(1);
                    unlink(o.proc.pid_filename.c_str());
                    // remember to reset config before continuing
                    needreset = true;
                    break;
                case 's':
                    read_config(o.config.configfile, 0);
                    return sysv_showpid(o.proc.pid_filename);
                case 'r':
                case 'g':
                    read_config(o.config.configfile, 0);
                    return sysv_hup(o.proc.pid_filename);
                case 't':
                    read_config(o.config.configfile, 0);
                    return sysv_usr1(o.proc.pid_filename);
                case 'v':
                    std::cout << "e2guardian " << PACKAGE_VERSION << std::endl
                              << std::endl
                              << "Built with: " << E2_CONFIGURE_OPTIONS << std::endl;
                    return 0;
                case 'N':
                    o.proc.no_daemon = true;
                    break;
                case 'c':   // already processed this - so skip
                        skip_next = true;
                    break;
                case 'i':
                    o.config.total_block_list = true;
                    break;
                case 'd':
                    if ((i + 1) < argc) {
                        debugoptions = argv[i+1];
                        i++;
                        dobreak = true;
                    };
                    break;

                case 'h':
                default:
                    std::cout << "Usage: " << argv[0] << " [-c ConfigFileName|-v|-h|-N|-q|-Q|-s|-r|-g|-i] [-d debuglevel]" << std::endl;
                    std::cout << "  -v gives the version number and build options." << std::endl;
                    std::cout << "  -h gives this message." << std::endl;
                    std::cout << "  -c allows you to specify a different configuration file location." << std::endl;
                    std::cout << "  -N Do not go into the background." << std::endl;
                    std::cout << "  -q causes e2guardian to kill any running copy." << std::endl;
                    std::cout << "  -Q kill any running copy AND start a new one with current options." << std::endl;
                    std::cout << "  -s shows the parent process PID and exits." << std::endl;
                    std::cout << "  -r reloads lists and group config files by issuing a HUP," << std::endl;
                    std::cout << "     but this does not reset the httpworkers option (amongst others)." << std::endl;
                    std::cout << "  -g  same as -r  (Issues a HUP)" << std::endl;
                    std::cout << "  -t  rotate logs (Issues a USR1)" << std::endl;
                    std::cout << "  -d  allows you to specify a debuglevel" << std::endl;
                    std::cout << "  -i read lists from stdin" << std::endl;
                    return 0;
                }
                if (dobreak)
                    break; // skip to the next argument
            }
        }
        if(skip_next) i++;
    }

    if (needreset) {
        DEBUG_trace("reset Options");
        o.reset();
    }

    if (!debugoptions.empty()) {
        loggerConfig.debuglevel(debugoptions);
    }
    
    return 0;

}    


int startDaemon()
{
    DEBUG_trace("prepare Start");
    if (sysv_amirunning(o.proc.pid_filename)) {
        E2LOGGER_error("I seem to be running already!");
        return 1; // can't have two copies running!!
    }

    // calc the number of listening processes
    int no_listen_fds;
    if (o.net.map_ports_to_ips) {
        no_listen_fds = o.net.filter_ip.size();
    } else {
        no_listen_fds = o.net.filter_ports.size() * o.net.filter_ip.size();
    }

    struct rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
        E2LOGGER_error( "getrlimit call returned error: ", errno);
        return 1;
    }

    // enough fds needed for listening_fds + logger + ipcs + stdin/out/err
    // in addition to two for each worker thread
    int max_free_fds = rlim.rlim_cur - (no_listen_fds + 6);
    int fd_needed = (o.http_workers *2) + no_listen_fds + 6;

    if (((o.http_workers * 2) ) > max_free_fds) {
        E2LOGGER_error("httpworkers option in e2guardian.conf has a value too high for current file id limit (", rlim.rlim_cur, ")" );
        E2LOGGER_error("httpworkers ", o.http_workers,  " must not exceed 50% of ", max_free_fds);
        E2LOGGER_error("in this configuration.");
        E2LOGGER_error("Reduce httpworkers ");
        E2LOGGER_error("Or increase the filedescriptors available with ulimit -n to at least=", fd_needed);
        return 1; // we can't have rampant proccesses can we?
    }

    if (!o.proc.find_user_ids()) return 1;
    if (!o.proc.become_proxy_user()) return 1;

    DEBUG_trace("Starting Main loop");
    while (true) {
        int rc = fc_controlit();
        // its a little messy, but I wanted to split
        // all the ground work and non-daemon stuff
        // away from the daemon class
        // However the line is not so fine.
        if (rc == 2) {

            // In order to re-read the conf files
            // we need to become root user again
            if (!o.proc.become_root_user()) return 1;

            DEBUG_trace("About to re-read conf file.");
            o.reset();
            if (!o.read(o.config.configfile, 2)) {
                // OptionContainer class had an error reading the conf or
                // other files so exit with errora
                E2LOGGER_error("Error re-parsing the e2guardian.conf file or other e2guardian configuration files");
                return 1;
            }
            DEBUG_trace("conf file read.");

            while (waitpid(-1, NULL, WNOHANG) > 0) {
            } // mop up defunts

            // become low priv again
            if (!o.proc.become_proxy_user()) return 1;
            continue;
        }

        if (o.proc.is_daemonised)
        	return 0; // exit without error
        if (rc > 0) {
            E2LOGGER_error("Exiting with error");
            return rc; // exit returning the error number
        }

    }

}

void prepareRegExp()
{
    urldecode_re.comp("%[0-9a-fA-F][0-9a-fA-F]"); // regexp for url decoding

#ifdef HAVE_PCRE
    // todo: these only work with PCRE enabled (non-greedy matching).
    // change them, or make them a feature for which you need PCRE?
    absurl_re.comp("[\"'](http|ftp)://.*?[\"']"); // find absolute URLs in quotes
    relurl_re.comp("(href|src)\\s*=\\s*[\"'].*?[\"']"); // find relative URLs in quotes
#endif

}
