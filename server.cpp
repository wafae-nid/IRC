#include "Server.hpp"

const std::string Server::SERVER_PASSWORD = "1234";
int  g_running = 1;

Server::Server()
{
   
    server_fd = -1;
    max_fd = -1;
    signal(SIGINT,  Server::signal_handler);
    FD_ZERO(&original_set);

}

Server::~Server()
{
    if (server_fd != -1)
        close(server_fd);
}

void Server::signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
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



void Server::recompute_max_fd()
{
    max_fd = server_fd;
    for (size_t i = 0; i < clients.size(); i++)
    {
        if (clients[i].fd > max_fd)
            max_fd = clients[i].fd;
    }
}

/* ---------------- CLIENT CLEANUP ---------------- */

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
    FD_CLR(fd, &original_set);
    close(fd);
    recompute_max_fd();
}

/*--------------- REGISTER ---------------- */

void Server::try_register(Client *client)
{
    if (client->pass_ok && client->nick_set && client->user_set && !client->registered)
    {
        client->registered = true;
        if(send_to_client(client->fd,"001 " + client->nickname + " :Welcome to the IRC server") == 0)
            remove_client(client->fd);
    }
}

/* ---------------- COMMANDS (logic ) ---------------- */


void Server::nick_command(Client *client, std::string command)
{
    std::string nick = command.substr(4);
    size_t index = nick.find_first_not_of(' ');
    if (index == std::string::npos)
    {
       if(send_to_client(client->fd, "461 PASS :Not enough parameters") == 0)
            remove_client(client->fd);
        return;
    }
    client->nickname = nick.substr(index);
    client->nick_set = true;
    try_register(client);
}

void Server::pass_command(Client *client, std::string command)
{
    std::string pass = command.substr(4);
    size_t index = pass.find_first_not_of(' ');
    if (index == std::string::npos)
    {
        if(send_to_client(client->fd, "461 PASS :Not enough parameters") == 0)
            remove_client(client->fd);
        return;
    }
    if (pass.substr(index) == SERVER_PASSWORD)
        client->pass_ok = true;
    else
    {  
        if(send_to_client(client->fd, "464 PASS :Wrong password") == 0)
            remove_client(client->fd);
    }

}

void Server::user_command(Client *client, std::string command)
{
    std::string user = command.substr(4);
    size_t index = user.find_first_not_of(' ');
    if (index == std::string::npos)
    {
        if(send_to_client(client->fd, "461 PASS :Not enough parameters") == 0)
            remove_client(client->fd);
        return;
    }
    client->username = user.substr(index);
    client->user_set = true;
    try_register(client);
}

/* ---------------- COMMAND HANDLER ---------------- */

void Server::handle_command(Client *client, std::string command)
{
    if (!client->pass_ok)
    {
        if (command.rfind("PASS", 0) == 0)
            pass_command(client, command);
        return;
    }

    if (command.rfind("PASS", 0) == 0)
        return;
    else if (command.rfind("NICK", 0) == 0)
        nick_command(client, command);
    else if (command.rfind("USER", 0) == 0)
        user_command(client, command);
    else
        send_to_client(client->fd, "Command not found");
}


/* ---------------- BUFFER ---------------- */


void Server::check_buffer(Client *client)
{
    size_t pos;
    std::string command;

    while ((pos = client->buffer.find("\r\n")) != std::string::npos)
    {
        command = client->buffer.substr(0, pos);
        handle_command(client, command);
        client->buffer.erase(0, pos + 2);
    }
}

/* ---------------- CLIENT HANDLING ---------------- */

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

void Server::server_setup()
{
    struct sockaddr_in addr;

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
    addr.sin_port = htons(6667);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

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

    FD_SET(server_fd, &original_set);
    max_fd = server_fd;

    std::cout << "server ready\n";
}

/* ---------------- MAIN LOOP ---------------- */

void Server::server_core()
{
    fd_set readfds;

    while (g_running)
    {
        readfds = original_set;

        if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0)
        {
            std::cout << "select failed \n";
            break;
        }

        for (int fd = 0; fd <= max_fd; fd++)
        {
            if (!FD_ISSET(fd, &readfds))
                continue;
            if (fd == server_fd)
            {
                struct sockaddr_in addr;
                socklen_t len = sizeof(addr);

                int client_fd = accept(fd, (struct sockaddr *)&addr, &len);
                if (client_fd < 0)
                {
                    std::cout<<"accept failed\n";
                    continue;
                }

                FD_SET(client_fd, &original_set);

                Client c;
                c.fd = client_fd;
                c.pass_ok = false;
                c.nick_set = false;
                c.user_set = false;
                c.registered = false;

                clients.push_back(c);

                recompute_max_fd();
            }
            else
                handle_client(fd);
        }
    }
}

/* ---------------- RUN ---------------- */

void Server::run()
{
    server_setup();
    server_core();
}

int main()
{

   signal(SIGPIPE, SIG_IGN);
   Server server;
   server.run(); 

}
