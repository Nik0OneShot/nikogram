#include "Loader.h"
#include "resource.h"
#include "MenuStartup.h"
#include <BlackBone/Process/Process.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <vector>
#include <stdexcept>
#include <sstream>
#include <cwctype>
#include <memory>
#include <string_view>

namespace loader {
namespace {
struct Handle {
 HANDLE h=nullptr;
 explicit Handle(HANDLE v=nullptr):h(v){}
 ~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
 Handle(const Handle&)=delete;
 Handle& operator=(const Handle&)=delete;
 bool valid()const{return h&&h!=INVALID_HANDLE_VALUE;}
};
uint64_t Birth(HANDLE h){
 FILETIME c{},e{},k{},u{};
 if(!GetProcessTimes(h,&c,&e,&k,&u))return 0;
 return (uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;
}
std::wstring Lower(std::wstring s){for(auto& c:s)c=wchar_t(towlower(c));return s;}
bool Identity(HANDLE h, DWORD pid, uint64_t born){
 wchar_t path[32768];DWORD size=32768,session=0,ours=0,exit=0;BOOL wow=TRUE;
 return GetProcessId(h)==pid && Birth(h)==born && born!=0
  && GetExitCodeProcess(h,&exit)&&exit==STILL_ACTIVE
  && ProcessIdToSessionId(pid,&session)&&ProcessIdToSessionId(GetCurrentProcessId(),&ours)&&session==ours
  && QueryFullProcessImageNameW(h,0,path,&size)
  && Lower(std::filesystem::path(path).filename().wstring())==L"tf_win64.exe"
  && IsWow64Process(h,&wow)&&!wow;
}
std::filesystem::path Cache(){
 PWSTR local=nullptr;
 if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&local)))throw std::runtime_error("local app data");
 std::filesystem::path result=std::filesystem::path(local)/L"NikogramLoader";
 CoTaskMemFree(local);std::filesystem::create_directories(result);return result;
}
std::filesystem::path Marker(const Target& t){return Cache()/(L"session-"+std::to_wstring(t.pid)+L"-"+std::to_wstring(t.born)+L".lock");}
enum class Attempt { None, NativeComplete, Legacy, Blocked };
Attempt ReadAttempt(const Target& t){
 Handle file(CreateFileW(Marker(t).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
 if(!file.valid())return GetLastError()==ERROR_FILE_NOT_FOUND?Attempt::None:Attempt::Blocked;
 char text[256]{};DWORD size=0;
 if(!ReadFile(file.h,text,sizeof(text),&size,nullptr))return Attempt::Blocked;
 std::string_view note(text,size);
 if(note=="native_complete\n")return Attempt::NativeComplete;
 if(note=="Nikogram loader: explicit injection attempt. Restart TF2 before retrying.\r\n")return Attempt::Legacy;
 return Attempt::Blocked;
}
struct AttemptGuard {
 Handle mutex;bool held=false;
 explicit AttemptGuard(const Target& t):mutex(CreateMutexW(nullptr,FALSE,(L"Local\\NikogramAttempt-"+std::to_wstring(t.pid)+L"-"+std::to_wstring(t.born)).c_str())){
  if(mutex.valid()){auto status=WaitForSingleObject(mutex.h,0);held=status==WAIT_OBJECT_0||status==WAIT_ABANDONED;}
 }
 ~AttemptGuard(){if(held)ReleaseMutex(mutex.h);}
};
bool Modules(DWORD pid,bool& loaded){
 Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid));
 if(!snapshot.valid())return false;
 MODULEENTRY32W entry{sizeof(entry)};bool client=false,engine=false,shader=false;
 if(!Module32FirstW(snapshot.h,&entry))return false;
 do{
  auto name=Lower(entry.szModule);
  client|=name==L"client.dll";engine|=name==L"engine.dll";shader|=name==L"shaderapidx9.dll";
  loaded|=name.find(L"nikogram")!=std::wstring::npos;
 }while(Module32NextW(snapshot.h,&entry));
 return GetLastError()==ERROR_NO_MORE_FILES&&client&&engine&&shader;
}
BOOL CALLBACK FindWindow(HWND w,LPARAM param){
 auto data=reinterpret_cast<std::pair<DWORD,bool>*>(param);DWORD pid=0;GetWindowThreadProcessId(w,&pid);
 if(pid==data->first&&IsWindowVisible(w)&&GetWindow(w,GW_OWNER)==nullptr){data->second=true;return FALSE;}return TRUE;
}
std::wstring StatusHex(NTSTATUS value){wchar_t text[24];swprintf_s(text,L"0x%08X",unsigned(value));return text;}
void Write(HANDLE file,std::span<const unsigned char> bytes){
 size_t offset=0;
 while(offset<bytes.size()){
  DWORD written=0,n=DWORD((std::min)(bytes.size()-offset,size_t(1024*1024)));
  if(!WriteFile(file,bytes.data()+offset,n,&written,nullptr)||!written)throw std::runtime_error("write");
  offset+=written;
 }
 if(!FlushFileBuffers(file))throw std::runtime_error("flush");
}
void WriteAttempt(HANDLE file,std::string_view note){
 LARGE_INTEGER zero{};
 if(!SetFilePointerEx(file,zero,nullptr,FILE_BEGIN)||!SetEndOfFile(file))throw std::runtime_error("attempt record");
 Write(file,{reinterpret_cast<const unsigned char*>(note.data()),note.size()});
}
std::wstring FileHash(HANDLE file){
 LARGE_INTEGER size{};
 if(!GetFileSizeEx(file,&size)||size.QuadPart<0||size.QuadPart>64*1024*1024)throw std::runtime_error("file size");
 LARGE_INTEGER zero{};if(!SetFilePointerEx(file,zero,nullptr,FILE_BEGIN))throw std::runtime_error("seek");
 std::vector<unsigned char> bytes(size_t(size.QuadPart));DWORD read=0;
 if(!ReadFile(file,bytes.data(),DWORD(bytes.size()),&read,nullptr)||read!=bytes.size())throw std::runtime_error("read");
 return Hash(bytes);
}
}

std::span<const unsigned char> Resource(int id){
 auto module=GetModuleHandleW(nullptr);auto info=FindResourceW(module,MAKEINTRESOURCEW(id),RT_RCDATA);
 if(!info)throw std::runtime_error("missing embedded resource");
 auto size=SizeofResource(module,info);auto data=LockResource(LoadResource(module,info));
 if(!data||!size)throw std::runtime_error("empty resource");
 return {static_cast<const unsigned char*>(data),size};
}
std::wstring Hash(std::span<const unsigned char> bytes){
 BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
 if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("sha256 provider");
 unsigned char digest[32];
 auto status=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0);
 if(status>=0)status=BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),0);
 if(status>=0)status=BCryptFinishHash(hash,digest,sizeof(digest),0);
 if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
 if(status<0)throw std::runtime_error("sha256");
 constexpr wchar_t hex[]=L"0123456789ABCDEF";std::wstring result;
 for(auto byte:digest){result+=hex[byte>>4];result+=hex[byte&15];}return result;
}
bool VerifyPayloads(){
    return Hash(Resource(ID_PAYLOAD))==L"1F0417A6A75F2A1EB11DB8E723FB9B12DEB813B11EBF5E2E2D887189AFFA3826"
        &&Hash(Resource(ID_SOURCE))==L"B8D6A367C01056F6AD76C853CDB5956C1FBB87274CD789F2DFDB3A1B3BC2AE3B";
}
std::wstring Error(DWORD error){
 wchar_t* message=nullptr;FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,error,0,reinterpret_cast<PWSTR>(&message),0,nullptr);
 std::wstring result=message?message:L"Windows error "+std::to_wstring(error);if(message)LocalFree(message);return result;
}
Target Detect(){
 Target t;Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
 if(!snapshot.valid()){t.status=L"unable to check running games";return t;}
 PROCESSENTRY32W entry{sizeof(entry)};DWORD ours=0;ProcessIdToSessionId(GetCurrentProcessId(),&ours);
 int found=0;
 if(Process32FirstW(snapshot.h,&entry))do{
  DWORD session=0;
  if(_wcsicmp(entry.szExeFile,L"tf_win64.exe")||!ProcessIdToSessionId(entry.th32ProcessID,&session)||session!=ours)continue;
  ++found;t.pid=entry.th32ProcessID;
 }while(Process32NextW(snapshot.h,&entry));
 if(!found)return t;
 if(found!=1){t.status=L"multiple TF2 instances; close extras";return t;}
 Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,t.pid));
 if(!process.valid()){t.status=L"TF2 access denied; check app permissions";return t;}
 t.born=Birth(process.h);
 if(!Identity(process.h,t.pid,t.born)){t.status=L"waiting for 64-bit TF2";return t;}
 bool loaded=false;bool modules=Modules(t.pid,loaded);
 if(!modules){t.status=L"waiting for a complete TF2 module scan";return t;}
 if(loaded){t.status=L"nikogram is already loaded";return t;}
 try{
  auto attempt=ReadAttempt(t);
  if(attempt==Attempt::Blocked){t.status=L"manual or uncertain attempt; restart TF2";return t;}
  t.legacyAttempt=attempt==Attempt::Legacy;
 }catch(...){t.status=L"local cache is unavailable";return t;}
 std::pair<DWORD,bool> window{t.pid,false};EnumWindows(FindWindow,reinterpret_cast<LPARAM>(&window));
 if(!modules||!window.second){t.status=L"TF2 is starting; waiting for game modules";return t;}
 t.ready=true;t.status=t.legacyAttempt?L"TF2 ready; previous native unload needs confirmation":L"tf2 detected";return t;
}
void Export(int resource,const std::filesystem::path& path,bool overwrite){
 Handle file(CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,overwrite?CREATE_ALWAYS:CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
 if(!file.valid())throw std::runtime_error("create export");
 Write(file.h,Resource(resource));
}
Result Inject(Target expected,bool confirmLegacyNativeUnload,int startupMenu){
 try{
  AttemptGuard attemptGuard(expected);
  if(!attemptGuard.held)return {false,L"Another loader operation is already in progress. No injection attempted."};
  if(!VerifyPayloads())return {false,L"Embedded payload verification failed. No injection attempted."};
  if(startupMenu < -1 || startupMenu > 1)return {false,L"Invalid startup menu choice. Nothing was injected."};
  auto current=Detect();
  if(!current.ready||current.pid!=expected.pid||current.born!=expected.born)return {false,L"TF2 changed or is not ready. No injection attempted."};
  if(current.legacyAttempt&&!confirmLegacyNativeUnload)return {false,L"Confirm that the old loader used native inject and that Nikogram has fully unloaded before retrying."};
  // Initialize native loading only after an explicit click. No driver or privilege adjustment.
  blackbone::Process process;
  constexpr DWORD access=PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|PROCESS_VM_WRITE|PROCESS_VM_OPERATION|PROCESS_CREATE_THREAD|PROCESS_DUP_HANDLE;
  auto status=process.Attach(expected.pid,access);
  if(!NT_SUCCESS(status))return {false,L"Could not open TF2 ("+StatusHex(status)+L"). Check that TF2 and the loader run at the same permission level."};
  if(!Identity(process.core().handle(),expected.pid,expected.born))return {false,L"TF2 restarted during preparation. Nothing was injected."};
  bool already=false;
  if(!Modules(expected.pid,already)||already)return {false,L"TF2 modules changed, or Nikogram is already loaded. Nothing was injected."};
  // Windows' native DLL loader needs a disk file. It stays in a versioned local
  // cache, never beside or inside TF2. Lock it against replacement while loading.
  auto folder=Cache()/L"payload"/Hash(Resource(ID_PAYLOAD));std::filesystem::create_directories(folder);
  auto filePath=folder/L"Nikogram.dll";
  if(!std::filesystem::exists(filePath))Export(ID_PAYLOAD,filePath,false);
  Handle locked(CreateFileW(filePath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
  if(!locked.valid()||FileHash(locked.h)!=Hash(Resource(ID_PAYLOAD)))return {false,L"The cached DLL is missing, busy or modified. No injection attempted."};
  // Completed native loads may be retried only after a full module scan shows
  // the DLL is gone. Pending/failed/manual attempts remain blocked. The mutex
  // serializes the final recheck and journal update across new loader copies.
  const auto attempt=ReadAttempt(expected);
  if(attempt==Attempt::Blocked||(attempt==Attempt::Legacy&&!confirmLegacyNativeUnload))
   return {false,L"An uncertain or manually mapped attempt exists. Restart TF2 before retrying."};
  MenuStartup::Request menuChoice(expected.pid,expected.born,startupMenu);
  if(startupMenu>=0 && !menuChoice.Ready())return {false,L"Could not prepare the startup menu choice. Nothing was injected."};
  Handle marker(CreateFileW(Marker(expected).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
  if(!marker.valid())return {false,L"Could not update the local attempt record. Nothing was injected."};
  WriteAttempt(marker.h,"native_pending\n");
  auto result=process.modules().Inject(filePath.wstring());
  if(!result||!result.result())return {false,L"Native injection failed ("+StatusHex(result.status)+L"). Restart TF2 before another attempt."};
  WriteAttempt(marker.h,"native_complete\n");
  if(startupMenu>=0 && !menuChoice.Wait())return {true,L"The DLL loaded, but did not acknowledge the startup menu choice. Do not inject again. Check the game; you can select the menu under Interface.",true};
  return {true,L"DLL loaded. Check Nikogram in-game; its own startup checks may still be running."};
 }catch(...){return {false,L"Loading could not complete. If an attempt started, restart TF2 before retrying. No automatic retry was made."};}
}
}
