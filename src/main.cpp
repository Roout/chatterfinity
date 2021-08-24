#include "App.hpp"
#include <locale>

int main() {
    // use UTF-8 locale
    // excluding number formatting i.e., comma for each thousand 
    // which is a standard numeric facet for UTF-8
    std::locale locale { std::locale::classic()
        , "en_US.UTF-8"
        , std::locale::ctype 
    };
    std::locale::global(locale);
    App app;
    app.Run();
    return 0;
}