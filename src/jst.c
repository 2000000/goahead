/*
    jst.c -- JavaScript templates

    Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************* Includes ***********************************/

#include    "goahead.h"
#include    "js.h"

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#if ME_GOAHEAD_JAVASCRIPT
/********************************** Locals ************************************/

static WebsHash websJstFunctions = -1;  /* Symbol table of functions */

/***************************** Forward Declarations ***************************/

static char *strtokcmp(char *s1, char *s2);
static char *skipWhite(char *s);

/************************************* Code ***********************************/
/*
    Process requests and expand all scripting commands. We read the entire web page into memory and then process. If
    you have really big documents, it is better to make them plain HTML files rather than Javascript web pages.
    Return true to indicate the request was handled, even for errors.
 */
static bool jstHandler(Webs *wp)
{
    WebsFileInfo    sbuf;
    char            *lang, *token, *result, *ep, *cp, *buf, *nextp, *last;
    ssize           len;
    int             rc, jid;

    assert(websValid(wp));
    assert(wp->filename && *wp->filename);
    assert(wp->ext && *wp->ext);

    buf = 0;
    if ((jid = jsOpenEngine(wp->vars, websJstFunctions)) < 0) {
        websError(wp, HTTP_CODE_INTERNAL_SERVER_ERROR, "Cannot create JavaScript engine");
        goto done;
    }
    jsSetUserHandle(jid, wp);

    if (websPageStat(wp, &sbuf) < 0) {
        websError(wp, HTTP_CODE_NOT_FOUND, "Cannot stat %s", wp->filename);
        goto done;
    }
    if (websPageOpen(wp, O_RDONLY | O_BINARY, 0666) < 0) {
        websError(wp, HTTP_CODE_NOT_FOUND, "Cannot open URL: %s", wp->filename);
        goto done;
    }
    /*
        Create a buffer to hold the web page in-memory
     */
    len = sbuf.size;
    if ((buf = walloc(len + 1)) == NULL) {
        websError(wp, HTTP_CODE_INTERNAL_SERVER_ERROR, "Cannot get memory");
        goto done;
    }
    buf[len] = '\0';

    if (websPageReadData(wp, buf, len) != len) {
        websError(wp, HTTP_CODE_NOT_FOUND, "Cannot read %s", wp->filename);
        goto done;
    }
    websPageClose(wp);
    websWriteHeaders(wp, (ssize) -1, 0);
    websWriteHeader(wp, "Pragma", "no-cache");
    websWriteHeader(wp, "Cache-Control", "no-cache");
    websWriteEndHeaders(wp);

    /*
        Scan for the next "<%"
     */
    last = buf;
    for (rc = 0; rc == 0 && *last && ((nextp = strstr(last, "<%")) != NULL); ) {
        websWriteBlock(wp, last, (nextp - last));
        nextp = skipWhite(nextp + 2);
        /*
            Decode the language
         */
        token = "language";
        if ((lang = strtokcmp(nextp, token)) != NULL) {
            if ((cp = strtokcmp(lang, "=javascript")) != NULL) {
                /* Ignore */;
            } else {
                cp = nextp;
            }
            nextp = cp;
        }

        /*
            Find tailing bracket and then evaluate the script
         */
        if ((ep = strstr(nextp, "%>")) != NULL) {

            *ep = '\0';
            last = ep + 2;
            nextp = skipWhite(nextp);
            /*
                Handle backquoted newlines
             */
            for (cp = nextp; *cp; ) {
                if (*cp == '\\' && (cp[1] == '\r' || cp[1] == '\n')) {
                    *cp++ = ' ';
                    while (*cp == '\r' || *cp == '\n') {
                        *cp++ = ' ';
                    }
                } else {
                    cp++;
                }
            }
            if (*nextp) {
                result = NULL;

                if (jsEval(jid, nextp, &result) == 0) {
                    /*
                         On an error, discard all output accumulated so far and store the error in the result buffer.
                         Be careful if the user has called websError() already.
                     */
                    rc = -1;
                    if (websValid(wp)) {
                        if (result) {
                            websWrite(wp, "<h2><b>Javascript Error: %s</b></h2>\n", result);
                            websWrite(wp, "<pre>%s</pre>", nextp);
                            wfree(result);
                        } else {
                            websWrite(wp, "<h2><b>Javascript Error</b></h2>\n%s\n", nextp);
                        }
                        websWrite(wp, "</body></html>\n");
                        rc = 0;
                    }
                    goto done;
                }
            }

        } else {
            websError(wp, HTTP_CODE_INTERNAL_SERVER_ERROR, "Unterminated script in %s: \n", wp->filename);
            goto done;
        }
    }
    /*
        Output any trailing HTML page text
     */
    if (last && *last && rc == 0) {
        websWriteBlock(wp, last, strlen(last));
    }

/*
    Common exit and cleanup
 */
done:
    if (websValid(wp)) {
        websPageClose(wp);
        if (jid >= 0) {
            jsCloseEngine(jid);
        }
    }
    websDone(wp);
    wfree(buf);
    return 1;
}


static void closeJst()
{
    if (websJstFunctions != -1) {
        hashFree(websJstFunctions);
        websJstFunctions = -1;
    }
}


PUBLIC int websJstOpen()
{
    read_reg();
    websJstFunctions = hashCreate(WEBS_HASH_INIT * 2);
    websDefineJst("write", websJstWrite);
    websDefineJst("readVer", websJstReadVer);
    websDefineJst("getVol", websJstGetVol);
    websDefineJst("getThermal", websJstGetThermal);
    websDefineHandler("jst", 0, jstHandler, closeJst, 0);
    return 0;
}


/*
    Define a Javascript function. Bind an Javascript name to a C procedure.
 */
PUBLIC int websDefineJst(cchar *name, WebsJstProc fn)
{
    return jsSetGlobalFunctionDirect(websJstFunctions, name, (JsProc) fn);
}


/*
    Javascript write command. This implemements <% write("text"); %> command
 */
PUBLIC int websJstWrite(int jid, Webs *wp, int argc, char **argv)
{
    int     i;

    assert(websValid(wp));

    for (i = 0; i < argc; ) {
        assert(argv);
        if (websWriteBlock(wp, argv[i], strlen(argv[i])) < 0) {
            return -1;
        }
        if (++i < argc) {
            if (websWriteBlock(wp, " ", 1) < 0) {
                return -1;
            }
        }
    }
    return 0;
}

/* -------------------添加自己的jst------------------- */
volatile unsigned int * map_base;

void read_reg(void)
{
    int fd;
    fd = open("/dev/mem", O_RDWR);
    if(fd<0) {
       printf("file open fail\n");
    }
    map_base = (unsigned int*)mmap(NULL, 0xff, PROT_READ|PROT_WRITE, MAP_SHARED, fd, ADC);
    //printf("map_base is %p \n", map_base);
}

unsigned int REG_READ_4byte(int offset)
{
	return *(map_base + offset);
}

unsigned int PL_REG_READ(int offset)
{
	return *(map_base + offset);
}


void PL_REG_WRITE(unsigned int addr, unsigned int value)
{
	*(map_base + addr) = value;
}

void  FPGA_write(unsigned int addr, unsigned int data)  //write data to all fpga
{      
        PL_REG_WRITE(ADDR_FPGA_rw_addr,addr);  //write the fpga addr to point in the pl
        PL_REG_WRITE(ADDR_FPGA_wr_data,data);  //write the data to bus in the pl
	 
        PL_REG_WRITE(ADDR_FPGA_rw_ctrl,0x00);  //normal

        PL_REG_WRITE(ADDR_FPGA_rw_ctrl,0x00);  //write bit7:contrl en; bit2-bit1:00(write all fpga)
        PL_REG_WRITE(ADDR_FPGA_rw_ctrl,0x01);  //write bit0:begin read or write
        PL_REG_WRITE(ADDR_FPGA_rw_ctrl,0x00);  //write bit0:begin read or write

        PL_REG_WRITE(ADDR_FPGA_rw_ctrl,0x00);  //normal
}


unsigned char FPGA_read(unsigned int fpga_id, unsigned int addr)  //write data to all fpga
{      
     unsigned char data_read_from_fpga;

        PL_REG_WRITE(ADDR_FPGA_rw_addr,addr);  //write the fpga addr to point in the pl
        

        PL_REG_WRITE(ADDR_FPGA_rw_ctrl,0x00);  //normal

        PL_REG_WRITE(ADDR_FPGA_rw_ctrl,0x00);  //write bit7:contrl en; bit2-bit1:00(write all fpga)


        switch (fpga_id) {                         //fpga0
	case 0: 
        PL_REG_WRITE(ADDR_FPGA_rw_ctrl,0x02);  //write bit7:contrl en; bit 0: 0 bit2-bit1:01
        PL_REG_WRITE(ADDR_FPGA_rw_ctrl,0x03);  //write bit7:contrl en; bit 1: 0 bit2-bit1:01
        PL_REG_WRITE(ADDR_FPGA_rw_ctrl,0x02);  //write bit7:contrl en; bit 0: 0 bit2-bit1:01
        break;
       case 1:                                       //fpga1
        PL_REG_WRITE(ADDR_FPGA_rw_ctrl,0x04);  //write bit7:contrl en; bit 0: 0 bit2-bit1:10
        PL_REG_WRITE(ADDR_FPGA_rw_ctrl,0x05);  //write bit7:contrl en; bit 1: 0 bit2-bit1:10
        PL_REG_WRITE(ADDR_FPGA_rw_ctrl,0x04);  //write bit7:contrl en;bit 0: 0 bit2-bit1:10
        break;

       default:
		break;
        }
        
        data_read_from_fpga = PL_REG_READ(ADDR_FPGA_rd_data);   //low 8 bit 
        PL_REG_WRITE(ADDR_FPGA_rw_ctrl,0x00);  //normal
	return data_read_from_fpga;
}




unsigned char FPGA_id_read(unsigned int fpga_id)  //used for judge whether the fpga is mount
{      
   return FPGA_read(fpga_id, 0x00);   
}



unsigned int FPGA_ver_read(unsigned int fpga_id)  //used for judge whether the fpga is mount
{      
   unsigned char byte1,byte2,byte3,byte4;

   byte1 = FPGA_read(fpga_id, 0x01); //high
   byte2 = FPGA_read(fpga_id, 0x02);
   byte3 = FPGA_read(fpga_id, 0x03);
   byte4 = FPGA_read(fpga_id, 0x04); //low

   return (byte1<<24 | byte2<<16 | byte3<<8 | byte4);

}


unsigned int FPGA_date_read(unsigned int fpga_id)  //used for judge whether the fpga is mount
{      
   unsigned char byte1,byte2,byte3,byte4;

   byte1 = FPGA_read(fpga_id, 0x05); //high
   byte2 = FPGA_read(fpga_id, 0x06);
   byte3 = FPGA_read(fpga_id, 0x07);
   byte4 = FPGA_read(fpga_id, 0x08); //low

   return (byte1<<24 | byte2<<16 | byte3<<8 | byte4);
   
}


PUBLIC int websJstReadVer(int jid, Webs *wp, int argc, char **argv)
{
    assert(websValid(wp));

    char tmp[32];	
    unsigned int zynq_ver = REG_READ_4byte(0x00);
    unsigned int fpga0_ver = FPGA_ver_read(0);
    unsigned int fpga1_ver = FPGA_ver_read(1);

    

    if(!strcmp(argv[0],"fpga1")){
        sprintf(tmp, "%08X", fpga0_ver);
        websWriteBlock(wp, tmp, strlen(tmp));
    } else if(!strcmp(argv[0],"zynq")){
        sprintf(tmp, "%08X", zynq_ver);
        websWriteBlock(wp, tmp, strlen(tmp));
    } else if(!strcmp(argv[0],"linux")) {
        websWriteBlock(wp, "kernel-4.14", strlen("kernel-4.14"));
    } else if(!strcmp(argv[0],"fpga0")){
        sprintf(tmp, "%08X", fpga1_ver);
        websWriteBlock(wp, tmp, strlen(tmp));
    }
    return 0;
}


PUBLIC int websJstGetVol(int jid, Webs *wp, int argc, char **argv)
{
    assert(websValid(wp));

    
    unsigned int vcc_int = REG_READ_4byte(VCCINT);
    unsigned int vcc_pint = REG_READ_4byte(VCCPINT);
    unsigned int ddr = REG_READ_4byte(DDR);

    float f_vcc_int = vcc_int/4096.0*3;
    float f_vcc_pint = vcc_pint/4096.0*3;
    float f_ddr = ddr/4096.0*3;

    char s_vcc_int[16], s_vcc_pint[16], s_ddr[16];

    sprintf(s_vcc_int, "%.2f", f_vcc_int);
    sprintf(s_vcc_pint, "%.2f", f_vcc_pint);
    sprintf(s_ddr, "%.2f", f_ddr);

    if(!strcmp(argv[0],"vol")){
        websWriteBlock(wp, s_vcc_int, strlen(s_vcc_int));
    } else if(!strcmp(argv[0],"core")){
        websWriteBlock(wp, s_vcc_pint, strlen(s_vcc_pint));
    } else if(!strcmp(argv[0],"interface")) {
        websWriteBlock(wp, "24.0V", strlen("24.0V"));
    } else if(!strcmp(argv[0],"ddr")){
        websWriteBlock(wp, s_ddr, strlen(s_ddr));
    }
	return 0;
}

PUBLIC int websJstGetThermal(int jid, Webs *wp, int argc, char **argv)
{
    assert(websValid(wp));
    
    unsigned int tmp = REG_READ_4byte(TEMP);
    //printf("++++++%x--\r\n", tmp); 

    float temp = tmp*503.975/4096 - 273.15;
    char  temp_s[64];
    sprintf(temp_s, "%.2f", temp);

    websWriteBlock(wp, temp_s, strlen(temp_s));
	return 0;
}

/* -------------------------------------------------- */

/*
    Find s2 in s1. We skip leading white space in s1.  Return a pointer to the location in s1 after s2 ends.
 */
static char *strtokcmp(char *s1, char *s2)
{
    ssize     len;

    s1 = skipWhite(s1);
    len = strlen(s2);
    for (len = strlen(s2); len > 0 && (tolower((uchar) *s1) == tolower((uchar) *s2)); len--) {
        if (*s2 == '\0') {
            return s1;
        }
        s1++;
        s2++;
    }
    if (len == 0) {
        return s1;
    }
    return NULL;
}


static char *skipWhite(char *s)
{
    assert(s);

    if (s == NULL) {
        return s;
    }
    while (*s && isspace((uchar) *s)) {
        s++;
    }
    return s;
}

#endif /* ME_GOAHEAD_JAVASCRIPT */

/*
    Copyright (c) Embedthis Software. All Rights Reserved.
    This software is distributed under commercial and open source licenses.
    You may use the Embedthis GoAhead open source license or you may acquire
    a commercial license from Embedthis Software. You agree to be fully bound
    by the terms of either license. Consult the LICENSE.md distributed with
    this software for full details and other copyrights.
 */
