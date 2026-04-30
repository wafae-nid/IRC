#include "server_client.hpp"

int main()
{

    std::vector<Client> clients;

    std::cout << "im in server\n";
    int fd =socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in addr;

    if(fd == -1)
    {
        std::cout << "socket failed \n";
        return(1);
    }
    std::cout << fd <<"\n";  
    addr.sin_family = AF_INET;
    addr.sin_port = htons(6667);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if(bind(fd,(struct sockaddr *)&addr,sizeof(addr)) == -1) // struct sockaddr for any type of addr nor just ipv4
    {
        std::cout << "binding failed \n";
        return(1);
    }
    if(listen(fd,2) == -1) // this doesnt mean i can have only 2 connections but my waiting queue is of size 2 and i do accept once so the third connection goes to the queue too  ps::this is accept queue and kernel might strech it eve though i did write 2
    {
        std::cout << "listening failed \n";
        return(1);
    }
    std::cout << "server ready \n";
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);

    while(1)
    {
        Client client_;
        size_t bytes = 0;
        char buff[1024];
        int i = 0;


        int connect_socket = accept(fd,(struct sockaddr *)&client_addr,&len);
        if(connect_socket == -1)
        {
            std::cout <<"failed to accept \n";
            return(1);
        }
        if(connect_socket ==  -1)
        {   
            std::cout << "accept failed \n";
            break;
        }
       client_.fd = connect_socket;
        
        bytes = recv(connect_socket,&buff,1026,0);
        if(bytes <= 0)
            break;
        buff[bytes] = '\0';
        client_.buffer = buff;
        clients.push_back(client_);
        std::cout << clients[i].buffer<<"\n" ;
        i++;
    }


}