

#include "server_client.hpp"

// int main()
// {
//     int sock = socket(AF_INET, SOCK_STREAM, 0);
//     if (sock < 0)
//     {
//         perror("socket");
//         return 1;
//     }

//     sockaddr_in server;
//     server.sin_family = AF_INET;
//     server.sin_port = htons(6667);
//     server.sin_addr.s_addr = inet_addr("127.0.0.1");

//     if (connect(sock, (sockaddr*)&server, sizeof(server)) < 0)
//     {
//         perror("connect");
//         return 1;
//     }

//     std::cout << "Connected to server\n";

//     char buffer[1024];
//     std::string input;

//     while (true)
//     {
//         fd_set set;
//         FD_ZERO(&set);
//         FD_SET(0, &set);
//         FD_SET(sock, &set);

//         int activity = select(sock + 1, &set, NULL, NULL, NULL);
//         if (activity < 0)
//         {
//             perror("select");
//             break;
//         }

//         if (FD_ISSET(0, &set))
//         {
//             std::getline(std::cin, input);
//             if(input == "exit")
//                 break;
//             input += "\r\n";
//             send(sock, input.c_str(), input.size(), 0);
//         }

//         if (FD_ISSET(sock, &set))
//         {
//             int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
//             if (bytes <= 0)
//             {
//                 std::cout << "Server closed connection\n";
//                 break;
//             }
//             buffer[bytes] = '\0';
//             std::cout << "Server: " << buffer;
//         }
//     }

//     close(sock);
//     return 0;
// }  
// #include "server_client.hpp"
#include <poll.h>
#include <unistd.h>
#include <iostream>
#include <string>

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
    server.sin_port = htons(6666);
    server.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (sockaddr*)&server, sizeof(server)) < 0)
    {
        perror("connect");
        return 1;
    }

    std::cout << "Connected to server\n";

    char buffer[1024];
    std::string input;

    struct pollfd fds[2];

    // stdin (keyboard)
    fds[0].fd = 0;
    fds[0].events = POLLIN;

    // socket
    fds[1].fd = sock;
    fds[1].events = POLLIN;

    while (true)
    {
        int activity = poll(fds, 2, -1);
        if (activity < 0)
        {
            perror("poll");
            break;
        }

        // stdin ready
        if (fds[0].revents & POLLIN)
        {
            std::getline(std::cin, input);

            if (input == "exit")
                break;

            input += "\r\n";
            send(sock, input.c_str(), input.size(), 0);
        }

        // socket ready
        if (fds[1].revents & POLLIN)
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