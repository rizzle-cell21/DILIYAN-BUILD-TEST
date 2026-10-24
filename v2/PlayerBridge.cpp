#include "PlayerBridge.h"
#include "Audio.hpp"
#include <atomic>
#include <thread>
#include <memory>
#include <chrono>
#include <mutex>
using namespace diliyan;
namespace {
constexpr int maxStreams=8,chunkFrames=4096,slots=8;
struct Chunk {std::vector<Frame> data;uint64_t epoch=0,first=0;int frames=0;};
struct Player {
 std::vector<std::unique_ptr<WavReader>> readers;
 std::array<double,maxStreams> matched{},original{},weight{},from{},target{};
 std::array<uint64_t,maxStreams> lengths{};
 std::array<Chunk,slots> chunks;
 std::atomic<uint64_t> write{0},read{0},epoch{0},seekSerial{0},position{0};
 std::atomic<uint64_t> seekFrame{0};
 std::atomic<int> selection{0},mode{0};
 std::atomic<bool> playing{false},mono{false},quit{false};
 std::atomic<int64_t> underruns{0};
 int count,offset=0,fadeRemaining=0,lastSelection=-1,lastMode=-1,transportRemaining=0;
 double transport=0,transportStep=0,transportTarget=0,monoValue=0;
 uint64_t seenSeek=0,pendingFrame=0;
 bool seeking=false,wasUnder=false,waiting=true;
 std::thread worker;
 Player(const char* const*paths,const double*m,const double*o,int n):count(n){
  require(n>0&&n<=maxStreams,"Invalid player stream count");
  for(int i=0;i<n;++i){readers.push_back(std::make_unique<WavReader>(paths[i]));require(readers.back()->channels==2,"Player requires stereo");matched[i]=m[i];original[i]=o[i];lengths[i]=readers.back()->frames;require(std::isfinite(m[i])&&std::isfinite(o[i])&&m[i]>=0&&o[i]>=0,"Invalid monitor gain");}
  for(auto &c:chunks)c.data.resize(size_t(n)*chunkFrames);
  worker=std::thread([this]{produce();});
 }
 ~Player(){quit.store(true);if(worker.joinable())worker.join();}
 void produce(){try{uint64_t currentEpoch=0,nextFrame=0;for(;;){
   if(quit.load())return;
   auto e=epoch.load(std::memory_order_acquire);
   if(e!=currentEpoch){currentEpoch=e;nextFrame=seekFrame.load(std::memory_order_acquire);for(auto&r:readers)r->seek(nextFrame);}
   uint64_t w=write.load(std::memory_order_relaxed),r=read.load(std::memory_order_acquire);
   if(w-r>=slots){std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
   auto &c=chunks[w%slots];c.epoch=currentEpoch;c.first=nextFrame;c.frames=chunkFrames;
   for(int i=0;i<count;++i){Frame*dst=c.data.data()+i*chunkFrames;size_t n=readers[i]->read(dst,chunkFrames);for(size_t j=n;j<chunkFrames;++j)dst[j]={};}
   nextFrame+=chunkFrames;write.store(w+1,std::memory_order_release);
  }}catch(...){underruns.fetch_add(1);quit.store(true);}}
 void rampTransport(double destination){if(destination!=transportTarget){transportTarget=destination;transportRemaining=1440;transportStep=(destination-transport)/1440;}}
 void render(float*l,float*r,int n){
  bool under=false;
  for(int frame=0;frame<n;++frame){l[frame]=r[frame]=0;
   int sel=std::clamp(selection.load(std::memory_order_relaxed),0,count-1),md=mode.load(std::memory_order_relaxed);
   if(sel!=lastSelection||md!=lastMode){from=weight;for(int i=0;i<count;++i)target[i]=i==sel?(md?original[i]:matched[i]):0;fadeRemaining=2880;lastSelection=sel;lastMode=md;}
   if(fadeRemaining>0){double a=1-double(fadeRemaining-1)/2880;for(int i=0;i<count;++i)weight[i]=from[i]+a*(target[i]-from[i]);--fadeRemaining;}
   uint64_t serial=seekSerial.load(std::memory_order_acquire);
   if(serial!=seenSeek){seenSeek=serial;pendingFrame=seekFrame.load(std::memory_order_acquire);seeking=true;}
   if(seeking&&transport==0){position.store(pendingFrame);epoch.fetch_add(1,std::memory_order_acq_rel);offset=0;seeking=false;waiting=true;}
   auto e=epoch.load(std::memory_order_acquire);auto rd=read.load(std::memory_order_relaxed);auto wr=write.load(std::memory_order_acquire);
   while(rd<wr&&chunks[rd%slots].epoch!=e){++rd;read.store(rd,std::memory_order_release);offset=0;}
   bool available=rd<wr;
   bool advance=playing.load(std::memory_order_relaxed)&&position.load(std::memory_order_relaxed)<lengths[sel];
   rampTransport(!seeking&&advance&&available?1:0);
   if(transportRemaining>0){transport+=transportStep;if(--transportRemaining==0)transport=transportTarget;}
   if(!available){if(advance&&!waiting)under=true;continue;}
   waiting=false;
   if(transport==0&&!advance)continue;
   if(transport==0&&seeking)continue;
   auto &c=chunks[rd%slots];Frame output{};
   for(int i=0;i<count;++i)output=output+c.data[size_t(i)*chunkFrames+offset]*weight[i];
   double monoTarget=mono.load(std::memory_order_relaxed)?1:0;monoValue+=std::clamp(monoTarget-monoValue,-1./2880,1./2880);
   double mid=(output.l+output.r)*.5;output.l+=(mid-output.l)*monoValue;output.r+=(mid-output.r)*monoValue;
   l[frame]=float(output.l*transport);r[frame]=float(output.r*transport);
   position.store(c.first+uint64_t(offset)+1,std::memory_order_relaxed);
   if(++offset>=c.frames){offset=0;read.store(rd+1,std::memory_order_release);}
  }
  if(under&&!wasUnder)underruns.fetch_add(1);
  wasUnder=under;
 }
};
thread_local std::string lastError;
}
extern "C" {
void*ds_create(const char* const*p,const double*m,const double*o,int n){try{lastError.clear();return new Player(p,m,o,n);}catch(const std::exception&e){lastError=e.what();return nullptr;}}
void ds_destroy(void*h){delete static_cast<Player*>(h);}
void ds_render(void*h,float*l,float*r,int n){if(h)static_cast<Player*>(h)->render(l,r,n);else{std::fill(l,l+n,0);std::fill(r,r+n,0);}}
void ds_select(void*h,int i){if(h)static_cast<Player*>(h)->selection.store(i);}
void ds_play(void*h,int p){if(h)static_cast<Player*>(h)->playing.store(p!=0);}
void ds_seek(void*h,double s){if(!h||!std::isfinite(s))return;auto*p=static_cast<Player*>(h);p->seekFrame.store(uint64_t(std::max(0.,s)*rate),std::memory_order_release);p->seekSerial.fetch_add(1,std::memory_order_acq_rel);}
void ds_mode(void*h,int m){if(h)static_cast<Player*>(h)->mode.store(m);}
void ds_mono(void*h,int m){if(h)static_cast<Player*>(h)->mono.store(m!=0);}
double ds_position(void*h){return h?static_cast<Player*>(h)->position.load()/double(rate):0;}
double ds_duration(void*h,int i){if(!h)return 0;auto*p=static_cast<Player*>(h);return i>=0&&i<p->count?p->lengths[i]/double(rate):0;}
int64_t ds_underruns(void*h){return h?static_cast<Player*>(h)->underruns.load():0;}
const char*ds_error(){return lastError.c_str();}
}
