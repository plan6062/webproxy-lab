/*
 * adder.c - a minimal CGI program that adds two numbers together
 */
#include "csapp.h"

int main(void) {
    char *buf, *p;
    char arg1[MAXLINE], arg2[MAXLINE], content[MAXBUF];
    int n1 = 0, n2 = 0;

    /* Extract the two arguments */
    if ((buf = getenv("QUERY_STRING")) != NULL) {
        
        /* 두 가지 형식 지원:
         * 1. 기존 형식: "15&20"
         * 2. 폼 형식: "first=15&second=20"
         */
        
        if (strstr(buf, "first=") && strstr(buf, "second=")) {
            /* 폼에서 온 형식: first=15&second=20 */
            
            /* first= 찾기 */
            if ((p = strstr(buf, "first=")) != NULL) {
                p += 6;  /* "first=" 건너뛰기 */
                
                /* & 찾아서 첫 번째 값 추출 */
                char *amp = strchr(p, '&');
                if (amp) {
                    strncpy(arg1, p, amp - p);
                    arg1[amp - p] = '\0';
                    
                    /* second= 찾기 */
                    if ((p = strstr(amp + 1, "second=")) != NULL) {
                        strcpy(arg2, p + 7);  /* "second=" 건너뛰기 */
                    }
                }
            }
        } else {
            /* 기존 형식: 15&20 */
            p = strchr(buf, '&');
            if (p) {
                strncpy(arg1, buf, p - buf);
                arg1[p - buf] = '\0';
                strcpy(arg2, p + 1);
            } else {
                strcpy(arg1, buf);
                strcpy(arg2, "0");
            }
        }
        
        n1 = atoi(arg1);
        n2 = atoi(arg2);
    }

    /* Make the response body */
    sprintf(content, "<!DOCTYPE html>\r\n");
    sprintf(content, "%s<html>\r\n", content);
    sprintf(content, "%s<head><title>Calculator Result</title></head>\r\n", content);
    sprintf(content, "%s<body style='font-family: Arial; text-align: center; margin: 50px;'>\r\n", content);
    sprintf(content, "%s<h1>🎉 Calculation Result</h1>\r\n", content);
    sprintf(content, "%s<div style='background: #f0f0f0; padding: 20px; border-radius: 10px; display: inline-block;'>\r\n", content);
    sprintf(content, "%s<h2>%d + %d = <span style='color: #4CAF50;'>%d</span></h2>\r\n", content, n1, n2, n1 + n2);
    sprintf(content, "%s</div>\r\n", content);
    sprintf(content, "%s<p><a href='/calculator.html' style='color: #4CAF50; text-decoration: none;'>🔙 Back to Calculator</a></p>\r\n", content);
    sprintf(content, "%s<hr>\r\n", content);
    sprintf(content, "%s<p><em>Tiny Web Server CGI Calculator</em></p>\r\n", content);
    sprintf(content, "%s</body></html>\r\n", content);

    /* Generate the HTTP response */
    printf("Content-length: %d\r\n", (int)strlen(content));
    printf("Content-type: text/html\r\n\r\n");
    printf("%s", content);
    fflush(stdout);

    exit(0);
}