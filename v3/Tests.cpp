#include "ReferenceMix.hpp"
#include <iostream>
using namespace diliyan;
void check(bool ok,const char *s){require(ok,s);std::cout<<"PASS "<<s<<'\n';}
int main(int argc,char**argv){try{require(argc==2,"folder missing");fs::create_directories(argv[1]);
 Metrics v,b,m,r;v.active=-24;b.active=-22;v.crest=18;r.crest=9;m.crest=10;v.bands.fill(.01);b.bands.fill(.01);m.bands.fill(1./13);r.bands=m.bands;b.bands[0]=.4;
 SectionMatch sm;sm.confidence=.55;sm.delta.fill(3);sm.delta[0]=-4;sm.attackDelta=.5;
 auto p=mixPlan(v,b,m,r,sm,"vdbult");check(p.vocalEQ[1]>0&&p.beatEQ[0]<0,"Reference deficit generates bounded, different source EQ decisions");check(p.ratio>1&&p.transientDB>0&&p.bassDynamicDB>0,"Vocal density, attack and bass proposals depend on measured features");
 auto none=mixPlan(v,b,m,r,sm,"-");StemProcessor3 bypass(none);double error=0;for(int i=0;i<rate;++i){double x=.1*std::sin(2*pi*700*i/rate);auto y=bypass.tick({x,x},{x*.5,-x*.5});error=std::max(error,std::abs(y.first.l-x)+std::abs(y.second.l-x*.5));}check(error==0,"All rejected decisions give exact stem bypass");
 sm.delta.fill(-3);auto darker=mixPlan(v,b,m,r,sm,"vdbult");check(darker.vocalEQ[1]<p.vocalEQ[1]&&darker.beatEQ[3]<p.beatEQ[3],"Changing the reference reverses tonal recommendations before mastering");
 StemProcessor3 processor(p);for(int i=0;i<rate*3;++i){double t=i/double(rate),a=.6*std::sin(2*pi*1500*t),q=.4*std::sin(2*pi*60*t);auto y=processor.tick({a,a},{q,q});require(std::isfinite(y.first.l)&&std::isfinite(y.second.r),"Nonfinite stem output");require(y.first.l==y.first.r&&y.second.l==y.second.r,"Stereo-linked processing broke mono");}
 check(processor.maxVocalGR>0&&processor.maxVocalGR<=2,"Actual vocal compression follows signal and respects 2 dB cap");check(processor.maxBassGR>0&&processor.maxBassGR<=1.5,"Actual dynamic bass correction respects 1.5 dB cap");
 Unmasker dynamic(-24,1,{630,1600,4000});for(int i=0;i<rate*3;++i){double t=i/double(rate);dynamic.tick({.08*std::sin(2*pi*1600*t),.08*std::sin(2*pi*1600*t)},{.15*std::sin(2*pi*1600*t),.15*std::sin(2*pi*1600*t)});}dynamic.finish();check(dynamic.stats.maximum[1]>.5,"Conflict-selected frequency actually controls dynamic unmasking");
 auto path=fs::path(argv[1])/"sections.wav";WavWriter w(path);std::array<Frame,4096> buf{};uint64_t pos=0;while(pos<rate*24){size_t n=std::min<uint64_t>(buf.size(),rate*24-pos);for(size_t i=0;i<n;++i){double amplitude=.02*(1+((pos+i)/(rate*6)));double x=amplitude*std::sin(2*pi*1000*(pos+i)/rate);buf[i]={x,x};}w.write(buf.data(),n);pos+=n;}w.close();auto ss=sections(path);auto match=matchSections(ss,ss,m,r);
 check(ss.size()==4&&match.confidence>=.5&&match.pairs==4,"Energy-strata matching works on measurable section changes");check(std::all_of(match.delta.begin(),match.delta.end(),[](double x){return std::abs(x)<1e-12;}),"Identical sections do not invent spectral differences");
 auto fallback=matchSections({},ss,m,r);check(fallback.confidence<.5&&fallback.method.find("Fallback")!=std::string::npos,"Insufficient comparable sections explicitly triggers global fallback");
 std::cout<<"V3 behavioral tests passed; no professional-quality claim.\n";
 }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
