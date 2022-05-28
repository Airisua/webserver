#include "epoll/server.h"

int main(int argc,char* argv[]) {

    Server server;
    if(server.init_server(26117,"wlic","123456","test_db",true,1,1024)) {
        server.run();
    }

    return 0;
}

