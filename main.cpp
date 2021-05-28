#include "Client.hpp"

int main() {
    auto context = std::make_shared<boost::asio::io_context>();
    std::shared_ptr<Client> client = std::make_shared<Client>(context);
    client->Run();
    return 0;
}