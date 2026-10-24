#pragma once
#include "Audio.hpp"
namespace diliyan {
struct UnmaskPoint {double time;std::array<double,3> reduction;};
struct UnmaskStats {std::array<double,3> maximum{},mean{};uint64_t frames=0,activeFrames=0;std::vector<UnmaskPoint> trace;};
class Unmasker {
    static Biquad band(double hz){double w=2*pi*hz/rate,a=std::sin(w)/(2*.8),c=std::cos(w),d=1+a;return {a/d,0,-a/d,-2*c/d,(1-a)/d};}
    std::array<StereoFilter,3> vf{StereoFilter(band(800)),StereoFilter(band(2000)),StereoFilter(band(4000))};
    std::array<StereoFilter,3> bf=vf;
    std::array<StereoFilter,3> eq{StereoFilter(Biquad{}),StereoFilter(Biquad{}),StereoFilter(Biquad{})};
    std::array<double,3> ve{},be{},gr{};
    double vfull=0,bfull=0,threshold,strength;
    uint64_t n=0;
    static double energy(Frame x){return .5*(x.l*x.l+x.r*x.r);}
    static double follow(double old,double value){double c=std::exp(-1./((value>old?.008:.08)*rate));return c*old+(1-c)*value;}
    static void slew(Biquad &current,const Biquad &target){constexpr double c=.004;current.b0+=c*(target.b0-current.b0);current.b1+=c*(target.b1-current.b1);current.b2+=c*(target.b2-current.b2);current.a1+=c*(target.a1-current.a1);current.a2+=c*(target.a2-current.a2);}
    std::array<Biquad,3> target{};
public:
    UnmaskStats stats;
    Unmasker(double activeVocalDB,double amount):threshold(std::max(-58.0,activeVocalDB-24)),strength(std::clamp(amount,0.0,1.0)){}
    Frame tick(Frame v,Frame beat) {
        vfull=follow(vfull,energy(v));bfull=follow(bfull,energy(beat));
        double activity=std::clamp((10*std::log10(std::max(vfull,1e-30))-threshold)/8,0.0,1.0);
        bool active=false;
        for(int k=0;k<3;++k){ve[k]=follow(ve[k],energy(vf[k].tick(v)));be[k]=follow(be[k],energy(bf[k].tick(beat)));
            double vr=10*std::log10(std::max(ve[k],1e-30)/std::max(vfull,1e-30));
            double br=10*std::log10(std::max(be[k],1e-30)/std::max(bfull,1e-30));
            double overlap=std::clamp((vr+20)/10,0.0,1.0)*std::clamp((br+20)/10,0.0,1.0);
            double competition=std::clamp((10*std::log10(std::max(be[k],1e-30)/std::max(ve[k],1e-30))+4)/10,0.0,1.0);
            double wanted=2.5*strength*activity*overlap*competition*(be[k]>1e-8?1:0);
            double c=std::exp(-1./((wanted>gr[k]?.02:.18)*rate));gr[k]=c*gr[k]+(1-c)*wanted;
            stats.maximum[k]=std::max(stats.maximum[k],gr[k]);stats.mean[k]+=gr[k];active|=gr[k]>.1;
            if(n%64==0)target[k]=Biquad::peak(k==0?800:k==1?2000:4000,.85,-gr[k]);
            slew(eq[k].l,target[k]);slew(eq[k].r,target[k]);
            beat=eq[k].tick(beat);
        }
        if(n%4800==0)stats.trace.push_back({n/double(rate),gr});
        ++n;++stats.frames;if(active)++stats.activeFrames;
        return beat;
    }
    void finish(){for(auto &v:stats.mean)v/=std::max<uint64_t>(1,stats.frames);}
};
struct MasterPlan2 {
    std::array<double,8> eq{};
    std::array<double,3> sideEQ{};
    double ratio=1,threshold=-15,attack=.03,release=.12,target=-14,budget=6;
    std::string reason;
};
inline constexpr std::array<double,8> masterFrequencies{70,160,350,800,1800,3200,6500,11000};
inline MasterPlan2 plan2(const Metrics&m,const Metrics&r,int profile) {
    MasterPlan2 p;p.target=r.lufs-(profile==1?.2:profile==2?.5:0);p.budget=profile==2?4.5:profile==1?5.5:6;
    for(size_t k=0;k<p.eq.size();++k){double hz=masterFrequencies[k];int j=0;while(j<12&&hz>Spectrum::edges[j+1])++j;
        double delta=0;double weights=0;
        for(int b=std::max(0,j-1);b<=std::min(12,j+1);++b){double w=b==j?2:1;delta+=w*std::clamp(ratioDB(r.bands[b],m.bands[b]),-6.0,6.0);weights+=w;}
        p.eq[k]=m.spectralWindows>0&&r.spectralWindows>0?std::clamp(delta/weights*.28,-1.6,1.6):0;
    }
    if(profile==1){p.eq[1]=std::clamp(p.eq[1]+.6*(1-std::clamp(m.bass/.6,0.0,1.0)),-1.6,1.6);p.eq[2]=std::clamp(p.eq[2]+.25,-1.6,1.6);p.eq[6]=std::clamp(p.eq[6]-.3*std::clamp(m.air/.1,0.0,1.0),-1.6,1.6);p.attack=.025;p.release=.15;}
    if(profile==2){p.eq[0]=std::clamp(p.eq[0]-.35*std::clamp(m.bass/.5,0.0,1.0),-1.6,1.6);p.eq[4]=std::clamp(p.eq[4]+.45*(1-std::clamp(m.presence/.4,0.0,1.0)),-1.6,1.6);p.attack=.045;p.release=.10;}
    if(m.crest>r.crest+1.5 && m.crest>10){p.ratio=std::clamp(1.15+(m.crest-r.crest)*.035,1.15,1.6);if(profile==2)p.ratio=1+(p.ratio-1)*.5;}
    p.threshold=std::clamp(m.active+5,-28.0,-8.0);
    for(int k=0;k<3;++k){int b=k==0?5:k==1?8:11;double delta=std::clamp(20*std::log10(std::max(r.sideBands[b],.01)/std::max(m.sideBands[b],.01))*.10,-.6,.6);if(m.correlation<.2)delta=std::min(0.,delta);p.sideEQ[k]=m.spectralWindows>0&&r.spectralWindows>0?delta:0;}
    p.reason="13 aktive FFT-Energiebaender; acht begrenzte EQ-Zonen; Kompression aus Crest-Abstand; Referenz-LUFS ohne -12-LUFS-Grenze. Keine automatische Refrain-Zuordnung.";
    return p;
}
class MasterProcessor {
    std::vector<StereoFilter> filters;
    MasterPlan2 p;double env=0,gr=0;
    Biquad sideLow=Biquad::lowpass(180);
    std::array<Biquad,3> sideFilters;
public:
    double maximumReduction=0;
    explicit MasterProcessor(MasterPlan2 v):p(v){sideFilters={Biquad::peak(600,.7,p.sideEQ[0]),Biquad::peak(2500,.7,p.sideEQ[1]),Biquad::peak(9000,.7,p.sideEQ[2])};for(int k=0;k<8;++k)filters.emplace_back(k==0?Biquad::shelf(masterFrequencies[k],p.eq[k],false):k==7?Biquad::shelf(masterFrequencies[k],p.eq[k],true):Biquad::peak(masterFrequencies[k],.75,p.eq[k]));}
    Frame tick(Frame f){for(auto &filter:filters)f=filter.tick(f);
        if(p.ratio>1){double power=.5*(f.l*f.l+f.r*f.r);double c=std::exp(-1./((power>env?p.attack:p.release)*rate));env=c*env+(1-c)*power;
            double over=std::max(0.0,10*std::log10(std::max(env,1e-30))-p.threshold);double target=std::min(3.0,over*(1-1/p.ratio));
            gr+=(1-std::exp(-1./(.005*rate)))*(target-gr);maximumReduction=std::max(maximumReduction,gr);f=f*gain(-gr);}
        double side=(f.l-f.r)*.5,high=side-sideLow.tick(side),processed=high;
        for(auto &filter:sideFilters)processed=filter.tick(processed);
        double change=processed-high;f.l+=change;f.r-=change;
        return f;}
};
struct LimitStats {double maximum=0,sum=0;uint64_t samples=0,over3=0;double mean()const{return sum/std::max<uint64_t>(1,samples);}double busy()const{return over3/double(std::max<uint64_t>(1,samples));}};
// Linked 8x limiter with actual sample envelopes. Final downsampled PCM is
// independently reconstructed and trimmed; oversampling alone is not a guarantee.
class HighRateLimiter {
    static constexpr int look=1920;
    std::deque<Frame> audio;
    std::deque<std::pair<uint64_t,double>> peaks;
    uint64_t index=0;double env=1;
public:
    LimitStats stats;
    bool tick(Frame x,Frame &out){double p=std::max(std::abs(x.l),std::abs(x.r));while(!peaks.empty()&&peaks.back().second<=p)peaks.pop_back();peaks.emplace_back(index,p);audio.push_back(x);
        bool ready=index>=look;
        if(ready){uint64_t begin=index-look;while(peaks.front().first<begin)peaks.pop_front();double target=std::min(1.,gain(-1.3)/std::max(peaks.front().second,1e-30));
            double c=std::exp(-1./((target<env?.0006:.10)*384000));env=c*env+(1-c)*target;
            out=audio.front();audio.pop_front();double guard=std::min(1.,gain(-1.3)/std::max({std::abs(out.l),std::abs(out.r),1e-30}));double g=std::min(env,guard);out=out*g;
            double reduction=-db(g);stats.maximum=std::max(stats.maximum,reduction);stats.sum+=reduction;++stats.samples;if(reduction>3)++stats.over3;
        }++index;return ready;}
    static constexpr int latency(){return look;}
};
inline LimitStats oversampledLimit(const fs::path&in,const fs::path&out,double level){
    WavReader reader(in);WavWriter writer(out);HighRateLimiter limiter;
    r8b::CDSPResampler24 ul(rate,rate*8,4096,.5),ur(rate,rate*8,4096,.5),dl(rate*8,rate,65536,.5),dr(rate*8,rate,65536,.5);
    std::array<Frame,4096> input{},buffer{};std::array<double,4096> l{},r{};std::array<double,65536> hl{},hr{};
    uint64_t upCount=0,limitedCount=0,written=0;size_t used=0;
    auto down=[&](int n){if(!n)return;double *a=nullptr,*b=nullptr;int na=dl.process(hl.data(),n,a),nb=dr.process(hr.data(),n,b);require(na==nb,"Limiter downsample lengths");
        for(int i=0;i<na&&written<reader.frames;++i){buffer[used++]={a[i],b[i]};++written;if(used==buffer.size()){writer.write(buffer.data(),used);used=0;}}};
    auto high=[&](Frame f){Frame result;if(limiter.tick(f,result)&&limitedCount<reader.frames*8){hl[limitedCount%65536]=result.l;hr[limitedCount%65536]=result.r;++limitedCount;if(limitedCount%65536==0)down(65536);}};
    auto up=[&](int n){double *a=nullptr,*b=nullptr;int na=ul.process(l.data(),n,a),nb=ur.process(r.data(),n,b);require(na==nb,"Limiter upsample lengths");for(int i=0;i<na && upCount<reader.frames*8;++i,++upCount)high({a[i],b[i]});};
    size_t n;while((n=reader.read(input.data(),input.size()))){for(size_t i=0;i<n;++i){l[i]=input[i].l*level;r[i]=input[i].r*level;}up(int(n));}
    l.fill(0);r.fill(0);for(int i=0;upCount<reader.frames*8&&i<64;++i)up(4096);require(upCount==reader.frames*8,"Limiter input flush");
    for(int i=0;i<HighRateLimiter::latency();++i)high({});
    int remainder=int(limitedCount%65536);if(remainder)down(remainder);
    hl.fill(0);hr.fill(0);for(int i=0;written<reader.frames&&i<64;++i)down(65536);
    require(written==reader.frames,"Limiter output frame preservation");if(used)writer.write(buffer.data(),used);writer.close();return limiter.stats;
}
}
