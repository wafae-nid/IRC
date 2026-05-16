#include "Server.hpp"

void Server::server_setup()
{
    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        std::cout << "socket failed \n";
        return;
    }

    int opt = 1;
    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        std::cout << "setsockopt failed \n";
        return;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(s_port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        std::cout << "binding failed \n";
        return;
    }

    if (listen(server_fd, 10) == -1)
    {
        std::cout << "listening failed \n";
        return;
    }

    pollfd server;
    server.fd = server_fd;
    server.events = POLLIN;
    server.revents = 0;

    fds.push_back(server); // I add server to my vector of fds;

    std::cout << "server ready\n";
}

/* ---------------- MAIN LOOP ---------------- */

void Server::server_core()
{

    while (g_running)
    {
    
        if (poll(fds.data(), fds.size(), -1) < 0)
        {
            std::cout << "poll failed\n";
            break;
        }

        for  (size_t i = 0; i < fds.size();)
        {
           if (fds[i].revents & (POLLHUP | POLLERR))
           {
                remove_client(fds[i].fd);
                continue;
            }
            if(fds[i].revents & (POLLIN))
            {
                if (fds[i].fd == server_fd)
                    handle_new_client();
                else
                    handle_client(fds[i].fd);
            }
            i++; // so i only increment if no removing happened
        }
    }
}

/* ---------------- RUN ---------------- */
void Server::run()
{
    server_setup();
    server_core();
}