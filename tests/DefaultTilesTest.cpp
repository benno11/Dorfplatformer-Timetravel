#include "DefaultTiles.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
int main(){
 assert(DefaultTiles::select(false,false,true,false)==40);
 assert(DefaultTiles::select(false,true,false,true)==9);
 assert(DefaultTiles::select(false,true,true,false)==11);
 assert(DefaultTiles::select(false,false,false,true)==39);
 for(int mask=0;mask<16;++mask) assert(DefaultTiles::contains(DefaultTiles::select(mask&1,mask&2,mask&4,mask&8)));
 for(int id=100;id<=103;++id) assert(DefaultTiles::contains(id));
 auto strip=[](int x,int y){return y==0 && x>=0 && x<=5;};
 assert(DefaultTiles::selectAt(0,0,strip)==39);
 assert(DefaultTiles::selectAt(1,0,strip)==103);
 assert(DefaultTiles::selectAt(4,0,strip)==100);
 assert(DefaultTiles::selectAt(5,0,strip)==40);
 auto platform=[](int x,int y){return y>=0 && y<=2 && x>=0 && x<=5;};
 assert(DefaultTiles::selectAt(0,0,platform)==9);
 assert(DefaultTiles::selectAt(1,0,platform)==101);
 assert(DefaultTiles::selectAt(4,0,platform)==102);
 assert(DefaultTiles::selectAt(5,0,platform)==11);
 assert(!DefaultTiles::contains(99)&&!DefaultTiles::contains(104));
}
