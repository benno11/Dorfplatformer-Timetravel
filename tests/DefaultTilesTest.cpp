#include "DefaultTiles.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
int main(){
 assert(DefaultTiles::select(false,false,true,false)==100);
 assert(DefaultTiles::select(false,true,false,true)==101);
 assert(DefaultTiles::select(false,true,true,false)==102);
 assert(DefaultTiles::select(false,false,false,true)==103);
 for(int mask=0;mask<16;++mask) assert(DefaultTiles::contains(DefaultTiles::select(mask&1,mask&2,mask&4,mask&8)));
 for(int id=100;id<=103;++id) assert(DefaultTiles::contains(id));
 assert(!DefaultTiles::contains(99)&&!DefaultTiles::contains(104));
}
