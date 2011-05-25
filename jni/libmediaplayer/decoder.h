#ifndef FFMPEG_DECODER_H
#define FFMPEG_DECODER_H

extern "C" {
	
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"

}

#include "thread.h"
#include "packetqueue.h"

class IDecoder : public Thread
{
public:
    IDecoder(AVStream* stream);
    ~IDecoder();
	
    void                                                stop();
    void                                                enqueue(AVPacket* packet);
    int                                                 packets();

protected:
    PacketQueue*                                        mQueue;
    AVStream*                                           mStream;

    virtual bool                                        prepare();
    virtual bool                                        decode();
    virtual bool                                        process(AVPacket *packet);

    virtual void                                        run();
};

#endif //FFMPEG_DECODER_H
