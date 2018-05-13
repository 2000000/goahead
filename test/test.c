/*
    test.c -- Unit test program for GoAhead

    Usage: goahead-test [options] [documents] [endpoints...]
        Options:
        --auth authFile        # User and role configuration
        --home directory       # Change to directory to run
        --log logFile:level    # Log to file file at verbosity level
        --route routeFile      # Route configuration file
        --verbose              # Same as --log stderr:2
        --version              # Output version information

    Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************* Includes ***********************************/
#include <stdlib.h>
#include <unistd.h>
#include    "goahead.h"
#include    "js.h"

/********************************* Defines ************************************/

static int finished = 0;


#define ADDR_DBG_mode        0x020

#define ADDR_DBG_thea        0x021
#define ADDR_DBG_phi         0x022
#define ADDR_DBG_fm          0x023
#define ADDR_DBG_sw          0x024
#define ADDR_DBG_dv          0x025


#define ADDR_DBG_calc_chno   0x026
#define ADDR_DBG_calc_value  0x027
#define ADDR_DBG_calc_rd     0x028

//single channel set
#define ADDR_SGL_set_value   0x030  // data to be writed
#define ADDR_SGL_set_dv      0x031  // config value to pl by write 0 -->1-->0 

////RS422
#define ADDR_RS422_LOOP   0x046 // loop mode
#define ADDR_RS422_PS_TST_DATA      0x044 // data to be writed
#define ADDR_RS422_PS_TST_DATA_dv   0x045 // data to be writed



#undef ME_GOAHEAD_LISTEN
/*
    These must match TOP.es.set
 */
#if TEST_IPV6
#if ME_COM_SSL
    #define ME_GOAHEAD_LISTEN "http://127.0.0.1:18080, https://127.0.0.1:14443, http://[::1]:18090, https://[::1]:14453"
#else
    #define ME_GOAHEAD_LISTEN "http://127.0.0.1:18080, http://[::1]:18090"
#endif
#else
#if ME_COM_SSL
    #define ME_GOAHEAD_LISTEN "http://127.0.0.1:18080, https://127.0.0.1:14443"
#else
    #define ME_GOAHEAD_LISTEN "http://127.0.0.1:18080"
#endif
#endif

/********************************* Forwards ***********************************/

static void initPlatform();
static void logHeader();
static void usage();

static bool testHandler(Webs *wp);
#if ME_GOAHEAD_JAVASCRIPT
static int aspTest(int eid, Webs *wp, int argc, char **argv);
static int bigTest(int eid, Webs *wp, int argc, char **argv);
#endif
static void actionTest(Webs *wp);
static void setipaddress(Webs *wp);
static void gettemp(Webs *wp);
static void readPhase(Webs *wp);
static void channelSet(Webs *wp);
static void rs422_normal(Webs *wp);
static void rs422_loop_line(Webs *wp);
static void rs422_loop_data(Webs *wp);
static void rs422_tx_test(Webs *wp);
static void sessionTest(Webs *wp);
static void showTest(Webs *wp);
#if ME_GOAHEAD_UPLOAD && !ME_ROM
static void uploadTest(Webs *wp);
#endif
#if ME_GOAHEAD_LEGACY
static int legacyTest(Webs *wp, char *prefix, char *dir, int flags);
#endif
#if ME_UNIX_LIKE
static void sigHandler(int signo);
#endif
static void exitProc(void *data, int id);

/*********************************** Code *************************************/

MAIN(goahead, int argc, char **argv, char **envp)
{
    char    *argp, *auth, *home, *documents, *endpoints, *endpoint, *route, *tok, *lspec;
    int     argind, duration;

    route = "route.txt";
    auth = "auth.txt";
    duration = 0;

    for (argind = 1; argind < argc; argind++) {
        argp = argv[argind];
        if (*argp != '-') {
            break;

        } else if (smatch(argp, "--auth") || smatch(argp, "-a")) {
            if (argind >= argc) usage();
            auth = argv[++argind];

#if ME_UNIX_LIKE && !MACOSX
        } else if (smatch(argp, "--background") || smatch(argp, "-b")) {
            websSetBackground(1);
#endif

        } else if (smatch(argp, "--debugger") || smatch(argp, "-d") || smatch(argp, "-D")) {
            websSetDebug(1);

        } else if (smatch(argp, "--duration")) {
            if (argind >= argc) usage();
            duration = atoi(argv[++argind]);

        } else if (smatch(argp, "--home")) {
            if (argind >= argc) usage();
            home = argv[++argind];
            if (chdir(home) < 0) {
                error("Cannot change directory to %s", home);
                exit(-1);
            }
        } else if (smatch(argp, "--log") || smatch(argp, "-l")) {
            if (argind >= argc) usage();
            logSetPath(argv[++argind]);

        } else if (smatch(argp, "--verbose") || smatch(argp, "-v")) {
            logSetPath("stdout:2");

        } else if (smatch(argp, "--route") || smatch(argp, "-r")) {
            route = argv[++argind];

        } else if (smatch(argp, "--version") || smatch(argp, "-V")) {
            printf("%s\n", ME_VERSION);
            exit(0);

        } else if (*argp == '-' && isdigit((uchar) argp[1])) {
            lspec = sfmt("stdout:%s", &argp[1]);
            logSetPath(lspec);
            wfree(lspec);

        } else {
            usage();
        }
    }
    documents = ME_GOAHEAD_DOCUMENTS;
    if (argc > argind) {
        documents = argv[argind++];
    }
    initPlatform();
    if (websOpen(documents, route) < 0) {
        error("Cannot initialize server. Exiting.");
        return -1;
    }
    logHeader();
    if (websLoad(auth) < 0) {
        error("Cannot load %s", auth);
        return -1;
    }
    if (argind < argc) {
        while (argind < argc) {
            endpoint = argv[argind++];
            if (websListen(endpoint) < 0) {
                return -1;
            }
        }
    } else {
        endpoints = sclone(ME_GOAHEAD_LISTEN);
        for (endpoint = stok(endpoints, ", \t", &tok); endpoint; endpoint = stok(NULL, ", \t,", &tok)) {
            if (getenv("TRAVIS")) {
                if (strstr(endpoint, "::1") != 0) {
                    /* Travis CI does not support IPv6 */
                    continue;
                }
            }
            if (websListen(endpoint) < 0) {
                return -1;
            }
        }
        wfree(endpoints);
    }

    websDefineHandler("test", testHandler, 0, 0, 0);
    websAddRoute("/test", "test", 0);
#if ME_GOAHEAD_LEGACY
    websUrlHandlerDefine("/legacy/", 0, 0, legacyTest, 0);
#endif
#if ME_GOAHEAD_JAVASCRIPT
    websDefineJst("aspTest", aspTest);
    websDefineJst("bigTest", bigTest);
#endif
    websDefineAction("test", actionTest);
    websDefineAction("setipaddress", setipaddress);
    websDefineAction("sessionTest", sessionTest);
    websDefineAction("gettemp", gettemp);
    websDefineAction("readPhase", readPhase);
    websDefineAction("channelSet", channelSet);
    websDefineAction("rs422_normal", rs422_normal);
    websDefineAction("rs422_loop_line", rs422_loop_line);
    websDefineAction("rs422_loop_data", rs422_loop_data);
    websDefineAction("rs422_tx_test", rs422_tx_test);
    websDefineAction("showTest", showTest);
#if ME_GOAHEAD_UPLOAD && !ME_ROM
    websDefineAction("uploadTest", uploadTest);
#endif

#if ME_UNIX_LIKE && !MACOSX
    /*
        Service events till terminated
    */
    if (websGetBackground()) {
        if (daemon(0, 0) < 0) {
            error("Cannot run as daemon");
            return -1;
        }
    }
#endif
    if (duration) {
        printf("Running for %d secs\n", duration);
        websStartEvent(duration * 1000, (WebsEventProc) exitProc, 0);
    }
    websServiceEvents(&finished);
    logmsg(1, "Instructed to exit\n");
    websClose();
    return 0;
}


static void exitProc(void *data, int id)
{
    websStopEvent(id);
    finished = 1;
}


static void logHeader()
{
    char    home[ME_GOAHEAD_LIMIT_STRING];

    getcwd(home, sizeof(home));
    logmsg(2, "Configuration for %s", ME_TITLE);
    logmsg(2, "---------------------------------------------");
    logmsg(2, "Version:            %s", ME_VERSION);
    logmsg(2, "BuildType:          %s", ME_DEBUG ? "Debug" : "Release");
    logmsg(2, "CPU:                %s", ME_CPU);
    logmsg(2, "OS:                 %s", ME_OS);
    logmsg(2, "Host:               %s", websGetServer());
    logmsg(2, "Directory:          %s", home);
    logmsg(2, "Documents:          %s", websGetDocuments());
    logmsg(2, "Configure:          %s", ME_CONFIG_CMD);
    logmsg(2, "---------------------------------------------");
}


static void usage() {
    fprintf(stderr, "\n%s Usage:\n\n"
        "  %s [options] [documents] [IPaddress][:port]...\n\n"
        "  Options:\n"
        "    --auth authFile        # User and role configuration\n"
#if ME_UNIX_LIKE && !MACOSX
        "    --background           # Run as a Unix daemon\n"
#endif
        "    --debugger             # Run in debug mode\n"
        "    --home directory       # Change to directory to run\n"
        "    --log logFile:level    # Log to file file at verbosity level\n"
        "    --route routeFile      # Route configuration file\n"
        "    --verbose              # Same as --log stderr:2\n"
        "    --version              # Output version information\n\n",
        ME_TITLE, ME_NAME);
    exit(-1);
}


void initPlatform()
{
#if ME_UNIX_LIKE
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);
    signal(SIGKILL, sigHandler);
    #ifdef SIGPIPE
        signal(SIGPIPE, SIG_IGN);
    #endif
#elif ME_WIN_LIKE
    _fmode=_O_BINARY;
#endif
}


#if ME_UNIX_LIKE
static void sigHandler(int signo)
{
    finished = 1;
}
#endif


/*
    Simple handler and route test
    Note: Accesses to "/" are normally remapped automatically to /index.html
 */
static bool testHandler(Webs *wp)
{
    if (smatch(wp->path, "/")) {
        websRewriteRequest(wp, "/home.html");
        /* Fall through */
    }
    return 0;
}


#if ME_GOAHEAD_JAVASCRIPT
/*
    Parse the form variables: name, address and echo back
 */
static int aspTest(int eid, Webs *wp, int argc, char **argv)
{
	char	*name, *address;

	if (jsArgs(argc, argv, "%s %s", &name, &address) < 2) {
		websError(wp, 400, "Insufficient args\n");
		return -1;
	}
	return (int) websWrite(wp, "Name: %s, Address %s", name, address);
}


/*
    Generate a large response
 */
static int bigTest(int eid, Webs *wp, int argc, char **argv)
{
    int     i;

    websSetStatus(wp, 200);
    websWriteHeaders(wp, -1, 0);
    websWriteEndHeaders(wp);
    websWrite(wp, "<html>\n");
    for (i = 0; i < 800; i++) {
        websWrite(wp, " Line: %05d %s", i, "aaaaaaaaaaaaaaaaaabbbbbbbbbbbbbbbbccccccccccccccccccddddddd<br/>\r\n");
    }
    websWrite(wp, "</html>\n");
    websDone(wp);
    return 0;
}
#endif


static void setipaddress(Webs *wp)
{
    cchar *netAddress, *subaddress;
    netAddress = websGetVar(wp, "netAddress", NULL);
    subaddress = websGetVar(wp, "subaddress", NULL);
    printf("ip addr is : %s\n", netAddress);
    printf("sub addr is: %s\n", subaddress);
}

/*
    Implement /action/actionTest. Parse the form variables: name, address and echo back.
 */
static void actionTest(Webs *wp)
{
	cchar	*name, *address;

	name = websGetVar(wp, "name", NULL);
	address = websGetVar(wp, "address", NULL);
    websSetStatus(wp, 200);
    websWriteHeaders(wp, -1, 0);
    websWriteEndHeaders(wp);
	websWrite(wp, "<html><body><h2>name: %s, address: %s</h2></body></html>\n", name, address);
    websFlush(wp, 0);
	websDone(wp);
}


static void sessionTest(Webs *wp)
{
	cchar	*number;

    if (scaselessmatch(wp->method, "POST")) {
        number = websGetVar(wp, "number", 0);
        websSetSessionVar(wp, "number", number);
    } else {
        number = websGetSessionVar(wp, "number", 0);
    }
    websSetStatus(wp, 200);
    websWriteHeaders(wp, -1, 0);
    websWriteEndHeaders(wp);
    websWrite(wp, "<html><body><p>Number %s</p></body></html>\n", number);
    websDone(wp);
}

static void gettemp(Webs *wp)
{

    read_reg();
    
    unsigned int tmp = REG_READ_4byte(TEMP);
    //printf("++++++%x--\r\n", tmp); 

    float temp = tmp*503.975/4096 - 273.15;

    websSetStatus(wp, 200);
    websWriteHeaders(wp, -1, 0);
    websWriteEndHeaders(wp);
	websWrite(wp, "%.2f", temp);
    websFlush(wp, 0);
	websDone(wp);

}

////////////////////////////////////////

void  DEBUG_ANGLE_SET(unsigned int theta,unsigned int phi)  //write data to all fpga
{      
        PL_REG_WRITE(ADDR_DBG_mode,1);  //OPEN THE DEBUG MODE
        PL_REG_WRITE(ADDR_DBG_thea,theta);  //OPEN THE DEBUG MODE
        PL_REG_WRITE(ADDR_DBG_phi,phi);  //OPEN THE DEBUG MODE
        //PL_REG_WRITE(ADDR_DBG_fm,fm);  //OPEN THE DEBUG MODE
        //PL_REG_WRITE(ADDR_DBG_sw,sw);  //OPEN THE DEBUG MODE

        PL_REG_WRITE(ADDR_DBG_dv,0x00);  //write the ctrl to bus in the pl
        PL_REG_WRITE(ADDR_DBG_dv,0x01);  //write the ctrl to bus in the pl
        PL_REG_WRITE(ADDR_DBG_dv,0x00);  //write the ctrl to bus in the pl

        PL_REG_WRITE(ADDR_DBG_mode,0);  //CLOSE THE DEBUG MODE
}



unsigned int phase_read_back(unsigned int ch_no) //phase readback
{
    PL_REG_WRITE(ADDR_DBG_calc_chno,ch_no);  //OPEN THE DEBUG MODE

        PL_REG_WRITE(ADDR_DBG_calc_rd,0x00);  //write the ctrl to bus in the pl
        PL_REG_WRITE(ADDR_DBG_calc_rd,0x01);  //write the ctrl to bus in the pl
        PL_REG_WRITE(ADDR_DBG_calc_rd,0x00);  //write the ctrl to bus in the pl

        return PL_REG_READ(ADDR_DBG_calc_value);  //write the ctrl to bus in the pl

}

////////////////////////////////////////


static void readPhase(Webs *wp)
{
    cchar *theta, *phi;
    theta = websGetVar(wp, "theta", NULL);
    phi = websGetVar(wp, "phi", NULL);
    printf("theta is : %s\n", theta);
    printf("phi is: %s\n", phi);

    unsigned int theta1, phi1;

    theta1 = atoi(theta);
    phi1 = atoi(phi);

    DEBUG_ANGLE_SET(theta1, phi1);

    usleep(1000);

    unsigned char read_back[24];
    int i;

    for (i = 0; i < 23; i ++) {
        read_back[i] =  phase_read_back(i);
    }

    char tostringtmp[128] = {0};
    
    for (i = 0; i < 23; i ++) {
        ;
    }
    
    websSetStatus(wp, 200);
    websWriteHeaders(wp, -1, 0);
    websWriteEndHeaders(wp);
    for(i=0; i<23; i++) {
        websWrite(wp,"%02x ", read_back[i]);
    }
    websDone(wp);
}

/*------------------------------------------------------------*/
void  DEBUG_SGL_SET(unsigned int ch_no,unsigned int datt,unsigned int phase,unsigned int sw)  //write data to all fpga
{      
        unsigned int set_data;

        PL_REG_WRITE(ADDR_DBG_mode,1);  //OPEN THE DEBUG MODE
        
        set_data = ch_no<<16 | sw<<15 | phase<<6 | datt;

        PL_REG_WRITE(ADDR_SGL_set_value,set_data);  //OPEN THE DEBUG MODE
        
        PL_REG_WRITE(ADDR_SGL_set_dv,0x00);  //write the ctrl to bus in the pl
        PL_REG_WRITE(ADDR_SGL_set_dv,0x01);  //write the ctrl to bus in the pl
        PL_REG_WRITE(ADDR_SGL_set_dv,0x00);  //write the ctrl to bus in the pl

        PL_REG_WRITE(ADDR_DBG_mode,0);  //CLOSE THE DEBUG MODE
        
}

static void channelSet(Webs *wp)
{
    cchar *cha, *datt, *phase, *sw;
    cha = websGetVar(wp, "cha", NULL);
    phase = websGetVar(wp, "phase", NULL);
    datt = websGetVar(wp, "datt", NULL);
    sw = websGetVar(wp, "sw", NULL);

    printf("param is: -%s- %s- %s- %s-\n", cha, phase, datt, sw);

    unsigned int cha1, phase1, datt1, sw1;

    cha1 = atoi(cha);
    phase1 = atoi(phase);
    datt1 = atoi(datt);
    //sw1 = atoi(sw);

    DEBUG_SGL_SET(cha1, datt1, phase1, sw1);
}

/*----------------------------------------------------------*/

void RS422_LOOP_LINE() //phase readback
{
  PL_REG_WRITE(ADDR_DBG_mode,1);  //open THE DEBUG MODE
  PL_REG_WRITE(ADDR_RS422_LOOP,0x01);  //write the ctrl to bus in the pl
}


void RS422_LOOP_DATA() //phase readback
{
  PL_REG_WRITE(ADDR_DBG_mode,1);  //open THE DEBUG MODE
  PL_REG_WRITE(ADDR_RS422_LOOP,0x02);  //write the ctrl to bus in the pl
}

void RS422_normal() //normal
{
  PL_REG_WRITE(ADDR_DBG_mode,0);  //CLOSE THE DEBUG MODE
  PL_REG_WRITE(ADDR_RS422_LOOP,0x00);  //write the ctrl to bus in the pl
}


void RS422_TX_TEST()//send 0 -->255
{
  int i;
   PL_REG_WRITE(ADDR_DBG_mode,1);  //OPEN THE DEBUG MODE
   for (i=0;i<255;i++)
  {
        PL_REG_WRITE(ADDR_RS422_PS_TST_DATA,i);  //write the data to bus in the pl
      
        PL_REG_WRITE(ADDR_RS422_PS_TST_DATA_dv,0x00);  //write the ctrl to bus in the pl
        PL_REG_WRITE(ADDR_RS422_PS_TST_DATA_dv,0x01);  //write the ctrl to bus in the pl
        PL_REG_WRITE(ADDR_RS422_PS_TST_DATA_dv,0x00);  //write the ctrl to bus in the pl
  }
}

static void rs422_normal(Webs *wp)
{
    RS422_normal();
}

static void rs422_loop_line(Webs *wp)
{
    RS422_LOOP_LINE();
}

static void rs422_loop_data(Webs *wp)
{
    RS422_LOOP_DATA();
}

static void rs422_tx_test(Webs *wp)
{
    RS422_TX_TEST();
}

/*-----------------------------------------------------*/
static void showTest(Webs *wp)
{
    WebsKey     *s;

    websSetStatus(wp, 200);
    websWriteHeaders(wp, -1, 0);
    websWriteEndHeaders(wp);
    websWrite(wp, "<html><body><pre>\n");
    for (s = hashFirst(wp->vars); s; s = hashNext(wp->vars, s)) {
        websWrite(wp, "%s=%s\n", s->name.value.string, s->content.value.string);
    }
    websWrite(wp, "</pre></body></html>\n");
    websDone(wp);
}


#if ME_GOAHEAD_UPLOAD && !ME_ROM
/*
    Dump the file upload details. Don't actually do anything with the uploaded file.
 */
#if 1
static void uploadTest(Webs *wp)
{
    WebsKey         *s;
    WebsUpload      *up;
    char            *upfile;

    websSetStatus(wp, 200);
    websWriteHeaders(wp, -1, 0);
    websWriteHeader(wp, "Content-Type", "text/plain");
    websWriteEndHeaders(wp);
    if (scaselessmatch(wp->method, "POST")) {
        for (s = hashFirst(wp->files); s; s = hashNext(wp->files, s)) {
            up = s->content.value.symbol;
            websWrite(wp, "FILE: %s\r\n", s->name.value.string);
            websWrite(wp, "FILENAME=%s\r\n", up->filename);
            websWrite(wp, "CLIENT=%s\r\n", up->clientFilename);
            websWrite(wp, "TYPE=%s\r\n", up->contentType);
            websWrite(wp, "SIZE=%d\r\n", up->size);
            //upfile = sfmt("%s/tmp/%s", websGetDocuments(), up->clientFilename);
            //sd card root dir 
            upfile = sfmt("%s/../../%s", websGetDocuments(), up->clientFilename);
            printf("%s\r\n", upfile);
            if (rename(up->filename, upfile) < 0) {
                error("Cannot rename uploaded file: %s to %s, errno %d", up->filename, upfile, errno);
            }
            wfree(upfile);
        }
        websWrite(wp, "\r\nVARS:\r\n");
        for (s = hashFirst(wp->vars); s; s = hashNext(wp->vars, s)) {
            websWrite(wp, "%s=%s\r\n", s->name.value.string, s->content.value.string);
        }
    }
    websDone(wp);
}
#else

static void uploadTest(Webs *wp)
{
    WebsKey         *s;
    WebsUpload      *up;
    char            *upfile;

     upfile = sfmt("%s/tmp/%s", websGetDocuments(), up->clientFilename);

     if (rename(up->filename, upfile) < 0) {
          error("Cannot rename uploaded file: %s to %s, errno %d", up->filename, upfile, errno);
     }
     wfree(upfile);
}
#endif

#endif


#if ME_GOAHEAD_LEGACY
/*
    Legacy handler with old parameter sequence
 */
static int legacyTest(Webs *wp, char *prefix, char *dir, int flags)
{
    websSetStatus(wp, 200);
    websWriteHeaders(wp, -1, 0);
    websWriteHeader(wp, "Content-Type", "text/plain");
    websWriteEndHeaders(wp);
    websWrite(wp, "Hello Legacy World\n");
    websDone(wp);
    return 1;
}

#endif

/*
    @copy   default

    Copyright (c) Embedthis Software LLC, 2003-2014. All Rights Reserved.

    This software is distributed under commercial and open source licenses.
    You may use the Embedthis GoAhead open source license or you may acquire
    a commercial license from Embedthis Software. You agree to be fully bound
    by the terms of either license. Consult the LICENSE.md distributed with
    this software for full details and other copyrights.

    Local variables:
    tab-width: 4
    c-basic-offset: 4
    End:
    vim: sw=4 ts=4 expandtab

    @end
 */
