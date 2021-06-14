//
// request.c: Does the bulk of the work for the web server.
//

#include "segel.h"
#include "request.h"

#define STAT_REQ_ARRIVAL "Stat-Req-Arrival:: "
#define STAT_REQ_DISPATCH "Stat-Req-Dispatch:: "
#define STAT_THREAD_ID "Stat-Thread-Id:: "
#define STAT_THREAD_COUNT "Stat-Thread-Count:: "
#define STAT_THREAD_STATIC "Stat-Thread-Static:: "
#define STAT_THREAD_DYNAMIC "Stat-Thread-Dynamic:: "

// requestError(      fd,    filename,        "404",    "Not found", "OS-HW3 Server could not find this file");
void requestError(ConnectionStruct cd, ThreadStats t_stats, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    // Create the body of the error message
    sprintf(body, "<html><title>OS-HW3 Error</title>");
    sprintf(body, "%s<body bgcolor="
                  "fffff"
                  ">\r\n",
            body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr>OS-HW3 Web Server\r\n", body);

    // Write out the header information for this response
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(cd->connfd, buf, strlen(buf));
    printf("%s", buf);

    sprintf(buf, "Content-Type: text/html\r\n");
    Rio_writen(cd->connfd, buf, strlen(buf));
    printf("%s", buf);

    sprintf(buf, "Content-Length: %lu\r\n", strlen(body));
    Rio_writen(cd->connfd, buf, strlen(buf));
    printf("%s", buf);

    sprintf(buf, STAT_REQ_ARRIVAL "%lu.%06lu\r\n", (unsigned long)cd->arrival.tv_sec, cd->arrival.tv_usec);
    Rio_writen(cd->connfd, buf, strlen(buf));
    printf("%s", buf);

    unsigned long diff_time = ((cd->dispatch.tv_sec * 1000000) + cd->dispatch.tv_usec % 1000000) \
                            - ((cd->arrival.tv_sec * 1000000) + cd->arrival.tv_usec % 1000000); // in miliseconds
    sprintf(buf, STAT_REQ_DISPATCH "%lu.%06lu\r\n", (diff_time / 1000000), (diff_time % 1000000));
    Rio_writen(cd->connfd, buf, strlen(buf));
    printf("%s", buf);

    sprintf(buf, STAT_THREAD_ID "%d\r\n", t_stats->thread_id);
    Rio_writen(cd->connfd, buf, strlen(buf));
    printf("%s", buf);

    sprintf(buf, STAT_THREAD_COUNT "%d\r\n", ++t_stats->thread_count);
    Rio_writen(cd->connfd, buf, strlen(buf));
    printf("%s", buf);

    sprintf(buf, STAT_THREAD_STATIC "%d\r\n", t_stats->thread_static);
    Rio_writen(cd->connfd, buf, strlen(buf));
    printf("%s", buf);

    sprintf(buf, STAT_THREAD_DYNAMIC "%d\r\n\r\n", t_stats->thread_dynamic);
    Rio_writen(cd->connfd, buf, strlen(buf));
    printf("%s", buf);

    // Write out the content
    Rio_writen(cd->connfd, body, strlen(body));
    printf("%s", body);
}

//
// Reads and discards everything up to an empty text line
//
void requestReadhdrs(rio_t *rp)
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    while (strcmp(buf, "\r\n"))
    {
        Rio_readlineb(rp, buf, MAXLINE);
    }
    return;
}

//
// Return 1 if static, 0 if dynamic content
// Calculates filename (and cgiargs, for dynamic) from uri
//
int requestParseURI(char *uri, char *filename, char *cgiargs)
{
    char *ptr;

    if (strstr(uri, ".."))
    {
        sprintf(filename, "./public/home.html");
        return 1;
    }

    if (!strstr(uri, "cgi"))
    {
        // static
        strcpy(cgiargs, "");
        sprintf(filename, "./public/%s", uri);
        if (uri[strlen(uri) - 1] == '/')
        {
            strcat(filename, "home.html");
        }
        return 1;
    }
    else
    {
        // dynamic
        ptr = index(uri, '?');
        if (ptr)
        {
            strcpy(cgiargs, ptr + 1);
            *ptr = '\0';
        }
        else
        {
            strcpy(cgiargs, "");
        }
        sprintf(filename, "./public/%s", uri);
        return 0;
    }
}

//
// Fills in the filetype given the filename
//
void requestGetFiletype(char *filename, char *filetype)
{
    if (strstr(filename, ".html"))
        strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg"))
        strcpy(filetype, "image/jpeg");
    else
        strcpy(filetype, "text/plain");
}

void requestServeDynamic(ConnectionStruct cd, ThreadStats t_stats, char *filename, char *cgiargs)
{
    char buf[MAXLINE], *emptylist[] = {NULL};

    // The server does only a little bit of the header.
    // The CGI script has to finish writing out the header.
    unsigned long diff_time = ((cd->dispatch.tv_sec * 1000000) + cd->dispatch.tv_usec % 1000000) \
                            - ((cd->arrival.tv_sec * 1000000) + cd->arrival.tv_usec % 1000000); // in miliseconds
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: OS-HW3 Web Server\r\n", buf);
    sprintf(buf, "%s" STAT_REQ_ARRIVAL "%lu.%06lu\r\n", buf, (long unsigned)cd->arrival.tv_sec, cd->arrival.tv_usec);
    sprintf(buf, "%s" STAT_REQ_DISPATCH "%lu.%06lu\r\n", buf, (diff_time / 1000000), (diff_time % 1000000));
    sprintf(buf, "%s" STAT_THREAD_ID "%d\r\n", buf, t_stats->thread_id);
    sprintf(buf, "%s" STAT_THREAD_COUNT "%d\r\n", buf, ++t_stats->thread_count);
    sprintf(buf, "%s" STAT_THREAD_STATIC "%d\r\n", buf, t_stats->thread_static);
    sprintf(buf, "%s" STAT_THREAD_DYNAMIC "%d\r\n", buf, ++t_stats->thread_dynamic);
    Rio_writen(cd->connfd, buf, strlen(buf));

    pid_t to_wait = -1;
    if ((to_wait = Fork()) == 0)
    {
        /* Child process */
        Setenv("QUERY_STRING", cgiargs, 1);
        /* When the CGI process writes to stdout, it will instead go to the socket */
        Dup2(cd->connfd, STDOUT_FILENO);
        Execve(filename, emptylist, environ);
    }
    WaitPid(to_wait, NULL, 0);
}

void requestServeStatic(ConnectionStruct cd, ThreadStats t_stats, char *filename, int filesize)
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];

    requestGetFiletype(filename, filetype);

    srcfd = Open(filename, O_RDONLY, 0);

    // Rather than call read() to read the file into memory,
    // which would require that we allocate a buffer, we memory-map the file
    srcp = (char *)Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    Close(srcfd);

    // put together response
    unsigned long diff_time = ((cd->dispatch.tv_sec * 1000000) + cd->dispatch.tv_usec % 1000000) \
                            - ((cd->arrival.tv_sec * 1000000) + cd->arrival.tv_usec % 1000000); // in miliseconds
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: OS-HW3 Web Server\r\n", buf);
    sprintf(buf, "%sContent-Length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-Type: %s\r\n", buf, filetype);
    sprintf(buf, "%s" STAT_REQ_ARRIVAL "%u.%06lu\r\n", buf, (unsigned)cd->arrival.tv_sec, cd->arrival.tv_usec);
    sprintf(buf, "%s" STAT_REQ_DISPATCH "%lu.%06lu\r\n", buf, (diff_time / 1000000), (diff_time % 1000000));
    sprintf(buf, "%s" STAT_THREAD_ID "%d\r\n", buf, t_stats->thread_id);
    sprintf(buf, "%s" STAT_THREAD_COUNT "%d\r\n", buf, ++t_stats->thread_count);
    sprintf(buf, "%s" STAT_THREAD_STATIC "%d\r\n", buf, ++t_stats->thread_static);
    sprintf(buf, "%s" STAT_THREAD_DYNAMIC "%d\r\n\r\n", buf, t_stats->thread_dynamic);

    Rio_writen(cd->connfd, buf, strlen(buf));

    //  Writes out to the client socket the memory-mapped file
    Rio_writen(cd->connfd, srcp, filesize);
    Munmap(srcp, filesize);
}

// handle a request
void requestHandle(ConnectionStruct cd, ThreadStats t_stats)
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    Rio_readinitb(&rio, cd->connfd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);

    printf("%s %s %s\n", method, uri, version);

    if (strcasecmp(method, "GET"))
    {
        requestError(cd, t_stats, method, "501", "Not Implemented", "OS-HW3 Server does not implement this method");
        return;
    }
    requestReadhdrs(&rio);

    is_static = requestParseURI(uri, filename, cgiargs);
    if (stat(filename, &sbuf) < 0)
    {
        requestError(cd, t_stats, filename, "404", "Not found", "OS-HW3 Server could not find this file");
        return;
    }

    if (is_static)
    {
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
        {
            requestError(cd, t_stats, filename, "403", "Forbidden", "OS-HW3 Server could not read this file");
            return;
        }
        requestServeStatic(cd, t_stats, filename, sbuf.st_size);
    }
    else
    {
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
        {
            requestError(cd, t_stats, filename, "403", "Forbidden", "OS-HW3 Server could not run this CGI program");
            return;
        }
        requestServeDynamic(cd, t_stats, filename, cgiargs);
    }
}
