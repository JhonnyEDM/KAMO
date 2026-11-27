#include "app/licenses.h"

#include <cstdio>

extern const unsigned char kamo_text_license[];
extern const unsigned kamo_text_license_size;
extern const unsigned char kamo_text_notices[];
extern const unsigned kamo_text_notices_size;
extern const unsigned char kamo_text_dmsans_ofl[];
extern const unsigned kamo_text_dmsans_ofl_size;

namespace kamo::licenses {

namespace {
std::string_view view(const unsigned char* p, unsigned n) { return {reinterpret_cast<const char*>(p), n}; }
}  // namespace

std::string_view kamo() { return view(kamo_text_license, kamo_text_license_size); }
std::string_view notices() { return view(kamo_text_notices, kamo_text_notices_size); }
std::string_view font_ofl() { return view(kamo_text_dmsans_ofl, kamo_text_dmsans_ofl_size); }

void print_all() {
    for (std::string_view t : {kamo(), notices(), font_ofl()}) {
        std::fwrite(t.data(), 1, t.size(), stdout);
        std::fputs("\n\n", stdout);
    }
}

}  // namespace kamo::licenses
