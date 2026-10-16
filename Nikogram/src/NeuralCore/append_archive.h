#pragma once
// Append-only, unencrypted, independently recoverable Zstandard record batches.
// One serialized owner per file. Flush before snapshots, commands and shutdown;
// call MaybeFlush on every service iteration (including idle) for the 100ms bound.
#include "archive.h"
#include "recovery_paths.h"
#include <atomic>
#include <chrono>
#include <limits>
#include <string_view>
#include <sstream>
namespace movement::appendlog {
namespace fs = std::filesystem;
using Cancel = archive::Cancel;
inline constexpr std::array<char,8> Magic{'N','I','K','O','A','P','P','1'};
// Reserve worst-case compression overhead so each explicit data buffer is <=1MiB.
inline constexpr uint32_t Block = archive::Block-8192, MaxFrame=archive::Block, Pending = 65536;
inline constexpr size_t HeaderSize = 24, FrameHeader = 32, Trailer = 24;
inline constexpr uint32_t FrameTag = 0x314d5246, EndTag = 0x31444e45; // FRM1 / END1
using archive::Require;
inline uint32_t CRC(const char* p,size_t n) { return archive::Crc(~0u,p,n)^~0u; }
inline void Put(char* p,uint64_t v,size_t n) { for(size_t i=0;i<n;++i)p[i]=char(v>>(8*i)); }
inline uint64_t Get(const char* p,size_t n) { uint64_t v=0;for(size_t i=0;i<n;++i)v|=uint64_t(uint8_t(p[i]))<<(8*i);return v; }
inline bool IsAppend(const fs::path& p) { std::ifstream f(p,std::ios::binary);std::array<char,8> m{};return bool(f.read(m.data(),8))&&m==Magic; }
struct Report {
 bool repaired=false,tornTail=false;
 fs::path backup;
 uint64_t logicalBytes=0,frames=0,physicalBytes=0,validBytes=HeaderSize;
 int lastByte=-1;
};
inline void WriteHeader(std::ostream& f) {
 std::array<char,HeaderSize> h{};std::copy(Magic.begin(),Magic.end(),h.begin());Put(h.data()+8,1,4);Put(h.data()+12,Block,4);Put(h.data()+16,0,4);Put(h.data()+20,CRC(h.data(),20),4);f.write(h.data(),h.size());Require(bool(f),"Append header write failed");
}
class Reader {
 std::ifstream file;
 uint64_t offset=HeaderSize,limit;
 Cancel cancel;
 std::unique_ptr<ZSTD_DCtx,decltype(&ZSTD_freeDCtx)> ctx{ZSTD_createDCtx(),ZSTD_freeDCtx};
 std::vector<char> encoded;
 bool terminal=false;
 bool Torn(bool allow) { report.tornTail=true;terminal=true;Require(allow,"Incomplete append tail; explicit recovery required");return false; }
public:
 Report report;
 std::vector<char> raw;
 explicit Reader(const fs::path& p,uint64_t max=archive::DefaultLimit,Cancel c={}):file(p,std::ios::binary),limit(max),cancel(std::move(c)),encoded(MaxFrame),raw(Block) {
  archive::Poll(cancel);Require(bool(file),"Cannot open append input");report.physicalBytes=fs::file_size(p);std::array<char,HeaderSize> h{};
  Require(report.physicalBytes>=HeaderSize&&bool(file.read(h.data(),h.size())),"Truncated append file header");
  Require(std::equal(Magic.begin(),Magic.end(),h.begin())&&Get(h.data()+8,4)==1&&Get(h.data()+12,4)==Block&&Get(h.data()+16,4)==0&&Get(h.data()+20,4)==CRC(h.data(),20),"Unsupported/corrupt append file header");
  Require(bool(ctx),"Cannot allocate append decoder");archive::Zcheck(ZSTD_DCtx_setParameter(ctx.get(),ZSTD_d_windowLogMax,20));
 }
 size_t Next(bool allowTorn=false) {
  if(terminal)return 0;archive::Poll(cancel);if(offset==report.physicalBytes){terminal=true;return 0;}
  auto left=report.physicalBytes-offset;
  if(left<FrameHeader){std::array<char,FrameHeader> h{};Require(bool(file.read(h.data(),std::streamsize(left))),"Append input changed");std::array<char,4>tag{};Put(tag.data(),FrameTag,4);Require(std::equal(h.begin(),h.begin()+std::min<size_t>(4,size_t(left)),tag.begin()),"Corrupt partial append frame header");Torn(allowTorn);return 0;}
  std::array<char,FrameHeader> h{};Require(bool(file.read(h.data(),h.size())),"Append input changed");
  Require(Get(h.data(),4)==FrameTag&&Get(h.data()+12,4)==0&&Get(h.data()+28,4)==CRC(h.data(),28),"Corrupt append frame header");
  auto n=Get(h.data()+4,4),packed=Get(h.data()+8,4),ordinal=Get(h.data()+16,8),crc=Get(h.data()+24,4);
  Require(n>0&&n<=Block&&packed>0&&packed<=MaxFrame&&ordinal==report.frames,"Invalid append frame dimensions/order");
  Require(report.logicalBytes<=limit&&n<=limit-report.logicalBytes,"Append output exceeds limit");
  if(left<FrameHeader+packed+Trailer){Torn(allowTorn);return 0;}
  Require(bool(file.read(encoded.data(),std::streamsize(packed))),"Append input changed");
  Require(ZSTD_getFrameContentSize(encoded.data(),size_t(packed))==n,"Unexpected append decoded size");
  auto frame=ZSTD_findFrameCompressedSize(encoded.data(),size_t(packed));archive::Zcheck(frame);Require(frame==packed,"Trailing/multiple append compressed frames");
  auto count=ZSTD_decompressDCtx(ctx.get(),raw.data(),raw.size(),encoded.data(),size_t(packed));archive::Zcheck(count);Require(count==n&&CRC(raw.data(),count)==crc,"Append raw checksum mismatch");
  std::array<char,Trailer> t{};Require(bool(file.read(t.data(),t.size())),"Append input changed");
  Require(Get(t.data(),4)==EndTag&&Get(t.data()+4,8)==ordinal&&Get(t.data()+12,8)==report.logicalBytes+n&&Get(t.data()+20,4)==CRC(t.data(),20),"Corrupt append frame trailer");
  offset+=FrameHeader+packed+Trailer;report.validBytes=offset;report.logicalBytes+=n;++report.frames;report.lastByte=uint8_t(raw[count-1]);return count;
 }
};
inline Report Scan(const fs::path& p,uint64_t limit=archive::DefaultLimit,const Cancel& cancel={}) { Reader r(p,limit,cancel);while(r.Next(true)){}return r.report; }
class Buffer:public std::streambuf {
 Reader reader;
 int_type underflow()override {if(gptr()&&gptr()<egptr())return traits_type::to_int_type(*gptr());auto n=reader.Next();if(!n)return traits_type::eof();setg(reader.raw.data(),reader.raw.data(),reader.raw.data()+n);return traits_type::to_int_type(*gptr());}
public:explicit Buffer(const fs::path& p,uint64_t limit,Cancel cancel):reader(p,limit,std::move(cancel)){}
};
class Input:public std::istream {
 Buffer buffer;
public:explicit Input(const fs::path& p,uint64_t limit=archive::DefaultLimit,Cancel cancel={}):std::istream(nullptr),buffer(p,limit,std::move(cancel)){rdbuf(&buffer);exceptions(std::ios::badbit);}
};
class Compressor {
 std::unique_ptr<ZSTD_CCtx,decltype(&ZSTD_freeCCtx)> ctx{ZSTD_createCCtx(),ZSTD_freeCCtx};
 std::vector<char> encoded;
public:
 Compressor():encoded(MaxFrame) {Require(bool(ctx)&&ZSTD_compressBound(Block)<=MaxFrame,"Cannot allocate append compressor or invalid frame bound");archive::Zcheck(ZSTD_CCtx_setParameter(ctx.get(),ZSTD_c_compressionLevel,9));archive::Zcheck(ZSTD_CCtx_setParameter(ctx.get(),ZSTD_c_windowLog,20));archive::Zcheck(ZSTD_CCtx_setParameter(ctx.get(),ZSTD_c_checksumFlag,1));archive::Zcheck(ZSTD_CCtx_setParameter(ctx.get(),ZSTD_c_nbWorkers,0));}
 void Frame(std::ostream& f,std::string_view raw,uint64_t ordinal,uint64_t prior) {
  Require(!raw.empty()&&raw.size()<=Block&&prior<=archive::DefaultLimit&&raw.size()<=archive::DefaultLimit-prior,"Append frame exceeds bounds");
  auto n=ZSTD_compress2(ctx.get(),encoded.data(),encoded.size(),raw.data(),raw.size());archive::Zcheck(n);
  std::array<char,FrameHeader> h{};Put(h.data(),FrameTag,4);Put(h.data()+4,raw.size(),4);Put(h.data()+8,n,4);Put(h.data()+16,ordinal,8);Put(h.data()+24,CRC(raw.data(),raw.size()),4);Put(h.data()+28,CRC(h.data(),28),4);
  std::array<char,Trailer> t{};Put(t.data(),EndTag,4);Put(t.data()+4,ordinal,8);Put(t.data()+12,prior+raw.size(),8);Put(t.data()+20,CRC(t.data(),20),4);
  f.write(h.data(),h.size());f.write(encoded.data(),std::streamsize(n));f.write(t.data(),t.size());Require(bool(f),"Append frame write failed");
 }
};
inline Report CreateFrom(std::istream& in,uint64_t bytes,const fs::path& dest,const Cancel& cancel={}) {
 archive::Poll(cancel);Require(bytes<=archive::DefaultLimit&&!fs::exists(dest),"Append destination exists or input exceeds limit");
 std::ofstream out(dest,std::ios::binary|std::ios::trunc);Require(bool(out),"Cannot create append file");WriteHeader(out);Compressor compressor;Report report;std::string data;data.reserve(Block);uint64_t left=bytes;
 while(left||!data.empty()) {
  archive::Poll(cancel);auto n=size_t(std::min<uint64_t>(left,Block-data.size()));auto old=data.size();data.resize(old+n);if(n)Require(bool(in.read(data.data()+old,std::streamsize(n))),"Append source changed or truncated");left-=n;
  auto emit=data.size();if(left){auto newline=data.rfind('\n');Require(newline!=std::string::npos,"Append source record exceeds1MiB");emit=newline+1;}
  if(emit){compressor.Frame(out,std::string_view(data.data(),emit),report.frames,report.logicalBytes);++report.frames;report.logicalBytes+=emit;report.lastByte=uint8_t(data[emit-1]);data.erase(0,emit);}
 }
 Require(bool(out.flush()),"Append creation flush failed");out.close();archive::Flush(dest);report.physicalBytes=report.validBytes=fs::file_size(dest);return report;
}
inline fs::path Unique(const fs::path& p,const char* suffix) {static std::atomic<uint64_t> serial{0};for(;;){auto q=p;q+=std::string(suffix)+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial);if(!fs::exists(q))return q;}}
inline Report Recover(const fs::path& p,const Cancel& cancel={}) {
 auto before=fs::last_write_time(p);auto report=Scan(p,archive::DefaultLimit,cancel);if(!report.tornTail)return report;
 archive::Poll(cancel);auto backup=Unique(recovery::Backup(p.parent_path(),"AppendRepair",p.filename()),".torn-backup-");Require(fs::copy_file(p,backup,fs::copy_options::none),"Cannot preserve incomplete append original");archive::Flush(backup);report.backup=backup;
 auto stage=Unique(p,".recovered-");{std::ifstream in(backup,std::ios::binary);std::ofstream out(stage,std::ios::binary);Require(bool(in)&&bool(out),"Cannot create append recovery copy");std::vector<char> buffer(65536);uint64_t left=report.validBytes;while(left){archive::Poll(cancel);auto n=std::min<uint64_t>(left,buffer.size());Require(bool(in.read(buffer.data(),std::streamsize(n))),"Recovery backup truncated");out.write(buffer.data(),std::streamsize(n));Require(bool(out),"Recovery copy write failed");left-=n;}Require(bool(out.flush()),"Recovery copy flush failed");}archive::Flush(stage);
 Require(fs::file_size(p)==report.physicalBytes&&fs::last_write_time(p)==before,"Append file changed during recovery; original retained");archive::Poll(cancel);archive::AtomicReplace(stage,p);report.repaired=true;return report;
}
class Writer {
 fs::path path;Compressor compressor;Report report;std::string pending;bool failed=false;
 std::chrono::steady_clock::time_point pendingSince{};
 void Drain(size_t count) {
  Require(!failed,"Append writer is failed; reopen after explicit recovery");if(!count)return;
  try {Require(fs::file_size(path)==report.validBytes,"Append file changed outside writer");std::ofstream out(path,std::ios::binary|std::ios::app);Require(bool(out),"Cannot open append output");compressor.Frame(out,std::string_view(pending.data(),count),report.frames,report.logicalBytes);Require(bool(out.flush()),"Append stream flush failed");out.close();archive::Flush(path);report.logicalBytes+=count;report.lastByte=uint8_t(pending[count-1]);++report.frames;report.physicalBytes=report.validBytes=fs::file_size(path);pending.erase(0,count);}
  catch(...){failed=true;throw;}
 }
public:
 explicit Writer(const fs::path& p):path(p) {if(!fs::exists(p)){std::istringstream empty;CreateFrom(empty,0,p);}report=Scan(p);Require(!report.tornTail,"Append writer requires explicit tail recovery");pending.reserve(Pending);}
 Writer(const Writer&)=delete;Writer& operator=(const Writer&)=delete;
 uint64_t LogicalBytes()const {return report.logicalBytes+pending.size();}
 uint64_t Bytes()const {return LogicalBytes();}
 int LastByte()const {return pending.empty()?report.lastByte:uint8_t(pending.back());}
 size_t PendingBytes()const {return pending.size();}
 void Flush() {Drain(pending.size());}
 void MaybeFlush() {if(!pending.empty()&&std::chrono::steady_clock::now()-pendingSince>=std::chrono::milliseconds(100))Flush();}
 void Append(std::string_view data) {
  Require(!failed,"Append writer is failed");Require(LogicalBytes()<=archive::DefaultLimit&&data.size()<=archive::DefaultLimit-LogicalBytes(),"Append output exceeds limit");
  // Validate complete incoming records before committing any part of this call.
  size_t start=0;while(start<data.size()){auto end=data.find('\n',start);auto n=(end==std::string_view::npos?data.size():end+1)-start;if(!start&&!pending.empty()){auto previous=pending.rfind('\n');n+=previous==std::string::npos?pending.size():pending.size()-previous-1;}Require(n<=Pending,"Append record exceeds64KiB");if(end==std::string_view::npos)break;start=end+1;}
  while(!data.empty()) {
   size_t n=std::min<size_t>(data.size(),Pending-pending.size());if(n<data.size()){auto newline=data.substr(0,n).rfind('\n');if(newline==std::string_view::npos){auto previous=pending.rfind('\n');Require(previous!=std::string::npos,"Append record exceeds64KiB");Drain(previous+1);continue;}n=newline+1;}
   if(pending.empty())pendingSince=std::chrono::steady_clock::now();pending.append(data.data(),n);data.remove_prefix(n);if(pending.size()==Pending||!data.empty())Flush();
  }
  MaybeFlush();
 }
};
}
