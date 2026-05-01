
#include "server_client.hpp"

// int main()
// {
//     struct sockaddr_in addr;

//     addr.sin_family = AF_INET;
//     addr.sin_port = htons(6667);
//     addr.sin_addr.s_addr = inet_addr("127.0.0.1");
//     for(int i = 0;i < 5 ;i++)
//     {
//         int client_fd = socket(AF_INET,SOCK_STREAM,0);
//         std::cout<< client_fd << "\n"; // each process had its ows file descriptor table 
//         if(client_fd == -1)
//         {
//             std::cout << " client socket failed \n";
//             return(1);
//         }
//         if(connect(client_fd,(struct sockaddr *)&addr,sizeof(addr)) == -1)
//         {
//             std::cout << "client failed to connect to server \n";
//             return(1);
//         }
//         std::cout << "connection made  \n";
//         const char *msg = "hello server\n";
//         send(client_fd, msg, 13, 0);
//     }
// } 

int main()
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("socket");
        return 1;
    }

    sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(6667);
    server.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (sockaddr*)&server, sizeof(server)) < 0)
    {
        perror("connect");
        return 1;
    }

    std::cout << "Connected to server\n";

    // make stdin + socket non-blocking style recv handling
    char buffer[1024];
    std::string input;

    while (true)
    {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(0, &set);      // stdin
        FD_SET(sock, &set);   // socket

        int maxfd = sock;

        int activity = select(maxfd + 1, &set, NULL, NULL, NULL);
        if (activity < 0)
        {
            perror("select");
            break;
        }

        if (FD_ISSET(0, &set))
        {
            std::getline(std::cin, input);
            input += "\n";

            send(sock, input.c_str(), input.size(), 0);
        }

        if (FD_ISSET(sock, &set))
        {
            int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);

            if (bytes <= 0)
            {
                std::cout << "Server closed connection\n";
                break;
            }

            buffer[bytes] = '\0';
            std::cout << "Server: " << buffer;
        }
    }

    close(sock);
    return 0;
}