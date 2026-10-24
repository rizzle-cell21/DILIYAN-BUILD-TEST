#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <stdexcept>
#include <vector>

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

// 8x, 32-tap/phase windowed-sinc reconstruction. Measurement estimate,
// not a certified BS.1770 true-peak implementation. Includes zero-padded tails.
class Peak8 {
    std::array<Frame,32> history{};
    std::array<std::array<double,32>,7> taps{};
    size_t pos=0;
public:
    double maximum=0;
    Peak8() {
        for(int p=1;p<8;++p) {
            double sum=0;
            for(int k=0;k<32;++k) {
                double x=k-15.0-p/8.0;
                double sinc=std::abs(x)<1e-12?1:std::sin(pi*x)/(pi*x);
                double win=0.42-0.5*std::cos(2*pi*k/31)+0.08*std::cos(4*pi*k/31);
                taps[p-1][k]=sinc*win; sum+=taps[p-1][k];
            }
            for(auto &v:taps[p-1]) v/=sum;
        }
    }
    void add(Frame f) {
        maximum=std::max({maximum,std::abs(f.l),std::abs(f.r)});
        history[pos]=f; pos=(pos+1)%32;
        for(const auto &phase:taps) {
            double l=0,r=0;
            for(size_t k=0;k<32;++k) {
                auto h=history[(pos+k)%32]; l+=phase[k]*h.l; r+=phase[k]*h.r;
            }
            maximum=std::max({maximum,std::abs(l),std::abs(r)});
        }
    }
    void finish() { for(int n=0;n<32;++n) add({}); }
};

struct Metrics {
    uint64_t frames=0,clipped=0;
    double lufs=-150,shortMax=-150,peak=-300,tp=-300,rms=-300,active=-300;
    double bass=0,presence=0,air=0,correlation=1,width=0,crest=0,onset=0;
    double leading=0;
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
    double samplePeak=0;
    void add(Frame f) {
        if(!std::isfinite(f.l)||!std::isfinite(f.r)) throw std::runtime_error("Nicht-endliche Audiosamples (NaN/Inf).");
        truePeak.add(f);
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
        Metrics m;m.frames=n;m.clipped=clip;m.peak=db(samplePeak);m.tp=db(truePeak.maximum);
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
class MasterDSP {
    StereoFilter low,presence,high;
    double env=0,reduction=0;
    Plan p;
public:
    explicit MasterDSP(Plan v):low(Biquad::shelf(140,v.low,false)),presence(Biquad::peak(2700,0.8,v.presence)),high(Biquad::shelf(8500,v.high,true)),p(v){}
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
