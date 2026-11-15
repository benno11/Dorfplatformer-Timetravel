#include "CampaignObjectRepair.h"
#include "AssetPath.h"
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <iostream>
static std::string damaged;
std::string ReadTextFile(const std::string& path){
    if(!damaged.empty()&&(path=="assets/levels/level_039.txt"||path=="custom/repair-test"))return damaged;
    std::ifstream f(path);return {std::istreambuf_iterator<char>(f),{}};
}
bool FileExists(const std::string& path){return std::ifstream(path).good();}
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){
    const auto original=ReadTextFile("assets/levels/level_039.txt");damaged=original;
    auto p=damaged.find("59 1840 496");require(p!=std::string::npos,"fixture");damaged.replace(p,11,"59 6840 496");
    TileMap map;LevelMeta meta;std::vector<ObjectInstance> objects;
    require(loadLevelBNNLVL("assets/levels/level_039.txt",map,objects,meta),"campaign load");
    require(std::any_of(objects.begin(),objects.end(),[](const auto& o){return o.id=="59"&&o.x==1840&&o.y==496;}),"runtime repair missing");
    require(loadLevelBNNLVL("custom/repair-test",map,objects,meta),"custom load");
    require(std::any_of(objects.begin(),objects.end(),[](const auto& o){return o.x==6840;}),"custom levels were modified");
    const auto refs=nlohmann::json::parse(ReadTextFile("assets/levels/object-reference.json"));
    objects[0].id="1234";
    require(CampaignObjectRepair::repair(objects,map,refs["level_039.txt"])==0,"mismatched IDs must not be guessed");
    std::cout<<"Runtime repair restores verified campaign positions and preserves custom levels\n";
}
