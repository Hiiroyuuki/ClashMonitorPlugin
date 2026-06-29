#include "ClashPlugin.h"
#include "PluginInterface.h"
#include <windows.h>
#include <stdio.h>
typedef ITMPlugin* (*GetInst)();
int main(){
  HMODULE h=LoadLibraryW(L"D:/pythonProject/trafficmonitor-plugins/ClashMonitorPlugin/ClashMonitorPlugin.dll");
  if(!h){printf("LOAD FAIL %lu\n",GetLastError());return 1;}
  ITMPlugin* p=((GetInst)GetProcAddress(h,"TMPluginGetInstance"))();
  CreateDirectoryW(L"D:/pythonProject/trafficmonitor-plugins/ClashMonitorPlugin/testcfg",0);
  p->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR,L"D:/pythonProject/trafficmonitor-plugins/ClashMonitorPlugin/testcfg");
  p->DataRequired();
  ClashPlugin& cp=ClashPlugin::Instance();
  printf("proxy before: %d\n", cp.IsProxyEnabled());
  cp.ToggleSystemProxy();
  printf("proxy after toggle: %d\n", cp.IsProxyEnabled());
  cp.ToggleSystemProxy();
  printf("proxy after toggle2: %d\n", cp.IsProxyEnabled());
  return 0;
}
