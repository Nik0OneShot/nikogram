#pragma once
// Rebuildable, process-local scratch files only. Never delete corpus files.
// All algorithms use bounded RAM independent of dataset length.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace movement::storage {
using Cancel=std::function<bool()>;
inline void Check(const Cancel& cancel){if(cancel&&cancel())throw std::runtime_error("Training cancelled; previous model retained");static thread_local auto yielded=std::chrono::steady_clock::now();auto now=std::chrono::steady_clock::now();if(now-yielded>=std::chrono::milliseconds(3)){std::this_thread::sleep_for(std::chrono::milliseconds(1));yielded=std::chrono::steady_clock::now();}}
inline void Fail(const std::filesystem::path&p,const char*what){throw std::runtime_error(std::string(what)+": "+p.string()+". Check free disk space and file access; original data retained.");}
struct Scratch {
 std::filesystem::path root; uint64_t serial=0;
 Scratch(){static std::atomic<uint64_t> ids=0;auto base=std::filesystem::temp_directory_path();for(int i=0;i<100;++i){root=base/("nikogram-corpus-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++ids));if(std::filesystem::create_directory(root))return;}throw std::runtime_error("Cannot create training scratch directory");}
 Scratch(const Scratch&)=delete;
 ~Scratch(){std::error_code ec;std::filesystem::remove_all(root,ec);}
 std::filesystem::path Next(){return root/(std::to_string(++serial)+".bin");}
};
template<class T> bool Read(std::istream&f,T&v,const std::filesystem::path&p){static_assert(std::is_trivially_copyable_v<T>);f.read(reinterpret_cast<char*>(&v),sizeof v);if(f.gcount()==sizeof v)return true;if(f.gcount()||!f.eof())Fail(p,"Scratch read failed");return false;}
template<class T> void Write(std::ostream&f,const T&v,const std::filesystem::path&p){static_assert(std::is_trivially_copyable_v<T>);if(!f.write(reinterpret_cast<const char*>(&v),sizeof v))Fail(p,"Scratch write failed");}
// A disk-backed forward range. Each iterator holds exactly one record.
template<class T> class Array {
 std::filesystem::path path_; uint64_t count_=0; std::ofstream output; Cancel cancel_;
public:
 explicit Array(Scratch&s,Cancel cancel={}):path_(s.Next()),output(path_,std::ios::binary),cancel_(std::move(cancel)){if(!output)Fail(path_,"Cannot create scratch file");}
 Array(const Array&)=delete;Array(Array&&)=default;
 const auto& path()const{return path_;} uint64_t size()const{return count_;}
 bool empty()const{return !count_;}
 void push_back(const T&v){if(count_>=uint64_t(INT64_MAX)/sizeof(T))throw std::overflow_error("Scratch offset exceeds signed 64-bit representation");Write(output,v,path_);++count_;}
 void Seal(){if(output.is_open()){if(!output.flush())Fail(path_,"Scratch flush failed");output.close();}}
 T operator[](uint64_t i)const{if(i>=count_)throw std::out_of_range("Scratch index");std::ifstream f(path_,std::ios::binary);f.seekg(std::streamoff(i*sizeof(T)));T v{};if(!Read(f,v,path_))Fail(path_,"Missing scratch record");return v;}
 T back()const{return (*this)[count_-1];}
 struct Sentinel{};
 struct Iterator {
  std::ifstream file;std::filesystem::path path;T value{};bool present=false;Cancel cancel;uint64_t ordinal=0;
  explicit Iterator(const std::filesystem::path&p,Cancel c):file(p,std::ios::binary),path(p),cancel(std::move(c)){if(!file)Fail(p,"Cannot open scratch file");++*this;}
  Iterator&operator++(){if(!(ordinal++%256)){Check(cancel);std::this_thread::yield();}present=Read(file,value,path);return *this;}
  const T&operator*()const{return value;}
  bool operator!=(Sentinel)const{return present;}
 };
 Iterator begin()const{return Iterator(path_,cancel_);} Sentinel end()const{return {};}
};
// Binary level compaction bounds run metadata to64 paths and merge memory to2 records.
template<class T,class Less=std::less<T>> class Sort {
 Scratch&scratch;Cancel cancel;Less less;std::vector<T> buffer;
 std::array<std::filesystem::path,64> levels{};
 static constexpr size_t Capacity=std::max(size_t(1),size_t(2*1024*1024)/sizeof(T));
 std::filesystem::path Merge(const std::filesystem::path&a,const std::filesystem::path&b){
  auto p=scratch.Next();std::ifstream x(a,std::ios::binary),y(b,std::ios::binary);std::ofstream out(p,std::ios::binary);if(!x||!y||!out)Fail(p,"Cannot open sort files");T u{},v{};bool au=Read(x,u,a),bv=Read(y,v,b);uint64_t n=0;
  while(au||bv){if(!(++n%256)){Check(cancel);std::this_thread::yield();}if(au&&(!bv||!less(v,u))){Write(out,u,p);au=Read(x,u,a);}else{Write(out,v,p);bv=Read(y,v,b);}}
  if(!out.flush())Fail(p,"Sort flush failed");x.close();y.close();out.close();std::filesystem::remove(a);std::filesystem::remove(b);return p;
 }
 void Flush(){if(buffer.empty())return;Check(cancel);std::sort(buffer.begin(),buffer.end(),less);auto p=scratch.Next();{std::ofstream out(p,std::ios::binary);for(const auto&v:buffer)Write(out,v,p);if(!out.flush())Fail(p,"Sort flush failed");}buffer.clear();for(auto&level:levels){if(level.empty()){level=p;return;}p=Merge(level,p);level.clear();}throw std::overflow_error("Dataset record count exceeds uint64 storage representation");}
public:
 Sort(Scratch&s,Cancel c={},Less l={}):scratch(s),cancel(std::move(c)),less(l){buffer.reserve(Capacity);}
 void Add(const T&v){buffer.push_back(v);if(buffer.size()==Capacity)Flush();}
 template<class F>void Each(F fn){Flush();std::filesystem::path p;for(auto&level:levels)if(!level.empty()){p=p.empty()?level:Merge(p,level);level.clear();}if(p.empty())return;std::ifstream in(p,std::ios::binary);T v{};uint64_t n=0;while(Read(in,v,p)){if(!(++n%256))Check(cancel);fn(v);}in.close();std::filesystem::remove(p);}
};
struct Groups {
 Array<uint64_t> values;
 explicit Groups(Scratch&s,Sort<uint64_t>&sort):values(s){uint64_t prior=0;sort.Each([&](uint64_t g){if(g!=prior){values.push_back(g);prior=g;}});values.Seal();}
 uint64_t size()const{return values.size();}uint64_t operator[](uint64_t n)const{return values[n];}uint64_t back()const{return values.back();}
 uint64_t Upper(uint64_t x)const{uint64_t lo=0,hi=size();while(lo<hi){auto mid=lo+(hi-lo)/2;if(values[mid]<=x)lo=mid+1;else hi=mid;}return lo;}
 uint64_t Rank(uint64_t x)const{uint64_t hi=Upper(x);if(!hi||values[hi-1]!=x)throw std::runtime_error("Missing dataset group");return hi-1;}
};
// Capture stream+byte watermark on the IO owner thread after its completed append.
// Copy on the trainer thread. Subsequent appends cannot enter this training snapshot.
struct Snapshot {
 std::filesystem::file_time_type modified{};
 std::filesystem::path source;std::shared_ptr<std::ifstream> input;uint64_t bytes=0;
 explicit Snapshot(const std::filesystem::path&p):source(p){if(!std::filesystem::exists(p))return;input=std::make_shared<std::ifstream>(p,std::ios::binary);if(!*input)Fail(p,"Cannot open dataset snapshot");bytes=std::filesystem::file_size(p);modified=std::filesystem::last_write_time(p);}
 void Copy(const std::filesystem::path&dest,const Cancel&cancel)const{std::ofstream out(dest,std::ios::binary);if(!out)Fail(dest,"Cannot create dataset snapshot");std::array<char,65536>buf{};uint64_t left=bytes;while(left){Check(cancel);auto n=std::streamsize(std::min(left,uint64_t(buf.size())));if(!input->read(buf.data(),n))Fail(source,"Dataset snapshot read failed");if(!out.write(buf.data(),n))Fail(dest,"Dataset snapshot write failed");left-=uint64_t(n);std::this_thread::yield();}if(!out.flush())Fail(dest,"Dataset snapshot flush failed");}
};
}
