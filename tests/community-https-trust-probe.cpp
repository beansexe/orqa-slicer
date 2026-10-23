#define CURL_STATICLIB
#include <curl/curl.h>
#include <iostream>

static size_t discard(char*, size_t a, size_t b, void*) { return a * b; }
int main(int argc, char** argv)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* curl = curl_easy_init();
    char error[CURL_ERROR_SIZE] = {};
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.bambulab.com/");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    if (argc == 2) curl_easy_setopt(curl, CURLOPT_CAINFO, argv[1]);
    const auto result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    std::cout << "TLS backend: " << curl_version_info(CURLVERSION_NOW)->ssl_version
              << "; result " << result << "; HTTP " << status << "; " << error << '\n';
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return result == CURLE_OK ? 0 : 1;
}
