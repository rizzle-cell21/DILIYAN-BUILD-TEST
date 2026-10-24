// Test-only independent 16x libsoxr meter. Not linked into or shipped with app.
#include "Audio.hpp"
#include <soxr.h>
#include <iostream>
using namespace diliyan;
int main(int argc,char**argv){try{
 require(argc==2,"WAV path missing");WavReader reader(argv[1]);
 auto io=soxr_io_spec(SOXR_FLOAT64_I,SOXR_FLOAT64_I);auto quality=soxr_quality_spec(SOXR_VHQ,0);quality.precision=33;quality.passband_end=.999;
 soxr_error_t error=nullptr;soxr_t converter=soxr_create(rate,rate*16,2,&error,&io,&quality,nullptr);require(converter&&!error,error?error:"soxr create failed");
 struct Cleanup{soxr_t s;~Cleanup(){soxr_delete(s);}}cleanup{converter};
 static_assert(sizeof(Frame)==2*sizeof(double),"Interleaved stereo layout required");
 std::vector<Frame> input(4096),output(65536);double peak=0;
 auto process=[&](const Frame*in,size_t n){size_t consumed=0;do{size_t used=0,made=0;auto e=soxr_process(converter,in?in+consumed:nullptr,in?n-consumed:0,&used,output.data(),output.size(),&made);require(!e,e?e:"soxr processing error");
  for(size_t i=0;i<made;++i){peak=std::max({peak,std::abs(output[i].l),std::abs(output[i].r)});}
  consumed+=used;if(!in&&made==0)break;if(in&&used==0&&made==0)throw std::runtime_error("soxr stalled");
 }while(!in||consumed<n);};
 process(input.data(),input.size());size_t n;while((n=reader.read(input.data(),input.size())))process(input.data(),n);
 std::fill(input.begin(),input.end(),Frame{});process(input.data(),input.size());process(nullptr,0);
 std::cout.precision(12);std::cout<<db(peak)<<std::endl;
 }catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}
