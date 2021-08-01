#include "App.hpp"
#include <locale>
int main() {
    // for work with console
    std::locale::global(std::locale("en_US.UTF-8"));
    App app;
    app.Run();
    return 0;
}