#pragma once

#include "savetinker/network.hpp"

namespace st {

class CurlHttpClient : public HttpClient {
public:
    explicit CurlHttpClient(std::string userAgent);
    ~CurlHttpClient() override;

    std::expected<HttpResponse, std::string> get(const std::string& url, std::uint64_t maxBytes,
                                                 const ProgressCallback& progress = {}) override;

private:
    std::string userAgent_;
};

}
