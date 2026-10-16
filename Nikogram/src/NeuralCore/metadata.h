#pragma once
// Bounded metadata IO, distinct from append-only observation/outcome logs.
#include "neural_input.h"
#include "archive.h"
#include "recovery_paths.h"
#include <atomic>
#include <chrono>
#include <sstream>
#include <string>
#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif
namespace movement::metadata {
inline std::string Read(const std::filesystem::path&path,uint64_t limit,const archive::Cancel&cancel={}){
 archive::Require(limit<=archive::DefaultLimit,"Metadata logical limit too large");neuralio::Input in(path,limit,cancel);std::string text;std::array<char,65536>buffer{};
 while(in){archive::Poll(cancel);in.read(buffer.data(),buffer.size());const auto n=in.gcount();archive::Require(n>=0&&uint64_t(n)<=limit-text.size(),"Metadata exceeds logical byte limit");text.append(buffer.data(),size_t(n));}
 archive::Require(in.eof()&&!in.bad(),"Metadata read failed");return text;
}
namespace detail {
inline std::filesystem::path Stage(const std::filesystem::path&path){
 static std::atomic<uint64_t>serial{0};for(int i=0;i<100;++i){auto p=path;p+=".metadata-stage-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial);
#ifdef _WIN32
 HANDLE h=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(h!=INVALID_HANDLE_VALUE){CloseHandle(h);return p;}const auto error=GetLastError();archive::Require(error==ERROR_FILE_EXISTS||error==ERROR_ALREADY_EXISTS,"Cannot reserve metadata staging file");
#else
 const int fd=::open(p.c_str(),O_CREAT|O_EXCL|O_WRONLY,0600);if(fd>=0){::close(fd);return p;}archive::Require(errno==EEXIST,"Cannot reserve metadata staging file");
#endif
 }throw std::runtime_error("Cannot reserve unique metadata staging path");}

struct Cleanup {std::filesystem::path path;~Cleanup(){if(!path.empty()){std::error_code ec;std::filesystem::remove(path,ec);}}};
}
inline void Replace(const std::filesystem::path&path,const std::string&text,uint64_t limit,const archive::Cancel&cancel={}){
 archive::Require(text.size()<=limit&&limit<=archive::DefaultLimit,"Metadata exceeds logical byte limit");archive::Poll(cancel);detail::Cleanup staged{detail::Stage(path)};
 std::istringstream plain(text);archive::Pack(plain,text.size(),staged.path,9,cancel);archive::Require(Read(staged.path,limit,cancel)==text,"Compressed metadata verification failed");archive::Poll(cancel);
 if(std::filesystem::exists(path)){auto previous=layout::Previous(path);std::filesystem::create_directories(previous.parent_path());recovery::RetainPrevious(path,previous);detail::Cleanup saved{detail::Stage(previous)};std::filesystem::copy_file(path,saved.path,std::filesystem::copy_options::overwrite_existing);archive::Flush(saved.path);archive::Poll(cancel);archive::AtomicReplace(saved.path,previous);saved.path.clear();}
 // Atomic replace leaves the old primary intact if publication fails.
 archive::AtomicReplace(staged.path,path);staged.path.clear();
}
// For the freshly completed plaintext corpus of a built-in export only.
// No full-corpus string allocation; original Learning data is not touched.
inline void CompressPlainFile(const std::filesystem::path&path,const archive::Cancel&cancel={}){
 archive::Poll(cancel);const auto bytes=std::filesystem::file_size(path);const auto modified=std::filesystem::last_write_time(path);detail::Cleanup staged{detail::Stage(path)};
 {std::ifstream source(path,std::ios::binary);archive::Require(bool(source),"Cannot read completed export");archive::Pack(source,bytes,staged.path,9,cancel);}
 {std::ifstream source(path,std::ios::binary);neuralio::Input restored(staged.path,archive::DefaultLimit,cancel);std::array<char,65536>a{},b{};uint64_t count=0;
  while(source){archive::Poll(cancel);source.read(a.data(),a.size());const auto n=source.gcount();if(n){restored.read(b.data(),n);archive::Require(restored.gcount()==n&&std::equal(a.begin(),a.begin()+n,b.begin()),"Compressed export byte verification failed");count+=uint64_t(n);}}
  archive::Require(source.eof()&&!source.bad()&&count==bytes&&restored.peek()==EOF&&!restored.bad(),"Compressed export length/footer verification failed");}
 archive::Require(std::filesystem::file_size(path)==bytes&&std::filesystem::last_write_time(path)==modified,"Completed export changed during compression");archive::Poll(cancel);archive::AtomicReplace(staged.path,path);staged.path.clear();
}
}
