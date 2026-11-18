#pragma once
#include "Platform.h"
#if defined(HAVE_CURL) && HAVE_CURL
#include <curl/curl.h>
#if PLATFORMER_IOS
#include "AssetPath.h"
#include <string>
#endif
inline void ConfigureNetworkTls(CURL* request) {
#if PLATFORMER_IOS
    // vcpkg's OpenSSL curl cannot use desktop CA paths inside an iOS sandbox.
    static const std::string certificates = ResolveAssetPath("assets/certificates/cacert.pem");
    curl_easy_setopt(request, CURLOPT_CAINFO, certificates.c_str());
    curl_easy_setopt(request, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(request, CURLOPT_SSL_VERIFYHOST, 2L);
#else
    (void)request;
#endif
}
#endif
