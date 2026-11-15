#include "SoundEvents.h"
#include "LevelLoader.h"
#include <fstream>
#include <iterator>
#include <filesystem>
#include <thread>
#include <vector>
#include <stdexcept>
#include <iostream>
std::string ReadTextFile(const std::string& path){
    if(path=="valid-sound-level")return "DFLVL4 1 1\n2\nOBJ 0\n";
    if(path=="invalid-sound-level")return "DFLVL4 -1 1\n";
    std::ifstream input(path);return {std::istreambuf_iterator<char>(input),{}};
}
bool FileExists(const std::string& path){return std::filesystem::exists(path);}
int main(){
    using namespace SoundEvents;
    for(auto path:paths) if(!std::filesystem::is_regular_file(path)) throw std::runtime_error(path);
    take();
    std::vector<std::thread> workers;
    for(unsigned i=0;i<paths.size();++i)workers.emplace_back([i]{for(int j=0;j<1000;++j)emit(static_cast<Event>(i));});
    for(auto& worker:workers)worker.join();
    if(take()!=((1u<<paths.size())-1) || take()!=0)throw std::runtime_error("Lost or repeated queued sound");
    TileMap map;LevelMeta meta;std::vector<ObjectInstance> objects;
    if(!loadLevelBNNLVL("valid-sound-level",map,objects,meta) || take()!=0)
        throw std::runtime_error("Successful load emitted an error");
    if(loadLevelBNNLVL("invalid-sound-level",map,objects,meta) || take()!=(1u<<static_cast<unsigned>(Event::Error)))
        throw std::runtime_error("Failed load did not emit an error");
    std::cout<<"All event assets exist; concurrent events coalesce and drain once\n";
}
