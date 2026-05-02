#include "server_client.hpp"

static const std::string SERVER_PASSWORD = "1234";
void recompute_max_fd(std::vector<Client> &clients, int server_fd, int *max_fd)
{
    *max_fd = server_fd;
    for (size_t i = 0; i < clients.size(); i++)
    {
        if (clients[i].fd > *max_fd)
            *max_fd = clients[i].fd;
    }
}
void try_register(Client *client)
{
    if (client->pass_ok && client->nick_set && client->user_set)
    {
        if (!client->registered)
        {
            client->registered = true;
            std::cout << "Client registered!\n";
        }
    }
}

void nick_command(Client *client, std::string nick)
{
    if (nick.empty())
    {
        std::cout << "ERR: No nickname given\n";
        return;
    }
    client->nickname = nick;
    std::cout << client-> nickname <<"\n";
    client->nick_set = true;
}
void pass_command(Client *client, std::string password)
{
  if (password == SERVER_PASSWORD)
  {
    std::cout << "good password\n";
    client->pass_ok = true;
  }
  else
    std::cout << "Wrong password\n";
}
void user_command(Client *client, std::string user)
{
    if (user.empty())
    {
        std::cout << "ERR: No Username given\n";
        return;
    }
    client->username = user;
    std::cout << client->username <<"\n";
    client->user_set = true;
}
void handle_command(Client *client, std::string command)
{
    std::string password;
    size_t index = 0;

    if (!client->pass_ok)
    {
        if(command.rfind("PASS",0)== 0)
        {
            password = command.substr(4); 
            index = password.find_first_not_of(' ');
            if (index == std::string::npos)
            {
                std::cout<< "enter password\n";
                    return;
            } // or error
            pass_command(client, password.substr(index));
        }
        else 
            return;
    }
    else
    {
        if(command.rfind("PASS",0) == 0)
            return;
        else if(command.rfind("NICK",0) == 0)
        {
            password = command.substr(4); 
            index = password.find_first_not_of(' ');
            if (index == std::string::npos)
                    return; // or error
            nick_command(client, password.substr(index));
        }
        else if(command.rfind("USER",0) == 0)
        {   
            password = command.substr(4); 
            index = password.find_first_not_of(' ');
            if (index == std::string::npos)
                    return; // or error
            user_command(client, password.substr(index));

        }
       else    
            std::cout <<"command not found\n";
    }
}
void check_buffer(Client *client)
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

void handle_client(int client_fd, int server_fd,std::vector<Client> *clients, fd_set *original_set, int *max_fd)
{

    char buff[1024];
    ssize_t bytes = 0;

    
    bytes = recv(client_fd,buff,sizeof(buff) - 1,0);
    if(bytes <= 0)
    {
        for (size_t i = 0; i < clients->size(); i++)
        {
            if ((*clients)[i].fd == client_fd)
            {
                clients->erase(clients->begin() + i);
                break;
            }
        }
        FD_CLR(client_fd, original_set);
        recompute_max_fd(*clients, server_fd, max_fd);
        close(client_fd); 
        return;
    }
    buff[bytes] = '\0';
    for (size_t i = 0; i < clients->size(); i++) // this loop is so i dont add an already there client to my vector 
    {
        if((*clients)[i].fd == client_fd)
        {
            (*clients)[i].buffer.append(buff, bytes); 
             check_buffer(&(*clients)[i]);// update existing
            // std::cout << (*clients)[i].buffer << "\n";
            break;
        }
    }
   
}


int server_setup(void)
{
    struct sockaddr_in addr;

    std::cout << "im in server\n";
    int server_fd = socket(AF_INET,SOCK_STREAM,0);

    if(server_fd == -1)
    {
        std::cout << "socket failed \n";
        return(-1);
    }
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
         perror("setsockopt");
        return -1;
    }   
    addr.sin_family = AF_INET;
    addr.sin_port = htons(6667);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if(bind(server_fd,(struct sockaddr *)&addr,sizeof(addr)) == -1) // struct sockaddr for any type of addr nor just ipv4
    {
        std::cout << "binding failed \n";
        return(-1);
    }
    if(listen(server_fd,10) == -1) // this doesnt mean i can have only 2 connections but my waiting queue is of size 2 and i do accept once so the third connection goes to the queue too  ps::this is accept queue and kernel might strech it eve though i did write 2
    {
        std::cout << "listening failed \n";
        return(-1);
    }
    std::cout << "server ready \n";
    return(server_fd);

}

void server_core(int server_fd ,std::vector<Client> *clients)
{
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);

    fd_set original_set, fd_reads;
    FD_ZERO(&original_set);
    FD_SET(server_fd, &original_set);
    int max_fd = server_fd;

    int client_fd;
    Client client_;

    while(1)
    {
        fd_reads = original_set;
        if(select(max_fd + 1, &fd_reads, NULL, NULL, NULL) < 0)
        {
            std::cout << "select failed \n";
            break;
        }
        for(int fd = 0;fd <= max_fd; fd++)
        {
            if(FD_ISSET(fd,&fd_reads) == 0)
                continue;
            if(fd == server_fd)
            {
                client_fd = accept(fd,(struct sockaddr *)&client_addr,&len);
                if(client_fd <0)
                {
                    std::cout<<"accept failed\n";
                    continue;
                }
                FD_SET(client_fd,&original_set);
                client_.fd = client_fd;
                client_.pass_ok = false;
                client_.registered = false;
                client_.nick_set = false;
                client_.user_set = false;
                clients->push_back(client_);
                recompute_max_fd(*clients, server_fd, &max_fd);
                std::cout << "handle serveer with accept\n";
            }
            else
            {
                handle_client(fd,server_fd,clients, &original_set,&max_fd);
            }
        }
    }
}

int main()
{

    std::vector<Client> clients;

    int server_fd = server_setup();
    server_core(server_fd ,&clients);
    

};