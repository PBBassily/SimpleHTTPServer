#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <vector>
#include <algorithm>


#define PORT "3490"  // the port users will be connecting to

#define BACKLOG 10	 // how many pending connections queue will hold
#define MAXDATASIZE 1000 // max number of bytes we can get at once

using namespace std;

void sigchld_handler(int s)
{
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
    {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

string check_type(string path)
{

    transform(path.begin(), path.end(), path.begin(), ::tolower);
    if(path.find(".html")!=string::npos)
    {
        return "text/html";
    }

    else if(path.find(".jpeg")!=string::npos)
    {
        return "image/jpeg";
    }
    else if(path.find(".png")!=string::npos)
    {
        return "image/png";
    }
    return "text/plain";

}
vector<string> get_first_line(char buffer[])
{
    int length = strlen(buffer);
    string line="";
    vector<string> data;
    for(int i = 0 ; i < length ; i++)
    {
        if(buffer[i] == '\r')
        {
            break;
        }

        if(buffer[i] == ' ')
        {
            //  line+='\0';
            data.push_back(line);
            line = "";
        }
        else
        {
            line+=buffer[i];
        }

    }
    return data;
}


char* reply_wrapper(char *buffer,int size,string type)
{

    string reply;

    reply = "HTTP/1.1 200 OK\r\n";
    reply+="Content-Length: ";
    reply+= size;
    reply +="\r\nContent-Type: "+type+"\r\n";
    reply+="\r\n";
    reply+=buffer;
    //reply+="\0";
    char *cstr = new char[reply.length() + 1];
    strcpy(cstr, reply.c_str());
    return cstr;

}

char* get_file(string name)
{
    name.erase(0, 1);

    if (FILE *fp = fopen(name.c_str(), "r+b"))
    {

        char *buffer = NULL;
        size_t size = 0;

        /* Open your_file in read-only mode */


        /* Get the buffer size */
        fseek(fp, 0, SEEK_END); /* Go to end of file */
        size = ftell(fp); /* How many bytes did we pass ? */

        /* Set position of stream to the beginning */
        rewind(fp);

        /* Allocate the buffer (no need to initialize it with calloc) */
        buffer = (char*)malloc((size + 1) * sizeof(*buffer)); /* size + 1 byte for the \0 */

        /* Read the file into the buffer */
        fread(buffer, size, 1, fp); /* Read 1 chunk of size bytes from fp into buffer */

        /* NULL-terminate the buffer */
        buffer[size] = '\0';

        printf("%s\n", buffer);
        cout<<"size of buffer "<<size;
        return reply_wrapper(buffer,size,check_type(name));

    }
    else
    {
        char *reply;
        reply = "HTTP/1.1 404 Not Found\r\n";
        return reply;

    }
}


int main(void)
{
    char buf[MAXDATASIZE];
    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    char s[INET6_ADDRSTRLEN];
    int rv;

    memset(&hints, 0, sizeof hints);

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next)
    {

        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1)
        {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                       sizeof(int)) == -1)
        {
            perror("setsockopt");
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)
    {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1)
    {
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchld_handler; // reap all dead processes

    sigemptyset(&sa.sa_mask);

    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(1);
    }

    printf("server: waiting for connections...\n");

    while(1)    // main accept() loop
    {

        sin_size = sizeof their_addr;

        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);

        if (new_fd == -1)
        {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family,
                  get_in_addr((struct sockaddr *)&their_addr),
                  s, sizeof s);

        printf("server: got connection from %s\n", s);

        if (!fork())   // this is the child process
        {
            close(sockfd); // child doesn't need the listener

            int numbytes;
            if ((numbytes = recv(new_fd, buf, MAXDATASIZE-1, 0)) == -1)
            {
                perror("recv");
                exit(1);
            }
            // print the get request
            printf("server: numbytes '%d'\n",numbytes);
            buf[numbytes] = '\0';

            vector<string> data = get_first_line(buf);

            string first_word = data[0];

            if((first_word.compare("GET")) == 0)
            {
                cout<<"get"<<"\n";

                char * reply = get_file(data[1]);

                cout<<"\n"<<reply<<"\n";
                if (send(new_fd, reply, strlen(reply), 0) == -1)
                    perror("send");
            }
            else if((first_word.compare("POST")) == 0)
            {
                cout<<"post";
            }
            else
            {
                cout<<"ERROR no get or post found";
            }

            close(new_fd);
            exit(0);
        }
        close(new_fd);  // parent doesn't need this
    }

    return 0;
}
