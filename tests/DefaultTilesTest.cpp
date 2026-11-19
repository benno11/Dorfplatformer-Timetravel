#include "DefaultTiles.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <initializer_list>
int main(){
 assert(DefaultTiles::select(false,false,true,false)==40);
 assert(DefaultTiles::select(false,true,false,true)==9);
 assert(DefaultTiles::select(false,true,true,false)==11);
 assert(DefaultTiles::select(false,false,false,true)==39);
 for(int mask=0;mask<16;++mask) assert(DefaultTiles::contains(DefaultTiles::select(mask&1,mask&2,mask&4,mask&8)));
 for(int id=100;id<=103;++id) assert(DefaultTiles::contains(id));
 auto strip=[](int x,int y){return y==0 && x>=0 && x<=5;};
 assert(DefaultTiles::selectAt(0,0,strip)==39);
 assert(DefaultTiles::selectAt(1,0,strip)==38);
 assert(DefaultTiles::selectAt(4,0,strip)==38);
 assert(DefaultTiles::selectAt(5,0,strip)==40);
 auto platform=[](int x,int y){return y>=0 && y<=2 && x>=0 && x<=5;};
 assert(DefaultTiles::selectAt(0,0,platform)==9);
 assert(DefaultTiles::selectAt(1,0,platform)==10);
 assert(DefaultTiles::selectAt(4,0,platform)==10);
 assert(DefaultTiles::selectAt(5,0,platform)==11);
 // A shelf meets a vertical wall. The extension belongs to the wall cell.
 for (bool grassOnRight : {false,true}) for (bool slim : {false,true}) {
  auto wallAndShelf=[&](int x,int y) {
   if (x==0) return y>=-2 && y<=2;
   const bool shelfX=grassOnRight ? x>=1 && x<=3 : x>=-3 && x<=-1;
   return shelfX && y>=0 && y<=(slim ? 0 : 2);
  };
  const auto expected=grassOnRight ? (slim ? 100 : 102) : (slim ? 103 : 101);
  assert(DefaultTiles::selectAt(0,0,wallAndShelf)==expected);
  // Neither the shelf nor the wall above/below becomes an extension.
  assert(DefaultTiles::selectAt(0,-1,wallAndShelf)==DefaultTiles::select(true,true,false,false));
  assert(DefaultTiles::selectAt(grassOnRight ? 2 : -2,0,wallAndShelf)==(slim ? 38 : 10));
 }
 assert(!DefaultTiles::contains(99)&&!DefaultTiles::contains(104));
}
