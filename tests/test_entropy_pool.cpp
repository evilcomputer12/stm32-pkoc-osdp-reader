#include "entropy_pool.h"
#include <cassert>
#include <cstring>
int main() {
    EntropyPool pool;
    uint8_t input[64], out[64];
    for (unsigned i=0;i<64;++i) input[i]=i;
    memset(out,0xa5,sizeof(out));
    assert(pool.take(out,1)<0 && out[0]==0xa5);
    assert(pool.append(input,64) && !pool.append(input,1));
    assert(pool.take(out,8)==0 && memcmp(out,input,8)==0);
    assert(pool.available()==56 && pool.append(input,8));
    assert(pool.take(out,64)==0 && memcmp(out,input+8,56)==0 && memcmp(out+56,input,8)==0);
    assert(pool.take(out,1)<0); // Consumed entropy cannot be reused.
    assert(pool.append(input,32));
    memset(out,0xa5,sizeof(out));
    assert(pool.take(out,33)<0 && pool.available()==32 && out[0]==0xa5);
    assert(pool.take(nullptr,1)<0 && pool.available()==32);
    assert(pool.take(out,32)==0 && memcmp(out,input,32)==0);
    assert(pool.append(input,64)); pool.clear();
    assert(pool.available()==0 && pool.take(out,1)<0);
    assert(pool.take(nullptr,0)==0);
}
