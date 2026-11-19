#include "protocol-v2.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x,name) do { if(!(x)){printf("FAIL %s\n",name);return 1;} } while(0)
static int reject(const char *s) { raya_v2_request r; return !raya_v2_parse_request((const unsigned char*)s,strlen(s),&r); }
int main(void) {
    raya_v2_request r;
    const char *good="RAYA/2 READ docs report.txt\n";
    CHECK(raya_v2_parse_request((const unsigned char*)good,strlen(good),&r),"valid READ");
    CHECK(r.operation==RAYA_V2_OP_READ,"operation");
    CHECK(strcmp(r.root_id,"docs")==0,"root id");
    CHECK(strcmp(r.relative_path,"report.txt")==0,"relative path");
    CHECK(reject("RAYA/1 READ docs report.txt\n"),"reject v1");
    CHECK(reject("RAYA/2 DELETE docs report.txt\n"),"reject unsupported verb");
    CHECK(reject("RAYA/2 READ docs ../secret\n"),"reject traversal");
    CHECK(reject("RAYA/2 READ docs docs/../secret\n"),"reject nested traversal");
    CHECK(reject("RAYA/2 READ docs C:/secret\n"),"reject drive path");
    CHECK(reject("RAYA/2 READ docs CON\n"),"reject reserved CON device");
    CHECK(reject("RAYA/2 READ docs nul.txt\n"),"reject reserved NUL device with extension");
    CHECK(reject("RAYA/2 READ docs folder/COM9.log\n"),"reject reserved COM device");
    CHECK(reject("RAYA/2 READ docs trailing.\n"),"reject trailing dot");
    CHECK(reject("RAYA/2 READ docs /etc/passwd\n"),"reject absolute path");
    CHECK(reject("RAYA/2 READ docs docs\\secret\n"),"reject backslash");
    CHECK(reject("RAYA/2 READ docs docs//secret\n"),"reject empty component");
    CHECK(reject("RAYA/2 READ docs report.txt"),"reject missing LF");
    CHECK(reject("RAYA/2 READ docs report.txt\r\n"),"reject CRLF");
    { const unsigned char nul[]={'R','A','Y','A','/','2',' ','R','E','A','D',' ','d','o','c','s',' ','x',0,'\n'};
      CHECK(!raya_v2_parse_request(nul,sizeof(nul),&r),"reject NUL"); }
    { unsigned char large[RAYA_V2_MAX_REQUEST+1]; memset(large,'A',sizeof(large));large[sizeof(large)-1]='\n';
      CHECK(!raya_v2_parse_request(large,sizeof(large),&r),"reject oversized"); }
    printf("PASS protocol-v2 parser cases: 18\n");
    return 0;
}