/* mediaplayer.cpp
**
*/

//#define LOG_NDEBUG 0
#define TAG "FFMpegMediaPlayer"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

extern "C" {
	
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/log.h"
	
} // end of extern C

#include <android/log.h>

// map system drivers methods
#include <drivers_map.h>

#include "mediaplayer.h"

MediaPlayer::MediaPlayer()
{
    __android_log_print(ANDROID_LOG_INFO, TAG, "constructor");
    mListener = NULL;
    mCookie = NULL;
    mDuration = -1;
    mStreamType = MUSIC;
    mCurrentPosition = -1;
    mSeekPosition = -1;
    mCurrentState = MEDIA_PLAYER_IDLE;
    mPrepareSync = false;
    mPrepareStatus = NO_ERROR;
    mLoop = false;
	pthread_mutex_init(&mLock, NULL);
    mLeftVolume = mRightVolume = 1.0;
    mVideoWidth = mVideoHeight = 0;
	//sPlayer = this;
}

MediaPlayer::~MediaPlayer()
{
}

status_t MediaPlayer::prepareAudio()
{
	__android_log_print(ANDROID_LOG_INFO, TAG, "prepareAudio");
	mFFmpegStorage.audio.stream = -1;
	for (int i = 0; i < mFFmpegStorage.pFormatCtx->nb_streams; i++) {
		if (mFFmpegStorage.pFormatCtx->streams[i]->codec->codec_type == CODEC_TYPE_AUDIO) {
			mFFmpegStorage.audio.stream = i;
		}
		if(mFFmpegStorage.audio.stream != -1) {
			break;
		}
	}
	
	if (mFFmpegStorage.audio.stream == -1) {
		return INVALID_OPERATION;
	}
	
	// Get a pointer to the codec context for the video stream
	mFFmpegStorage.audio.codec_ctx = mFFmpegStorage.pFormatCtx->streams[mFFmpegStorage.audio.stream]->codec;
	mFFmpegStorage.audio.codec = avcodec_find_decoder(mFFmpegStorage.audio.codec_ctx->codec_id);
	if (mFFmpegStorage.audio.codec == NULL) {
		return INVALID_OPERATION;
	}
	
	// Open codec
	if (avcodec_open(mFFmpegStorage.audio.codec_ctx, 
					 mFFmpegStorage.audio.codec) < 0) {
		return INVALID_OPERATION;	}
	return NO_ERROR;
}

status_t MediaPlayer::prepareVideo()
{
	__android_log_print(ANDROID_LOG_INFO, TAG, "prepareVideo");
	// Find the first video stream
	mFFmpegStorage.video.stream = -1;
	for (int i = 0; i < mFFmpegStorage.pFormatCtx->nb_streams; i++) {
		if (mFFmpegStorage.pFormatCtx->streams[i]->codec->codec_type == CODEC_TYPE_VIDEO) {
			mFFmpegStorage.video.stream = i;
		}
		if(mFFmpegStorage.video.stream != -1) {
			break;
		}
	}
	
	if (mFFmpegStorage.video.stream == -1) {
		return INVALID_OPERATION;
	}
	
	// Get a pointer to the codec context for the video stream
	mFFmpegStorage.video.codec_ctx = mFFmpegStorage.pFormatCtx->streams[mFFmpegStorage.video.stream]->codec;
	mFFmpegStorage.video.codec = avcodec_find_decoder(mFFmpegStorage.video.codec_ctx->codec_id);
	if (mFFmpegStorage.video.codec == NULL) {
		return INVALID_OPERATION;
	}
	
	// Open codec
	if (avcodec_open(mFFmpegStorage.video.codec_ctx, mFFmpegStorage.video.codec) < 0) {
		return INVALID_OPERATION;
	}
	// Allocate video frame
	mFFmpegStorage.pFrame = avcodec_alloc_frame();
	
	mVideoWidth = mFFmpegStorage.video.codec_ctx->width;
	mVideoHeight = mFFmpegStorage.video.codec_ctx->height;
	mDuration =  mFFmpegStorage.pFormatCtx->duration;
	mFFmpegStorage.img_convert_ctx = sws_getContext(mVideoWidth, 
												    mVideoHeight, 
												    mFFmpegStorage.video.codec_ctx->pix_fmt, 
												    mVideoWidth, 
												    mVideoHeight,
												    PIX_FMT_RGB565, 
												    SWS_POINT, 
												    NULL, 
												    NULL, 
												    NULL);
	
	return NO_ERROR;
}

status_t MediaPlayer::prepare()
{
	status_t ret;
	mCurrentState = MEDIA_PLAYER_PREPARING;
	av_log_set_callback(ffmpegNotify);
	if ((ret = prepareVideo()) != NO_ERROR) {
		mCurrentState = MEDIA_PLAYER_STATE_ERROR;
		return ret;
	}
	/*
	if ((ret = prepareAudio()) != NO_ERROR) {
		mCurrentState = MEDIA_PLAYER_STATE_ERROR;
		return ret;
	}
	*/
	mCurrentState = MEDIA_PLAYER_PREPARED;
	return NO_ERROR;
}

status_t MediaPlayer::setListener(MediaPlayerListener* listener)
{
    __android_log_print(ANDROID_LOG_INFO, TAG, "setListener");
    mListener = listener;
    return NO_ERROR;
}

status_t MediaPlayer::setDataSource(const char *url)
{
    __android_log_print(ANDROID_LOG_INFO, TAG, "setDataSource(%s)", url);
    status_t err = BAD_VALUE;
	// Open video file
	if(av_open_input_file(&mFFmpegStorage.pFormatCtx, url, NULL, 0, NULL) != 0) {
		return INVALID_OPERATION;
	}
	// Retrieve stream information
	if(av_find_stream_info(mFFmpegStorage.pFormatCtx) < 0) {
		return INVALID_OPERATION;
	}
	mCurrentState = MEDIA_PLAYER_INITIALIZED;
    return NO_ERROR;
}

AVFrame* MediaPlayer::createAndroidFrame()
{
	void*		pixels;
	AVFrame*	frame;
	
	frame = avcodec_alloc_frame();
	if (frame == NULL) {
		return NULL;
	}
	
	if(VideoDriver_getPixels(mVideoWidth, 
							 mVideoHeight, 
							 &pixels) != ANDROID_SURFACE_RESULT_SUCCESS) {
		return NULL;
	}
	
	// Assign appropriate parts of buffer to image planes in pFrameRGB
	// Note that pFrameRGB is an AVFrame, but AVFrame is a superset
	// of AVPicture
	avpicture_fill((AVPicture *) frame, 
				   (uint8_t *)pixels, 
				   PIX_FMT_RGB565, 
				   mVideoWidth, 
				   mVideoHeight);
	
	return frame;
}

status_t MediaPlayer::suspend() {
	__android_log_print(ANDROID_LOG_INFO, TAG, "suspend");
	// Free the YUV frame
	av_free(mFFmpegStorage.pFrame);
	
	// Close the codec
	avcodec_close(mFFmpegStorage.video.codec_ctx);
	avcodec_close(mFFmpegStorage.audio.codec_ctx);
	
	// Close the video file
	av_close_input_file(mFFmpegStorage.pFormatCtx);
    return NO_ERROR;
}

status_t MediaPlayer::resume() {
	pthread_mutex_lock(&mLock);
	mCurrentState = MEDIA_PLAYER_STARTED;
	pthread_mutex_unlock(&mLock);
    return NO_ERROR;
}

/**
 * TO-DO remove this ugly hack for creating android audio track
 **
 jclass clazz = env->FindClass("android/media/AudioTrack");
 jmethodID constr = env->GetMethodID(clazz, "<init>", "(IIIIII)V");
 jobject jaudiotrack = env->NewObject(clazz, 
 constr, 
 MUSIC, 
 mFFmpegStorage.audio.codec_ctx->sample_rate, 
 (mFFmpegStorage.audio.codec_ctx->channels == 2) ? CHANNEL_OUT_STEREO : CHANNEL_OUT_MONO,
 PCM_16_BIT,
 AVCODEC_MAX_AUDIO_FRAME_SIZE, 
 0 // mode static
 );
 if(AudioDriver_register(env, jaudiotrack) != ANDROID_AUDIOTRACK_RESULT_SUCCESS) {
 return INVALID_OPERATION;
 }*/

status_t MediaPlayer::setVideoSurface(JNIEnv* env, jobject jsurface)
{ 
	if(VideoDriver_register(env, jsurface) != ANDROID_SURFACE_RESULT_SUCCESS) {
		return INVALID_OPERATION;
	}
    return NO_ERROR;
}

status_t MediaPlayer::processVideo(AVPacket *packet, AVFrame *pFrame)
{
	int	completed;
	
	// Decode video frame
	avcodec_decode_video(mFFmpegStorage.video.codec_ctx, 
						 mFFmpegStorage.pFrame, 
						 &completed,
						 packet->data, 
						 packet->size);
	
	if (completed) {
		// Convert the image from its native format to RGB
		sws_scale(mFFmpegStorage.img_convert_ctx, 
				  mFFmpegStorage.pFrame->data, 
				  mFFmpegStorage.pFrame->linesize, 
				  0,
				  mVideoHeight, 
				  pFrame->data, 
				  pFrame->linesize);
		
		VideoDriver_updateSurface();
		return NO_ERROR;
	}
	return INVALID_OPERATION;
}

status_t MediaPlayer::processAudio(AVPacket *packet, int16_t *samples, int samples_size) 
{
	int size = samples_size;
	int len = avcodec_decode_audio3(mFFmpegStorage.audio.codec_ctx, samples, &size, packet);
	
	if(AudioDriver_stop() != ANDROID_AUDIOTRACK_RESULT_SUCCESS) {
		return INVALID_OPERATION;
	}
	if(AudioDriver_reload() != ANDROID_AUDIOTRACK_RESULT_SUCCESS) {
		return INVALID_OPERATION;
	}
	if(AudioDriver_write(samples, size) <= 0) {
		return INVALID_OPERATION;
	}
	if(AudioDriver_start() != ANDROID_AUDIOTRACK_RESULT_SUCCESS) {
		return INVALID_OPERATION;
	}
	return NO_ERROR;
}

status_t MediaPlayer::start()
{
	AVPacket				pPacket;
	int16_t*				pAudioSamples;
	AVFrame*				pFrameRGB;
	
	if (mCurrentState != MEDIA_PLAYER_PREPARED) {
		return INVALID_OPERATION;
	}
	
	if((pFrameRGB = createAndroidFrame()) == NULL) {
		return INVALID_OPERATION;
	}
	pAudioSamples = (int16_t *) av_malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
	
	mCurrentState = MEDIA_PLAYER_STARTED;
	__android_log_print(ANDROID_LOG_INFO, TAG, "playing %ix%i", mVideoWidth, mVideoHeight);
	while (mCurrentState != MEDIA_PLAYER_STOPPED && 
			mCurrentState != MEDIA_PLAYER_STATE_ERROR) {

		if(mCurrentState == MEDIA_PLAYER_PAUSED) {
			usleep(50);
			continue;
		}
		
		if(av_read_frame(mFFmpegStorage.pFormatCtx, &pPacket) < 0) {
			mCurrentState = MEDIA_PLAYER_STOPPED;
			continue;
		}
		
		pthread_mutex_lock(&mLock);
		
		// Is this a packet from the video stream?
		if (pPacket.stream_index == mFFmpegStorage.video.stream) {
			if(processVideo(&pPacket, pFrameRGB) != NO_ERROR) {
				mCurrentState = MEDIA_PLAYER_STATE_ERROR;
			}
		} else if (pPacket.stream_index == mFFmpegStorage.audio.stream) {
			/*
			if(processAudio(&pPacket, pAudioSamples, AVCODEC_MAX_AUDIO_FRAME_SIZE) != NO_ERROR) {
				mCurrentState = MEDIA_PLAYER_STATE_ERROR;
			}
			*/
		}
		
		// Free the packet that was allocated by av_read_frame
		av_free_packet(&pPacket);
		
		pthread_mutex_unlock(&mLock);
	}
	
	VideoDriver_unregister();
	//AudioDriver_unregister();
	   
	av_free(pAudioSamples);
	// Free the RGB image
	av_free(pFrameRGB);
	
	//suspend();
	
	if(mCurrentState == MEDIA_PLAYER_STATE_ERROR) {
		__android_log_print(ANDROID_LOG_INFO, TAG, "playing err");
		return INVALID_OPERATION;
	}
	mCurrentState = MEDIA_PLAYER_PLAYBACK_COMPLETE;
	__android_log_print(ANDROID_LOG_INFO, TAG, "end of playing");
	return NO_ERROR;
}

status_t MediaPlayer::stop()
{
	pthread_mutex_lock(&mLock);
	mCurrentState = MEDIA_PLAYER_STOPPED;
	pthread_mutex_unlock(&mLock);
    return NO_ERROR;
}

status_t MediaPlayer::pause()
{
	pthread_mutex_lock(&mLock);
	mCurrentState = MEDIA_PLAYER_PAUSED;
	pthread_mutex_unlock(&mLock);
	return NO_ERROR;
}

bool MediaPlayer::isPlaying()
{
    return mCurrentState == MEDIA_PLAYER_STARTED;
}

status_t MediaPlayer::getVideoWidth(int *w)
{
	if (mCurrentState != MEDIA_PLAYER_PREPARED) {
		return INVALID_OPERATION;
	}
	*w = mVideoWidth;
    return NO_ERROR;
}

status_t MediaPlayer::getVideoHeight(int *h)
{
	if (mCurrentState != MEDIA_PLAYER_PREPARED) {
		return INVALID_OPERATION;
	}
	*h = mVideoHeight;
    return NO_ERROR;
}

status_t MediaPlayer::getCurrentPosition(int *msec)
{
	return INVALID_OPERATION;
}

status_t MediaPlayer::getDuration(int *msec)
{
	if (mCurrentState != MEDIA_PLAYER_PREPARED) {
		return INVALID_OPERATION;
	}
	*msec = mDuration / 1000;
    return NO_ERROR;
}

status_t MediaPlayer::seekTo(int msec)
{
    return INVALID_OPERATION;
}

status_t MediaPlayer::reset()
{
    return INVALID_OPERATION;
}

status_t MediaPlayer::setAudioStreamType(int type)
{
	return NO_ERROR;
}

void MediaPlayer::ffmpegNotify(void* ptr, int level, const char* fmt, va_list vl) {
	
	switch(level) {
			/**
			 * Something went really wrong and we will crash now.
			 */
		case AV_LOG_PANIC:
			__android_log_print(ANDROID_LOG_ERROR, TAG, "AV_LOG_PANIC: %s", fmt);
			//sPlayer->mCurrentState = MEDIA_PLAYER_STATE_ERROR;
			break;
			
			/**
			 * Something went wrong and recovery is not possible.
			 * For example, no header was found for a format which depends
			 * on headers or an illegal combination of parameters is used.
			 */
		case AV_LOG_FATAL:
			__android_log_print(ANDROID_LOG_ERROR, TAG, "AV_LOG_FATAL: %s", fmt);
			//sPlayer->mCurrentState = MEDIA_PLAYER_STATE_ERROR;
			break;
			
			/**
			 * Something went wrong and cannot losslessly be recovered.
			 * However, not all future data is affected.
			 */
		case AV_LOG_ERROR:
			__android_log_print(ANDROID_LOG_ERROR, TAG, "AV_LOG_ERROR: %s", fmt);
			//sPlayer->mCurrentState = MEDIA_PLAYER_STATE_ERROR;
			break;
			
			/**
			 * Something somehow does not look correct. This may or may not
			 * lead to problems. An example would be the use of '-vstrict -2'.
			 */
		case AV_LOG_WARNING:
			__android_log_print(ANDROID_LOG_ERROR, TAG, "AV_LOG_WARNING: %s", fmt);
			break;
			
		case AV_LOG_INFO:
			__android_log_print(ANDROID_LOG_INFO, TAG, "%s", fmt);
			break;
			
		case AV_LOG_DEBUG:
			__android_log_print(ANDROID_LOG_DEBUG, TAG, "%s", fmt);
			break;
			
	}
}

void MediaPlayer::notify(int msg, int ext1, int ext2)
{
    __android_log_print(ANDROID_LOG_INFO, TAG, "message received msg=%d, ext1=%d, ext2=%d", msg, ext1, ext2);
	/*
    bool send = true;
    bool locked = false;

    // TODO: In the future, we might be on the same thread if the app is
    // running in the same process as the media server. In that case,
    // this will deadlock.
    //
    // The threadId hack below works around this for the care of prepare
    // and seekTo within the same process.
    // FIXME: Remember, this is a hack, it's not even a hack that is applied
    // consistently for all use-cases, this needs to be revisited.
     if (mLockThreadId != getThreadId()) {
        mLock.lock();
        locked = true;
    }

    if (mPlayer == 0) {
        __android_log_print(ANDROID_LOG_INFO, TAG, "notify(%d, %d, %d) callback on disconnected mediaplayer", msg, ext1, ext2);
        if (locked) mLock.unlock();   // release the lock when done.
        return;
    }

    switch (msg) {
    case MEDIA_NOP: // interface test message
        break;
    case MEDIA_PREPARED:
        __android_log_print(ANDROID_LOG_INFO, TAG, "prepared");
        mCurrentState = MEDIA_PLAYER_PREPARED;
        if (mPrepareSync) {
            __android_log_print(ANDROID_LOG_INFO, TAG, "signal application thread");
            mPrepareSync = false;
            mPrepareStatus = NO_ERROR;
            mSignal.signal();
        }
        break;
    case MEDIA_PLAYBACK_COMPLETE:
        __android_log_print(ANDROID_LOG_INFO, TAG, "playback complete");
        if (!mLoop) {
            mCurrentState = MEDIA_PLAYER_PLAYBACK_COMPLETE;
        }
        break;
    case MEDIA_ERROR:
        // Always log errors.
        // ext1: Media framework error code.
        // ext2: Implementation dependant error code.
        LOGE("error (%d, %d)", ext1, ext2);
        mCurrentState = MEDIA_PLAYER_STATE_ERROR;
        if (mPrepareSync)
        {
            __android_log_print(ANDROID_LOG_INFO, TAG, "signal application thread");
            mPrepareSync = false;
            mPrepareStatus = ext1;
            mSignal.signal();
            send = false;
        }
        break;
    case MEDIA_INFO:
        // ext1: Media framework error code.
        // ext2: Implementation dependant error code.
        LOGW("info/warning (%d, %d)", ext1, ext2);
        break;
    case MEDIA_SEEK_COMPLETE:
        __android_log_print(ANDROID_LOG_INFO, TAG, "Received seek complete");
        if (mSeekPosition != mCurrentPosition) {
            __android_log_print(ANDROID_LOG_INFO, TAG, "Executing queued seekTo(%d)", mSeekPosition);
            mSeekPosition = -1;
            seekTo_l(mCurrentPosition);
        }
        else {
            __android_log_print(ANDROID_LOG_INFO, TAG, "All seeks complete - return to regularly scheduled program");
            mCurrentPosition = mSeekPosition = -1;
        }
        break;
    case MEDIA_BUFFERING_UPDATE:
        __android_log_print(ANDROID_LOG_INFO, TAG, "buffering %d", ext1);
        break;
    case MEDIA_SET_VIDEO_SIZE:
        __android_log_print(ANDROID_LOG_INFO, TAG, "New video size %d x %d", ext1, ext2);
        mVideoWidth = ext1;
        mVideoHeight = ext2;
        break;
    default:
        __android_log_print(ANDROID_LOG_INFO, TAG, "unrecognized message: (%d, %d, %d)", msg, ext1, ext2);
        break;
    }

    sp<MediaPlayerListener> listener = mListener;
    if (locked) mLock.unlock();

    // this prevents re-entrant calls into client code
    if ((listener != 0) && send) {
        Mutex::Autolock _l(mNotifyLock);
        __android_log_print(ANDROID_LOG_INFO, TAG, "callback application");
        listener->notify(msg, ext1, ext2);
        __android_log_print(ANDROID_LOG_INFO, TAG, "back from callback");
    }
	*/
}