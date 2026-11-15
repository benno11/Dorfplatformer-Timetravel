#include "NetworkTransport.h"
#include "SoundEvents.h"
#include "AudioSystem.h"
#include <stdexcept>
#include <string>
// No window or audio device is needed to verify transport event production.
void AudioSystem::pumpEventSfx() {}
void DrawGameText(SDL_Renderer*,int,int,int,const std::string&) {}
int main(){
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* request=curl_easy_init();
    if(!request)throw std::runtime_error("curl init");
    SoundEvents::take();
    curl_easy_setopt(request,CURLOPT_URL,"file:///df-no-such-sound-test-file-934832");
    if(PerformNetworkRequest(request)==CURLE_OK)throw std::runtime_error("Expected failed request");
    if(SoundEvents::take()!=(1u<<static_cast<unsigned>(SoundEvents::Event::Error)))
        throw std::runtime_error("Failed transport did not emit an error sound");
    curl_easy_setopt(request,CURLOPT_URL,"file:///dev/null");
#ifndef _WIN32
    if(PerformNetworkRequest(request)!=CURLE_OK || SoundEvents::take()!=0)
        throw std::runtime_error("Successful request emitted an error sound");
#endif
    curl_easy_cleanup(request);curl_global_cleanup();
}
