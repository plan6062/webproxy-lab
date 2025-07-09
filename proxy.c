/*
 * proxy.c - 동시성을 지원하는 HTTP 프록시 서버
 * 
 * 동작 원리:
 * 1. 클라이언트로부터 HTTP 요청을 받음
 * 2. 요청을 파싱하여 목적지 서버 정보 추출
 * 3. 목적지 서버에 연결하여 요청 전달
 * 4. 서버 응답을 클라이언트에게 중계
 * 
 * 멀티스레딩을 통해 여러 클라이언트 요청을 동시에 처리
 */

#include <stdio.h>
#include <pthread.h>      // 멀티스레딩을 위한 pthread 라이브러리

/* 캐시 관련 상수 정의 (Part III에서 사용 예정) */
#define MAX_CACHE_SIZE 1049000    // 최대 캐시 크기: 1MB
#define MAX_OBJECT_SIZE 102400    // 캐시 가능한 객체 최대 크기: 100KB

/* 프록시가 서버에게 보낼 User-Agent 헤더 (브라우저 식별 정보) */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

#include "csapp.h"        // 교재에서 제공하는 소켓 프로그래밍 라이브러리

/* 함수 프로토타입 선언 */
void parse_uri(char *uri, char *hostname, char *path, int *port);
void doit(int clientfd);
void *thread_routine(void *vargp);

/*
 * main - 프록시 서버의 메인 함수
 * 
 * 역할:
 * 1. 지정된 포트에서 클라이언트 연결 대기
 * 2. 연결이 들어올 때마다 새로운 스레드 생성
 * 3. 각 스레드가 독립적으로 클라이언트 요청 처리
 */
int main(int argc, char **argv) {
    int listenfd, *clientfdp;           // 서버 소켓, 클라이언트 소켓 포인터
    socklen_t clientlen;                // 클라이언트 주소 구조체 크기
    struct sockaddr_storage clientaddr; // 클라이언트 주소 정보
    pthread_t tid;                      // 스레드 ID

    /* 명령행 인수 검사: 프로그램명 + 포트번호 = 2개 */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    /* SIGPIPE 신호 무시 설정
     * - 클라이언트가 연결을 끊었을 때 프록시가 종료되지 않도록 함
     * - 소켓에 쓰기 시도 시 발생할 수 있는 신호를 무시
     */
    Signal(SIGPIPE, SIG_IGN);

    /* 지정된 포트에서 클라이언트 연결을 대기하는 소켓 생성 */
    listenfd = Open_listenfd(argv[1]);
    
    /* 무한 루프: 계속해서 클라이언트 연결 수락 */
    while (1) {
        clientlen = sizeof(clientaddr);
        
        /* 클라이언트 소켓 파일 디스크립터를 동적 할당
         * - 각 스레드가 고유한 소켓 정보를 가져야 하므로 동적 할당 필요
         * - 스택 변수를 사용하면 여러 스레드가 같은 메모리를 공유하게 됨
         */
        clientfdp = Malloc(sizeof(int));
        
        /* 클라이언트 연결 수락 (블로킹 - 연결이 올 때까지 대기) */
        *clientfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        
        /* 새로운 스레드 생성하여 클라이언트 요청 처리
         * - thread_routine: 스레드가 실행할 함수
         * - clientfdp: 스레드에 전달할 인수 (클라이언트 소켓)
         */
        Pthread_create(&tid, NULL, thread_routine, clientfdp);
        
        /* 스레드를 detached 모드로 설정
         * - 스레드 종료 시 자동으로 리소스 해제
         * - 메모리 누수 방지
         * - main 스레드가 각 스레드의 종료를 기다리지 않아도 됨
         */
        Pthread_detach(tid);
    }
    return 0;
}

/*
 * thread_routine - 각 스레드가 실행하는 함수
 * 
 * 매개변수: vargp - 클라이언트 소켓 파일 디스크립터의 포인터
 * 
 * 역할:
 * 1. 전달받은 소켓 정보 추출
 * 2. 동적 할당된 메모리 해제
 * 3. HTTP 요청 처리
 * 4. 소켓 연결 종료
 */
void *thread_routine(void *vargp) {
    /* void 포인터를 int 포인터로 캐스팅하여 소켓 번호 추출 */
    int clientfd = *((int *)vargp);
    
    /* main에서 동적 할당한 메모리 해제
     * - 메모리 누수 방지
     * - 각 스레드가 자신의 메모리를 책임짐
     */
    Free(vargp);
    
    /* 실제 HTTP 요청 처리 함수 호출 */
    doit(clientfd);
    
    /* 클라이언트와의 연결 종료 */
    Close(clientfd);
    
    return NULL;  // 스레드 종료
}

/*
 * doit - HTTP 요청을 처리하는 핵심 함수
 * 
 * 매개변수: clientfd - 클라이언트와 연결된 소켓 파일 디스크립터
 * 
 * 처리 과정:
 * 1. 클라이언트 요청 읽기 및 파싱
 * 2. 목적지 서버에 연결
 * 3. HTTP 요청을 서버에 전달
 * 4. 서버 응답을 클라이언트에 중계
 */
void doit(int clientfd) {
    rio_t rio_client, rio_server;       // RIO 버퍼 (클라이언트용, 서버용)
    char buf[MAXLINE];                  // 범용 버퍼
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE]; // HTTP 요청 라인 구성 요소
    char hostname[MAXLINE], path[MAXLINE], portstr[8];    // URI 파싱 결과
    int serverfd, port;                 // 서버 소켓, 포트 번호

    /* === 1단계: 클라이언트 요청 읽기 === */
    
    /* 클라이언트 소켓에 대한 RIO 버퍼 초기화 */
    Rio_readinitb(&rio_client, clientfd);
    
    /* HTTP 요청의 첫 번째 줄(요청 라인) 읽기
     * 형식: "GET http://www.example.com/path HTTP/1.1"
     */
    if (!Rio_readlineb(&rio_client, buf, MAXLINE))
        return;  // 읽기 실패 시 함수 종료

    printf("Request line: %s", buf);  // 디버깅용 출력
    
    /* 요청 라인을 메소드, URI, 버전으로 분리
     * 예: "GET", "http://www.example.com/path", "HTTP/1.1"
     */
    sscanf(buf, "%s %s %s", method, uri, version);

    /* GET 메소드만 지원 (POST, PUT 등은 처리하지 않음) */
    if (strcasecmp(method, "GET")) {
        printf("Only GET supported\n");
        return;
    }

    /* === 2단계: URI 파싱 === */
    
    /* URI에서 호스트명, 경로, 포트 번호 추출
     * 예: "http://www.example.com:8080/path" → 
     *     hostname="www.example.com", port=8080, path="/path"
     */
    parse_uri(uri, hostname, path, &port);

    /* === 3단계: 목적지 서버에 연결 === */
    
    /* 포트 번호를 정수에서 문자열로 변환 (Open_clientfd 함수 요구사항) */
    sprintf(portstr, "%d", port);
    
    /* 목적지 서버에 소켓 연결 생성 */
    serverfd = Open_clientfd(hostname, portstr);
    if (serverfd < 0) {
        printf("Failed to connect to end server\n");
        return;  // 연결 실패 시 함수 종료
    }

    /* 서버 소켓에 대한 RIO 버퍼 초기화 */
    Rio_readinitb(&rio_server, serverfd);

    /* === 4단계: HTTP 요청을 서버에 전달 === */
    
    /* HTTP 요청 라인 생성 및 전송
     * 클라이언트의 HTTP/1.1 요청을 HTTP/1.0으로 변환
     * 예: "GET /path HTTP/1.0\r\n"
     */
    sprintf(buf, "GET %s HTTP/1.0\r\n", path);
    Rio_writen(serverfd, buf, strlen(buf));

    /* 클라이언트가 보낸 헤더들을 서버로 중계 */
    while (Rio_readlineb(&rio_client, buf, MAXLINE) > 0) {
        /* 빈 줄이 나오면 헤더 끝 (HTTP 프로토콜 규칙) */
        if (strcmp(buf, "\r\n") == 0)
            break;
            
        /* 특정 헤더들은 프록시에서 직접 처리하므로 제외
         * - Connection: 연결 관리 (프록시가 직접 설정)
         * - Proxy-Connection: 프록시 연결 관리
         * - User-Agent: 브라우저 정보 (프록시가 직접 설정)
         */
        if (strncasecmp(buf, "Connection:", 11) != 0 &&
            strncasecmp(buf, "Proxy-Connection:", 17) != 0 &&
            strncasecmp(buf, "User-Agent:", 11) != 0) {
            Rio_writen(serverfd, buf, strlen(buf));
        }
    }

    /* 프록시에서 설정하는 필수 헤더들 추가 */
    
    /* User-Agent: 브라우저 식별 정보 */
    sprintf(buf, "User-Agent: %s", user_agent_hdr);
    Rio_writen(serverfd, buf, strlen(buf));
    
    /* Connection: close - 응답 후 연결 종료 */
    sprintf(buf, "Connection: close\r\n");
    Rio_writen(serverfd, buf, strlen(buf));
    
    /* Proxy-Connection: close - 프록시 연결 종료 */
    sprintf(buf, "Proxy-Connection: close\r\n\r\n");
    Rio_writen(serverfd, buf, strlen(buf));

    /* === 5단계: 서버 응답을 클라이언트에 중계 === */
    
    /* 서버로부터 응답을 읽어서 클라이언트에게 그대로 전달
     * HTTP 헤더와 본문(HTML, 이미지 등) 모두 포함
     */
    while (1) {
        ssize_t n = Rio_readlineb(&rio_server, buf, MAXLINE);
        if (n <= 0)  // 더 이상 읽을 데이터가 없으면 종료
            break;
        Rio_writen(clientfd, buf, n);  // 클라이언트에게 전송
    }
    
    /* 서버와의 연결 종료 */
    Close(serverfd);
}

/*
 * parse_uri - URI를 파싱하여 호스트명, 경로, 포트 정보 추출
 * 
 * 매개변수:
 * - uri: 파싱할 URI 문자열 (예: "http://www.example.com:8080/path")
 * - hostname: 추출된 호스트명을 저장할 버퍼 (예: "www.example.com")
 * - path: 추출된 경로를 저장할 버퍼 (예: "/path")
 * - port: 추출된 포트 번호를 저장할 포인터 (예: 8080)
 * 
 * 처리 예시:
 * "http://www.example.com:8080/index.html" →
 * hostname="www.example.com", port=8080, path="/index.html"
 */
void parse_uri(char *uri, char *hostname, char *path, int *port) {
    *port = 80; // 기본 HTTP 포트 설정
    
    /* "http://" 부분 건너뛰기 */
    char *hostbegin = strstr(uri, "//");
    hostbegin = hostbegin ? hostbegin + 2 : uri;  // "//" 다음부터 또는 처음부터

    /* 호스트명의 끝을 찾기 (':'는 포트, '/'는 경로 시작) */
    char *hostend = strpbrk(hostbegin, ":/");
    
    /* 호스트명 길이 계산 */
    int hostlen = hostend ? hostend - hostbegin : strlen(hostbegin);
    
    /* 호스트명 복사 */
    strncpy(hostname, hostbegin, hostlen);
    hostname[hostlen] = '\0';  // 문자열 종료 문자 추가

    /* 포트와 경로 파싱 */
    if (hostend && *hostend == ':') {
        /* 포트 번호가 명시된 경우
         * 예: "www.example.com:8080/path" → port=8080, path="/path"
         */
        sscanf(hostend + 1, "%d%s", port, path);
    } else if (hostend && *hostend == '/') {
        /* 포트 없이 경로만 있는 경우
         * 예: "www.example.com/path" → port=80(기본값), path="/path"
         */
        strcpy(path, hostend);
    } else {
        /* 포트도 경로도 없는 경우
         * 예: "www.example.com" → port=80(기본값), path="/"
         */
        strcpy(path, "/");
    }
}