#include "Audio.hpp"
#include <iostream>
#include <random>
using namespace diliyan;
void check(bool condition,const char *text){if(!condition)throw std::runtime_error(text);std::cout<<"PASS "<<text<<'\n';}
double magnitude(const Biquad &f,double hz) {
    double w=2*pi*hz/rate;
    double nr=f.b0+f.b1*std::cos(w)+f.b2*std::cos(2*w),ni=-f.b1*std::sin(w)-f.b2*std::sin(2*w);
    double dr=1+f.a1*std::cos(w)+f.a2*std::cos(2*w),di=-f.a1*std::sin(w)-f.a2*std::sin(2*w);
    return std::sqrt((nr*nr+ni*ni)/(dr*dr+di*di));
}
int main(int argc,char **argv) {
    try {
        require(argc==2,"Testausgabeordner fehlt.");fs::path dir=argv[1];fs::create_directories(dir);
        auto bypass=Biquad::peak(2700,0.8,0);double null=0;
        for(int i=0;i<rate;++i){double x=std::sin(i*0.31)*0.6;null=std::max(null,std::abs(x-bypass.tick(x)));}
        check(null<1e-12,"Zero-dB EQ null test");
        check(std::abs(db(magnitude(Biquad::peak(2700,0.8,1.2),2700))-1.2)<1e-9,"Peaking EQ center gain");
        check(std::abs(db(magnitude(Biquad::shelf(140,1.5,false),0))-1.5)<1e-9,"Low shelf DC response");
        check(std::abs(db(magnitude(Biquad::shelf(8500,-1,true),rate/2))+1)<1e-9,"High shelf Nyquist response");
        Meter stereo,anti,silence;
        WavWriter writer(dir/"roundtrip.wav",24);
        std::vector<Frame> tone(rate*4);
        for(size_t i=0;i<tone.size();++i) {
            double x=0.1*std::sin(2*pi*1000*i/rate);tone[i]={x,x};
            stereo.add({x,x});anti.add({x,-x});silence.add({});
        }
        writer.write(tone.data(),tone.size());writer.close();
        auto sm=stereo.finish(),am=anti.finish(),zm=silence.finish();
        check(std::abs(sm.lufs+20)<0.12,"48k stereo 1kHz sine loudness approximately -20 LUFS");
        check(std::abs(sm.shortMax+20)<0.12,"Three-second short-term loudness");
        check(sm.correlation>0.999999&&sm.width<1e-10,"Identical stereo mono compatibility");
        check(am.correlation<-.999999&&am.width>.999999,"Anti-phase detection");
        check(zm.lufs==-150&&zm.frames==tone.size(),"Silence gate and frame count");
        auto wm=measure(dir/"roundtrip.wav");
        check(wm.frames==tone.size()&&wm.clipped==0,"PCM24 roundtrip length and headroom");
        WavReader reader(dir/"roundtrip.wav");std::vector<Frame> actual(tone.size());reader.read(actual.data(),actual.size());
        double error=0;for(size_t i=0;i<tone.size();++i)error=std::max(error,std::abs(tone[i].l-actual[i].l));
        check(error<2.0/8388608,"PCM24 TPDF error bounded below two LSB");
        Peak8 intersample;
        for(int i=0;i<rate;++i){double x=0.9*std::sin(2*pi*12000*i/rate+pi/4);intersample.add({x,x});}
        intersample.finish();
        check(intersample.maximum>0.88,"Intersample peak exceeds sample peak of phase-offset sine");
        Limiter limiter;std::vector<Frame> input(10003),output;input[0]={2,-2};input[240]={-4,4};input.back()={3,-3};
        auto push=[&](Frame f){Frame y;if(limiter.add(f,y))output.push_back(y);};
        for(auto f:input)push(f);
        for(size_t i=0;i<Limiter::latency();++i)push({});
        check(output.size()==input.size(),"Limiter compensated length including first/last frame");
        double max=0;for(auto f:output)max=std::max({max,std::abs(f.l),std::abs(f.r)});
        check(max<=gain(-1.3)+1e-12,"Lookahead limiter sample ceiling");
        check(output.front().l>0&&output.back().l>0,"Boundary transients retained");
        check(std::abs(output[240].l+output[240].r)<1e-12,"Limiter stereo linking");
        Metrics mix=sm,ref=sm;mix.bass=.2;mix.presence=.15;mix.air=.05;ref.bass=.3;ref.presence=.2;ref.air=.04;
        auto a=plan(mix,ref,0),b=plan(mix,ref,1),c=plan(mix,ref,2);
        check(b.low>a.low&&c.presence>a.presence,"Material-adaptive profile directions");
        auto different=mix;different.bass=.6;
        check(std::abs(plan(different,ref,1).low-b.low)>.1,"Plans change with material rather than fixed presets");
        std::cout<<"All portable DSP tests passed. Not a perceptual quality certification.\n";
    }catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
