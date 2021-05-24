#include "utils.h"
#include "server.h"


int main(int argc, char *argv[]) {
    auto options = get_options(argc, argv);
    Server server(options);
    server.run();
}
