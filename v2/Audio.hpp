#pragma once
#include "DSP.hpp"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace diliyan {
namespace fs=std::filesystem;
constexpr size_t blockSize=4096;
inline void require(bool condition,const std::string &message) { if(!condition) throw std::runtime_error(message); }
inline uint16_t u16(const unsigned char *b) {return uint16_t(b[0])|(uint16_t(b[1])<<8);}
inline uint32_t u32(const unsigned char *b) {return uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);}
inline void put16(std::ostream &s,uint16_t v) {s.put(char(v));s.put(char(v>>8));}
inline void put32(std::ostream &s,uint32_t v) {put16(s,uint16_t(v));put16(s,uint16_t(v>>16));}

class WavWriter {
    std::ofstream file;
    uint64_t frames=0;
    int bits;
    std::streampos dataSizePosition{};
    bool closed=false;
    uint32_t random=0x2194fea1;
    double uniform() {random^=random<<13;random^=random>>17;random^=random<<5;return random/double(UINT32_MAX);}
public:
    explicit WavWriter(const fs::path &p,int depth=32):file(p,std::ios::binary|std::ios::trunc),bits(depth) {
        require(bits==24||bits==32,"Interner WAV-Writer unterstützt nur PCM24 oder Float32.");
        require(bool(file),"Ausgabedatei nicht schreibbar: "+p.string());
        file.write("RIFF",4);put32(file,0);file.write("WAVEfmt ",8);put32(file,16);
        put16(file,bits==32?3:1);put16(file,2);put32(file,rate);put32(file,rate*2*bits/8);
        put16(file,2*bits/8);put16(file,bits);
        if(bits==32) {file.write("fact",4);put32(file,4);put32(file,0);}
        file.write("data",4);dataSizePosition=file.tellp();put32(file,0);
    }
    void write(const Frame *data,size_t count) {
        require(!closed,"Ausgabedatei bereits geschlossen.");
        require((frames+count)*2*bits/8 < UINT32_MAX-44,"WAV überschreitet RIFF-Grenze.");
        std::vector<unsigned char> bytes(count*2*bits/8);
        size_t offset=0;
        for(size_t i=0;i<count;++i) for(double x:{data[i].l,data[i].r}) {
            require(std::isfinite(x),"Ungültiges Sample beim Schreiben.");
            if(bits==32) {
                float v=float(x);uint32_t b;std::memcpy(&b,&v,4);
                for(int k=0;k<4;++k) bytes[offset++]=uint8_t(b>>(8*k));
            } else {
                // Deterministic TPDF, one output LSB peak per uniform difference.
                require(std::abs(x)<1,"Interner Fehler: PCM-Export würde clippen.");
                auto v=int32_t(std::llround(std::clamp(x*8388608.0+(uniform()-uniform()),-8388608.0,8388607.0)));
                for(int k=0;k<3;++k) bytes[offset++]=uint8_t(uint32_t(v)>>(8*k));
            }
        }
        file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
        require(bool(file),"Schreibfehler / Datenträger voll.");frames+=count;
    }
    void close() {
        if(closed)return;
        uint32_t size=uint32_t(frames*2*bits/8);
        file.seekp(4);put32(file,uint32_t(dataSizePosition)+size-4);
        if(bits==32) {file.seekp(44);put32(file,uint32_t(frames));}
        file.seekp(dataSizePosition);put32(file,size);file.flush();
        require(bool(file),"WAV-Abschluss fehlgeschlagen.");file.close();closed=true;
    }
    ~WavWriter() {if(!closed) {try{close();}catch(...){}}}
};

class WavReader {
    std::ifstream file;
    uint64_t remaining=0;
    std::streampos dataStart{};
public:
    uint32_t sampleRate=0;
    uint16_t channels=0,bits=0,format=0;
    uint64_t frames=0;
    explicit WavReader(const fs::path &path):file(path,std::ios::binary) {
        require(bool(file),"WAV nicht lesbar: "+path.string());
        unsigned char head[12];file.read(reinterpret_cast<char*>(head),12);
        require(file.gcount()==12 && std::memcmp(head,"RIFF",4)==0 && std::memcmp(head+8,"WAVE",4)==0,"Kein unterstütztes RIFF-WAV.");
        bool fmt=false,data=false;std::streampos dataPos{};uint32_t dataSize=0;
        while(file && !(fmt&&data)) {
            unsigned char ch[8];file.read(reinterpret_cast<char*>(ch),8);if(file.gcount()!=8)break;
            uint32_t size=u32(ch+4);auto start=file.tellg();
            if(std::memcmp(ch,"fmt ",4)==0) {
                require(size>=16&&size<=4096,"Ungültiger fmt-Chunk.");
                std::vector<unsigned char> b(size);file.read(reinterpret_cast<char*>(b.data()),size);
                require(bool(file),"Unvollständiger WAV-Header.");
                format=u16(b.data());channels=u16(b.data()+2);sampleRate=u32(b.data()+4);bits=u16(b.data()+14);
                if(format==0xfffe&&size>=40) format=u16(b.data()+24);
                require(channels>=1&&channels<=2,"Nur Mono/Stereo unterstützt.");
                require(u16(b.data()+12)==channels*(bits/8),"Ungültiges WAV-Blockformat.");fmt=true;
            } else if(std::memcmp(ch,"data",4)==0) {data=true;dataSize=size;dataPos=start;}
            file.seekg(start+std::streamoff(size+(size&1)));
        }
        require(fmt&&data&&((format==1&&(bits==16||bits==24||bits==32))||(format==3&&(bits==32||bits==64))),"WAV-Kodierung nicht unterstützt.");
        require(sampleRate==rate,"Interne WAV-Datei muss 48 kHz haben.");
        require(dataSize%(channels*bits/8)==0,"Abgeschnittener WAV-Datenblock.");
        file.clear();file.seekg(0,std::ios::end);auto end=file.tellg();
        require(end>=dataPos+std::streamoff(dataSize),"WAV enthält weniger Daten als angegeben.");
        frames=dataSize/(channels*bits/8);remaining=frames;dataStart=dataPos;file.seekg(dataPos);
    }
    void seek(uint64_t frame) {frame=std::min(frame,frames);file.clear();file.seekg(dataStart+std::streamoff(frame*channels*bits/8));remaining=frames-frame;}
    size_t read(Frame *out,size_t capacity) {
        size_t n=size_t(std::min<uint64_t>(remaining,capacity));
        std::vector<unsigned char> b(n*channels*bits/8);
        file.read(reinterpret_cast<char*>(b.data()),std::streamsize(b.size()));
        require(size_t(file.gcount())==b.size(),"Lesefehler in WAV.");
        size_t offset=0;
        for(size_t i=0;i<n;++i) {
            double values[2]{};
            for(int c=0;c<channels;++c) {
                auto p=b.data()+offset;double v=0;
                if(format==3&&bits==32) {uint32_t u=u32(p);float f;std::memcpy(&f,&u,4);v=f;}
                else if(format==3) {uint64_t u=uint64_t(u32(p))|(uint64_t(u32(p+4))<<32);std::memcpy(&v,&u,8);}
                else if(bits==16) v=int16_t(u16(p))/32768.0;
                else if(bits==24) {int32_t s=int32_t(uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16));if(s&0x800000)s|=int32_t(0xff000000);v=s/8388608.0;}
                else v=int32_t(u32(p))/2147483648.0;
                require(std::isfinite(v),"Audiodatei enthält NaN oder Inf.");values[c]=v;offset+=bits/8;
            }
            out[i]={values[0],channels==1?values[0]:values[1]};
        }
        remaining-=n;return n;
    }
};

struct SourceInfo {double sampleRate=0,seconds=0;int channels=0,bits=0;};
#ifdef __APPLE__
inline void status(OSStatus s,const char *what) {require(s==noErr,std::string(what)+" (Core Audio "+std::to_string(s)+")");}
class Decoder {
    ExtAudioFileRef file=nullptr;
public:
    SourceInfo info;
    explicit Decoder(const fs::path &p) {
        auto text=p.string();
        CFURLRef url=CFURLCreateFromFileSystemRepresentation(nullptr,reinterpret_cast<const UInt8*>(text.data()),text.size(),false);
        require(url!=nullptr,"Ungültiger Dateipfad.");
        OSStatus error=ExtAudioFileOpenURL(url,&file);CFRelease(url);status(error,"Audio öffnen fehlgeschlagen");
        try {
            AudioStreamBasicDescription original{};UInt32 size=sizeof(original);
            status(ExtAudioFileGetProperty(file,kExtAudioFileProperty_FileDataFormat,&size,&original),"Audioformat lesen");
            SInt64 length=0;size=sizeof(length);
            status(ExtAudioFileGetProperty(file,kExtAudioFileProperty_FileLengthFrames,&size,&length),"Audiolänge lesen");
            info={original.mSampleRate,length/original.mSampleRate,int(original.mChannelsPerFrame),int(original.mBitsPerChannel)};
            require(info.channels==1||info.channels==2,"Nur Mono und Stereo zulässig, kein unbemerkter Mehrkanal-Downmix.");
            require(info.sampleRate>=8000&&info.sampleRate<=192000,"Sample-Rate außerhalb 8–192 kHz.");
            require(info.seconds>=0.4&&info.seconds<=1800,"Dateien müssen 0,4 Sekunden bis 30 Minuten lang sein.");
            AudioStreamBasicDescription client{};client.mSampleRate=rate;client.mFormatID=kAudioFormatLinearPCM;
            client.mFormatFlags=kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked;
            client.mChannelsPerFrame=UInt32(info.channels);client.mBitsPerChannel=32;client.mFramesPerPacket=1;
            client.mBytesPerFrame=UInt32(4*info.channels);client.mBytesPerPacket=client.mBytesPerFrame;
            status(ExtAudioFileSetProperty(file,kExtAudioFileProperty_ClientDataFormat,sizeof(client),&client),"PCM-Konvertierung einrichten");
            AudioConverterRef converter=nullptr;size=sizeof(converter);
            status(ExtAudioFileGetProperty(file,kExtAudioFileProperty_AudioConverter,&size,&converter),"Audio-Konverter abfragen");
            if(converter && info.sampleRate!=rate) {
                UInt32 quality=kAudioConverterQuality_Max;
                status(AudioConverterSetProperty(converter,kAudioConverterSampleRateConverterQuality,sizeof(quality),&quality),"Resampling-Qualität einstellen");
            }
        } catch(...) {ExtAudioFileDispose(file);file=nullptr;throw;}
    }
    size_t read(Frame *out,size_t capacity) {
        std::vector<float> data(capacity*info.channels);
        AudioBufferList buffers{};buffers.mNumberBuffers=1;buffers.mBuffers[0].mNumberChannels=UInt32(info.channels);
        buffers.mBuffers[0].mDataByteSize=UInt32(data.size()*sizeof(float));buffers.mBuffers[0].mData=data.data();
        UInt32 n=UInt32(capacity);status(ExtAudioFileRead(file,&n,&buffers),"Audio dekodieren");
        for(size_t i=0;i<n;++i) {
            double l=data[i*info.channels],r=data[i*info.channels+info.channels-1];
            require(std::isfinite(l)&&std::isfinite(r),"Audiodatei enthält NaN/Inf.");out[i]={l,r};
        }
        return n;
    }
    ~Decoder(){if(file)ExtAudioFileDispose(file);}
};
#else
// Linux backend is only for portable engine tests: 48k WAV, no FLAC/SRC.
class Decoder {
    WavReader reader;
public:
    SourceInfo info;
    explicit Decoder(const fs::path &p):reader(p) {info={double(reader.sampleRate),reader.frames/double(rate),reader.channels,reader.bits};}
    size_t read(Frame *f,size_t n){return reader.read(f,n);}
};
#endif

inline Metrics measure(const fs::path &p) {
    WavReader r(p);Meter meter;std::array<Frame,blockSize> b{};size_t n;
    while((n=r.read(b.data(),b.size()))) for(size_t i=0;i<n;++i)meter.add(b[i]);
    return meter.finish();
}
inline void scaleFile(const fs::path &source,const fs::path &dest,double level,int bits) {
    WavReader r(source);WavWriter w(dest,bits);std::array<Frame,blockSize>b{};size_t n;
    while((n=r.read(b.data(),b.size()))) {for(size_t i=0;i<n;++i)b[i]=b[i]*level;w.write(b.data(),n);}w.close();
}
} // namespace
