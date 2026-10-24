#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <stdexcept>
#include <vector>
#include <complex>
#include "vendor/r8brain/CDSPResampler.h"

namespace diliyan {
constexpr int rate = 48000;
constexpr double pi = 3.14159265358979323846;
inline double gain(double db) { return std::pow(10.0, db / 20.0); }
inline double db(double amplitude) { return 20 * std::log10(std::max(amplitude, 1e-15)); }
struct Frame { double l = 0, r = 0; };
inline Frame operator*(Frame a, double g) { return {a.l*g,a.r*g}; }
inline Frame operator+(Frame a, Frame b) { return {a.l+b.l,a.r+b.r}; }

// Transposed direct form II; each channel owns its own state.
struct Biquad {
    double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
    double tick(double x) {
        double y=b0*x+z1;
        z1=b1*x-a1*y+z2; z2=b2*x-a2*y;
        return y;
    }
    static Biquad lowpass(double hz) {
        double w=2*pi*hz/rate,c=std::cos(w),s=std::sin(w),a=s/std::sqrt(2.0),d=1+a;
        return {(1-c)/2/d,(1-c)/d,(1-c)/2/d,-2*c/d,(1-a)/d};
    }
    static Biquad peak(double hz,double q,double amount) {
        double A=std::pow(10.0,amount/40),w=2*pi*hz/rate,c=std::cos(w),a=std::sin(w)/(2*q),d=1+a/A;
        return {(1+a*A)/d,-2*c/d,(1-a*A)/d,-2*c/d,(1-a/A)/d};
    }
    static Biquad shelf(double hz,double amount,bool high) {
        double A=std::pow(10.0,amount/40),w=2*pi*hz/rate,c=std::cos(w),s=std::sin(w);
        double t=s*std::sqrt(2*A),d;
        if(high) {
            d=(A+1)-(A-1)*c+t;
            return {A*((A+1)+(A-1)*c+t)/d,-2*A*((A-1)+(A+1)*c)/d,
                A*((A+1)+(A-1)*c-t)/d,2*((A-1)-(A+1)*c)/d,((A+1)-(A-1)*c-t)/d};
        }
        d=(A+1)+(A-1)*c+t;
        return {A*((A+1)-(A-1)*c+t)/d,2*A*((A-1)-(A+1)*c)/d,
            A*((A+1)-(A-1)*c-t)/d,-2*((A-1)+(A+1)*c)/d,((A+1)+(A-1)*c-t)/d};
    }
};

struct StereoFilter {
    Biquad l,r;
    explicit StereoFilter(Biquad f):l(f),r(f){}
    Frame tick(Frame f) { return {l.tick(f.l),r.tick(f.r)}; }
};

// V2 peak reconstruction: vendored MIT r8brain, 8x, 0.5% transition,
// 180 dB stopband. Prefix and flush include file-boundary ringing.
class Peak8 {
    r8b::CDSPResampler24 left{48000,384000,4096,0.5},right{48000,384000,4096,0.5};
    std::array<double,4096> l{},r{};
    size_t used=0;
    uint64_t input=0,output=0;
    void process(int n) {
        double *ol=nullptr,*orr=nullptr;
        int a=left.process(l.data(),n,ol),b=right.process(r.data(),n,orr);
        if(a!=b)throw std::runtime_error("Peak resampler channel length mismatch");
        for(int i=0;i<a;++i)maximum=std::max({maximum,std::abs(ol[i]),std::abs(orr[i])});
        output+=uint64_t(a);
    }
public:
    double maximum=0;
    Peak8(){process(4096);}
    void add(Frame f) {
        maximum=std::max({maximum,std::abs(f.l),std::abs(f.r)});
        l[used]=f.l;r[used]=f.r;++used;++input;
        if(used==4096){process(4096);used=0;}
    }
    void finish() {
        if(used){for(size_t i=used;i<4096;++i)l[i]=r[i]=0;process(4096);}
        l.fill(0);r.fill(0);
        for(int i=0;output<(input+8192)*8 && i<64;++i)process(4096);
        if(output<(input+8192)*8)throw std::runtime_error("Peak resampler did not flush");
    }
};

// Power spectrum of active Hann windows; 13 log bands, no invented sections.
class Spectrum {
    static constexpr int N=2048;
    std::array<double,N> midL{},midR{};
    int used=0;
    double activeFloor;
    static void fft(std::array<std::complex<double>,N> &a) {
        for(int i=1,j=0;i<N;++i){int bit=N>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}
        for(int len=2;len<=N;len*=2){auto step=std::polar(1.0,-2*pi/len);for(int i=0;i<N;i+=len){std::complex<double>w=1;for(int j=0;j<len/2;++j){auto u=a[i+j],v=a[i+j+len/2]*w;a[i+j]=u+v;a[i+j+len/2]=u-v;w*=step;}}}
    }
    void window() {
        double e=0;for(int i=0;i<N;++i)e+=midL[i]*midL[i]+midR[i]*midR[i];
        if(e/(2*N)<activeFloor)return;
        std::array<std::complex<double>,N> l{},r{};
        for(int i=0;i<N;++i){double w=.5-.5*std::cos(2*pi*i/(N-1));l[i]=midL[i]*w;r[i]=midR[i]*w;}
        fft(l);fft(r);++windows;
        for(int k=1;k<=N/2;++k){double hz=k*double(rate)/N;int band=0;while(band<12 && hz>edges[band+1])++band;
            if(hz>=edges[0]&&hz<=edges[13]){double power=std::norm(l[k])+std::norm(r[k]);energy[band]+=power;sideEnergy[band]+=std::norm((l[k]-r[k])*.5);total+=power;}}
    }
public:
    inline static constexpr std::array<double,14> edges{20,80,125,200,315,500,800,1250,2000,3150,5000,8000,12500,24000};
    std::array<double,13> energy{},sideEnergy{};
    double total=0;uint64_t windows=0;
    explicit Spectrum(double floorDB=-55):activeFloor(std::pow(10.0,floorDB/10)){}
    void add(Frame f){midL[used]=f.l;midR[used]=f.r;++used;if(used==N){window();for(int i=0;i<N/2;++i){midL[i]=midL[i+N/2];midR[i]=midR[i+N/2];}used=N/2;}}
    std::array<double,13> fractions()const{auto r=energy;for(auto &x:r)x/=std::max(total,1e-30);return r;}
    std::array<double,13> widths()const{std::array<double,13> r{};for(size_t i=0;i<13;++i)r[i]=std::sqrt(2*sideEnergy[i]/std::max(energy[i],1e-30));return r;}
};

struct Metrics {
    uint64_t frames=0,clipped=0;
    double lufs=-150,shortMax=-150,peak=-300,tp=-300,rms=-300,active=-300;
    double bass=0,presence=0,air=0,correlation=1,width=0,crest=0,onset=0;
    double leading=0;
    std::array<double,13> bands{},sideBands{};
    uint64_t spectralWindows=0;
};

// Stereo BS.1770 K weighting at 48 kHz; 400ms blocks/100ms hop,
// absolute -70 LUFS then relative -10 LU gate. Silent/short files -> sentinel.
class Meter {
    StereoFilter shelf{Biquad{1.53512485958697,-2.69169618940638,1.19839281085285,-1.69065929318241,0.73248077421585}};
    StereoFilter highpass{Biquad{1,-2,1,-1.99004745483398,0.99007225036621}};
    StereoFilter low{Biquad::lowpass(180)};
    StereoFilter pLow{Biquad::lowpass(1500)},pHigh{Biquad::lowpass(5000)},airLow{Biquad::lowpass(8000)};
    std::array<double,rate*3> ring{};
    std::vector<double> integratedBlocks,activeBlocks;
    uint64_t n=0,clip=0,first=std::numeric_limits<uint64_t>::max();
    double sum400=0,sum3=0,total=0,ll=0,rr=0,lr=0,mid=0,side=0;
    double bassEnergy=0,presenceEnergy=0,airEnergy=0,shortMax=-150;
    double rawHop=0,previousHop=0,onsets=0;
public:
    Peak8 truePeak;
    Spectrum spectrum;
    double samplePeak=0;
    void add(Frame f) {
        if(!std::isfinite(f.l)||!std::isfinite(f.r)) throw std::runtime_error("Nicht-endliche Audiosamples (NaN/Inf).");
        truePeak.add(f);spectrum.add(f);
        samplePeak=std::max({samplePeak,std::abs(f.l),std::abs(f.r)});
        if(std::max(std::abs(f.l),std::abs(f.r))>=1) ++clip;
        if(first==std::numeric_limits<uint64_t>::max() && std::max(std::abs(f.l),std::abs(f.r))>gain(-60)) first=n;
        double e=f.l*f.l+f.r*f.r;
        total+=e; rawHop+=e/2;
        ll+=f.l*f.l;rr+=f.r*f.r;lr+=f.l*f.r;
        mid+=std::pow((f.l+f.r)/2,2);side+=std::pow((f.l-f.r)/2,2);
        auto b=low.tick(f),pl=pLow.tick(f),ph=pHigh.tick(f),al=airLow.tick(f);
        bassEnergy+=b.l*b.l+b.r*b.r;
        presenceEnergy+=std::pow(ph.l-pl.l,2)+std::pow(ph.r-pl.r,2);
        airEnergy+=std::pow(f.l-al.l,2)+std::pow(f.r-al.r,2);
        auto k=highpass.tick(shelf.tick(f));
        double ke=k.l*k.l+k.r*k.r;
        size_t pos=n%ring.size();
        if(n>=19200) sum400-=ring[(n-19200)%ring.size()];
        if(n>=ring.size()) sum3-=ring[pos];
        ring[pos]=ke;sum400+=ke;sum3+=ke;++n;
        if(n%4800==0) {
            if(n>=19200) integratedBlocks.push_back(std::max(0.0,sum400/19200));
            if(n>=ring.size()) shortMax=std::max(shortMax,-0.691+10*std::log10(std::max(sum3/ring.size(),1e-30)));
            double energy=rawHop/4800;
            if(energy>1e-7) activeBlocks.push_back(energy);
            onsets+=std::max(0.0,std::sqrt(energy)-std::sqrt(previousHop));
            previousHop=energy;rawHop=0;
        }
    }
    Metrics finish() {
        truePeak.finish();
        Metrics m;m.bands=spectrum.fractions();m.sideBands=spectrum.widths();m.spectralWindows=spectrum.windows;m.frames=n;m.clipped=clip;m.peak=db(samplePeak);m.tp=db(truePeak.maximum);
        m.rms=db(std::sqrt(total/std::max<uint64_t>(1,2*n)));
        double absGate=std::pow(10.0,(-70+0.691)/10),sum=0;size_t count=0;
        for(auto e:integratedBlocks) if(e>=absGate) {sum+=e;++count;}
        if(count) {
            double gate=std::max(absGate,sum/count*0.1);sum=0;count=0;
            for(auto e:integratedBlocks) if(e>=gate) {sum+=e;++count;}
            if(count) m.lufs=-0.691+10*std::log10(sum/count);
        }
        if(!activeBlocks.empty()) {
            auto middle=activeBlocks.begin()+activeBlocks.size()/2;
            std::nth_element(activeBlocks.begin(),middle,activeBlocks.end());
            m.active=10*std::log10(*middle);
        }
        m.shortMax=shortMax;
        m.bass=bassEnergy/std::max(total,1e-30);m.presence=presenceEnergy/std::max(total,1e-30);m.air=airEnergy/std::max(total,1e-30);
        m.correlation=lr/std::max(std::sqrt(ll*rr),1e-30);
        m.width=std::sqrt(side/std::max(mid+side,1e-30));m.crest=m.peak-m.rms;
        m.onset=onsets/std::max(1.0,n/4800.0);
        m.leading=first==std::numeric_limits<uint64_t>::max()?n/double(rate):first/double(rate);
        return m;
    }
};

// Linked stereo sample lookahead limiter. Offline true-peak safety correction
// is performed on the actual PCM24 export using Peak8 afterwards.
class Limiter {
    static constexpr size_t look=240;
    std::deque<Frame> audio;
    std::deque<std::pair<uint64_t,double>> maxima;
    uint64_t index=0;
    double envelope=1;
    const double ceiling=gain(-1.3),release=std::exp(-1.0/(0.12*rate));
public:
    double reduction=0;
    bool add(Frame f,Frame &out) {
        double p=std::max(std::abs(f.l),std::abs(f.r));
        while(!maxima.empty()&&maxima.back().second<=p) maxima.pop_back();
        maxima.emplace_back(index,p);audio.push_back(f);
        bool ready=index>=look;
        if(ready) {
            uint64_t start=index-look;
            while(!maxima.empty()&&maxima.front().first<start) maxima.pop_front();
            double target=std::min(1.0,ceiling/std::max(maxima.front().second,1e-20));
            envelope=target<envelope?target:release*envelope+(1-release)*target;
            out=audio.front()*envelope;audio.pop_front();
            reduction=std::max(reduction,-db(envelope));
        }
        ++index; return ready;
    }
    static size_t latency() {return look;}
};

struct Plan {
    double low=0,presence=0,high=0,compression=0,threshold=-12,target=-14;
};
inline double ratioDB(double reference,double own) {return 10*std::log10(std::max(reference,1e-8)/std::max(own,1e-8));}
inline Plan plan(const Metrics &mix,const Metrics &reference,int variant) {
    Plan p;
    p.low=std::clamp(ratioDB(reference.bass,mix.bass)*0.25,-1.5,1.5);
    p.presence=std::clamp(ratioDB(reference.presence,mix.presence)*0.20,-1.2,1.2);
    p.high=std::clamp(ratioDB(reference.air,mix.air)*0.15,-1.0,1.0);
    p.target=std::clamp(reference.lufs,-18.0,-12.0);
    // Profile directions diminish when the input already has the property.
    if(variant==1) {
        p.low=std::clamp(p.low+(1-std::clamp(mix.bass/0.55,0.0,1.0))*0.7,-1.5,1.5);
        p.high=std::clamp(p.high-0.25*std::clamp(mix.air/0.1,0.0,1.0),-1.0,1.0);
    }
    if(variant==2) {
        p.low=std::clamp(p.low-0.5*std::clamp(mix.bass/0.5,0.0,1.0),-1.5,1.5);
        p.presence=std::clamp(p.presence+0.6*(1-std::clamp(mix.presence/0.4,0.0,1.0)),-1.2,1.2);
        p.target-=0.5; // extra headroom to preserve transients
    }
    if(mix.crest>15 && reference.crest<mix.crest-2 && variant!=2) p.compression=variant==1?1.25:1.15;
    return p;
}
class LegacyMasterDSP {
    StereoFilter low,presence,high;
    double env=0,reduction=0;
    Plan p;
public:
    explicit LegacyMasterDSP(Plan v):low(Biquad::shelf(140,v.low,false)),presence(Biquad::peak(2700,0.8,v.presence)),high(Biquad::shelf(8500,v.high,true)),p(v){}
    Frame tick(Frame x) {
        x=high.tick(presence.tick(low.tick(x)));
        if(p.compression>1) {
            double detector=std::max(std::abs(x.l),std::abs(x.r));
            double c=std::exp(-1.0/((detector>env?0.025:0.15)*rate));env=c*env+(1-c)*detector;
            double over=std::max(0.0,db(env)-p.threshold);
            reduction=std::min(2.0,over*(1-1/p.compression));
            x=x*gain(-reduction);
        }
        return x;
    }
};
} // namespace
