#include "Server.hpp"

std::string Server::prefix(Client &c)
{
    return ":" + c.nickname + "!" + c.username + "@" + c.hostname;
}

void Server::reply(Client *client, const std::string &code, const std::string &command, const std::string &message)
{
    std::string msg = ":" + SERVER_NAME + " " + code;

    std::string target;

    if (client->registered && !client->nickname.empty())
        target = client->nickname;
    else
        target = "*";

    msg += " " + target;

    if (!command.empty())
        msg += " " + command;

    msg += " :" + message;

    if (!send_to_client(client->fd, msg))
        remove_client(client->fd);
}

bool Server::send_to_client(int fd, std::string msg)
{
    msg += "\r\n";
    if(send(fd, msg.c_str(), msg.size(), 0) == -1)
    {
        return false;
    }
    return true;
}

/* ---------------- CLIENT HANDLING ---------------- */
void Server::handle_new_client(void)
{
    sockaddr_in addr;
    socklen_t len = sizeof(addr);

    int client_fd = accept(server_fd, (sockaddr*)&addr, &len);
    if (client_fd < 0)
    {
        std::cout << "accept failed\n";
        return;
    }

    if (fcntl(client_fd , F_SETFL, O_NONBLOCK) == -1) // so the socket fd becomes non blocking
    {
        std::cout << "fcntl failed \n";
        close(client_fd);
        return;
    }
    
    char ip[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip)) == NULL)
    {
        close(client_fd);
        return ;
    }

    pollfd client;
    client.fd = client_fd;
    client.events = POLLIN;
    client.revents = 0;

    fds.push_back(client);

    Client c;
    c.fd = client_fd;
    c.pass_ok = false;
    c.nick_set = false;
    c.user_set = false;
    c.registered = false;
    c.hostname = ip;

    clients.push_back(c);
}

void Server::handle_client(int client_fd)
{
    char buff[1024];
    ssize_t bytes = recv(client_fd, buff, sizeof(buff) - 1, 0);

    if (bytes <= 0)
    {
        remove_client(client_fd);
        return;
    }

    buff[bytes] = '\0';

    for (size_t i = 0; i < clients.size(); i++)
    {
        if (clients[i].fd == client_fd)
        {
            clients[i].buffer.append(buff, bytes);
            check_buffer(&clients[i]);
            break;
        }
    }
}

void Server::remove_client(int fd)
{
    for (size_t i = 0; i < clients.size(); i++)
    {
        if (clients[i].fd == fd)
        {
            clients.erase(clients.begin() + i);
            break;
        }
    }
     for (size_t i = 0; i < fds.size(); i++)
    {
        if (fds[i].fd == fd)
        {
            fds.erase(fds.begin() + i);
            break;
        }
    }
    close(fd);
}