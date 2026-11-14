#include "curl_http_client.hpp"

#include <curl/curl.h>

namespace st {

namespace {

struct Transfer {
    HttpResponse response;
    std::uint64_t maxBytes;
    bool tooLarge = false;
    const ProgressCallback* progress;
};

std::size_t receive(char* data, std::size_t size, std::size_t count, void* userData) {
    auto& transfer = *static_cast<Transfer*>(userData);
    const std::size_t length = size * count;
    if (transfer.response.body.size() + length > transfer.maxBytes) {
        transfer.tooLarge = true;
        return 0;
    }
    transfer.response.body.insert(transfer.response.body.end(), data, data + length);
    return length;
}

int reportProgress(void* userData, curl_off_t total, curl_off_t received, curl_off_t, curl_off_t) {
    auto& transfer = *static_cast<Transfer*>(userData);
    if (transfer.progress && *transfer.progress)
        (*transfer.progress)(static_cast<std::uint64_t>(received), static_cast<std::uint64_t>(total));
    return 0;
}

}

CurlHttpClient::CurlHttpClient(std::string userAgent) : userAgent_(std::move(userAgent)) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

CurlHttpClient::~CurlHttpClient() {
    curl_global_cleanup();
}

std::expected<HttpResponse, std::string> CurlHttpClient::get(const std::string& url, std::uint64_t maxBytes,
                                                              const ProgressCallback& progress) {
    if (!url.starts_with("https://") && !url.starts_with("http://")) return std::unexpected("unsupported URL: " + url);
    CURL* handle = curl_easy_init();
    if (!handle) return std::unexpected("cannot start a download");

    Transfer transfer{{}, maxBytes, false, &progress};
    char errorText[CURL_ERROR_SIZE] = {};
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_USERAGENT, userAgent_.c_str());
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT, 64L);
    curl_easy_setopt(handle, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, reportProgress);
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &transfer);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, errorText);

    const CURLcode code = curl_easy_perform(handle);
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &transfer.response.status);
    curl_easy_cleanup(handle);

    if (transfer.tooLarge) return std::unexpected("download is larger than allowed");
    if (code != CURLE_OK) return std::unexpected(errorText[0] ? std::string(errorText) : std::string(curl_easy_strerror(code)));
    return std::move(transfer.response);
}

}
