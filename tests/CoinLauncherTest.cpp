#include "CoinLauncher.h"
#include <cassert>
int main() {
    float y=200, vy=900;
    assert(CoinLauncher::bounce(31,100,100,100,0,100,y,vy)==-1);
    assert(y==76 && vy==-1800);
    // No retrigger while moving away, and springs ignore underside hits.
    float next=y-10;
    assert(!CoinLauncher::bounce(31,100,100,100,y,100,next,vy));
    y=0; vy=-900;
    assert(!CoinLauncher::bounce(31,100,100,100,200,100,y,vy));
    assert(CoinLauncher::bounce(46,100,100,100,200,100,y,vy)==1);
    assert(y==122 && vy==1200);
    y=200; vy=900;
    assert(CoinLauncher::bounce(46,100,100,100,0,100,y,vy)==-1);
    assert(y==78 && vy==-1200);
    y=200; vy=900;
    assert(!CoinLauncher::bounce(46,100,100,150,0,150,y,vy));
    // Test horizontal position at time of crossing, rather than frame end.
    y=200; vy=900;
    assert(CoinLauncher::bounce(31,100,100,62,0,162,y,vy)==-1);
    // Both wrapped axes; the object image is centered at (420, 420).
    y=500; vy=900;
    assert(CoinLauncher::bounce(31,20,20,420,300,420,y,vy,400,400)==-1);
    assert(y==396);
    y=500; vy=900;
    assert(!CoinLauncher::bounce(31,20,20,420,300,420,y,vy));
}
