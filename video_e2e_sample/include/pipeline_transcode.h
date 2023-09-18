/******************************************************************************\
Copyright (c) 2005-2019, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

This sample was distributed or derived from the Intel's Media Samples package.
The original version of this sample may be obtained from https://software.intel.com/en-us/intel-media-server-studio
or https://software.intel.com/en-us/media-client-solutions-support.
\**********************************************************************************/

#ifndef __SAMPLE_PIPELINE_TRANSCODE_H__
#define __SAMPLE_PIPELINE_TRANSCODE_H__

#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef ENABLE_INFERENCE
#include <opencv2/opencv.hpp>
#include <opencv2/photo/photo.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/video/video.hpp>
#endif
#include "mfxplugin.h"
//#include "mfxplugin++.h"
#include "mfxdeprecated.h"

#include <memory>
#include <vector>
#include <list>
#include <ctime>
#include <map>
#include <future>
#include <chrono>
#include <mutex>
#include <chrono>
#include <iomanip>

#include "sample_defs.h"
#include "sample_utils.h"
#include "base_allocator.h"
#include "sysmem_allocator.h"
#include "rotate_plugin_api.h"
#include "mfx_multi_vpp.h"
#include "mfxdeprecated.h"

#include "vpl/mfxvideo.h"
#include "vpl/mfxvideo++.h"
#include "vpl/mfxmvc.h"
#include "vpl/mfxjpeg.h"
//#include "vpl/mfxla.h"
#include "vpl/mfxvp8.h"

#include "hw_device.h"
//#include "plugin_loader.h"
#include "sample_defs.h"
#include "plugin_utils.h"
#include "preset_manager.h"

#ifdef ENABLE_INFERENCE
#include "media_inference_manager.h"
#endif
#include "file_and_rtsp_bitstream_reader.h"
#include "v4l2_bitstream_reader.hpp"
#include "alsa_audiostream_reader.h"

using namespace std;
#ifdef ENABLE_INFERENCE
using namespace InferenceEngine;
#endif

#if (MFX_VERSION >= 1024)
#include "brc_routines.h"
#endif

#define TIME_STATS 1 // Enable statistics processing
#include "time_statistics.h"

#if defined(_WIN32) || defined(_WIN64)
#include "decode_render.h"
#endif

#if defined(_WIN32) || defined(_WIN64)
    #define MSDK_CPU_ROTATE_PLUGIN  MSDK_STRING("sample_rotate_plugin.dll")
    #define MSDK_OCL_ROTATE_PLUGIN  MSDK_STRING("sample_plugin_opencl.dll")
#else
    #define MSDK_CPU_ROTATE_PLUGIN  MSDK_STRING("libsample_rotate_plugin.so")
    #define MSDK_OCL_ROTATE_PLUGIN  MSDK_STRING("libsample_plugin_opencl.so")
#endif

#define MAX_PREF_LEN    256
/* maximum number for jpeg encoding and saving file name*/
#define MAX_ENSAVE_FILE_NUM 999999

#ifndef MFX_VERSION
#error MFX_VERSION not defined
#endif

#ifdef ENABLE_MCTF
const mfxU16  MCTF_MID_FILTER_STRENGTH = 10;
const mfxF64  MCTF_LOSSLESS_BPP = 0.0;
#endif

namespace TranscodingSample
{
    enum PipelineMode
    {
        Native = 0,        // means that pipeline is based depends on the cmd parameters (decode/encode/transcode)
        Sink,              // means that pipeline makes decode only and put data to shared buffer
        Source,            // means that pipeline makes vpp + encode and get data from shared buffer
        VppComp,           // means that pipeline makes vpp composition + encode and get data from shared buffer
        VppCompOnly,       // means that pipeline makes vpp composition and get data from shared buffer
        VppCompOnlyEncode, // means that pipeline makes vpp composition + encode and get data from shared buffer
        FakeSink           // means that pipeline get data from share buffer and do nothing.
    };

    enum VppCompDumpMode
    {
        NULL_RENDER_VPP_COMP = 1,
        DUMP_FILE_VPP_COMP = 2
    };

    enum EFieldCopyMode
    {
        FC_NONE=0,
        FC_T2T=1,
        FC_T2B=2,
        FC_B2T=4,
        FC_B2B=8,
        FC_FR2FR=16
    };

    struct sVppCompDstRect
    {
        mfxU32 DstX;
        mfxU32 DstY;
        mfxU32 DstW;
        mfxU32 DstH;
        mfxU16 TileId;
    };

#ifdef ENABLE_MCTF
    typedef enum
    {
        VPP_FILTER_DISABLED = 0,
        VPP_FILTER_ENABLED_DEFAULT = 1,
        VPP_FILTER_ENABLED_CONFIGURED = 7

    } VPPFilterMode;

    // this is a structure with mctf-parameteres
    // that can be changed in run-time;
    struct sMctfRunTimeParam
    {
        mfxU16 FilterStrength;
    };

    struct sMctfRunTimeParams
    {
        sMctfRunTimeParams() : CurIdx(0)
        {}

        mfxU32 CurIdx;
        std::vector<sMctfRunTimeParam> RunTimeParams;
        // returns rt-param corresponding to CurIdx or NULL if
        // CurIdx is behind available info
        const sMctfRunTimeParam* GetCurParam();
        // move CurIdx forward
        void MoveForward();
        // set CurIdx to the begining; restart indexing;
        void Restart();
        // reset vector & index
        void Reset();
        // test for emptiness
        bool Empty() { return RunTimeParams.empty(); };
    };

    struct sMCTFParam
    {
        sMctfRunTimeParams   rtParams;
        mfxExtVppMctf        params;
        VPPFilterMode        mode;
    };
#endif

    enum ExtBRCType {
        EXTBRC_DEFAULT,
        EXTBRC_OFF,
        EXTBRC_ON,
        EXTBRC_IMPLICIT
    };

    struct __sInputParams
    {
        // session parameters
        bool         bIsJoin;
        mfxPriority  priority;
        // common parameters
        mfxIMPL libType;  // Type of used mediaSDK library
#if defined(LINUX32) || defined(LINUX64)
        std::string strDevicePath;
#endif
//#if (defined(_WIN32) || defined(_WIN64)) && (MFX_VERSION >= 1031)
        //Adapter type
        mfxU16 adapterType = mfxMediaAdapterType::MFX_MEDIA_UNKNOWN;
        mfxI32 dGfxIdx     = -1;
        mfxI32 adapterNum  = -1;
        //#if (defined(_WIN32) || defined(_WIN64)) && (MFX_VERSION >= 1031)
        bool bPrefferiGfx;
        bool bPrefferdGfx;
//#endif
        bool   bIsPerf;   // special performance mode. Use pre-allocated bitstreams, output
        mfxU16 nThreadsNum; // number of internal session threads number
        bool bRobustFlag;   // Robust transcoding mode. Allows auto-recovery after hardware errors
        bool bSoftRobustFlag;

        mfxU32 EncodeId; // type of output coded video
        mfxU32 DecodeId; // type of input coded video

        bool bDropDecOutput; // only works with o::raw when the file name is /dev/null

        msdk_char  strSrcFile[MSDK_MAX_FILENAME_LEN]; // source bitstream file
        msdk_char  strDstFile[MSDK_MAX_FILENAME_LEN]; // destination bitstream file
        msdk_char  strDumpVppCompFile[MSDK_MAX_FILENAME_LEN]; // VPP composition output dump file
        msdk_char  strMfxParamsDumpFile[MSDK_MAX_FILENAME_LEN];

        //For VCD
        msdk_char strV4l2SubdevName[MSDK_MAX_FILENAME_LEN];
        msdk_char strV4l2VideoDeviceName[MSDK_MAX_FILENAME_LEN];
        msdk_char strACaptureDeviceName[MSDK_MAX_FILENAME_LEN];
        msdk_char strAPlayDeviceName[MSDK_MAX_FILENAME_LEN];
        msdk_char strAMP3Name[MSDK_MAX_FILENAME_LEN];
        msdk_char strAMP4Name[MSDK_MAX_FILENAME_LEN];

        ELTDevicePort ltDevicePort;
        unsigned int vRefreshRate;

#ifdef ENABLE_INFERENCE
        //Support inference in pipeline
        int InferType; //if > 0, will run inference after decoding
        bool InferOffline; // Default false. If true, the results won't be rendered
        MediaInferenceManager::InferDeviceType InferDevType; //Target inference device
        int InferMaxObjNum; // The maximum number of detected objects for classification
        int InferInterval; //The distance of two inferenced frames
        bool InferRemoteBlob; //If OpenVINO remote blob is used.
#endif

        uint sinkNum; //The number of sink encode sessions
        uint sourceNum; //The number of source decode sessions
        bool RtspDumpOnly; // No transcoding. Only Rtsp dump is enabled.
        bool bV4l2RawInput; //The bitStream input is the raw data from v4l2 FrameWork.
        bool bAlsaAudioInput; //The audio stream input is the raw data from Alsa FrameWork.

        msdk_char  strIRFileDir[MSDK_MAX_FILENAME_LEN]; // directory that contains IR files and label file
        msdk_char  strRtspSaveFile[MSDK_MAX_FILENAME_LEN]; // save rtsp to local file
        msdk_char  strDetectResultSaveFile[MSDK_MAX_FILENAME_LEN]={'\0'}; // save detect results to local file

        // specific encode parameters
        mfxU16 nTargetUsage;
        mfxF64 dDecoderFrameRateOverride;
        mfxF64 dEncoderFrameRateOverride;
        mfxU16 EncoderPicstructOverride;
        mfxF64 dVPPOutFramerate;
        mfxU16 nBitRate;
        mfxU16 nBitRateMultiplier;
        mfxU16 nQuality; // quality parameter for JPEG encoder
        mfxU16 nDstWidth;  // destination picture width, specified if resizing required
        mfxU16 nDstHeight; // destination picture height, specified if resizing required

        mfxU16 nFrameSkip; // number of frames to drop
        msdk_char  strFtpAddr[MSDK_MAX_FILENAME_LEN]; // Ftp address

        mfxU16 nEncTileRows; // number of rows for encoding tiling
        mfxU16 nEncTileCols; // number of columns for encoding tiling

        bool bEnableDeinterlacing;
        mfxU16 DeinterlacingMode;
        int DenoiseLevel;
        int DetailLevel;
        mfxU16 FRCAlgorithm;
        EFieldCopyMode fieldProcessingMode;

        mfxU16 nAsyncDepth; // asyncronous queue

        PipelineMode eMode;
        PipelineMode eModeExt;

        mfxU32 FrameNumberPreference; // how many surfaces user wants
        mfxU32 MaxFrameNumber; // maximum frames for transcoding
        mfxU32 numSurf4Comp;
        mfxU16 numTiles4Comp;

        mfxU16 nSlices; // number of slices for encoder initialization
        mfxU16 nMaxSliceSize; //maximum size of slice

        mfxU16 WinBRCMaxAvgKbps;
        mfxU16 WinBRCSize;
        mfxU16 BufferSizeInKB;
        mfxU16 GopPicSize;
        mfxU16 GopRefDist;
        mfxU16 NumRefFrame;
        mfxU16 nBRefType;
        mfxU16 RepartitionCheckMode;
        mfxU16 GPB;
        mfxU16 nTransformSkip;

        mfxU16 CodecLevel;
        mfxU16 CodecProfile;
        mfxU16 MaxKbps;
        mfxU16 InitialDelayInKB;
        mfxU16 GopOptFlag;

        mfxU16 WeightedPred;
        mfxU16 WeightedBiPred;
        mfxU16 ExtBrcAdaptiveLTR;

        bool   bExtMBQP;

        // MVC Specific Options
        bool   bIsMVC; // true if Multi-View-Codec is in use
        mfxU32 numViews; // number of views for Multi-View-Codec

        mfxU16 nRotationAngle; // if specified, enables rotation plugin in mfx pipeline
        msdk_char strVPPPluginDLLPath[MSDK_MAX_FILENAME_LEN]; // plugin dll path and name

        sPluginParams decoderPluginParams;
        sPluginParams encoderPluginParams;

        mfxU32 nTimeout; // how long transcoding works in seconds
        mfxU32 nFPS; // limit transcoding to the number of frames per second

        mfxU32 statisticsWindowSize;
        FILE* statisticsLogFile;

        bool bLABRC; // use look ahead bitrate control algorithm
        mfxU16 nLADepth; // depth of the look ahead bitrate control  algorithm
        bool bEnableExtLA;
        bool bEnableBPyramid;
        mfxU16 nRateControlMethod;
        mfxU16 nQPI;
        mfxU16 nQPP;
        mfxU16 nQPB;
        bool bDisableQPOffset;

        mfxU16 nAvcTemp;
        mfxU16 nBaseLayerPID;
        mfxU16 nAvcTemporalLayers[8];
        mfxU16 nSPSId;
        mfxU16 nPPSId;
        mfxU16 nPicTimingSEI;
        mfxU16 nNalHrdConformance;
        mfxU16 nVuiNalHrdParameters;

        bool bOpenCL;
        mfxU16 reserved[4];

        mfxU16 nVppCompDstX;
        mfxU16 nVppCompDstY;
        mfxU16 nVppCompDstW;
        mfxU16 nVppCompDstH;
        mfxU16 nVppCompSrcW;
        mfxU16 nVppCompSrcH;
        mfxU16 nVppCompTileId;

        mfxU32 DecoderFourCC;
        mfxU32 EncoderFourCC;

        sVppCompDstRect* pVppCompDstRects;

//        bool bUseOpaqueMemory;
        bool bForceSysMem;
        mfxU16 VppOutPattern;
        mfxU16 nGpuCopyMode;

        mfxU16 nRenderColorForamt; /*0 NV12 - default, 1 is ARGB*/

        mfxI32  monitorType;
        bool shouldUseGreedyFormula;
        bool enableQSVFF;
        bool bSingleTexture;

        ExtBRCType nExtBRC;

        mfxU16 nAdaptiveMaxFrameSize;
        mfxU16 LowDelayBRC;

        mfxU16 IntRefType;
        mfxU16 IntRefCycleSize;
        mfxU16 IntRefQPDelta;
        mfxU16 IntRefCycleDist;

        mfxU32 nMaxFrameSize;

#if (MFX_VERSION >= 1025)
        mfxU16 numMFEFrames;
        mfxU16 MFMode;
        mfxU32 mfeTimeout;
#endif

#if defined(LIBVA_WAYLAND_SUPPORT)
        mfxU16 nRenderWinX;
        mfxU16 nRenderWinY;
        bool  bPerfMode;
#endif

#if defined(LIBVA_SUPPORT)
        mfxI32  libvaBackend;
#endif // defined(MFX_LIBVA_SUPPORT)

        CHWDevice             *m_hwdev;

        EPresetModes PresetMode;
        bool shouldPrintPresets;
    };

    struct sInputParams: public __sInputParams
    {
        sInputParams();
        msdk_string DumpLogFileName;
#if MFX_VERSION >= 1022
        std::vector<mfxExtEncoderROI> m_ROIData;

        bool bDecoderPostProcessing;
        bool bROIasQPMAP;
#endif //MFX_VERSION >= 1022
#ifdef ENABLE_MCTF
        sMCTFParam mctfParam;
#endif
    };

    struct PreEncAuxBuffer
    {
       mfxEncodeCtrl     encCtrl;
       mfxU16            Locked;
       mfxENCInput       encInput;
       mfxENCOutput      encOutput;
    }; 

    struct ExtendedSurface
    {
        mfxFrameSurface1 *pSurface;
        PreEncAuxBuffer  *pAuxCtrl;
        mfxEncodeCtrl    *pEncCtrl;
        mfxSyncPoint      Syncp;
    };

    struct ExtendedBS
    {
        bool IsFree = true;
        mfxBitstreamWrapper Bitstream;
        mfxSyncPoint Syncp = nullptr;
        PreEncAuxBuffer* pCtrl = nullptr;
    };

    class CIOStat : public CTimeStatistics
    {
        public:
            CIOStat()
              : CTimeStatistics()
              , ofile(stdout)
            {
                MSDK_ZERO_MEMORY(bufDir);
                DumpLogFileName.clear();
            }

            CIOStat(const msdk_char *dir)
              : CTimeStatistics()
              , ofile(stdout)
            {
                msdk_strncopy_s(bufDir, MAX_PREF_LEN, dir, MAX_PREF_LEN - 1);
                bufDir[MAX_PREF_LEN - 1] = 0;
            }

            ~CIOStat()
            {
            }

            inline void SetOutputFile(FILE *file)
            {
                ofile = file;
            }

            inline void SetDumpName(const msdk_char* name)
            {
                DumpLogFileName = name;
                if (!DumpLogFileName.empty())
                {
                    TurnOnDumping();
                }
                else
                {
                    TurnOffDumping();
                }
            }

            inline void SetDirection(const msdk_char *dir)
            {
                if (dir)
                {
                    msdk_strncopy_s(bufDir, MAX_PREF_LEN, dir, MAX_PREF_LEN - 1);
                    bufDir[MAX_PREF_LEN - 1] = 0;
                }
            }

            inline void PrintStatistics(mfxU32 numPipelineid, mfxF64 target_framerate = -1 /*default stands for infinite*/)
            {
                // print timings in ms
                msdk_fprintf(   ofile, MSDK_STRING("stat[%u.%llu]: %s=%d;Framerate=%.3f;Total=%.3lf;Samples=%lld;StdDev=%.3lf;Min=%.3lf;Max=%.3lf;Avg=%.3lf\n"),
                                msdk_get_current_pid(), rdtsc(),
                                bufDir, numPipelineid,
                                target_framerate,
                                GetTotalTime(false), GetNumMeasurements(),
                                GetTimeStdDev(false), GetMinTime(false), GetMaxTime(false), GetAvgTime(false));
                fflush(ofile);

                if(!DumpLogFileName.empty())
                {
                    msdk_char buf[MSDK_MAX_FILENAME_LEN];
                    msdk_sprintf(buf, MSDK_STRING("%s_ID_%d.log"), DumpLogFileName.c_str(), numPipelineid);
                    DumpDeltas(buf);
                }
            }

            inline void DumpDeltas(msdk_char* file_name)
            {
                if (m_time_deltas.empty())
                    return;

                FILE* dump_file = NULL;
                if (!MSDK_FOPEN(dump_file, file_name, MSDK_STRING("a")) && (dump_file != NULL))
                {
                    for (std::vector<mfxF64>::const_iterator it = m_time_deltas.begin(); it != m_time_deltas.end(); ++it)
                    {
                        fprintf(dump_file, "%.3f, ", (*it));
                    }
                }
                else
                {
                    perror("DumpDeltas: file cannot be open");
                    if (dump_file != NULL)
                        fclose(dump_file);
                }
            }
        protected:
            msdk_tstring DumpLogFileName;
            FILE*     ofile;
            msdk_char bufDir[MAX_PREF_LEN];
    };


    class ExtendedBSStore
    {
    public:
        explicit ExtendedBSStore(mfxU32 size)
        {
            m_pExtBS.resize(size);
        }
        virtual ~ExtendedBSStore()
        {
            m_pExtBS.clear();

        }
        ExtendedBS* GetNext()
        {
            for (mfxU32 i=0; i < m_pExtBS.size(); i++)
            {
                if (m_pExtBS[i].IsFree)
                {
                    m_pExtBS[i].IsFree = false;
                    return &m_pExtBS[i];
                }
            }
            return NULL;
        }
        void Release(ExtendedBS* pBS)
        {
            for (mfxU32 i=0; i < m_pExtBS.size(); i++)
            {
                if (&m_pExtBS[i] == pBS)
                {
                    m_pExtBS[i].IsFree = true;
                    return;
                }
            }
            return;
        }
        void ReleaseAll()
        {
            for (mfxU32 i = 0; i < m_pExtBS.size(); i++)
            {
                m_pExtBS[i].IsFree = true;
            }
            return;
        }
        void FlushAll()
        {
            for (mfxU32 i = 0; i < m_pExtBS.size(); i++)
            {
                m_pExtBS[i].Bitstream.DataLength = 0;
                m_pExtBS[i].Bitstream.DataOffset = 0;
            }
            return;
        }
    protected:
        std::vector<ExtendedBS> m_pExtBS;

    private:
        DISALLOW_COPY_AND_ASSIGN(ExtendedBSStore);
    };

    class CTranscodingPipeline;
    // thread safety buffer heterogeneous pipeline
    // only for join sessions
    class SafetySurfaceBuffer
    {
    public:
        struct SurfaceDescriptor
        {
            ExtendedSurface   ExtSurface;
            mfxU32            Locked;
        };

        SafetySurfaceBuffer(SafetySurfaceBuffer *pNext);
        virtual ~SafetySurfaceBuffer();

        mfxU32            GetLength();
        mfxStatus         WaitForSurfaceRelease(mfxU32 msec);
        mfxStatus         WaitForSurfaceInsertion(mfxU32 msec);
        void              AddSurface(ExtendedSurface Surf);
        mfxStatus         GetSurface(ExtendedSurface &Surf);
        mfxStatus         ReleaseSurface(mfxFrameSurface1* pSurf);
        mfxStatus         ReleaseSurfaceAll();
        void              CancelBuffering();

        SafetySurfaceBuffer         *m_pNext;

    protected:

        std::mutex                   m_mutex;
        std::list<SurfaceDescriptor> m_SList;
        bool                         m_IsBufferingAllowed;
        MSDKEvent*                   pRelEvent;
        MSDKEvent*                   pInsEvent;
    private:
        DISALLOW_COPY_AND_ASSIGN(SafetySurfaceBuffer);
    };

    class FileBitstreamProcessor
    {
    public:
        FileBitstreamProcessor();
        virtual ~FileBitstreamProcessor();
        virtual mfxStatus SetReader(std::unique_ptr<FileAndRTSPBitstreamReader>& reader);
        virtual mfxStatus SetReader(std::unique_ptr<CSmplYUVReader>& reader);
        virtual mfxStatus SetReader(std::unique_ptr<V4l2BitstreamReader>& reader);
        virtual mfxStatus SetReader(std::unique_ptr<AlsaAudioStreamReader>& reader);
        virtual mfxStatus SetWriter(std::unique_ptr<CSmplBitstreamWriter>& writer);
        virtual mfxStatus SetWriterFileName(msdk_char *pFileName);
        virtual mfxStatus SetMp4FileName(msdk_char* pFileName, int videoWidth, int videoHeight);
        virtual mfxStatus GetInputBitstream(mfxBitstreamWrapper **pBitstream);
        virtual mfxStatus GetInputFrame(mfxFrameSurface1 *pSurface);
        virtual mfxStatus GetV4l2InputFrame(mfxFrameSurface1* pSurface);
        virtual mfxStatus QueryV4l2FrameInfo(v4l2FrameInfo& frameInfo);
        virtual mfxStatus ProcessOutputBitstream(mfxBitstreamWrapper* pBitstream);
        virtual mfxStatus StartAudioStreamProcess();
        virtual mfxStatus ResetInput();
        virtual mfxStatus ResetOutput();

    protected:
        std::unique_ptr<CSmplBitstreamReader> m_pFileReader;
        std::unique_ptr<CSmplYUVReader> m_pYUVFileReader;
        std::unique_ptr<V4l2BitstreamReader> m_pV4l2Reader;
        std::unique_ptr<AlsaAudioStreamReader> m_pAlsaReader;
        // for performance options can be zero
        std::unique_ptr<CSmplBitstreamWriter> m_pFileWriter;

        mfxBitstreamWrapper m_Bitstream;
        AVMP4Muxer* m_pAVMP4Muxer;
    private:
        DISALLOW_COPY_AND_ASSIGN(FileBitstreamProcessor);
    };

    // Bitstream is external via BitstreamProcessor
    class CTranscodingPipeline
    {
    public:
        CTranscodingPipeline();
        virtual ~CTranscodingPipeline();

        virtual mfxStatus Init(sInputParams *pParams,
                               MFXFrameAllocator *pMFXAllocator,
                               void* hdl,
                               CTranscodingPipeline *pParentPipeline,
                               SafetySurfaceBuffer  **pBuffer,
                               FileBitstreamProcessor   *pBSProc,
                               VPLImplementationLoader* mfxLoader);

        // frames allocation is suspended for heterogeneous pipeline
        virtual mfxStatus CompleteInit();
        virtual void      Close();
        virtual mfxStatus Reset(VPLImplementationLoader* mfxLoader);
        virtual mfxStatus Join(MFXVideoSession *pChildSession);
        virtual mfxStatus Run();
        virtual mfxStatus FlushLastFrames(){return MFX_ERR_NONE;}

        mfxU32 GetProcessFrames() {return m_nProcessedFramesNum;}

        bool   GetJoiningFlag() {return m_bIsJoinSession;}

        mfxStatus QueryMFXVersion(mfxVersion *version)
        { MSDK_CHECK_POINTER(m_pmfxSession.get(), MFX_ERR_NULL_PTR); return m_pmfxSession->QueryVersion(version); };
        inline mfxU32 GetPipelineID(){return m_nID;}
        inline void SetPipelineID(mfxU32 id){m_nID = id;}
        void StopSession();
        bool IsOverlayUsed();
        size_t GetRobustFlag();

        msdk_string GetSessionText()
        {
            msdk_stringstream ss;
            ss << m_pmfxSession->operator mfxSession();

            return ss.str();
        }
#if (defined(_WIN32) || defined(_WIN64)) && (MFX_VERSION >= 1031)
        //Adapter type
        void SetPrefferiGfx(bool prefferiGfx) { bPrefferiGfx = prefferiGfx; };
        void SetPrefferdGfx(bool prefferdGfx) { bPrefferdGfx = prefferdGfx; };
        bool IsPrefferiGfx() { return bPrefferiGfx; };
        bool IsPrefferdGfx() { return bPrefferdGfx; };
#endif
#ifdef ENABLE_INFERENCE
        void SetVADisplayHandle(VADisplay vaDpy) {mVADpy = vaDpy;};
#endif
    protected:
        virtual mfxStatus CheckRequiredAPIVersion(mfxVersion& version, sInputParams *pParams);

        virtual mfxStatus Decode();
        virtual mfxStatus Encode();
        virtual mfxStatus DummyFakeSink();
        virtual mfxStatus Transcode();
        virtual mfxStatus DecodeOneFrame(ExtendedSurface *pExtSurface);
        virtual mfxStatus DecodeLastFrame(ExtendedSurface *pExtSurface);
        virtual mfxStatus VPPOneFrame(ExtendedSurface *pSurfaceIn, ExtendedSurface *pExtSurface);
        virtual mfxStatus EncodeOneFrame(ExtendedSurface *pExtSurface, mfxBitstreamWrapper *pBS);
//        virtual mfxStatus PreEncOneFrame(ExtendedSurface *pInSurface, ExtendedSurface *pOutSurface);

        virtual mfxStatus DecodePreInit(sInputParams *pParams);
        virtual mfxStatus VPPPreInit(sInputParams *pParams);
        virtual mfxStatus EncodePreInit(sInputParams *pParams);
//        virtual mfxStatus PreEncPreInit(sInputParams *pParams);

        mfxVideoParam GetDecodeParam();

 /*       mfxExtOpaqueSurfaceAlloc GetDecOpaqueAlloc()
        {
            mfxExtOpaqueSurfaceAlloc* opaq = m_pmfxVPP ? m_mfxVppParams : m_mfxDecParams;
            return opaq ? *opaq : mfxExtOpaqueSurfaceAlloc();
        };  */

        mfxExtMVCSeqDesc GetDecMVCSeqDesc()
        {
            mfxExtMVCSeqDesc* mvc = m_mfxDecParams;
            return mvc ? *mvc : mfxExtMVCSeqDesc();
        }

        static void ModifyParamsUsingPresets(sInputParams& params, mfxF64 fps, mfxU32 width, mfxU32 height);

        // alloc frames for all component
        mfxStatus AllocFrames(mfxFrameAllocRequest  *pRequest, bool isDecAlloc);
        mfxStatus AllocFrames();

//        mfxStatus CorrectPreEncAuxPool(mfxU32 num_frames_in_pool);
//        mfxStatus AllocPreEncAuxPool();

        // need for heterogeneous pipeline
        mfxStatus CalculateNumberOfReqFrames(mfxFrameAllocRequest  &pRequestDecOut, mfxFrameAllocRequest  &pRequestVPPOut);
        void      CorrectNumberOfAllocatedFrames(mfxFrameAllocRequest  *pNewReq);
        void      FreeFrames();

//        void      FreePreEncAuxPool();
        mfxStatus LoadStaticSurface();

        mfxFrameSurface1* GetFreeSurface(bool isDec, mfxU64 timeout);
        mfxU32 GetFreeSurfacesCount(bool isDec);
//CorrectPreEncAuxPool        PreEncAuxBuffer*  GetFreePreEncAuxBuffer();
        void SetEncCtrlRT(ExtendedSurface& extSurface, bool bInsertIDR);

        // parameters configuration functions
        mfxStatus InitDecMfxParams(sInputParams *pInParams);
        mfxStatus InitVppMfxParams(sInputParams *pInParams);
        virtual mfxStatus InitEncMfxParams(sInputParams *pInParams);
        mfxStatus InitPluginMfxParams(sInputParams *pInParams);
//        mfxStatus InitPreEncMfxParams(sInputParams *pInParams);

        void FillFrameInfoForEncoding(mfxFrameInfo& info, sInputParams *pInParams);

        mfxStatus AllocAndInitVppDoNotUse(sInputParams *pInParams);
        mfxStatus AllocMVCSeqDesc();
//        mfxStatus InitOpaqueAllocBuffers();

        void FreeVppDoNotUse();
        void FreeMVCSeqDesc();

        mfxStatus AllocateSufficientBuffer(mfxBitstreamWrapper* pBS);
        mfxStatus PutBS();

        mfxStatus DumpSurface2File(mfxFrameSurface1* pSurface);
        mfxStatus Surface2BS(ExtendedSurface* pSurf,mfxBitstreamWrapper* pBS, mfxU32 fourCC);
        mfxStatus NV12toBS(mfxFrameSurface1* pSurface,mfxBitstreamWrapper* pBS);
        mfxStatus NV12asI420toBS(mfxFrameSurface1* pSurface, mfxBitstreamWrapper* pBS);
        mfxStatus RGB4toBS(mfxFrameSurface1* pSurface,mfxBitstreamWrapper* pBS);
        mfxStatus YUY2toBS(mfxFrameSurface1* pSurface,mfxBitstreamWrapper* pBS);

        msdk_char *FindChar(msdk_char *strSrcString, mfxU16 nStrLen, msdk_char chr);

    /* Add sequence number as filename suffix to save*/
        mfxStatus AddSuffixToFilename(msdk_char *strFileName, msdk_char* strSuffixFileName);

        void NoMoreFramesSignal();
        mfxStatus AddLaStreams(mfxU16 width, mfxU16 height);

//        void LockPreEncAuxBuffer(PreEncAuxBuffer* pBuff);
//        void UnPreEncAuxBuffer  (PreEncAuxBuffer* pBuff);

        mfxU32 GetNumFramesForReset();
        void   SetNumFramesForReset(mfxU32 nFrames);

        void   HandlePossibleGpuHang(mfxStatus& sts);

        mfxStatus   SetAllocatorAndHandleIfRequired();
//        mfxStatus   LoadGenericPlugin();

        mfxBitstreamWrapper *m_pmfxBS;  // contains encoded input data

        mfxVersion m_Version; // real API version with which library is initialized

        mfxLoader m_mfxLoader;

//        std::unique_ptr<MFXVideoSession>  m_pmfxSession;
        std::unique_ptr<MainVideoSession>  m_pmfxSession;
        std::unique_ptr<MFXVideoDECODE>   m_pmfxDEC;
        std::unique_ptr<MFXVideoENCODE>   m_pmfxENC;
        std::unique_ptr<MFXVideoMultiVPP> m_pmfxVPP; // either VPP or VPPPlugin which wraps [VPP]-Plugin-[VPP] pipeline
//        std::unique_ptr<MFXVideoENC>      m_pmfxPreENC;
//        std::unique_ptr<MFXVideoUSER>     m_pUserDecoderModule;
//        std::unique_ptr<MFXVideoUSER>     m_pUserEncoderModule;
//        std::unique_ptr<MFXVideoUSER>     m_pUserEncModule;
//        std::unique_ptr<MFXPlugin>        m_pUserDecoderPlugin;
//        std::unique_ptr<MFXPlugin>        m_pUserEncoderPlugin;
//        std::unique_ptr<MFXPlugin>        m_pUserEncPlugin;

        mfxFrameAllocResponse           m_mfxDecResponse;  // memory allocation response for decoder
        mfxFrameAllocResponse           m_mfxEncResponse;  // memory allocation response for encoder

        MFXFrameAllocator              *m_pMFXAllocator;
        void*                           m_hdl; // Diret3D device manager
        bool                            m_bIsInterOrJoined;

        mfxU32                          m_numEncoders;
        mfxU32                          m_encoderFourCC;

        CSmplYUVWriter                  m_dumpVppCompFileWriter;
        mfxU32                          m_vppCompDumpRenderMode;

#if defined(_WIN32) || defined(_WIN64)
        CDecodeD3DRender*               m_hwdev4Rendering;
#else
        CHWDevice*                      m_hwdev4Rendering;
#endif

        typedef std::vector<mfxFrameSurface1*> SurfPointersArray;
        SurfPointersArray  m_pSurfaceDecPool;
        SurfPointersArray  m_pSurfaceEncPool;
        mfxU16 m_EncSurfaceType; // actual type of encoder surface pool
        mfxU16 m_DecSurfaceType; // actual type of decoder surface pool

//        typedef std::vector<PreEncAuxBuffer>    PreEncAuxArray;
//        PreEncAuxArray                          m_pPreEncAuxPool;

        // transcoding pipeline specific
        typedef std::list<ExtendedBS*>       BSList;
        BSList  m_BSPool;

        mfxInitParamlWrap              m_initPar;

        volatile bool                  m_bForceStop;

        sPluginParams                  m_decoderPluginParams;
        sPluginParams                  m_encoderPluginParams;

        MfxVideoParamsWrapper          m_mfxDecParams;
        MfxVideoParamsWrapper          m_mfxEncParams;
        MfxVideoParamsWrapper          m_mfxVppParams;
        MfxVideoParamsWrapper          m_mfxPluginParams;
        bool                           m_bIsVpp; // true if there's VPP in the pipeline
        bool                           m_bIsFieldWeaving;
        bool                           m_bIsFieldSplitting;
        bool                           m_bIsPlugin; //true if there's Plugin in the pipeline
        RotateParam                    m_RotateParam;
        MfxVideoParamsWrapper          m_mfxPreEncParams;
        mfxU32                         m_nTimeout;
        bool                           m_bUseOverlay;
        bool                           m_bV4l2RawInput; //The bitStream input is the raw data from v4l2 device.
        bool                           m_bAlsaAudioInput;
        bool                           m_bAudiostarted;

        bool                           m_bDropDecOutput; // only works with o::raw when the file name is /dev/null

        bool                           m_bROIasQPMAP;
        bool                           m_bExtMBQP;
        // various external buffers
        bool m_bOwnMVCSeqDescMemory; // true if the pipeline owns memory allocated for MVCSeqDesc structure fields

        // to enable to-do list
        // number of video enhancement filters (denoise, procamp, detail, video_analysis, multi_view, ste, istab, tcc, ace, svc)
        //constexpr static uint32_t ENH_FILTERS_COUNT = 20;
#ifndef ENH_FILTERS_COUNT
#define ENH_FILTERS_COUNT    (20)
#endif
        mfxU32                       m_tabDoUseAlg[ENH_FILTERS_COUNT];

        mfxU32         m_nID;
        mfxU16         m_AsyncDepth;
        mfxU32         m_nProcessedFramesNum;
        msdk_char      m_strDetectResultSaveFile[MSDK_MAX_FILENAME_LEN]={'\0'}; // save detect results to local file
        /* add for jpeg transcoding */
        mfxU16         m_nFrameSkip; // number of frames to skip
        msdk_char      m_strBSFile[MSDK_MAX_FILENAME_LEN]; // destination bitstream file
        mfxU32         m_nEncodedFramesNum;

        bool           m_bIsJoinSession;

        bool           m_bDecodeEnable;
        bool           m_bEncodeEnable;
        mfxU32         m_nVPPCompEnable;
        mfxI32         m_libvaBackend;

//        bool           m_bUseOpaqueMemory; // indicates if opaque memory is used in the pipeline

        mfxSyncPoint   m_LastDecSyncPoint;

        SafetySurfaceBuffer   *m_pBuffer;
        SafetySurfaceBuffer   **m_pBufferArray;
        CTranscodingPipeline  *m_pParentPipeline;

        mfxFrameAllocRequest   m_Request;
        bool                   m_bIsInit;

        mfxU32          m_NumFramesForReset;
        std::mutex      m_mReset;
        std::mutex      m_mStopSession;
        bool            m_bRobustFlag;
        bool            m_bSoftGpuHangRecovery;

        bool isHEVCSW;

        bool m_bInsertIDR;

        std::unique_ptr<ExtendedBSStore>        m_pBSStore;

        mfxU32                                m_FrameNumberPreference;
        mfxU32                                m_MaxFramesForTranscode;

        // pointer to already extended bs processor
        FileBitstreamProcessor                   *m_pBSProcessor;

        msdk_tick m_nReqFrameTime; // time required to transcode one frame

        mfxU32    statisticsWindowSize; // Sliding window size for Statistics
        mfxU32    m_nOutputFramesNum;

        CIOStat inputStatistics;
        CIOStat outputStatistics;

        bool shouldUseGreedyFormula;

#if MFX_VERSION >= 1022
        // ROI data
        std::vector<mfxExtEncoderROI> m_ROIData;
        mfxU32         m_nSubmittedFramesNum;

        // ROI with MBQP map data
        bool              m_bUseQPMap;

        std::map<void*, mfxExtMBQP> m_bufExtMBQP;
        std::map<void*, std::vector<mfxU8> > m_qpMapStorage;
        std::map<void*, std::vector<mfxExtBuffer*> > m_extBuffPtrStorage;
        std::map<void*, mfxEncodeCtrl > encControlStorage;

        mfxU32            m_QPmapWidth;
        mfxU32            m_QPmapHeight;
        mfxU32            m_GOPSize;
        mfxU32            m_QPforI;
        mfxU32            m_QPforP;

        msdk_string       m_sGenericPluginPath;
        mfxU16            m_nRotationAngle;

        msdk_string       m_strMfxParamsDumpFile;

        void FillMBQPBuffer(mfxExtMBQP &qpMap, mfxU16 pictStruct);
#endif //MFX_VERSION >= 1022

#ifdef  ENABLE_MCTF
        sMctfRunTimeParams   m_MctfRTParams;
#endif
#if (defined(_WIN32) || defined(_WIN64)) && (MFX_VERSION >= 1031)
        //Adapter type
        bool bPrefferiGfx;
        bool bPrefferdGfx;
#endif
        int mInferType; //if > 0, will run inference after decoding
        int mInferOffline; // If true, the results won't be rendered
        msdk_char  mStrIRFileDir[MSDK_MAX_FILENAME_LEN]; // directory that contains IR files and label file
#ifdef ENABLE_INFERENCE
        MediaInferenceManager mInferMnger;
        MediaInferenceManager::InferDeviceType mInferDevType;
        int mInferMaxObjNum;  // The maximum number of detected objects for classification
        int mInferInterval; // The distance between two inferenced frame
        int m_decOutW;  //The width of SFC or VPP output
        int m_decOutH;  //The height of SFC or VPP output
        bool mRemoteBlob; //If OpenVINO remote blob is enabled
        bool mInferRGBP; //Decode output is in RGBP format and no csc is needed.
        VADisplay mVADpy;
#endif
        int mNumSurf4Comp; //Number of input surfaces for N to 1 composition
        uint m_sinkNum;    //Number of sink sessions that consume the surface
        uint m_sourceNum;  //Number of source sessions that produce the surface

        chrono::high_resolution_clock::time_point time1;
        chrono::high_resolution_clock::time_point time2;
        bool isMark = true;

    private:
        DISALLOW_COPY_AND_ASSIGN(CTranscodingPipeline);

    };

    struct ThreadTranscodeContext
    {
        // Pointer to the session's pipeline
        std::unique_ptr<CTranscodingPipeline> pPipeline;
        // Pointer to bitstream handling object
        FileBitstreamProcessor *pBSProcessor = nullptr;
        // Session implementation type
        mfxIMPL implType = MFX_IMPL_AUTO;

        // Session's starting status
        mfxStatus startStatus = MFX_ERR_NONE;
        // Session's working time
        mfxF64 working_time = 0;

        // Number of processed frames
        mfxU32 numTransFrames = 0;
        // Status of the finished session
        mfxStatus transcodingSts = MFX_ERR_NONE;

        // Thread handle
        std::future<void> handle;

        void TranscodeRoutine()
        {
            using namespace std::chrono;
            MSDK_CHECK_POINTER_NO_RET(pPipeline);
            transcodingSts = MFX_ERR_NONE;

            auto start_time = system_clock::now();
            while (MFX_ERR_NONE == transcodingSts)
            {
                transcodingSts = pPipeline->Run();
            }
            working_time = duration_cast<duration<mfxF64>>(system_clock::now() - start_time).count();

            MSDK_IGNORE_MFX_STS(transcodingSts, MFX_WRN_VALUE_NOT_CHANGED);
            numTransFrames = pPipeline->GetProcessFrames();
        }
    };
}

#endif
