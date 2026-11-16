// Exercise the actual compatibility adapter against SDL_mixer's streaming API.
#include "../src/AudioSystem.cpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <cmath>
std::string ResolveAssetPath(const std::string& path){return path;}
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
static void wave(const std::filesystem::path& path){
    std::ofstream file(path,std::ios::binary);
    auto number=[&](unsigned n,int bytes){for(int i=0;i<bytes;++i)file.put(static_cast<char>((n>>(8*i))&255));};
    file.write("RIFF",4);number(36+1600,4);file.write("WAVEfmt ",8);number(16,4);
    number(1,2);number(1,2);number(8000,4);number(16000,4);number(2,2);number(16,2);
    file.write("data",4);number(1600,4);for(int i=0;i<800;++i)number(i%16<8?1000:64536,2);
}
int main(){
    SDL_setenv_unsafe("SDL_AUDIODRIVER","dummy",1);
    require(SDL_Init(SDL_INIT_AUDIO),"SDL audio init");
    AudioSystem audio;require(audio.initialize(),"mixer init");
    const auto path=std::filesystem::temp_directory_path()/("df-stream-"+std::to_string(SDL_GetTicksNS())+".wav");wave(path);
    auto* effect=Mix_LoadWAVCompat(path.string().c_str());
    auto* music=Mix_LoadMUSCompat(path.string().c_str());
    require(effect&&music&&g_mix_streams.empty(),"registration must not retain decoded audio or open streams");
    Mix_VolumeChunkCompat(effect,0);Mix_VolumeMusicCompat(64);
    const int a=Mix_PlayChannelCompat(-1,effect,0),b=Mix_PlayChannelCompat(-1,effect,0);
    require(a>=0&&b>=0&&a!=b,"effects must overlap with independent tracks");
    require(g_mix_streams.size()==2&&g_mix_streams[g_mix_channels[a]]!=g_mix_streams[g_mix_channels[b]],"effects must not share a seek cursor");
    require(Mix_PlayMusicCompat(music,-1)==0,"streaming music start");
    require(MIX_GetTrackGain(g_mix_channels[a])==0.0f,"new muted SFX track must remain silent");
    require(std::fabs(MIX_GetTrackGain(g_mix_music_track)-0.5f)<0.01f,"music volume must apply to new streams");
    Mix_VolumeMusicCompat(128);
    require(MIX_GetTrackGain(g_mix_channels[a])==0.0f,"music volume must not change SFX gain");
    SDL_Delay(350);mix_collect_streams();
    require(Mix_PlayingMusicCompat()!=0,"streamed music must loop past EOF");
    require(!Mix_PlayingCompat(a)&&!Mix_PlayingCompat(b),"one-shot streams must finish");
    require(Mix_PlayingMusicCompat()&&g_mix_streams.size()==1,"looped music must survive effect cleanup");
    Mix_HaltMusicCompat();require(g_mix_streams.empty(),"stopped music stream leaked");
    require(Mix_PlayMusicCompat(music,0)==0,"stream must reopen for replay");
    Mix_HaltChannelCompat(-1);require(g_mix_streams.empty(),"halt all leaked file streams");
    Mix_FreeChunkCompat(effect);Mix_FreeMusicCompat(music);
    auto* mp3=Mix_LoadWAVCompat("assets/Audio/sfx/Coin.mp3");
    require(mp3&&Mix_PlayChannelCompat(-1,mp3,0)>=0,"MP3 effect must stream");
    Mix_HaltChannelCompat(-1);
    auto* menu=Mix_LoadMUSCompat("assets/Audio/Music/Menu.mp3");
    require(menu&&Mix_PlayMusicCompat(menu,-1)==0,"MP3 music must stream");
    Mix_HaltMusicCompat();require(g_mix_streams.empty(),"halt must release file handles");
    Mix_FreeChunkCompat(mp3);Mix_FreeMusicCompat(menu);
    audio.shutdown();require(g_mix_streams.empty(),"shutdown leaked file streams");
    std::filesystem::remove(path);SDL_Quit();
    std::cout<<"Stream registration, overlapping effects, EOF cleanup, music looping and shutdown passed\n";
}
