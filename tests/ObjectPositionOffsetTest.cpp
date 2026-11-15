#include "LevelLoader.h"
#include "ObjectWrapping.h"
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <iostream>
static std::string roundTrip;
std::string ReadTextFile(const std::string& path){
    if(path.find("roundtrip-offset-fixture")!=std::string::npos)return roundTrip;
    if(path.find("offset-fixture")!=std::string::npos){
        const bool wrap=path.find("nowrap")==std::string::npos;
        if(path.find("modern")!=std::string::npos)
            return std::string("META wrap_x=")+(wrap?"1":"0")+" ENDMETA\nDFLVL4 3 2\n0 4 2 0 4 2\nOBJ 1\n46 48 48\n";
        // Two rows with an empty first column, tile 4 in column 1, and
        // a bumper centered on tile 4. Campaign loading removes that column.
        return std::string("META wrap_x=")+(wrap?"1":"0")+" ENDMETA\n1_3_2_0_0a_4a_2a_0a_4a_2a_2_3_46_4_96";
    }
    std::ifstream file(path);return {std::istreambuf_iterator<char>(file),{}};
}
bool FileExists(const std::string&){return false;}
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
int main(){
    for(bool wrap:{false,true}){
        TileMap map;LevelMeta meta;std::vector<ObjectInstance> objects;
        const std::string path=std::string("assets/levels/offset-fixture-")+(wrap?"wrap":"nowrap");
        require(loadLevelBNNLVL(path,map,objects,meta),"fixture load failed");
        require(meta.wrapX==wrap&&objects.size()==2,"wrap setting or objects changed");
        require(map.tileIds[3]==4,"campaign tile must be normalized to column zero");
        require(objects[0].x==16&&objects[0].y==48&&objects[1].x==16&&objects[1].y==16,
            "objects must receive the same horizontal shift as tiles");
        roundTrip=SerializeLevelBNNLVL(map,objects,meta);
        { TileMap restored; LevelMeta restoredMeta; std::vector<ObjectInstance> restoredObjects;
          require(loadLevelBNNLVL("assets/levels/roundtrip-offset-fixture",restored,restoredObjects,restoredMeta),"round-trip load failed");
          require(restoredObjects[0].x==16&&restoredObjects[0].y==48,"saving and reloading must not apply the shift twice"); }
        const auto copies=ObjectWrapping::copies(objects[0].x,objects[0].y,32,32,96,0,96,64,wrap?96:0,0);
        require(wrap ? (!copies.empty()&&copies[0].x==0) : copies.empty(),"wrapped object must align with its tile copy");
        require(loadLevelBNNLVL("custom/offset-fixture",map,objects,meta)&&objects[0].x==48,
            "unshifted custom maps must retain original object coordinates");
        require(loadLevelBNNLVL("assets/levels/modern-offset-fixture",map,objects,meta)&&objects[0].x==48,
            "modern explicit object coordinates must not be shifted again");
    }
    std::cout<<"Legacy object positions follow tile normalization; X wrapping adds no offset\n";
}
