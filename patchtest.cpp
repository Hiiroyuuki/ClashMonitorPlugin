#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <string>
int main(){
  HINTERNET s=WinHttpOpen(L"t",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
  HINTERNET c=WinHttpConnect(s,L"127.0.0.1",9097,0);
  HINTERNET r=WinHttpOpenRequest(c,L"PATCH",L"/configs",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0);
  std::string body="{\"mode\":\"global\"}";
  std::wstring hdr=L"Content-Type: application/json\r\nAuthorization: Bearer admin\r\n";
  BOOL ok=WinHttpSendRequest(r,hdr.c_str(),(DWORD)hdr.size(),(LPVOID)body.data(),(DWORD)body.size(),(DWORD)body.size(),0);
  printf("send=%d err=%lu\n",ok,GetLastError());
  if(ok){ WinHttpReceiveResponse(r,nullptr); DWORD code=0,sz=sizeof(code);
    WinHttpQueryHeaders(r,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&code,&sz,WINHTTP_NO_HEADER_INDEX);
    printf("PATCH global status=%lu\n",code);
  }
  return 0;
}
