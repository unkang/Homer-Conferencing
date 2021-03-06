/*****************************************************************************
 *
 * Copyright (C) 2009 Thomas Volkert <thomas@homer-conferencing.com>
 *
 * This software is free software.
 * Your are allowed to redistribute it and/or modify it under the terms of
 * the GNU General Public License version 2 as published by the Free Software
 * Foundation.
 *
 * This source is published in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License version 2
 * along with this program. Otherwise, you can write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 * Alternatively, you find an online version of the license text under
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 *****************************************************************************/

/*
 * Purpose: Implementation of a ffmpeg based local oss audio source
 * Since:   2009-02-11
 */

//##############################################################################################################
// DEPRECATED DEPRECATED DEPRECATED DEPRECATED DEPRECATED DEPRECATED DEPRECATED DEPRECATED DEPRECATED DEPRECATED
//##############################################################################################################

#include <MediaSourceOss.h>
#include <MediaSource.h>
#include <ProcessStatisticService.h>
#include <Logger.h>

#include <cstdio>
#include <string.h>
#include <stdlib.h>

namespace Homer { namespace Multimedia {

///////////////////////////////////////////////////////////////////////////////

using namespace std;
using namespace Homer::Monitor;

///////////////////////////////////////////////////////////////////////////////

MediaSourceOss::MediaSourceOss(string pDesiredDevice):
    MediaSource("OSS: local capture")
{
    mSourceType = SOURCE_DEVICE;
    // set category for packet statistics
    ClassifyStream(DATA_TYPE_AUDIO, SOCKET_RAW);

    bool tNewDeviceSelected = false;
    SelectDevice(pDesiredDevice, MEDIA_AUDIO, tNewDeviceSelected);
    if (!tNewDeviceSelected)
    {
        LOG(LOG_INFO, "Haven't selected new oss device when creating source object");
    }
}

MediaSourceOss::~MediaSourceOss()
{
    if (mMediaSourceOpened)
        CloseGrabDevice();
}

void MediaSourceOss::getAudioDevices(AudioDevices &pAList)
{
    static bool tFirstCall = true;
    AudioDeviceDescriptor tDevice;
    string tDeviceFile;
    FILE *tFile;

    #ifdef MSO_DEBUG_PACKETS
        tFirstCall = true;
    #endif

    if (tFirstCall)
        LOG(LOG_VERBOSE, "Enumerating hardware..");

    for (int tDeviceId = -1; tDeviceId < 10; tDeviceId++)
    {
        tDeviceFile = "/dev/dsp";
        if (tDeviceId != -1)
            tDeviceFile += char(tDeviceId + 48);

        if (((tFile = fopen(tDeviceFile.c_str(), "r")) != NULL) && (fclose(tFile) != EOF))
        {
            tDevice.Name = "OSS device ";
            if (tDeviceId != -1)
                tDevice.Name += char(tDeviceId + 48);
            tDevice.Card = tDeviceFile;
            tDevice.Desc = "oss based audio device";
            tDevice.IoType = "Input/Output";

            if (tFirstCall)
                LOG(LOG_VERBOSE, "Found OSS audio device: %s", tDevice.Name.c_str());

            // add this device to the result list
            pAList.push_back(tDevice);
        }
    }
    tFirstCall = false;
}

///////////////////////////////////////////////////////////////////////////////

bool MediaSourceOss::OpenVideoGrabDevice(int pResX, int pResY, float pFps)
{
    LOG(LOG_ERROR, "Wrong media type");
    return false;
}

bool MediaSourceOss::OpenAudioGrabDevice(int pSampleRate, int pChannels)
{
    FILE                *tFile;
    int                 tResult;
    AVFormatParameters  tFormatParams;
    AVInputFormat       *tFormat;
    AVCodec             *tCodec;

    mMediaType = MEDIA_AUDIO;
    mOutputAudioChannels = pChannels;
    mOutputAudioSampleRate = pSampleRate;
    mOutputAudioFormat = AV_SAMPLE_FMT_S16; // assume we always want signed 16 bit

    LOG(LOG_VERBOSE, "Trying to open the audio source");

    if (mMediaType == MEDIA_VIDEO)
    {
        LOG(LOG_ERROR, "Wrong media type detected");
        return false;
    }

    SVC_PROCESS_STATISTIC.AssignThreadName("Audio-Grabber(OSS)");

    if (mMediaSourceOpened)
        return false;

    LOG(LOG_VERBOSE, "Desired device is \"%s\"", mDesiredDevice.c_str());

    if ((mDesiredDevice == "") || (mDesiredDevice == "auto") || (mDesiredDevice == "automatic"))
        mDesiredDevice = "";

    tFormatParams.sample_rate = mOutputAudioSampleRate; // sampling rate
    tFormatParams.channels = mOutputAudioChannels;
    tFormatParams.initial_pause = 0;
    tFormatParams.prealloced_context = 0;
    //deprecated: tFormatParams.audio_codec_id = AV_CODEC_ID_PCM_S16LE;
    tFormat = av_find_input_format("oss");
    if (tFormat == NULL)
    {
        LOG(LOG_ERROR, "Couldn't find input format");
        return false;
    }

    if (mDesiredDevice != "")
    {
        //########################################
        //### probing given device file
        //########################################
        tResult = 0;
        if (((tFile = fopen(mDesiredDevice.c_str(), "r")) == NULL) || (fclose(tFile) != 0) || ((tResult = av_open_input_file(&mFormatContext, mDesiredDevice.c_str(), tFormat, 0, &tFormatParams)) != 0))
        {
            if (tResult != 0)
                LOG(LOG_ERROR, "Couldn't open device \"%s\" because of \"%s\".", mDesiredDevice.c_str(), strerror(AVUNERROR(tResult)));
            else
                LOG(LOG_ERROR, "Couldn't find device \"%s\".", mDesiredDevice.c_str());
            return false;
        }
    }else
    {
        //########################################
        //### auto probing possible device files
        //########################################
        LOG(LOG_VERBOSE, "Auto-probing for OSS capture device");
        bool tFound = false;
        string tDesiredDevice;
        for (int tDeviceId = -1; tDeviceId < 10; tDeviceId++)
        {
            tDesiredDevice = "/dev/dsp";
            if (tDeviceId != -1)
                tDesiredDevice += char(tDeviceId + 48);
            tResult = 0;
            if (((tFile = fopen(tDesiredDevice.c_str(), "r")) != NULL) && (fclose(tFile) == 0) && ((tResult = av_open_input_file(&mFormatContext, tDesiredDevice.c_str(), tFormat, 0, &tFormatParams)) == 0))
            {
                tFound = true;
                break;
            }
        }
        if (!tFound)
        {
            LOG(LOG_WARN, "Couldn't find a fitting OSS audio device");
            return false;
        }
        mDesiredDevice = tDesiredDevice;
    }
    mCurrentDevice = mDesiredDevice;

    //######################################################
    //### retrieve stream information and find right stream
    //######################################################
    if ((tResult = av_find_stream_info(mFormatContext)) < 0)
    {
        LOG(LOG_ERROR, "Couldn't find stream information because of \"%s\".", strerror(AVUNERROR(tResult)));
        // Close the OSS audio file
        HM_avformat_close_input(mFormatContext);
        return false;
    }

    // Find the first audio stream
    mMediaStreamIndex = -1;
    for (int i = 0; i < (int)mFormatContext->nb_streams; i++)
    {
        if(mFormatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            mMediaStreamIndex = i;
            break;
        }
    }
    if (mMediaStreamIndex == -1)
    {
        LOG(LOG_ERROR, "Couldn't find a audio stream");
        // Close the OSS audio file
        HM_avformat_close_input(mFormatContext);
        return false;
    }

    //######################################################
    //### dump ffmpeg information about format
    //######################################################

    av_dump_format(mFormatContext, mMediaStreamIndex, "MediaSourceOss (audio)", false);

    // Get a pointer to the codec context for the audio stream
    mCodecContext = mFormatContext->streams[mMediaStreamIndex]->codec;

    //######################################################
    //### search for correct decoder for the video stream
    //######################################################
    if((tCodec = avcodec_find_decoder(mCodecContext->codec_id)) == NULL)
    {
        LOG(LOG_ERROR, "Couldn't find a fitting codec");
        // Close the OSS audio file
        HM_avformat_close_input(mFormatContext);
        return false;
    }

    //######################################################
    //### open the selected codec
    //######################################################
    // Inform the codec that we can handle truncated bitstreams
    // bitstreams where sample boundaries can fall in the middle of packets
    if(tCodec->capabilities & CODEC_CAP_TRUNCATED)
        mCodecContext->flags |= CODEC_FLAG_TRUNCATED;

    // Open codec
    if ((tResult = avcodec_open2(mCodecContext, tCodec, NULL)) < 0)
    {
        LOG(LOG_ERROR, "Couldn't open codec because of \"%s\".", strerror(AVUNERROR(tResult)));
        // Close the OSS audio file
        HM_avformat_close_input(mFormatContext);
        return false;
    }

    //###########################################################################################
    //### seek to the current position and drop data received during codec auto detection phase
    //##########################################################################################
    av_seek_frame(mFormatContext, mMediaStreamIndex, mFormatContext->streams[mMediaStreamIndex]->cur_dts, AVSEEK_FLAG_ANY);

    mMediaType = MEDIA_AUDIO;
    MarkOpenGrabDeviceSuccessful();

    return true;
}

bool MediaSourceOss::CloseGrabDevice()
{
    bool tResult = false;

    LOG(LOG_VERBOSE, "Going to close");

    if (mMediaType == MEDIA_VIDEO)
    {
        LOG(LOG_ERROR, "Wrong media type");
        return false;
    }

    if (mMediaSourceOpened)
    {
        CloseAll();

        tResult = true;
    }else
        LOG(LOG_INFO, "...wasn't open");

    mGrabbingStopped = false;
    mMediaType = MEDIA_UNKNOWN;

    ResetPacketStatistic();

    return tResult;
}

int MediaSourceOss::GrabChunk(void* pChunkBuffer, int& pChunkSize, bool pDropChunk)
{
    AVFrame             *tSourceFrame, *tRGBFrame;
    AVPacket            tPacket;
    int                 tSampleFinished = 0;

    // lock grabbing
    mGrabMutex.lock();

    if (pChunkBuffer == NULL)
    {
        // unlock grabbing
        mGrabMutex.unlock();

        // acknowledge failed
        MarkGrabChunkFailed("grab buffer is NULL");

        return GRAB_RES_INVALID;
    }

    if (!mMediaSourceOpened)
    {
        // unlock grabbing
        mGrabMutex.unlock();

        // acknowledge failed
        MarkGrabChunkFailed("audio source is closed");

        return GRAB_RES_INVALID;
    }

    if (mGrabbingStopped)
    {
        // unlock grabbing
        mGrabMutex.unlock();

        // acknowledge failed
        MarkGrabChunkFailed("audio source is paused");

        return GRAB_RES_INVALID;
    }

    // Read new packet
    // return 0 if OK, < 0 if error or end of file.
    do
    {
        // read next sample from audio source - blocking
        if (av_read_frame(mFormatContext, &tPacket) != 0)
        {
            // unlock grabbing
            mGrabMutex.unlock();

            // acknowledge failed
            MarkGrabChunkFailed("couldn't read next audio frame");

            return GRAB_RES_INVALID;
        }

        //LOG(LOG_INFO, "    ..and stream ID: %d", tPacket.stream_index);
        //LOG(LOG_INFO, "    ..and position: %"PRId64"", (long long int)tPacket.pos);
        //LOG(LOG_INFO, "    ..and size: %d", tPacket.size);

    }while (tPacket.stream_index != mMediaStreamIndex);

    if ((tPacket.data != NULL) && (tPacket.size > 0))
    {
        // log packet statistics
        AnnouncePacket(tPacket.size);

        //LOG(LOG_INFO, "New sample %6d with size: %d and index: %d\n", mSampleNumber, tPacket.size, tPacket.stream_index);

        if ((!pDropChunk) || (mRecording))
        {
            //LOG(LOG_INFO, "##DecodeAudio..\n");
            // Decode the next chunk of data
            int tOutputBufferSize = MEDIA_SOURCE_SAMPLES_MULTI_BUFFER_SIZE;
            int tBytesDecoded = HM_avcodec_decode_audio(mCodecContext, (int16_t *)pChunkBuffer, &tOutputBufferSize, &tPacket);
            pChunkSize = tBytesDecoded;

            // re-encode the frame and write it to file
            if (mRecording)
                RecordSamples((int16_t *)pChunkBuffer, pChunkSize);

            //LOG(LOG_INFO, "    ..with result(!= 0 => OK): %d bytes: %i\n", tFrameFinished, tBytesDecoded);

            if (tBytesDecoded <= 0)
            {
                LOG(LOG_ERROR, "Couldn't decode audio sample");
            }
        }
        av_free_packet(&tPacket);
    }

    // unlock grabbing
    mGrabMutex.unlock();

    mFrameNumber++;

    // acknowledge success
    MarkGrabChunkSuccessful(mFrameNumber);

    return mFrameNumber;
}

bool MediaSourceOss::SupportsRecording()
{
    return true;
}

string MediaSourceOss::GetSourceCodecStr()
{
    return "Raw";
}

string MediaSourceOss::GetSourceCodecDescription()
{
    return "Raw";
}

///////////////////////////////////////////////////////////////////////////////

}} //namespace
