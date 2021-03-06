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
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <time.h>
#include <mutex>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#define PORT "8080"  // default port if not specified by server arguments

#define BACKLOG 10	 // how many pending connections queue will hold

#define MAXDATASIZE 4096 // max number of bytes we can get or send at once
#define INTER_COMMAND_INTERVAL 1000000 // 1 sec
#define INTER_PACKET_INTERVAL 500 // 500 microsecond
#define FILE_NOT_FOUND_DESC -1 // file descriptor value when file is not found
#define DELAY 10 // inter processes delay


using namespace std;
mutex mtx;
static int *client_num_pointer;

// basic finction
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


/***
    @param file name/path
    @return the string which defines the Content Type
        of the HTTP reply

**/
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

/**

    @param request buffer
    @return vector of request strings split by space

**/

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

/**
    @param file descriptor
    @return file size in bytes
*/

long get_file_size(int fd)
{
    struct stat stat_buf;
    int rc = fstat(fd, &stat_buf);
    return rc == 0 ? stat_buf.st_size : -1;
}

/**
    @param file path
    @return file descriptor
*/
int get_file_descriptor(string file_name)
{
    char *cstr = new char[file_name.length() + 1];
    strcpy(cstr, file_name.c_str());
    return open(cstr, O_RDONLY);
}

/**
    @param file name of found file & size
    @return the OK -200- HTTP reply
*/

char * file_found_reply(string path, long size)
{
    string reply;
    stringstream ss  ;
    ss.str("");
    ss<<"HTTP/1.1 200 OK\r\n";

    ss<<"Content-Length: " ;

    ss<<size;

    ss<<"\r\nContent-Type: ";
    ss<<check_type(path);
    ss<<"\r\n";
    ss<<"\r\n";

    reply = ss.str();

    char *cstr = new char[reply.length() + 1];
    strcpy(cstr, reply.c_str());
    return cstr;
}

/**
    @param file name of not found file & size
    @return the NOT FOUND -404- HTTP reply
*/

char * file_not_found_reply ()
{
    char *reply;
    reply = "HTTP/1.1 404 Not Found\r\nContent-Length: 0 \r\n\r\n";
    return reply;

}

/**
    @param file_found_boolean , file path , file size
    @return proper HTTP reply
*/

char* get_header(bool file_is_found, string path, long size)
{
    if(file_is_found)
        return file_found_reply(path,size);
    else
        return file_not_found_reply();

}

int get_file_size_from_header(char buffer[])
{
    int length = strlen(buffer);
    int file_size=0;
    string line = "";

    for(int i = 0 ; i < length ; i++)
    {
        if(buffer[i] == ' ' || buffer[i] == '\r' || buffer[i] == '\n')
        {

            if( (line.compare("Content-Length:")) == 0)
            {
                line = "";
                i++;

                while(buffer[i] != '\r')
                {
                    line+= buffer[i];
                    i++;
                }

                file_size = stoi(line);

                return file_size;
            }

            line = "";
        }
        else
        {
            line+=buffer[i];
        }

    }

    return -1;

}

/**
    this is time out function close the connection after certain time
    @param start start time of last connection
*/
double timer(time_t start)
{

    time_t end = time(NULL);

    double duration  = (double)(end-start);
    std::cout<<"Execution Time: "<< duration<<" Seconds"<<std::endl;
    return duration;
}

int establish_conncetion (char* port_number)
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

    if ((rv = getaddrinfo(NULL, port_number, &hints, &servinfo)) != 0)
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

        mtx.lock();
        ++(*client_num_pointer);
        mtx.unlock();

        time_t start = time(NULL);

        cout<<"server: got connection client"<<(*client_num_pointer)<<" address is "<<s<<endl;

        if (!fork())   // this is the child process
        {
            //client_num++;
            close(sockfd); // child doesn't need the listener
            int connection_number =0;

           // time_t start =  time(NULL);

            while (1)
            {
                timeval timeout = { DELAY/(*client_num_pointer), 0 };
                fd_set in_set;

                FD_ZERO(&in_set);
                FD_SET(new_fd, &in_set);

                // select the set
                int cnt = select(new_fd + 1, &in_set, NULL, NULL, &timeout);

                //cout << "cnt: "<<cnt<<endl;
                if (FD_ISSET(new_fd, &in_set))
                {
                    cout <<"server : connection number : "<<connection_number<<endl;

                    // listen for a request
                    int numbytes;
                    if ((numbytes = recv(new_fd, buf, MAXDATASIZE-1, 0)) == -1)
                    {
                        perror("recv");
                        exit(1);
                    }

                    // add null to the request
                    buf[numbytes] = '\0';


                    // parse the request
                    vector<string> data = get_first_line(buf);

                    // get request type
                    string first_word = data[0];

                    cout << "client "<<(*client_num_pointer)<<" : "<<data[0]<<data[1]<<endl;

                    if((first_word.compare("GET")) == 0)
                    {
                      //  start =  time(NULL);

                        timeout = { 10/(*client_num_pointer), 0 };
                        string file_name = data[1];
                        file_name.erase(0,1);


                        int fd = get_file_descriptor (file_name);

                        if(fd!=FILE_NOT_FOUND_DESC)
                        {
                            // file is found

                            long file_size = get_file_size(fd);


                            char * reply_header = get_header(true,file_name,file_size);
                            cout << "server  : GET header sent ... \n"<<reply_header;

                            if (send(new_fd, reply_header, strlen(reply_header), 0) == -1)
                                perror("send");

                            usleep(INTER_PACKET_INTERVAL);
                            off_t offset = 0;
                            int remain_data = file_size;
                            size_t sent_bytes = 0;

                            /* Sending file data */

                            while (((sent_bytes = sendfile(new_fd, fd, &offset, MAXDATASIZE)) > 0) && (remain_data > 0))
                            {
                                remain_data -= sent_bytes;
                                usleep(INTER_PACKET_INTERVAL);
                                timeout = { 10/(*client_num_pointer), 0 };
                               // start =  time(NULL);
//                            fprintf(stdout, " sent  = %d bytes, offset : %d, remaining data = %d\n",
//                                    sent_bytes, offset, remain_data);
                            }
//
                            cout<< "server : sent data\n\n";


                        }

                        else
                        {
                            // file not found
                            char * reply_header = get_header(false,file_name,0);
                            cout << "server  : GET header sent ... \n"<<reply_header;

                            if (send(new_fd, reply_header, strlen(reply_header), 0) == -1)
                                perror("send");
                        }

                        // close the file descriptor
                        close(fd);


                    }
                    else if((first_word.compare("POST")) == 0)
                    {
                      //  start =  time(NULL);
                        timeout = { 10/(*client_num_pointer), 0 };
                        int file_size = get_file_size_from_header(buf);



                        char * reply = "OK";

                        // send OK to the client to start exchanging the data
                        if (send(new_fd, reply, strlen(reply), 0) == -1)
                            perror("send");

                        cout << "server : OK"<<endl;

                        // recv the data
                        int numbytes;

                        char buf_post[MAXDATASIZE];
                        FILE * recieved_file ;
                        string path = data[1];
                        path.erase(0,1);
                        char *file_name = new char[path.length() + 1];
                        strcpy(file_name, path.c_str());

                        recieved_file = fopen(file_name, "w");

                        if (recieved_file == NULL)
                        {
                            fprintf(stderr, "Failed to open file foo --> %s\n", strerror(errno));

                            exit(EXIT_FAILURE);
                        }

                        int remain_data = file_size;

                        while (remain_data > 0 &&(numbytes = recv(new_fd, buf_post, MAXDATASIZE, 0)) > 0)
                        {
                            fwrite(buf_post, sizeof(char), numbytes, recieved_file);
                            remain_data -= numbytes;
//                        fprintf(stdout, "Receive %d bytes\n", numbytes);
//                        fprintf(stdout, "remain %d bytes\n", remain_data);
                            //start =  time(NULL);
                            timeout = { 10/(*client_num_pointer), 0 };

                        }

                        cout << "server  : file received "<<endl;

                        fclose(recieved_file);
                        usleep(INTER_COMMAND_INTERVAL);

                    }
                    else if((first_word.compare("finish")) == 0)
                    {
                        mtx.lock();
                        --(*client_num_pointer);
                        mtx.unlock();
                        cout<<"duration : "<<timer(start)<<endl;
                        cout<<"===>client num : "<<(*client_num_pointer)<<endl;
                        cout<< " client finish his work and close\n";
                    }
                    else
                    {
                        cout<<"server : invalid request"<<endl;
                    }
                    connection_number++;
                    usleep(INTER_COMMAND_INTERVAL);
                    cout<<"===>client num : "<<(*client_num_pointer)<<endl;

                }

                else
                {
                    // nothing received from client in last 5 seconds
                    mtx.lock();
                    --(*client_num_pointer);
                    mtx.unlock();

                    cout<<"duration : "<<timer(start)<<endl;
                    cout<<"===>client num : "<<(*client_num_pointer)<<endl;
                    cout << "nothing received from client in last 3 seconds\n";
                    break;
                }

            }

            cout<< "connection closed \n";
            close(new_fd);
            exit(0);
        }
        cout<< "connection closed \n";
        close(new_fd);  // parent doesn't need this
    }

    return 0;

}
char * get_port_number(int argc, char* argv [])
{
    char* port_number = PORT ;
    if (argc > 1)
    {
        port_number = argv[1];
    }
    return port_number ;
}

int main(int argc, char *argv[])
{
    client_num_pointer = (int*)mmap(NULL, sizeof *client_num_pointer, PROT_READ | PROT_WRITE,
                                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    *client_num_pointer = 0;
    char * port_number = get_port_number(argc,argv);
    int status = establish_conncetion(port_number);

    return status;
}

