#pragma once
#include "Processing.hpp"
#include <memory>
#include <numeric>
namespace diliyan {
// Energy strata are relative within each recording, not semantic musical labels.
// Six-second non-overlapping windows keep RAM independent of recording length.
struct Section {
 double start=0,seconds=0,rms=-150,crest=0,correlation=1,width=0,midPresence=0,attack=0;
 int stratum=0;std::array<double,13> bands{};
};
inline std::vector<Section> sections(const fs::path&path){
 WavReader r(path);std::array<Frame,4096> buf{};std::vector<Section> result;
 auto spectrum=std::make_unique<Spectrum>();Biquad ml=Biquad::lowpass(1500),mh=Biquad::lowpass(5000);
 uint64_t n=0,total=0;double power=0,peak=0,ll=0,rr=0,lr=0,side=0,mp=0,fast=0,slow=0,attacks=0;size_t count;
 auto finish=[&](){if(n<rate)return;Section s;s.start=(total-n)/double(rate);s.seconds=n/double(rate);s.rms=10*std::log10(std::max(power/(2*n),1e-30));s.crest=db(peak)-s.rms;s.bands=spectrum->fractions();s.correlation=lr/std::max(std::sqrt(ll*rr),1e-30);s.width=std::sqrt(side/std::max(power*.5,1e-30));s.midPresence=mp/std::max(power*.5,1e-30);s.attack=attacks/n;result.push_back(s);};
 while((count=r.read(buf.data(),buf.size())))for(size_t i=0;i<count;++i){auto f=buf[i];spectrum->add(f);double e=f.l*f.l+f.r*f.r;power+=e;peak=std::max({peak,std::abs(f.l),std::abs(f.r)});ll+=f.l*f.l;rr+=f.r*f.r;lr+=f.l*f.r;side+=std::pow((f.l-f.r)*.5,2);double mid=(f.l+f.r)*.5;mp+=std::pow(mh.tick(mid)-ml.tick(mid),2);
  double c=std::exp(-1./(.005*rate)),d=std::exp(-1./(.080*rate));fast=c*fast+(1-c)*e;slow=d*slow+(1-d)*e;attacks+=std::clamp(10*std::log10(std::max(fast,1e-30)/std::max(slow,1e-30)),0.,12.);++n;++total;
  if(n==6*rate){finish();n=0;power=peak=ll=rr=lr=side=mp=fast=slow=attacks=0;spectrum=std::make_unique<Spectrum>();ml=Biquad::lowpass(1500);mh=Biquad::lowpass(5000);}}
 finish();std::vector<double> energies;for(auto s:result)if(s.rms> -55)energies.push_back(s.rms);
 std::sort(energies.begin(),energies.end());if(!energies.empty())for(auto &s:result){if(s.rms<=-55)s.stratum=-1;else{double rank=std::lower_bound(energies.begin(),energies.end(),s.rms)-energies.begin();s.stratum=std::min(2,int(3*rank/energies.size()));}}
 return result;
}
struct SectionMatch {
 std::array<double,13> delta{};double crestDelta=0,attackDelta=0,midPresenceDelta=0,confidence=0;
 int pairs=0;std::string method;
};
inline SectionMatch matchSections(const std::vector<Section>&own,const std::vector<Section>&ref,const Metrics&m,const Metrics&r){
 SectionMatch out;std::array<double,13> acc{};double weight=0;
 // Duration-weighted means within relative energy strata; every stratum equally
 // weighted, so a long quiet intro cannot dominate an energetic passage.
 for(int group=0;group<3;++group){std::array<double,13>a{},b{};double na=0,nb=0,ca=0,cb=0,aa=0,ab=0,ma=0,mb=0;int ac=0,bc=0;
  auto collect=[&](const std::vector<Section>&ss,std::array<double,13>&bands,double&n,double&c,double&attack,double&mid,int&cnt){for(auto s:ss)if(s.stratum==group&&s.rms> -55){for(int k=0;k<13;++k)bands[k]+=s.bands[k]*s.seconds;n+=s.seconds;c+=s.crest*s.seconds;attack+=s.attack*s.seconds;mid+=s.midPresence*s.seconds;++cnt;}};
  collect(own,a,na,ca,aa,ma,ac);collect(ref,b,nb,cb,ab,mb,bc);
  if(na>=6&&nb>=6){for(int k=0;k<13;++k)acc[k]+=std::clamp(ratioDB(b[k]/nb,a[k]/na),-6.,6.);out.crestDelta+=cb/nb-ca/na;out.attackDelta+=ab/nb-aa/na;out.midPresenceDelta+=std::clamp(ratioDB(mb/nb,ma/na),-6.,6.);weight+=1;out.pairs+=std::min(ac,bc);}}
 if(weight>=2&&out.pairs>=4){for(int k=0;k<13;++k)out.delta[k]=acc[k]/weight;out.crestDelta/=weight;out.attackDelta/=weight;out.midPresenceDelta/=weight;out.confidence=.55;out.method="6-s-Fenster, relative Energieklassen ruhig/mittel/hoch; keine Refrain- oder Instrumentenerkennung. Zuordnung heuristisch.";}
 else{for(int k=0;k<13;++k)out.delta[k]=std::clamp(ratioDB(r.bands[k],m.bands[k]),-6.,6.);out.crestDelta=r.crest-m.crest;out.confidence=.25;out.method="Zu wenige vergleichbare Energieklassen: konservativer globaler FFT-Fallback. Keine isolierte Referenz-Vocal messbar.";}
 return out;
}
struct MixPlan3 {
 std::array<double,3> vocalEQ{};std::array<double,4> beatEQ{};std::array<double,3> centers{800,2000,4000};
 double gainDB=0,ratio=1,threshold=-20,transientDB=0,bassDynamicDB=0;
 SectionMatch match;std::string flags;
 bool enabled(char c)const{return flags.find(c)!=std::string::npos;}
};
inline MixPlan3 mixPlan(const Metrics&v,const Metrics&b,const Metrics&m,const Metrics&r,const SectionMatch&sm,const std::string&flags){
 MixPlan3 p;p.flags=flags;p.match=sm;double scale=sm.confidence>=.5?.32:.18;
 auto gap=[&](int a,int z){double x=0;for(int k=a;k<=z;++k)x+=sm.delta[k];return x/(z-a+1);};
 // Known source occupancy allocates a mixed-reference tonal gap conservatively;
 // it does NOT assert the reference's isolated vocal or drum properties.
 double vp=std::accumulate(v.bands.begin()+7,v.bands.begin()+10,0.0)*gain(v.active*2);
 double bp=std::accumulate(b.bands.begin()+7,b.bands.begin()+10,0.0)*gain(b.active*2);
 double share=vp/std::max(vp+bp,1e-30);double presence=gap(7,9);
 p.gainDB=std::clamp(presence*.25*(1-share),-1.,1.);
 p.vocalEQ={std::clamp(gap(2,4)*scale*.65,-1.,1.),std::clamp(presence*scale,-1.2,1.2),std::clamp(gap(10,12)*scale*.5,-.7,.7)};
 p.beatEQ={std::clamp(gap(0,1)*scale,-1.5,1.),std::clamp(gap(3,5)*scale*.7,-1.,1.),std::clamp(gap(7,9)*scale*(.5+share*.5),-1.,1.),std::clamp(gap(10,12)*scale*.6,-.8,.8)};
 if(v.crest>r.crest+3&&v.crest>12)p.ratio=std::clamp(1+(v.crest-r.crest-3)*.04,1.,1.4);
 p.threshold=std::clamp(v.active+3,-36.,-8.);
 // Beat onset proxy is from the combined mix/reference, never a separated kick.
 if(sm.confidence>=.5&&sm.attackDelta>.08&&m.crest<r.crest+3)p.transientDB=std::clamp(sm.attackDelta*.6,0.,.8);
 if(sm.delta[0]<-1.5&&b.bands[0]>.15)p.bassDynamicDB=std::min(1.5,-sm.delta[0]*.3);
 // Three highest actual overlaps from seven candidate bands, spaced apart.
 constexpr std::array<double,7> hz{630,1000,1600,2500,3150,4000,5000};constexpr std::array<int,7> ix{5,6,7,8,8,9,10};
 std::vector<std::pair<double,double>> candidates;for(int k=0;k<7;++k){double score=std::min(v.bands[ix[k]],b.bands[ix[k]])*(1+std::max(0.,presence)*.04);candidates.push_back({score,hz[k]});}
 std::stable_sort(candidates.begin(),candidates.end(),[](auto a,auto b){return a.first>b.first;});std::vector<double> picked;
 for(auto c:candidates){bool spaced=true;for(auto h:picked)if(std::abs(std::log2(h/c.second))<.6)spaced=false;if(spaced)picked.push_back(c.second);if(picked.size()==3)break;}
 if(picked.size()==3){std::sort(picked.begin(),picked.end());std::copy(picked.begin(),picked.end(),p.centers.begin());}
 return p;
}
struct MixControl {double vocalGR=0,transient=0,bassGR=0;};
class StemProcessor3 {
 MixPlan3 p;std::array<StereoFilter,3> ve;
 std::array<StereoFilter,4> be;StereoFilter low{Biquad::lowpass(160)};
 StereoFilter bassEQ{Biquad{}};Biquad bassTarget{};double env=0,gr=0,fast=0,slow=0,bass=0,full=0,trans=0,bgr=0;
 uint64_t n=0;
 static void coefficients(Biquad&f,const Biquad&t){constexpr double a=.003;f.b0+=a*(t.b0-f.b0);f.b1+=a*(t.b1-f.b1);f.b2+=a*(t.b2-f.b2);f.a1+=a*(t.a1-f.a1);f.a2+=a*(t.a2-f.a2);}
public:
 MixControl control;double maxVocalGR=0,maxTransient=0,maxBassGR=0;
 explicit StemProcessor3(MixPlan3 plan):p(plan),ve{StereoFilter(Biquad::shelf(180,p.vocalEQ[0],false)),StereoFilter(Biquad::peak(2800,.8,p.vocalEQ[1])),StereoFilter(Biquad::shelf(9000,p.vocalEQ[2],true))},be{StereoFilter(Biquad::shelf(85,p.beatEQ[0],false)),StereoFilter(Biquad::peak(350,.7,p.beatEQ[1])),StereoFilter(Biquad::peak(2700,.8,p.beatEQ[2])),StereoFilter(Biquad::shelf(8500,p.beatEQ[3],true))}{}
 std::pair<Frame,Frame> tick(Frame vocal,Frame beat){
  if(p.enabled('v')){for(auto &f:ve)vocal=f.tick(vocal);vocal=vocal*gain(p.gainDB);}
  if(p.enabled('d')&&p.ratio>1){double e=.5*(vocal.l*vocal.l+vocal.r*vocal.r);double c=std::exp(-1./((e>env?.025:.12)*rate));env=c*env+(1-c)*e;double wanted=std::min(2.,std::max(0.,10*std::log10(std::max(env,1e-30))-p.threshold)*(1-1/p.ratio));gr+=(1-std::exp(-1./(.005*rate)))*(wanted-gr);vocal=vocal*gain(-gr);}
  if(p.enabled('b'))for(auto &f:be)beat=f.tick(beat);
  auto l=low.tick(beat);double lo=.5*(l.l*l.l+l.r*l.r),e=.5*(beat.l*beat.l+beat.r*beat.r);double cf=std::exp(-1./(.004*rate)),cs=std::exp(-1./(.08*rate));fast=cf*fast+(1-cf)*e;slow=cs*slow+(1-cs)*e;
  if(p.enabled('t')&&p.transientDB>0){double wanted=p.transientDB*std::clamp(10*std::log10(std::max(fast,1e-30)/std::max(slow,1e-30))/6.,0.,1.);trans+=(1-std::exp(-1./(.002*rate)))*(wanted-trans);beat=beat*gain(trans);}
  bass=cs*bass+(1-cs)*lo;full=cs*full+(1-cs)*e;
  if(p.enabled('l')&&p.bassDynamicDB>0){double wanted=p.bassDynamicDB*std::clamp((bass/std::max(full,1e-30)-.35)/.35,0.,1.);bgr+=(1-std::exp(-1./(.03*rate)))*(wanted-bgr);if(n%64==0)bassTarget=Biquad::shelf(100,-bgr,false);coefficients(bassEQ.l,bassTarget);coefficients(bassEQ.r,bassTarget);beat=bassEQ.tick(beat);}
  ++n;control={gr,trans,bgr};maxVocalGR=std::max(maxVocalGR,gr);maxTransient=std::max(maxTransient,trans);maxBassGR=std::max(maxBassGR,bgr);return {vocal,beat};
 }
};
}
