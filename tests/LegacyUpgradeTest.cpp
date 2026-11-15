#include "LevelLoader.h"
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <iostream>
static std::string input;
std::string ReadTextFile(const std::string& path){
    if(path=="fixture")return input;
    std::ifstream f(path);return {std::istreambuf_iterator<char>(f),{}};
}
bool FileExists(const std::string& path){return std::ifstream(path).good();}
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){
    TileMap map;LevelMeta meta;std::vector<ObjectInstance> objects;
    require(loadLevelBNNLVL("tests/fixtures/level39-legacy.txt",map,objects,meta),"original level 39 must load");
    require(objects.size()==49&&meta.legacyEncoding,"original level 39 object count must be exact");
    const auto originals=objects;const auto tiles=map.tileIds;
    input=SerializeLevelBNNLVL(map,objects,meta);
    require(!input.empty()&&loadLevelBNNLVL("fixture",map,objects,meta),"upgrade reload failed");
    require(objects.size()==49&&map.tileIds==tiles&&!meta.legacyEncoding,"upgrade changed object count or grid");
    for(size_t i=0;i<objects.size();++i)
        require(objects[i].id==originals[i].id&&objects[i].x==originals[i].x&&objects[i].y==originals[i].y,
                "upgrade changed object identity or coordinates");
    require(SerializeLevelBNNLVL(map,objects,meta)==input,"second upgrade must be byte stable");
    for(const auto& bad:{"1_2_2_0_2a", "1_2_2_0_2e_0", "1_2_2_0_2d_1_1",
        "1_2_2_0_2d_1_5_46", "1_2_2_0_2d_1_1a_46", "1_2_2_0_2d_1_1_0",
        "1_2_2_0_2d_1_1_46_999_888", "1_2_2_0_2d_2a_1_46", "1_2_2_0_2d_1_1_99999999999999999999",
        "1_2_2_0_99999999999999999999a_2c_0"}){
        const auto before=SerializeLevelBNNLVL(map,objects,meta);
        input=bad;require(!loadLevelBNNLVL("fixture",map,objects,meta),"malformed legacy data was accepted for upgrade");
        require(SerializeLevelBNNLVL(map,objects,meta)==before,"failed upgrade exposed partial decoded state");
    }
    input="1_2_2_0_2d_1_1_46";
    require(loadLevelBNNLVL("fixture",map,objects,meta)&&objects.size()==1&&objects[0].id=="46",
            "object count must be read immediately after the last run");
    objects.push_back({"invalid-object-id",16,16});
    require(SerializeLevelBNNLVL(map,objects,meta).empty(),"upgrade must reject invalid IDs instead of dropping objects");
    std::cout<<"Legacy upgrade preserves all 49 original objects and rejects malformed section boundaries\n";
}
