#include "server_client.hpp"



void check_buffer(Client *client)
{
    size_t pos;
    std::string command;
    while ((pos = client->buffer.find("\r\n")) != std::string::npos)
    {
        command = client->buffer.substr(0, pos);
        client->buffer.erase(0, pos + 2);
        std::cout << "[" << command << "]\n";
    }
}

void handle_client(int client_fd, std::vector<Client> *clients, fd_set *original_set)
{
    Client client_;

    char buff[1024];
    size_t bytes = 0;

    
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
            return;
        }
    }
    client_.fd = client_fd;;
    client_.buffer.append(buff, bytes);
    check_buffer(&client_);
    clients->push_back(client_);
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
    addr.sin_family = AF_INET;
    addr.sin_port = htons(6667);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if(bind(server_fd,(struct sockaddr *)&addr,sizeof(addr)) == -1) // struct sockaddr for any type of addr nor just ipv4
    {
        std::cout << "binding failed \n";
        return(-1);
    }
    if(listen(server_fd,2) == -1) // this doesnt mean i can have only 2 connections but my waiting queue is of size 2 and i do accept once so the third connection goes to the queue too  ps::this is accept queue and kernel might strech it eve though i did write 2
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

    while(1)
    {
        fd_reads = original_set;
        select(max_fd + 1, &fd_reads, NULL, NULL, NULL);
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
                if(client_fd > max_fd) // if a client fd is closed the kernel will cycle its fd thats why this check is important
                    max_fd = client_fd;
                std::cout << "handle serveer with accept\n";
            }
            else
            {
                handle_client(fd, clients, &original_set);
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