
#include "server_client.hpp"

int main()
{
    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(6667);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    for(int i = 0;i < 5 ;i++)
    {
        int client_fd = socket(AF_INET,SOCK_STREAM,0);
        std::cout<< client_fd << "\n"; // each process had its ows file descriptor table 
        if(client_fd == -1)
        {
            std::cout << " client socket failed \n";
            return(1);
        }
        if(connect(client_fd,(struct sockaddr *)&addr,sizeof(addr)) == -1)
        {
            std::cout << "client failed to connect to server \n";
            return(1);
        }
        std::cout << "connection made  \n";
        const char *msg = "hello server\n";
        send(client_fd, msg, 13, 0);
    }
}