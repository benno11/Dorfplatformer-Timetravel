#include "GameSandbox.h"
#include <fstream>
#include <cassert>
#include <unistd.h>
#include <vector>
std::filesystem::path state;
int entry(int,char**){
    bool again=!std::filesystem::exists(state);
    std::ofstream out(state,std::ios::app);out<<getpid()<<"\n";out.close();return again?75:0;
}
int main(int argc,char** argv){
    state=std::filesystem::temp_directory_path()/("df-broker-test-"+std::to_string(getpid()));
    std::filesystem::remove(state);
    const auto broker=getpid();
    const int result=GameSandbox::supervise(argc,argv,entry);
    if(getpid()!=broker)return result;
    assert(result==0);
    std::ifstream in(state);int first=0,second=0;in>>first>>second;
    assert(first>0&&second>0&&first!=second&&first!=getpid()&&second!=getpid());
    std::filesystem::remove(state);
}
