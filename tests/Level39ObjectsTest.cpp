#include "LevelLoader.h"
#include "EditorObjectTools.h"
#include <fstream>
#include <iterator>
#include <cmath>
#include <stdexcept>
#include <iostream>
std::string ReadTextFile(const std::string& path){
    std::ifstream file(path);return {std::istreambuf_iterator<char>(file),{}};
}
bool FileExists(const std::string& path){return std::ifstream(path).good();}
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){
    TileMap map;LevelMeta meta;std::vector<ObjectInstance> objects;
    require(loadLevelBNNLVL(argc>1?argv[1]:"assets/levels/level_039.txt",map,objects,meta),"level 39 failed to load");
    require(!meta.legacyEncoding&&meta.levelId==39&&map.w==60&&map.h==34,"wrong level 39 fixture");
    require(objects.size()==34,"OBJ section must produce exactly the declared objects");
    for(const auto& object:objects){
        require(object.x>=0&&object.x<map.w*map.tileSize&&object.y>=0&&object.y<map.h*map.tileSize,
                "level 39 object is outside the editor grid");
        require(std::fmod(object.x,32)==16&&std::fmod(object.y,32)==16,"level 39 object is off the tile-center grid");
        require(EditorObjects::find(objects,static_cast<int>(object.x/32),static_cast<int>(object.y/32),object.layer)>=0,
                "editor cannot select an object loaded by gameplay");
    }
    for(const auto& expected:std::vector<ObjectInstance>{{"60",48,784},{"59",1616,208},{"59",1808,496},
        {"59",1840,496},{"59",1872,496},{"61",1680,112},{"97",1840,144}}){
        require(std::any_of(objects.begin(),objects.end(),[&](const ObjectInstance& object){
            return object.id==expected.id&&object.x==expected.x&&object.y==expected.y;
        }),"repaired object differs from its intended level 39 position");
    }
    require(std::count_if(objects.begin(),objects.end(),[](const ObjectInstance& object){
        return object.id=="97"&&object.x==1840&&object.y==144;
    })==1,"duplicate shield at the repaired position");
    require(meta.entitySpawnPos.empty()&&meta.entitySpawnType.empty(),"modern level must not consume legacy object data");
    std::cout<<"All 34 level 39 objects are on-grid, within bounds and selectable in the editor\n";
}
