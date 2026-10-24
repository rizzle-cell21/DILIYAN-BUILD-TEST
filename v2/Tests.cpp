#include "Processing.hpp"
#include "PlayerBridge.h"
#include <iostream>
#include <thread>
using namespace diliyan;
void check(bool x,const char*message){require(x,message);std::cout<<"PASS "<<message<<'\n';}
int main(int argc,char**argv){try{
 require(argc==2,"Output folder missing");fs::path dir=argv[1];fs::create_directories(dir);
 Unmasker masker(-22,1),disabled(-22,0);double silentDelta=0,offDelta=0,after=0,on=0;
 for(int i=0;i<rate*5;++i){double t=i/double(rate);Frame v=t>=1&&t<2?Frame{.08*std::sin(2*pi*2000*t),.08*std::sin(2*pi*2000*t)}:Frame{};Frame b{.12*std::sin(2*pi*2000*t),.12*std::sin(2*pi*2000*t)};
  auto y=masker.tick(v,b),d=disabled.tick(v,b);offDelta=std::max(offDelta,std::abs(d.l-b.l));if(t<.8)silentDelta=std::max(silentDelta,std::abs(y.l-b.l));}
 masker.finish();disabled.finish();
 for(auto p:masker.stats.trace){if(p.time>1.4&&p.time<1.9)on=std::max(on,p.reduction[1]);if(p.time>3.4)after=std::max(after,p.reduction[1]);}
 check(on>.5&&on<=2.5,"Dynamic unmasking engages on competing vocal band");
 check(silentDelta<1e-12,"No beat change before vocal activity");check(after<.01,"Unmasking releases during vocal pause");check(offDelta<1e-12,"Strength zero gives exact bypass");
 Unmasker noOverlap(-22,1);for(int i=0;i<rate*3;++i){double t=i/double(rate);noOverlap.tick({.15*std::sin(2*pi*2000*t),.15*std::sin(2*pi*2000*t)},{.15*std::sin(2*pi*60*t),.15*std::sin(2*pi*60*t)});}noOverlap.finish();
 check(*std::max_element(noOverlap.stats.maximum.begin(),noOverlap.stats.maximum.end())<.05,"No unnecessary ducking of non-overlapping bass");
 auto source=dir/"source.wav";WavWriter w(source);std::vector<Frame> tone(rate*2);for(size_t i=0;i<tone.size();++i){double t=i/double(rate);tone[i]={.1*std::sin(2*pi*1000*t),.1*std::sin(2*pi*1000*t)};}tone.front()={.05,.05};tone.back()={.03,.03};w.write(tone.data(),tone.size());w.close();
 auto stats=oversampledLimit(source,dir/"limited.wav",1);auto m=measure(dir/"limited.wav");check(m.frames==tone.size(),"8x limiter preserves exact frame count");check(m.tp< -10&&stats.maximum<.01,"8x limiter does not reduce quiet signal");
 WavReader rr(dir/"limited.wav");std::vector<Frame> roundtrip(tone.size());rr.read(roundtrip.data(),roundtrip.size());double signal=0,error=0;for(size_t i=rate/10;i<tone.size()-rate/10;++i){signal+=tone[i].l*tone[i].l;error+=std::pow(roundtrip[i].l-tone[i].l,2);}check(10*std::log10(error/signal)< -100,"8x roundtrip is aligned and transparent in steady passband");
 Meter meter;for(auto x:tone)meter.add(x);auto measured=meter.finish();check(std::abs(measured.lufs+20)<.15,"K-weighted LUFS sine baseline");
 WavWriter ww(dir/"ramp.wav");std::vector<Frame> ramp(rate*3);for(size_t i=0;i<ramp.size();++i)ramp[i]={.01+double(i)/ramp.size()*.1,-.01-double(i)/ramp.size()*.1};ww.write(ramp.data(),ramp.size());ww.close();
 std::string path=(dir/"ramp.wav").string();const char*paths[]{path.c_str(),path.c_str()};double matched[]{2,.5},original[]{1,1};void*p=ds_create(paths,matched,original,2);require(p!=nullptr,ds_error());
 ds_select(p,0);ds_play(p,1);std::this_thread::sleep_for(std::chrono::milliseconds(20));std::array<float,512> l{},r{};double maxStep=0,previous=0;int64_t lastPosition=0;
 for(int block=0;block<80;++block){ds_render(p,l.data(),r.data(),512);for(size_t i=0;i<512;++i){maxStep=std::max(maxStep,std::abs(l[i]-previous));previous=l[i];}
  if(block==25)ds_select(p,1);
  if(block==40)ds_mode(p,1);
  if(block==50)ds_mono(p,1);
  auto now=int64_t(ds_position(p)*rate);require(now>=lastPosition,"Player position regressed on switch");lastPosition=now;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 check(maxStep<.001,"Per-sample comparison fades avoid gain steps, including gain above unity");check(ds_position(p)>.5,"Streaming player advances shared position");
 ds_seek(p,.2);for(int i=0;i<16;++i){ds_render(p,l.data(),r.data(),512);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 check(ds_position(p)>.2&&ds_position(p)<.5,"Seek resets all comparison streams together");
 ds_play(p,0);for(int i=0;i<8;++i)ds_render(p,l.data(),r.data(),512);double stopped=ds_position(p);ds_render(p,l.data(),r.data(),512);check(ds_position(p)==stopped,"Pause holds exact rendered sample position");
 ds_select(p,0);ds_mode(p,1);ds_mono(p,0);ds_play(p,1);
 for(int i=0;i<12;++i){ds_render(p,l.data(),r.data(),512);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 double expected=.01+(ds_position(p)*rate-1)/ramp.size()*.1;
 check(std::abs(l.back()-expected)<1e-7,"Original Level uses actual file amplitude without individual matching");
 ds_mode(p,0);for(int i=0;i<8;++i){ds_render(p,l.data(),r.data(),512);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 expected=2*(.01+(ds_position(p)*rate-1)/ramp.size()*.1);
 check(std::abs(l.back()-expected)<1e-7,"Matched mode applies accurate gain above unity in audio samples");
 MasterPlan2 widthPlan;widthPlan.sideEQ={.6,-.5,.4};MasterProcessor widthDSP(widthPlan);double monoError=0;
 for(int i=0;i<rate;++i){double v=.1*std::sin(2*pi*900*i/rate);auto y=widthDSP.tick({v,v});monoError=std::max(monoError,std::abs(y.l-v));}
 check(monoError<1e-12,"Side-only reference adjustment preserves mono input");
 ds_destroy(p);std::cout<<"Technical V2 tests completed; musical quality is not certified.\n";
 }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
