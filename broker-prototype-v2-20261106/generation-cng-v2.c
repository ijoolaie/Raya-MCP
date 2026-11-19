#include "generation-cng-v2.h"
#include <windows.h>
#include <bcrypt.h>
int raya_generation_cng_random(void*context,uint8_t*out,unsigned length){(void)context;if(!out||!length)return 0;return BCryptGenRandom(NULL,(PUCHAR)out,(ULONG)length,BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0?1:0;}
