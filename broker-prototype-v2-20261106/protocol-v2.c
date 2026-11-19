#include "protocol-v2.h"
#include <string.h>

static int root_char(unsigned char c) {
    return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-';
}
static int path_char(unsigned char c) {
    return (c>=0x21&&c<=0x7e)&&c!='\\'&&c!=':'&&c!='%'&&c!='"'&&c!='<'&&c!='>'&&c!='|'&&c!='?'&&c!='*';
}
static int windows_component_safe(const unsigned char *p, size_t n) {
    char base[16]; size_t i, len=0;
    if(!n || p[n-1]=='.') return 0;
    while(len<n && p[len]!='.') len++;
    if(len>=sizeof(base)) return 1;
    for(i=0;i<len;i++) { unsigned char c=p[i]; base[i]=(c>='a'&&c<='z')?(char)(c-32):(char)c; }
    base[len]='\0';
    if(!strcmp(base,"CON")||!strcmp(base,"PRN")||!strcmp(base,"AUX")||!strcmp(base,"NUL")) return 0;
    if(len==4 && (!memcmp(base,"COM",3)||!memcmp(base,"LPT",3)) && base[3]>='1'&&base[3]<='9') return 0;
    return 1;
}
static int valid_relative(const unsigned char *p, size_t n) {
    size_t i=0, start=0;
    if(!n||n>RAYA_V2_MAX_RELATIVE_PATH||p[0]=='/'||p[n-1]=='/') return 0;
    for(i=0;i<=n;i++) {
        if(i<n && p[i]!='/') { if(!path_char(p[i])) return 0; continue; }
        if(i==start) return 0;
        if((i-start==1&&p[start]=='.')||(i-start==2&&p[start]=='.'&&p[start+1]=='.')) return 0;
        if(!windows_component_safe(p+start,i-start)) return 0;
        start=i+1;
    }
    return 1;
}
int raya_v2_parse_request(const unsigned char *in, size_t n, raya_v2_request *out) {
    size_t i, verb_start, verb_len, root_start, root_len, path_start;
    raya_v2_operation op=RAYA_V2_OP_INVALID;
    if(!in||!out||n<1||n>RAYA_V2_MAX_REQUEST||in[n-1]!='\n') return 0;
    for(i=0;i<n;i++) if(in[i]==0||in[i]=='\r'||in[i]<0x20&&in[i]!='\n'||in[i]>0x7e) return 0;
    if(n<10||memcmp(in,"RAYA/2 ",7)!=0) return 0;
    verb_start=7; i=verb_start; while(i<n&&in[i]!=' ') i++;
    if(i==n) return 0;
    verb_len=i-verb_start;
    if(verb_len==4&&memcmp(in+verb_start,"READ",4)==0) op=RAYA_V2_OP_READ;
    else if(verb_len==4&&memcmp(in+verb_start,"INFO",4)==0) op=RAYA_V2_OP_INFO;
    else if(verb_len==4&&memcmp(in+verb_start,"TAIL",4)==0) op=RAYA_V2_OP_TAIL;
    else return 0;
    root_start=++i; while(i<n&&in[i]!=' '&&in[i]!='\n') i++;
    root_len=i-root_start;
    if(!root_len||root_len>RAYA_V2_MAX_ROOT_ID) return 0;
    for(size_t j=0;j<root_len;j++) if(!root_char(in[root_start+j])) return 0;
    if(i>=n||in[i]!=' ') return 0;
    path_start=++i;
    if(!valid_relative(in+path_start,n-path_start-1)) return 0;
    memset(out,0,sizeof(*out));
    out->operation=op;
    memcpy(out->root_id,in+root_start,root_len);
    memcpy(out->relative_path,in+path_start,n-path_start-1);
    return 1;
}
