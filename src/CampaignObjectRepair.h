#pragma once
#include "LevelLoader.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <cmath>
#include <cstdlib>
namespace CampaignObjectRepair {
inline bool bundled(const std::string& path){
    if(path.find("://")!=std::string::npos)return false;
    const auto normalized=std::filesystem::path(path).lexically_normal().generic_string();
    return normalized.rfind("assets/levels/",0)==0 || normalized.find("/assets/levels/")!=std::string::npos;
}
inline bool invalid(const ObjectInstance& object,const TileMap& map){
    if(!std::isfinite(object.x)||!std::isfinite(object.y)||object.x<0||object.y<0||
        object.x>=map.w*map.tileSize||object.y>=map.h*map.tileSize)return true;
    // Parameter objects support deliberate sub-tile positioning.
    const int id=std::atoi(object.id.c_str());
    return id<1000 && (std::fabs(std::remainder(object.x-map.tileSize*0.5f,map.tileSize))>0.01f ||
                      std::fabs(std::remainder(object.y-map.tileSize*0.5f,map.tileSize))>0.01f);
}
inline size_t repair(std::vector<ObjectInstance>& objects,const TileMap& map,const nlohmann::json& reference){
    if(map.tileSize<=0||!reference.is_array()||reference.size()!=objects.size())return 0;
    // Match the complete ordered ID sequence before trusting per-slot positions.
    for(size_t i=0;i<objects.size();++i){
        const auto& row=reference[i];
        if(!row.is_array()||row.size()!=3||!row[0].is_number_integer()||!row[1].is_number()||!row[2].is_number()||
            std::to_string(row[0].get<int>())!=objects[i].id)return 0;
        auto expected=objects[i];expected.x=row[1].get<float>();expected.y=row[2].get<float>();
        if(invalid(expected,map))return 0;
    }
    size_t count=0;
    for(size_t i=0;i<objects.size();++i)if(invalid(objects[i],map)){
        objects[i].x=reference[i][1].get<float>();objects[i].y=reference[i][2].get<float>();++count;
    }
    return count;
}
}
