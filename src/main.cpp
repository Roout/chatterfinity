#include "App.hpp"
#include <locale>

int main() {
    // for work with console
    std::locale locale { std::locale::classic(), "en_US.UTF-8", std::locale::ctype };
    std::locale::global(locale);
    App app;
    app.Run();
    return 0;
}