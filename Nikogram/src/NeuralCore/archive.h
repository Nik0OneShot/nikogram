#pragma once
// Unencrypted byte-exact observation archives. Codec: vendored Zstandard1.5.7.
#include "zstd/zstd.h"
#include <array>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <streambuf>
#include <thread>
#include <vector>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winioctl.h>
#endif
namespace movement::archive {
namespace fs=std::filesystem;
using Cancel=std::function<bool()>;
inline constexpr uint32_t Block=1024*1024,MaxFrame=Block+8192;
inline constexpr uint64_t DefaultLimit=32ull*1024*1024*1024;
inline constexpr std::array<char,8>Magic{'N','I','K','O','Z','S','T','1'};
inline bool IsPacked(const fs::path&p){std::ifstream in(p,std::ios::binary);std::array<char,8>magic{};return bool(in.read(magic.data(),8))&&magic==Magic;}
inline void Require(bool ok,const char*message){if(!ok)throw std::runtime_error(message);}
inline void Poll(const Cancel&cancel){if(cancel&&cancel())throw std::runtime_error("Archive operation cancelled; originals retained");std::this_thread::yield();}
inline uint32_t Crc(uint32_t crc,const char*data,size_t n){
 static const auto table=[] {std::array<uint32_t,256>t{};for(uint32_t i=0;i<256;++i){uint32_t v=i;for(int k=0;k<8;++k)v=(v>>1)^((v&1)?0xedb88320u:0);t[i]=v;}return t;}();
 for(size_t i=0;i<n;++i)crc=table[(crc^uint8_t(data[i]))&255]^(crc>>8);return crc;
}
inline void Put(std::ostream&out,uint64_t value,int width){for(int i=0;i<width;++i)out.put(char(value>>(8*i)));Require(bool(out),"Archive write failed");}
inline uint64_t Get(std::istream&in,int width){uint64_t v=0;for(int i=0;i<width;++i){int c=in.get();Require(c!=EOF,"Truncated archive metadata");v|=uint64_t(uint8_t(c))<<(8*i);}return v;}
inline void Zcheck(size_t n){if(ZSTD_isError(n))throw std::runtime_error(std::string("Zstandard: ")+ZSTD_getErrorName(n));}
inline void Flush(const fs::path&p){
#ifdef _WIN32
 auto h=CreateFileW(p.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Require(h!=INVALID_HANDLE_VALUE,"Cannot open archive for durable flush");bool ok=FlushFileBuffers(h)!=0;CloseHandle(h);Require(ok,"Archive durable flush failed");
#else
 (void)p;
#endif
}
inline void AtomicReplace(const fs::path&from,const fs::path&to){
#ifdef _WIN32
 Require(MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"Cannot atomically commit archive metadata");
#else
 fs::rename(from,to);
#endif
}
// Best effort only on a NEW working copy, never original or backup files.
inline bool NativeCompress(const fs::path&p,uint64_t expectedBytes=0){
#ifdef _WIN32
 if(expectedBytes>=30ull*1024*1024*1024)return false;
 const bool directory=fs::is_directory(p);auto h=CreateFileW(p.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,directory?FILE_FLAG_BACKUP_SEMANTICS:FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)return false;
 USHORT format=COMPRESSION_FORMAT_DEFAULT;DWORD done=0;bool ok=DeviceIoControl(h,FSCTL_SET_COMPRESSION,&format,sizeof format,nullptr,0,&done,nullptr)!=0;CloseHandle(h);auto attr=GetFileAttributesW(p.c_str());return ok&&attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_COMPRESSED);
#else
 (void)p;(void)expectedBytes;return false;
#endif
}
inline void Pack(std::istream&in,uint64_t bytes,const fs::path&dest,int level=9,const Cancel&cancel={}){
 Poll(cancel);
 Require(level==9||level==19,"Supported archive levels are9 and19");Require(bytes<=DefaultLimit,"Archive exceeds32GiB per-file limit");
 std::ofstream out(dest,std::ios::binary|std::ios::trunc);Require(bool(out),"Cannot create archive");out.write(Magic.data(),8);Put(out,1,4);Put(out,Block,4);Put(out,bytes,8);
 std::array<char,16>header{};for(int i=0;i<4;++i){header[i]=char(1u>>(8*i));header[i+4]=char(Block>>(8*i));}for(int i=0;i<8;++i)header[i+8]=char(bytes>>(8*i));Put(out,Crc(~0u,header.data(),header.size())^~0u,4);
 std::unique_ptr<ZSTD_CCtx,decltype(&ZSTD_freeCCtx)>ctx(ZSTD_createCCtx(),ZSTD_freeCCtx);Require(bool(ctx),"Cannot allocate compressor");Zcheck(ZSTD_CCtx_setParameter(ctx.get(),ZSTD_c_compressionLevel,level));Zcheck(ZSTD_CCtx_setParameter(ctx.get(),ZSTD_c_windowLog,20));Zcheck(ZSTD_CCtx_setParameter(ctx.get(),ZSTD_c_checksumFlag,1));Zcheck(ZSTD_CCtx_setParameter(ctx.get(),ZSTD_c_nbWorkers,0));
 std::vector<char>raw(Block),packed(MaxFrame);uint64_t left=bytes,index=0;uint32_t total=~0u;
 while(left){Poll(cancel);const auto n=size_t(std::min(left,uint64_t(Block)));Require(bool(in.read(raw.data(),std::streamsize(n))),"Source changed or snapshot truncated");auto size=ZSTD_compress2(ctx.get(),packed.data(),packed.size(),raw.data(),n);Zcheck(size);Require(size<=MaxFrame,"Compressed frame exceeds bound");Put(out,n,4);Put(out,size,4);Put(out,index++,8);Put(out,Crc(~0u,raw.data(),n)^~0u,4);out.write(packed.data(),std::streamsize(size));Require(bool(out),"Archive write failed");total=Crc(total,raw.data(),n);left-=n;}
 Put(out,0,4);Put(out,index,8);Put(out,total^~0u,4);Require(bool(out.flush()),"Archive flush failed");out.close();Flush(dest);
}
class Buffer:public std::streambuf {
 std::ifstream file;bool packed=false,finished=false;uint64_t remaining=0,index=0;uint32_t total=~0u;std::vector<char>raw,encoded;std::unique_ptr<ZSTD_DCtx,decltype(&ZSTD_freeDCtx)>ctx{nullptr,ZSTD_freeDCtx};Cancel cancel;
 int_type underflow()override{
  if(gptr()&&gptr()<egptr())return traits_type::to_int_type(*gptr());if(finished)return traits_type::eof();Poll(cancel);
  if(!packed){file.read(raw.data(),std::streamsize(raw.size()));auto n=file.gcount();Require(!file.bad(),"Text read failed");if(!n){finished=true;return traits_type::eof();}setg(raw.data(),raw.data(),raw.data()+n);return traits_type::to_int_type(*gptr());}
  auto n=Get(file,4);if(!remaining){Require(n==0&&Get(file,8)==index&&Get(file,4)==(total^~0u),"Archive footer/checksum mismatch");Require(file.peek()==EOF&&!file.bad(),"Trailing archive data");finished=true;return traits_type::eof();}
  auto size=Get(file,4),ordinal=Get(file,8),crc=Get(file,4);Require(n==std::min(remaining,uint64_t(Block))&&size>0&&size<=MaxFrame&&ordinal==index++,"Invalid archive block dimensions/order");Require(bool(file.read(encoded.data(),std::streamsize(size))),"Truncated archive frame");Require(ZSTD_getFrameContentSize(encoded.data(),size)==n,"Unexpected decompressed frame size");auto frame=ZSTD_findFrameCompressedSize(encoded.data(),size);Zcheck(frame);Require(frame==size,"Multiple or trailing compressed frames");auto actual=ZSTD_decompressDCtx(ctx.get(),raw.data(),Block,encoded.data(),size);Zcheck(actual);Require(actual==n&&(Crc(~0u,raw.data(),actual)^~0u)==crc,"Archive block checksum mismatch");total=Crc(total,raw.data(),actual);remaining-=actual;setg(raw.data(),raw.data(),raw.data()+actual);return traits_type::to_int_type(*gptr());
 }
public:
 explicit Buffer(const fs::path&p,uint64_t limit=DefaultLimit,Cancel c={}):file(p,std::ios::binary),raw(Block),cancel(std::move(c)){
  Require(bool(file),"Cannot open observation input");std::array<char,8>magic{};file.read(magic.data(),8);packed=file.gcount()==8&&magic==Magic;file.clear();file.seekg(packed?8:0);if(!packed)return;
  auto version=Get(file,4),block=Get(file,4);remaining=Get(file,8);auto crc=Get(file,4);std::array<char,16>header{};for(int i=0;i<4;++i){header[i]=char(version>>(8*i));header[i+4]=char(block>>(8*i));}for(int i=0;i<8;++i)header[i+8]=char(remaining>>(8*i));Require(version==1&&block==Block&&remaining<=limit&&crc==(Crc(~0u,header.data(),16)^~0u),"Unsupported/corrupt archive header or output exceeds limit");
  const auto blocks=remaining/Block+(remaining%Block!=0);Require(blocks<=(fs::file_size(p)-28)/20,"Impossible archive total size");encoded.resize(MaxFrame);ctx.reset(ZSTD_createDCtx());Require(bool(ctx),"Cannot allocate decompressor");Zcheck(ZSTD_DCtx_setParameter(ctx.get(),ZSTD_d_windowLogMax,20));
 }
};
class Input:public std::istream {
 Buffer buffer;
public:explicit Input(const fs::path&p,uint64_t limit=DefaultLimit,Cancel c={}):std::istream(nullptr),buffer(p,limit,std::move(c)){rdbuf(&buffer);exceptions(std::ios::badbit);}
};
inline uint64_t Unpack(const fs::path&source,const fs::path&dest,uint64_t limit=DefaultLimit,const Cancel&cancel={}){
 Poll(cancel);{std::ifstream header(source,std::ios::binary);std::array<char,8>magic{};Require(bool(header.read(magic.data(),8))&&magic==Magic,"Not a supported NIKOZST1 archive");}
 Input in(source,limit,cancel);std::ofstream out(dest,std::ios::binary|std::ios::trunc);Require(bool(out),"Cannot create unpacked output");std::vector<char>buf(65536);uint64_t total=0;
 while(in){Poll(cancel);in.read(buf.data(),buf.size());auto n=in.gcount();Require(uint64_t(n)<=limit-total,"Unpacked output exceeds limit");total+=n;if(n)out.write(buf.data(),n);Require(bool(out),"Unpack write failed");}Require(in.eof()&&!in.bad(),"Unpack read failed");Require(bool(out.flush()),"Unpack flush failed");out.close();Flush(dest);return total;
}
inline void PrepareWorking(const fs::path&path){if(!fs::exists(path)&&fs::exists(path.parent_path()/"compressed-working-v1.txt")){std::ofstream out(path,std::ios::binary);Require(bool(out),"Cannot create working observations");out.close();NativeCompress(path);}}
}
