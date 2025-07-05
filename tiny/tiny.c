/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the 
 *     GET method to serve static and dynamic content.
 * 
 * Tiny 웹서버: GET 메소드로 정적/동적 콘텐츠를 제공하는 간단한 HTTP/1.0 서버
 */
#include "csapp.h"

void doit(int fd);                              // HTTP 요청 처리 함수
void read_requesthdrs(rio_t *rp);               // HTTP 헤더 읽기 함수  
int parse_uri(char *uri, char *filename, char *cgiargs); // URI 파싱 함수
void serve_static(int fd, char *filename, int filesize); // 정적 파일 서빙 함수
void get_filetype(char *filename, char *filetype);       // 파일 타입 결정 함수
void serve_dynamic(int fd, char *filename, char *cgiargs); // 동적 콘텐츠 서빙 함수
void clienterror(int fd, char *cause, char *errnum,       // 에러 응답 함수
                 char *shortmsg, char *longmsg);

/*
 * main - Tiny 웹서버의 메인 함수
 * 사용법: tiny <포트번호>
 * 예시: tiny 8000
 */
int main(int argc, char **argv) 
{
    int listenfd, connfd;                   // 리스닝 소켓, 연결 소켓
    char hostname[MAXLINE], port[MAXLINE];  // 클라이언트 호스트명, 포트
    socklen_t clientlen;                    // 클라이언트 주소 구조체 크기
    struct sockaddr_storage clientaddr;    // 클라이언트 주소 정보

    /* 명령행 인수 검사: 프로그램명 + 포트번호 = 2개여야 함 */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    /* 지정된 포트에서 클라이언트 연결 대기 소켓 생성 */
    listenfd = Open_listenfd(argv[1]);

    /* 무한 루프로 클라이언트 요청 처리 */
    while (1) {
        clientlen = sizeof(clientaddr);
        
        /* 클라이언트 연결 수락 (블로킹 - 연결될 때까지 대기) */
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        
        /* 클라이언트의 호스트명과 포트 번호 얻기 */
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        
        /* HTTP 트랜잭션 수행 */
        doit(connfd);
        
        /* 클라이언트와의 연결 종료 */
        Close(connfd);
    }
}

/*
 * doit - HTTP 트랜잭션 처리 함수
 * 1개의 HTTP 요청을 읽고 분석해서 적절한 응답을 보냄
 */
void doit(int fd) 
{
    int is_static;                          // 정적(1) vs 동적(0) 콘텐츠 구분
    struct stat sbuf;                       // 파일 정보 구조체
    char buf[MAXLINE], method[MAXLINE];     // 요청 라인, HTTP 메소드
    char uri[MAXLINE], version[MAXLINE];    // URI, HTTP 버전
    char filename[MAXLINE], cgiargs[MAXLINE]; // 파일명, CGI 인수
    rio_t rio;                              // RIO 버퍼

    /* HTTP 요청 라인과 헤더 읽기 */
    Rio_readinitb(&rio, fd);                // RIO 버퍼 초기화
    Rio_readlineb(&rio, buf, MAXLINE);      // 첫 번째 줄(요청 라인) 읽기
    printf("Request headers:\n");
    printf("%s", buf);                      // 요청 라인 출력
    
    /* 요청 라인을 메소드, URI, 버전으로 파싱 */
    /* 예: "GET /index.html HTTP/1.1" → method="GET", uri="/index.html", version="HTTP/1.1" */
    sscanf(buf, "%s %s %s", method, uri, version);

    /* GET이 아니면 거절: Tiny는 GET 메소드만 지원 */
    if (strcasecmp(method, "GET")) {
        clienterror(fd, method, "501", "Not Implemented",
                    "Tiny does not implement this method");
        return;
    }
    
    /* 나머지 헤더들 읽기: Accept, User-Agent 등 (실제로는 무시함) */
    read_requesthdrs(&rio);

    /* URI를 파싱해서 파일명과 CGI 인수 추출 */
    is_static = parse_uri(uri, filename, cgiargs);
    
    /* 요청한 파일이 존재하는지 확인 */
    if (stat(filename, &sbuf) < 0) {
        clienterror(fd, filename, "404", "Not found",
                    "Tiny couldn't find this file");
        return;
    }

    /* 정적 vs 동적 콘텐츠에 따라 분기 */
    if (is_static) { /* 정적 콘텐츠 제공 */
        /* 파일 권한 확인: 일반 파일이고 읽기 권한이 있는지 체크 */
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
            clienterror(fd, filename, "403", "Forbidden",
                        "Tiny couldn't read the file");
            return;
        }
        /* 정적 파일 서빙 */
        serve_static(fd, filename, sbuf.st_size);
    }
    else { /* 동적 콘텐츠 제공 */
        /* 실행 권한 확인: 일반 파일이고 실행 권한이 있는지 체크 */
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
            clienterror(fd, filename, "403", "Forbidden",
                        "Tiny couldn't run the CGI program");
            return;
        }
        /* 동적 콘텐츠 서빙 */
        serve_dynamic(fd, filename, cgiargs);
    }
}

/*
 * read_requesthdrs - HTTP 요청 헤더들을 읽어서 출력
 * 실제로는 헤더 내용을 무시하고 그냥 읽어서 버림
 * 빈 줄(\r\n)이 나올 때까지 계속 읽음
 */
void read_requesthdrs(rio_t *rp) 
{
    char buf[MAXLINE];

    /* 첫 번째 헤더 라인 읽기 */
    Rio_readlineb(rp, buf, MAXLINE);
    
    /* 빈 줄이 나올 때까지 헤더들 읽기 */
    /* HTTP는 헤더 끝을 빈 줄로 표시함 */
    while(strcmp(buf, "\r\n")) {
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);              // 헤더 출력 (디버깅용)
    }
    return;
}

/*
 * parse_uri - URI를 파싱해서 파일명과 CGI 인수 추출
 * 
 * 입력: uri (예: "/index.html" 또는 "/cgi-bin/adder?1&2")
 * 출력: filename (실제 파일 경로), cgiargs (CGI 인수)
 * 반환: 정적 콘텐츠면 1, 동적 콘텐츠면 0
 */
int parse_uri(char *uri, char *filename, char *cgiargs) 
{
    char *ptr;

    /* 정적 콘텐츠: URI에 "cgi-bin"이 없으면 정적 파일 */
    if (!strstr(uri, "cgi-bin")) {
        strcpy(cgiargs, "");                    // CGI 인수는 빈 문자열
        strcpy(filename, ".");                  // 현재 디렉토리에서 시작
        strcat(filename, uri);                  // URI를 파일명에 추가
        
        /* 디렉토리 요청이면 기본 파일(home.html) 추가 */
        if (uri[strlen(uri)-1] == '/')
            strcat(filename, "home.html");      // 예: "/" → "./home.html"
        return 1;                               // 정적 콘텐츠
    }
    /* 동적 콘텐츠: URI에 "cgi-bin"이 있으면 CGI 프로그램 */
    else {
        /* CGI 인수 추출: '?' 문자를 찾아서 분리 */
        ptr = index(uri, '?');                  // '?' 위치 찾기
        if (ptr) {
            strcpy(cgiargs, ptr+1);             // '?' 다음이 CGI 인수
            *ptr = '\0';                        // URI에서 '?' 제거
        }
        else 
            strcpy(cgiargs, "");                // CGI 인수 없음
            
        strcpy(filename, ".");                  // 현재 디렉토리에서 시작
        strcat(filename, uri);                  // URI를 파일명에 추가
        return 0;                               // 동적 콘텐츠
    }
}

/*
 * serve_static - 정적 파일을 클라이언트에게 전송
 * mmap을 사용해서 효율적으로 파일을 메모리에 매핑 후 전송
 */
void serve_static(int fd, char *filename, int filesize) 
{
    int srcfd;                                  // 소스 파일 디스크립터
    char *srcp, filetype[MAXLINE], buf[MAXBUF]; // 파일 포인터, 타입, 버퍼

    /* HTTP 응답 헤더 생성 */
    get_filetype(filename, filetype);           // 파일 확장자로 MIME 타입 결정
    sprintf(buf, "HTTP/1.0 200 OK\r\n");       // 상태 라인
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    
    /* 응답 헤더를 클라이언트에게 전송 */
    Rio_writen(fd, buf, strlen(buf));
    printf("Response headers:\n");
    printf("%s", buf);

    /* 파일 내용을 클라이언트에게 전송 */
    srcfd = Open(filename, O_RDONLY, 0);        // 파일을 읽기 전용으로 열기
    
    /* mmap: 파일을 메모리에 매핑 (큰 파일도 효율적으로 처리) */
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    Close(srcfd);                               // 파일 디스크립터 닫기
    
    /* 매핑된 메모리를 클라이언트에게 전송 */
    Rio_writen(fd, srcp, filesize);
    
    /* 메모리 매핑 해제 */
    Munmap(srcp, filesize);
}

/*
 * get_filetype - 파일명에서 MIME 타입을 결정
 * 웹 브라우저가 파일을 올바르게 처리할 수 있도록 도움
 */
void get_filetype(char *filename, char *filetype) 
{
    if (strstr(filename, ".html"))
        strcpy(filetype, "text/html");          // HTML 파일
    else if (strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");          // GIF 이미지
    else if (strstr(filename, ".png"))
        strcpy(filetype, "image/png");          // PNG 이미지  
    else if (strstr(filename, ".jpg"))
        strcpy(filetype, "image/jpeg");         // JPEG 이미지
    else
        strcpy(filetype, "text/plain");         // 기본값: 일반 텍스트
}

/*
 * serve_dynamic - 동적 콘텐츠(CGI 프로그램)를 실행해서 결과 전송
 * fork()로 자식 프로세스를 만들어서 CGI 프로그램 실행
 */
void serve_dynamic(int fd, char *filename, char *cgiargs) 
{
    char buf[MAXLINE], *emptylist[] = {NULL};   // 버퍼, 빈 인수 리스트

    /* HTTP 응답 헤더의 첫 부분만 전송 */
    /* CGI 프로그램이 나머지 헤더와 본문을 생성함 */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* 자식 프로세스 생성 */
    if (Fork() == 0) { /* 자식 프로세스 */
        /* CGI 인수를 환경변수로 설정 */
        setenv("QUERY_STRING", cgiargs, 1);
        
        /* stdout을 클라이언트 소켓으로 리다이렉트 */
        /* CGI 프로그램의 출력이 바로 클라이언트에게 전송됨 */
        Dup2(fd, STDOUT_FILENO);
        
        /* CGI 프로그램 실행 */
        Execve(filename, emptylist, environ);
    }
    
    /* 부모 프로세스는 자식이 끝날 때까지 대기 */
    Wait(NULL);
}

/*
 * clienterror - 클라이언트에게 에러 메시지를 HTML 형태로 전송
 * HTTP 에러 상태 코드와 설명을 포함한 에러 페이지 생성
 */
void clienterror(int fd, char *cause, char *errnum, 
                 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* 에러 페이지 본문 작성: HTML 형태의 에러 메시지 */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* 에러 응답 전송: HTTP 응답 헤더 + 에러 페이지 */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}