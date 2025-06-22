#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>

static void die(const char *msg) {
    perror(msg);
    exit(1);
}

int main(int argc, char **argv){

    // Change!
    // Ignore SIGPIPE so that we don’t terminate when we call
    // send() on a disconnected socket
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR){
        die("signal() failed");
    }

    if(argc != 5){
        fprintf(stderr, "%s <server_port> <web_root> <mdb-lookup-host> <mdb-lookup-port>", argv[0]);
        exit(1);
    }

    unsigned short servPort = atoi(argv[1]);
    unsigned short mdbPort = atoi(argv[4]);//b
    const char *webRoot = argv[2];
    const char *mdbHost = argv[3];//b
    int servsock;
    if ((servsock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        die("socket failed");

    struct hostent *he;//b
    if ((he = gethostbyname(mdbHost)) == NULL) {
        die("gethostbyname failed");//b
    }
    char *serverIP = inet_ntoa(*(struct in_addr *)he->h_addr);//b
    int mdbsock;
    if ((mdbsock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        die("socket failed");
    }
    //Conctruct mdb-lookup server address structure, b
    struct sockaddr_in mdbLookupServer;
    memset(&mdbLookupServer, 0, sizeof(mdbLookupServer));
    mdbLookupServer.sin_family = AF_INET;
    mdbLookupServer.sin_addr.s_addr = inet_addr(serverIP); // any network interface
    mdbLookupServer.sin_port = htons(mdbPort);

    if (connect(mdbsock, (struct sockaddr *) &mdbLookupServer, sizeof(mdbLookupServer)) < 0)
        die("connect failed");

    FILE *fm = fdopen(mdbsock, "r");
    if (fm ==  NULL){
        die("mdb-socket can't open");
    }

    //server address
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr)); 
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(servPort);

    // Bind to the local address

    if (bind(servsock, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0)
        die("bind failed");

    // Start listening for incoming connections

    if (listen(servsock, 5 /* queue size for connection requests */ ) < 0)
        die("listen failed");

    int clntsock;
    socklen_t clntlen;
    struct sockaddr_in clntaddr;


    while (1) {

        // Accept an incoming connection

        clntlen = sizeof(clntaddr); // initialize the in-out parameter

        if ((clntsock = accept(servsock,
                        (struct sockaddr *) &clntaddr, &clntlen)) < 0){
            die("accept failed");
        }

        char get[256];

        FILE *fd = fdopen(clntsock,"r");

        if(fgets(get,sizeof(get),fd)==NULL){
            //close socket instead of closing
            close(clntsock);
            fclose(fd);
            continue;
        }
        //parsing in the characters
        char *token_separators = "\t \r\n"; // tab, space, new line
        char *method = strtok(get, token_separators);
        char *requestURI = strtok(NULL, token_separators);
        char *httpVersion = strtok(NULL, token_separators);

        if(!strstr(method, "GET")){
            char res501[100];//4096
                             //debug info 

            snprintf(res501, sizeof(res501),"HTTP/1.0 501 Not Implemented\r\n\r\n<html><body><h1>501 Not Implemented</h1></body></html>\r\n");//extra line? 
            if (send(clntsock, res501, strlen(res501), 0) != strlen(res501)){
                perror("send() failed");
                fclose(fd);
                close(clntsock);
                continue;
            }
            close(clntsock);
            fclose(fd);
            fprintf(stderr, "%s\"%s %s %s\" 501 Not Implemented\n",inet_ntoa(clntaddr.sin_addr),method,requestURI,httpVersion);
        }

        else if(!strstr(httpVersion, "HTTP/1.0") && !strstr(httpVersion, "HTTP/1.1")){
            char res501[100];//4096

            snprintf(res501, sizeof(res501),"HTTP/1.0 501 Not Implemented\r\n\r\n<html><body><h1>501 Not Implemented</h1></body></html>\r\n");//extra line?
            if (send(clntsock, res501, strlen(res501), 0) != strlen(res501)){
                perror("send() failed");
                fclose(fd);
                close(clntsock);
                continue;
            }
            close(clntsock);
            fclose(fd);
            fprintf(stderr, "%s \"%s %s %s\" 501 Not Implemented\n", inet_ntoa(clntaddr.sin_addr),method, requestURI, httpVersion);
        }
        else if ((*requestURI)!='/'||strstr(requestURI, "/../")!=NULL||strstr(requestURI, "/..")!=NULL){//check!!!
            char res400[100];//4096

            snprintf(res400, sizeof(res400),"HTTP/1.0 400 Bad Request\r\n\r\n<html><body><h1>400 Bad Request</h1></body></html>\r\n");//extra line?
            if(send(clntsock, res400, strlen(res400), 0) != strlen(res400)){
                perror("send() failed");
                close(clntsock);
                fclose(fd);
                continue;
            }
            close(clntsock);
            fprintf(stderr, "%s \"%s %s %s\" 400 Bad Request\n",inet_ntoa(clntaddr.sin_addr), method, requestURI, httpVersion);
        }//b
        else if(strcmp(requestURI, "/mdb-lookup") == 0){
            const char *form =
                "<h1>mdb-lookup</h1>\n"
                "<p>\n"
                "<form method=GET action=/mdb-lookup>\n"
                "lookup: <input type=text name=key>\n"
                "<input type=submit>\n"
                "</form>\n"
                "<p>\n";
            char buf1[256];
            snprintf(buf1,sizeof(buf1),"HTTP/1.0 200 OK\r\n\r\n%s\r\n", form);
            if(send(clntsock, buf1, strlen(buf1), 0) != strlen(buf1)){
                perror("send() failed");
                close(clntsock);
                continue;
            }
            close(clntsock);
            fprintf(stderr, "%s \"%s %s %s\" 200 OK\n",inet_ntoa(clntaddr.sin_addr), method, requestURI, httpVersion);//check the status code

        }
        else if(strstr(requestURI,"/mdb-lookup?key=")){
            const char *form =
                "<h1>mdb-lookup</h1>\n"
                "<p>\n"
                "<form method=GET action=/mdb-lookup>\n"
                "lookup: <input type=text name=key>\n"
                "<input type=submit>\n"
                "</form>\n"
                "<p>\n";

            char *line;
            line = strstr(requestURI, "=");
            if(line==NULL){
                perror("invalid argument");
                close(clntsock);
                fclose(fd);
                continue;
            }
            if (strlen(line)==1){
                line[strlen(line)-1]='\0';
            }
            else{
                ++line;
            }

            char line1[1001];
            snprintf(line1, sizeof(line1), "%s\n", line);
            if(send(mdbsock, line1, strlen(line1), 0)!= strlen(line1)){
                perror("mdb-lookup-server connection terminated");
                char res500 [256];
                snprintf(res500, sizeof(res500),"HTTP/1.0 500 Internal Server Error\r\n\r\n<html><body><h1>500 Internal Server Error</h1></body></html>\r\n\r\n");
                if(send(clntsock, res500, strlen(res500), 0) != strlen(res500)){
                    perror("send() failed");
                    fclose(fd);
                    close(clntsock);
                    continue;
                }
                fprintf(stderr, "looking up [%s]: %s \"%s %s %s\" 500 Internal Server Error\n",line,inet_ntoa(clntaddr.sin_addr), method, requestURI, httpVersion);
                close(clntsock);
                continue;
            }

            char buf1[256];
            snprintf(buf1,sizeof(buf1),"HTTP/1.0 200 OK\r\n\r\n%s\r\n", form);
            if(send(clntsock, buf1, strlen(buf1), 0) != strlen(buf1)){
                fprintf(stderr, "send() failed");
                close(clntsock);
                continue;
            }
            char buf2[256];
            snprintf(buf2,sizeof(buf2),"<p><table border>\n");//r\n?

            if(send(clntsock, buf2, strlen(buf2), 0) != strlen(buf2)){
                fprintf(stderr, "send() failed");
                close(clntsock);
                continue;
            }

            char line2[100];
            char * c;
            int count=0;//create the co variable to detect the c
            while((c=fgets(line2, sizeof(line2), fm))!= NULL){
                if(strcmp(c, "\n")==0){
                    break;
                }
                count++;
                char buf3[256];
                if((count%2)==0){
                    snprintf(buf3,sizeof(buf3),"<tr><td bgcolor=yellow>%s\n",line2);
                }
                else{
                    snprintf(buf3,sizeof(buf3),"<tr><td>%s\n",line2);
                }
                if(send(clntsock, buf3, strlen(buf3), 0) != strlen(buf3)){
                    fprintf(stderr, "send failed");
                    close(clntsock);
                    continue;
                }
            }


            char buf4[100];
            snprintf(buf4, sizeof(buf4), "</table>\n</body></html>\n\n");
            if(send(clntsock, buf4, strlen(buf4), 0) != strlen(buf4)){
                fprintf(stderr, "send failed");
                close(clntsock);
                continue;
            }
            fprintf(stderr, "looking up [%s]: %s \"%s %s %s\" 200 OK\n",line,inet_ntoa(clntaddr.sin_addr), method, requestURI, httpVersion);
            close(clntsock);

        }


        else{
            char response[4096];
            char *path;
            if(requestURI[strlen(requestURI)-1]=='/'){
                path = (char *) malloc(strlen(requestURI)+strlen(webRoot)+1+strlen("index.html"));
                if (path==NULL){
                    perror("memory allocation failed");
                    close(clntsock);
                    free(path);
                    continue;
                }
                strcpy(path, webRoot);
                strcat(path, requestURI);
                strcat(path, "index.html");
            }
            else{
                path = (char *) malloc(strlen(requestURI)+strlen(webRoot)+1);
                if (path==NULL){
                    perror("memory allocation failed");
                    close(clntsock);
                    free(path);
                    continue;
                }
                strcpy(path, webRoot);
                strcat(path, requestURI);

            }

            struct stat filestatus;
            if (stat(path, &filestatus)!=0){
                char res404[100];//4096
                snprintf(res404, sizeof(res404),"HTTP/1.0 404 Not Found\r\n\r\n<html><body><h1>404 NOT FOUND</h1></body></html>\r\n");
                if(send(clntsock, res404, strlen(res404), 0) != strlen(res404)){
                    perror("send() failed");
                    close(clntsock);
                    fclose(fd);
                    free(path);
                    continue;
                }
                fprintf(stderr, "%s \"%s %s %s\" 404 Not Found\n",inet_ntoa(clntaddr.sin_addr),method,requestURI, httpVersion);
                close(clntsock);
                fclose(fd);
                free(path);
                continue;
            }
            if (S_ISDIR(filestatus.st_mode)!=0&&path[strlen(path)-1]!='/'){
                char res403[100];
                snprintf(res403, sizeof(res403),"HTTP/1.0 403 Forbidden\r\n\r\n<html><body><h1>403 Forbidden</h1></body></html>\r\n");//extra line?
                if (send(clntsock, res403, strlen(res403), 0) != strlen(res403)){
                    perror("send() failed");
                    close(clntsock);
                    fclose(fd);
                    free(path);
                    continue;
                }
                fprintf(stderr, "%s \"%s %s %s\" 403 Forbidden\n",inet_ntoa(clntaddr.sin_addr),method,requestURI, httpVersion);
                close(clntsock);
                continue;
            }
            char *filepath= path;
            FILE *fp = fopen(filepath, "rb"); 
            if (fp == NULL){ 
                char res404[100];//4096
                snprintf(res404, sizeof(res404),"HTTP/1.0 404 Not Found\r\n\r\n<html><body><h1>404 NOT FOUND</h1></body></html>\r\n");//extra line?
                if(send(clntsock, res404, strlen(res404), 0) != strlen(res404)){
                    perror("send() failed");
                    close(clntsock);
                    fclose(fd);
                    free(path);
                    continue;
                }
                fprintf(stderr, "%s \"%s %s %s\" 404 Not Found\n",inet_ntoa(clntaddr.sin_addr),method,requestURI, httpVersion);
                close(clntsock);
                continue;
            }

            size_t n;
            char statusline[100];
            snprintf(statusline, sizeof(statusline),"HTTP/1.0 200 OK\r\n\r\n");
            send(clntsock, statusline,strlen(statusline), 0);
            while ((n = fread(response, 1, sizeof(response), fp)) > 0) {
                if (send(clntsock, response, n, 0) != n){//cond jump
                    perror("send() failed");
                    close(clntsock);
                    fclose(fd);
                    free(path);
                    continue;
                }

            }
            close(clntsock);
            fclose(fd);
            fprintf(stderr, "%s \"%s %s %s\" 200 OK\n",inet_ntoa(clntaddr.sin_addr),method, requestURI, httpVersion);
            free(path);
            fclose(fp);


        }



    }
    close(servsock);
    return 0;


}




