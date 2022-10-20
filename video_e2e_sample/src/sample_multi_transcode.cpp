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

#include "mfx_samples_config.h"
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

#include "sample_multi_transcode.h"
#include <iomanip>

#define VIDEO_E2E_MAX_DISPLAY 4

#if defined(LIBVA_WAYLAND_SUPPORT)
#include "class_wayland.h"
#endif

#ifndef MFX_VERSION
#error MFX_VERSION not defined
#endif

#include <future>
#include <stdlib.h>
using namespace std;
using namespace TranscodingSample;

#if (defined(_WIN32) || defined(_WIN64)) && (MFX_VERSION >= 1031)
mfxU32 GetPreferredAdapterNum(const mfxAdaptersInfo & adapters, const sInputParams & params)
{
    if (adapters.NumActual == 0 || !adapters.Adapters)
        return 0;

    if (params.bPrefferdGfx)
    {
        // Find dGfx adapter in list and return it's index

        auto idx = std::find_if(adapters.Adapters, adapters.Adapters + adapters.NumActual,
            [](const mfxAdapterInfo info)
        {
            return info.Platform.MediaAdapterType == mfxMediaAdapterType::MFX_MEDIA_DISCRETE;
        });

        // No dGfx in list
        if (idx == adapters.Adapters + adapters.NumActual)
        {
            msdk_printf(MSDK_STRING("Warning: No dGfx detected on machine. Will pick another adapter\n"));
            return 0;
        }

        return static_cast<mfxU32>(std::distance(adapters.Adapters, idx));
    }

    if (params.bPrefferiGfx)
    {
        // Find iGfx adapter in list and return it's index

        auto idx = std::find_if(adapters.Adapters, adapters.Adapters + adapters.NumActual,
            [](const mfxAdapterInfo info)
        {
            return info.Platform.MediaAdapterType == mfxMediaAdapterType::MFX_MEDIA_INTEGRATED;
        });

        // No iGfx in list
        if (idx == adapters.Adapters + adapters.NumActual)
        {
            msdk_printf(MSDK_STRING("Warning: No iGfx detected on machine. Will pick another adapter\n"));
            return 0;
        }

        return static_cast<mfxU32>(std::distance(adapters.Adapters, idx));
    }

    // Other ways return 0, i.e. best suitable detected by dispatcher
    return 0;
}
#endif

bool GlobalValue::AppStop = false;

Launcher::Launcher():
    m_parser(),
    m_pThreadContextArray(),
    m_pAllocArray(),
    m_InputParamsArray(),
    m_pBufferArray(),
    m_pExtBSProcArray(),
    m_pAllocParams(),
    m_hwdevs(),
    m_accelerationMode(MFX_ACCEL_MODE_NA),
    m_pLoader(),
    m_StartTime(0),
    m_eDevType(static_cast<mfxHandleType>(0))
{
} // Launcher::Launcher()

Launcher::~Launcher()
{
    Close();
} // Launcher::~Launcher()

CTranscodingPipeline* CreatePipeline()
{
    return new CTranscodingPipeline;
}

mfxStatus Launcher::Init(int argc, msdk_char *argv[])
{
    mfxStatus sts;
    mfxU32 i = 0;
    SafetySurfaceBuffer* pBuffer = NULL;
    mfxU32 BufCounter = 0;
    mfxHDL hdl = NULL;
    std::vector<mfxHDL> hdls;
    sInputParams    InputParams;
    bool lowLatencyMode      = true;
    bool bNeedToCreateDevice = true;

    //parent transcode pipeline
    CTranscodingPipeline *pParentPipeline = NULL;
    // source transcode pipeline use instead parent in heterogeneous pipeline
    CTranscodingPipeline *pSinkPipeline = NULL;

    // parse input par file
    sts = m_parser.ParseCmdLine(argc, argv);
    MSDK_CHECK_PARSE_RESULT(sts, MFX_ERR_NONE, sts);
    if(sts == MFX_WRN_OUT_OF_RANGE)
    {
        // There's no error in parameters parsing, but we should not continue further. For instance, in case of -? option
        return sts;
    }


    // get parameters for each session from parser
    while(m_parser.GetNextSessionParams(InputParams))
    {
        m_InputParamsArray.push_back(InputParams);
    }

    //If one session enables RTSP dump only mode(no transcoding), the other
    //sessions(in the same par file) must enable RTSP dump only mode as well.
    sts = VerifyRtspDumpOptions();
    MSDK_CHECK_STATUS(sts, "VerifyRtspDumpOptions failed");
    
    if (m_InputParamsArray[0].RtspDumpOnly)
    {
        //RTSP dump only mode only need two parameters
        return MFX_ERR_NONE;
    }

    // check correctness of input parameters
    sts = VerifyCrossSessionsOptions();
    MSDK_CHECK_STATUS(sts, "VerifyCrossSessionsOptions failed");

    m_pLoader.reset(new VPLImplementationLoader);
    /*m_pLoader->ConfigureVersion({ { 0, 2 } });

    if (m_InputParamsArray[0].dGfxIdx >= 0) {
        m_pLoader->SetDiscreteAdapterIndex(m_InputParamsArray[0].dGfxIdx);
    }
    else
        m_pLoader->SetAdapterType(m_InputParamsArray[0].adapterType); */

#ifdef ONEVPL_EXPERIMENTAL
    if (m_InputParamsArray[0].PCIDeviceSetup)
        m_pLoader->SetPCIDevice(m_InputParamsArray[0].PCIDomain,
                                m_InputParamsArray[0].PCIBus,
                                m_InputParamsArray[0].PCIDevice,
                                m_InputParamsArray[0].PCIFunction);

    #if defined(_WIN32)
    if (m_InputParamsArray[0].luid.HighPart > 0 || m_InputParamsArray[0].luid.LowPart > 0)
        m_pLoader->SetupLUID(m_InputParamsArray[0].luid);
    #endif
#endif

    if (m_InputParamsArray[0].adapterNum >= 0)
        m_pLoader->SetAdapterNum(m_InputParamsArray[0].adapterNum);

    /*sts = m_pLoader->ConfigureAndEnumImplementations(m_InputParamsArray[0].libType,
                                                     m_accelerationMode,
                                                     lowLatencyMode);*/

    sts = m_pLoader->ConfigureAndEnumImplementations(2,
                                                     MFX_ACCEL_MODE_VIA_VAAPI,
                                                     true);
    MSDK_CHECK_STATUS(sts, "EnumImplementations failed");

    VADisplay vaDpy=NULL;
    for (i = 0; i < m_InputParamsArray.size(); i++) {
         /* In the case of joined sessions, need to create device only for a zero session
         * In the case of a shared buffer, need to create device only for decode */
        if ((m_InputParamsArray[i].bIsJoin && i != 0) || m_InputParamsArray[i].eMode == Source)
            bNeedToCreateDevice = false;


#if (defined(_WIN32) || defined(_WIN64)) && (MFX_VERSION >= 1031)
    // check available adapters
    sts = QueryAdapters();
    MSDK_CHECK_STATUS(sts, "QueryAdapters failed");
#endif

#if defined(_WIN32) || defined(_WIN64)
    if (m_eDevType == MFX_HANDLE_D3D9_DEVICE_MANAGER)
    {
        m_pAllocParam.reset(new D3DAllocatorParams);
        m_hwdev.reset(new CD3D9Device());
        /* The last param set in vector always describe VPP+ENCODE or Only VPP
         * So, if we want to do rendering we need to do pass HWDev to CTranscodingPipeline */
        if (m_InputParamsArray[m_InputParamsArray.size() -1].eModeExt == VppCompOnly)
        {
            /* Rendering case */
            sts = m_hwdev->Init(NULL, 1, MSDKAdapter::GetNumber(0,MFX_IMPL_VIA_D3D9) );
            m_InputParamsArray[m_InputParamsArray.size() -1].m_hwdev = m_hwdev.get();
        }
        else /* NO RENDERING*/
        {
            sts = m_hwdev->Init(NULL, 0, MSDKAdapter::GetNumber(0,MFX_IMPL_VIA_D3D9) );
        }
        MSDK_CHECK_STATUS(sts, "m_hwdev->Init failed");
        sts = m_hwdev->GetHandle(MFX_HANDLE_D3D9_DEVICE_MANAGER, (mfxHDL*)&hdl);
        MSDK_CHECK_STATUS(sts, "m_hwdev->GetHandle failed");
        // set Device Manager to external dx9 allocator
        D3DAllocatorParams *pD3DParams = dynamic_cast<D3DAllocatorParams*>(m_pAllocParam.get());
        pD3DParams->pManager =(IDirect3DDeviceManager9*)hdl;
    }
#if MFX_D3D11_SUPPORT
    else if (m_eDevType == MFX_HANDLE_D3D11_DEVICE)
    {

        m_pAllocParam.reset(new D3D11AllocatorParams);
        m_hwdev.reset(new CD3D11Device());
        /* The last param set in vector always describe VPP+ENCODE or Only VPP
         * So, if we want to do rendering we need to do pass HWDev to CTranscodingPipeline */
        if (m_InputParamsArray[m_InputParamsArray.size() -1].eModeExt == VppCompOnly)
        {
            /* Rendering case */
            sts = m_hwdev->Init(NULL, 1, MSDKAdapter::GetNumber(0,MFX_IMPL_VIA_D3D11) );
            m_InputParamsArray[m_InputParamsArray.size() -1].m_hwdev = m_hwdev.get();
        }
        else /* NO RENDERING*/
        {
            sts = m_hwdev->Init(NULL, 0, MSDKAdapter::GetNumber(0,MFX_IMPL_VIA_D3D11) );
        }
        MSDK_CHECK_STATUS(sts, "m_hwdev->Init failed");
        sts = m_hwdev->GetHandle(MFX_HANDLE_D3D11_DEVICE, (mfxHDL*)&hdl);
        MSDK_CHECK_STATUS(sts, "m_hwdev->GetHandle failed");
        // set Device to external dx11 allocator
        D3D11AllocatorParams *pD3D11Params = dynamic_cast<D3D11AllocatorParams*>(m_pAllocParam.get());
        pD3D11Params->pDevice =(ID3D11Device*)hdl;

        // All sessions use same allocator parameters, so we'll take settings for the 0 session and use it for all
        // (bSingleTexture is set for all sessions of for no one in VerifyCrossSessionsOptions())
        pD3D11Params->bUseSingleTexture = m_InputParamsArray[0].bSingleTexture;

    }
#endif
#elif defined(LIBVA_X11_SUPPORT) || defined(LIBVA_DRM_SUPPORT) || defined(ANDROID)

    if (m_eDevType == MFX_HANDLE_VA_DISPLAY)
    {
        if (bNeedToCreateDevice) {
        mfxI32  libvaBackend = 0;
        mfxAllocatorParams* pAllocParam(new vaapiAllocatorParams);
        std::unique_ptr<CHWDevice> hwdev;

        vaapiAllocatorParams *pVAAPIParams = dynamic_cast<vaapiAllocatorParams*>(pAllocParam);
        /* The last param set in vector always describe VPP+ENCODE or Only VPP
         * So, if we want to do rendering we need to do pass HWDev to CTranscodingPipeline */
        if (m_InputParamsArray[m_InputParamsArray.size() -1].eModeExt == VppCompOnly)
        {
            sInputParams& params = m_InputParamsArray[m_InputParamsArray.size() -1];
            libvaBackend = params.libvaBackend;

            /* Rendering case */
            hwdev.reset(CreateVAAPIDevice(InputParams.strDevicePath, params.libvaBackend));
            if(!hwdev.get()) {
                msdk_printf(MSDK_STRING("error: failed to initialize VAAPI device\n"));
                return MFX_ERR_DEVICE_FAILED;
            }
            sts = hwdev->Init(&params.monitorType, 1, MSDKAdapter::GetNumber(m_pLoader.get()) );
#if defined(LIBVA_X11_SUPPORT) || defined(LIBVA_DRM_SUPPORT)
            if (params.libvaBackend == MFX_LIBVA_DRM_MODESET) {
                CVAAPIDeviceDRM* drmdev = dynamic_cast<CVAAPIDeviceDRM*>(hwdev.get());
                pVAAPIParams->m_export_mode = vaapiAllocatorParams::CUSTOM_FLINK;
                pVAAPIParams->m_exporter = dynamic_cast<vaapiAllocatorParams::Exporter*>(drmdev->getRenderer());
                sts = hwdev->GetHandle(MFX_HANDLE_VA_DISPLAY, (mfxHDL *)&vaDpy);
                for (uint j = 0; j < m_InputParamsArray.size(); j++)
                {
                    m_InputParamsArray[j].vRefreshRate = hwdev->GetRefreshRate();
                }
            }
            else if (params.libvaBackend == MFX_LIBVA_X11)
            {
                pVAAPIParams->m_export_mode = vaapiAllocatorParams::PRIME;
                sts = hwdev->GetHandle(MFX_HANDLE_VA_DISPLAY, (mfxHDL *)&vaDpy);
            }

#endif
#if defined(LIBVA_WAYLAND_SUPPORT)
            else if (params.libvaBackend == MFX_LIBVA_WAYLAND) {
                VADisplay va_dpy = NULL;
                sts = m_hwdev->GetHandle(MFX_HANDLE_VA_DISPLAY, (mfxHDL *)&va_dpy);
                MSDK_CHECK_STATUS(sts, "m_hwdev->GetHandle failed");
                hdl = pVAAPIParams->m_dpy =(VADisplay)va_dpy;

                CVAAPIDeviceWayland* w_dev = dynamic_cast<CVAAPIDeviceWayland*>(hwdev.get());
                if (!w_dev)
                {
                    MSDK_CHECK_STATUS(MFX_ERR_DEVICE_FAILED, "Failed to reach Wayland VAAPI device");
                }
                Wayland *wld = w_dev->GetWaylandHandle();
                if (!wld)
                {
                    MSDK_CHECK_STATUS(MFX_ERR_DEVICE_FAILED, "Failed to reach Wayland VAAPI device");
                }

                wld->SetRenderWinPos(params.nRenderWinX, params.nRenderWinY);
                wld->SetPerfMode(params.bPerfMode);

                pVAAPIParams->m_export_mode = vaapiAllocatorParams::PRIME;
            }
#endif // LIBVA_WAYLAND_SUPPORT
            params.m_hwdev = hwdev.get();
        }
        else /* NO RENDERING*/
        {
            hwdev.reset(CreateVAAPIDevice(InputParams.strDevicePath));
            if(!hwdev.get()) {
                msdk_printf(MSDK_STRING("error: failed to initialize VAAPI device\n"));
                return MFX_ERR_DEVICE_FAILED;
            }
            sts = hwdev->Init(NULL, 0, MSDKAdapter::GetNumber(m_pLoader.get()));

            pVAAPIParams->m_export_mode = vaapiAllocatorParams::PRIME;
            sts = hwdev->GetHandle(MFX_HANDLE_VA_DISPLAY, (mfxHDL *)&vaDpy);
        }
        if (libvaBackend != MFX_LIBVA_WAYLAND) {
            MSDK_CHECK_STATUS(sts, "m_hwdev->Init failed");
            sts = hwdev->GetHandle(MFX_HANDLE_VA_DISPLAY, (mfxHDL*)&hdl);
            MSDK_CHECK_STATUS(sts, "m_hwdev->GetHandle failed");
            pVAAPIParams->m_dpy =(VADisplay)hdl;
            vaDpy = pVAAPIParams->m_dpy;
        }
        m_pAllocParams.push_back(std::shared_ptr<mfxAllocatorParams>(pAllocParam));
        m_hwdevs.push_back(std::move(hwdev));
        hdls.push_back(hdl);
        }
        else {
                if (!m_pAllocParams.empty() && !hdls.empty()) {
                    m_pAllocParams.push_back(m_pAllocParams.back());
                    hdls.push_back(hdls.back());
                }
                else {
                    msdk_printf(MSDK_STRING("error: failed to initialize alloc parameters\n"));
                    return MFX_ERR_MEMORY_ALLOC;
                }
        }
    }
#endif
  }    

    if (m_pAllocParams.empty()) {
        m_pAllocParams.push_back(std::make_shared<mfxAllocatorParams>());
        hdls.push_back(NULL);

        for (i = 1; i < m_InputParamsArray.size(); i++) {
            m_pAllocParams.push_back(m_pAllocParams.back());
            hdls.push_back(NULL);
        }
    }

    uint sinkNum = 0;
    for (i = 0; i < m_InputParamsArray.size(); i++)
    {
        if (Source == m_InputParamsArray[i].eMode) 
        {
            sinkNum++;
        }
    }

    m_sinkNum = sinkNum;
    m_sourceNum = m_InputParamsArray.size() - sinkNum;
    uint cur_sink = 0;
    msdk_printf(MSDK_STRING("Sink Sessions num %d:  source num %d\n"), sinkNum, m_sourceNum);
 
    // each pair of source and sink has own safety buffer
    sts = CreateSafetyBuffers();
    MSDK_CHECK_STATUS(sts, "CreateSafetyBuffers failed");

    /* One more hint. Example you have 3 dec + 1 enc sessions
    * (enc means vpp_comp call invoked. m_InputParamsArray.size() is 4.
    * You don't need take vpp comp params from last one session as it is enc session.
    * But you need process {0, 1, 2} sessions - totally 3.
    * So, you need start from 0 and end at 2.
    * */
    for(mfxI32 jj = 0; jj<(mfxI32)m_InputParamsArray.size() - 1; jj++)
    {
        /* Save params for VPP composition */
        sVppCompDstRect tempDstRect;
        tempDstRect.DstX   = m_InputParamsArray[jj].nVppCompDstX;
        tempDstRect.DstY   = m_InputParamsArray[jj].nVppCompDstY;
        tempDstRect.DstW   = m_InputParamsArray[jj].nVppCompDstW;
        tempDstRect.DstH   = m_InputParamsArray[jj].nVppCompDstH;
        tempDstRect.TileId = m_InputParamsArray[jj].nVppCompTileId;
        m_VppDstRects.push_back(tempDstRect);
    }

    // create sessions, allocators
    for (i = 0; i < m_InputParamsArray.size(); i++)
    {
        msdk_printf(MSDK_STRING("Session %d:\n"), i);
        std::unique_ptr<GeneralAllocator> pAllocator(new GeneralAllocator);
        sts = pAllocator->Init(m_pAllocParams[i].get());
        MSDK_CHECK_STATUS(sts, "pAllocator->Init failed");

        m_pAllocArray.push_back(std::move(pAllocator));        

        std::unique_ptr<ThreadTranscodeContext> pThreadPipeline(new ThreadTranscodeContext);
        // extend BS processing init
        m_pExtBSProcArray.push_back(std::make_unique<FileBitstreamProcessor>());

        pThreadPipeline->pPipeline.reset(CreatePipeline());
#ifdef ENABLE_INFERENCE
        pThreadPipeline->pPipeline->SetVADisplayHandle(vaDpy);
#endif

#if (defined(_WIN32) || defined(_WIN64)) && (MFX_VERSION >= 1031)
        pThreadPipeline->pPipeline->SetPrefferiGfx(m_InputParamsArray[i].bPrefferiGfx);
        pThreadPipeline->pPipeline->SetPrefferdGfx(m_InputParamsArray[i].bPrefferdGfx);
#endif

        pThreadPipeline->pBSProcessor = m_pExtBSProcArray.back().get();

        std::unique_ptr<FileAndRTSPBitstreamReader> reader;
        std::unique_ptr<CSmplYUVReader> yuvreader;
        std::unique_ptr<V4l2BitstreamReader> v4l2reader;
        std::unique_ptr<AlsaAudioStreamReader> alsareader;

        if (m_InputParamsArray[i].DecodeId == MFX_CODEC_VP9)
        {
#ifdef RTSP_SUPPORT
            msdk_printf(MSDK_STRING("VP9 decode is not supported when RTSP is enabled\n"));
            return MFX_ERR_UNSUPPORTED;
#else
            reader.reset(new CIVFFrameReader());
#endif
        }
        else if (m_InputParamsArray[i].DecodeId == MFX_CODEC_RGB4)
        {
            // YUV reader for RGB4 overlay
            yuvreader.reset(new CSmplYUVReader());
        }
        else if (m_InputParamsArray[i].bV4l2RawInput)
        {
            v4l2reader.reset(new V4l2BitstreamReader());
        }
        else
        {
            reader.reset(new FileAndRTSPBitstreamReader());
        }

        if (m_InputParamsArray[i].bAlsaAudioInput)
        {
            alsareader.reset(new AlsaAudioStreamReader(m_InputParamsArray[i].strACaptureDeviceName, m_InputParamsArray[i].strAPlayDeviceName, m_InputParamsArray[i].strAMP4Name, m_InputParamsArray[i].strAMP3Name));
            sts = alsareader->Init();
            MSDK_CHECK_STATUS(sts, "Alsa reader->Init failed");
            sts = m_pExtBSProcArray.back()->SetReader(alsareader);
            MSDK_CHECK_STATUS(sts, "m_pExtBSProcArray.back() set alsareader failed");
        }

        if (reader.get())
        {
            sts = reader->Init(m_InputParamsArray[i].strSrcFile);
            MSDK_CHECK_STATUS(sts, "reader->Init failed");

            if (msdk_strlen(m_InputParamsArray[i].strRtspSaveFile) > 0)
            {
                reader->CreateRtspDumpFile(m_InputParamsArray[i].strRtspSaveFile);
            }

            sts = m_pExtBSProcArray.back()->SetReader(reader);
            MSDK_CHECK_STATUS(sts, "m_pExtBSProcArray.back()->SetReader failed");
        }
        else if (yuvreader.get())
        {
            std::list<msdk_string> input;
            input.push_back(m_InputParamsArray[i].strSrcFile);
            sts = yuvreader->Init(input, MFX_FOURCC_RGB4);
            MSDK_CHECK_STATUS(sts, "m_YUVReader->Init failed");
            sts = m_pExtBSProcArray.back()->SetReader(yuvreader);
            MSDK_CHECK_STATUS(sts, "m_pExtBSProcArray.back()->SetReader failed");
        }
        else if (v4l2reader.get())
        {
            sts = v4l2reader->Init(m_InputParamsArray[i].ltDevicePort, m_InputParamsArray[i].strV4l2VideoDeviceName, m_InputParamsArray[i].strV4l2SubdevName);
            MSDK_CHECK_STATUS(sts, "reader->Init failed");
            sts = m_pExtBSProcArray.back()->SetReader(v4l2reader);
        }

        std::unique_ptr<CSmplBitstreamWriter> writer(new CSmplBitstreamWriter());

        if (m_InputParamsArray[i].eModeExt != FakeSink)
        {
            sts = writer->Init(m_InputParamsArray[i].strDstFile);
            MSDK_CHECK_STATUS(sts, " writer->Init failed");
        }

        sts = m_pExtBSProcArray.back()->SetWriter(writer);
        MSDK_CHECK_STATUS(sts, "m_pExtBSProcArray.back()->SetWriter failed");

        SafetySurfaceBuffer **pBufferArray = nullptr;
        m_InputParamsArray[i].sinkNum = m_sinkNum;
        m_InputParamsArray[i].sourceNum = m_sourceNum;
        if (m_sinkNum >= 1)
        {
            if (Sink == m_InputParamsArray[i].eMode)
            {
                pBufferArray = new SafetySurfaceBuffer * [m_sinkNum];
                
                /* N_to_1 mode */
                if ((VppComp == m_InputParamsArray[i].eModeExt) ||
                        (VppCompOnly == m_InputParamsArray[i].eModeExt))
                {
                    std::cout<<std::endl;
                    // Taking buffers from tail because they are stored in m_pBufferArray in reverse order
                    // So, by doing this we'll fill buffers properly according to order from par file
                    for (uint j = 0; j < m_sinkNum; j++)
                    {
                        pBufferArray[j] = m_pBufferArray[m_sourceNum * (j + 1) - BufCounter - 1].get();
                    }
                    msdk_printf(MSDK_STRING("sink n to 1\n"));
                    BufCounter++;
                }
                else /* 1_to_N mode*/
                {
                    pBufferArray[0] = m_pBufferArray[m_pBufferArray.size() - 1].get();
                    msdk_printf(MSDK_STRING("sink 1 to n\n"));
                }
                pSinkPipeline = pThreadPipeline->pPipeline.get();
            }
            else if (Source == m_InputParamsArray[i].eMode)
            {
                pBufferArray = new SafetySurfaceBuffer * [1];
                /* N_to_1 mode */
                if ((VppComp == m_InputParamsArray[i].eModeExt) ||
                        (VppCompOnly == m_InputParamsArray[i].eModeExt) ||
                        (FakeSink == m_InputParamsArray[i].eModeExt))
                {
                    pBufferArray[0] = m_pBufferArray[m_sourceNum - 1 + m_sourceNum * cur_sink].get();
                    msdk_printf(MSDK_STRING("source n to 1\n"));
                    cur_sink++;
                }
                else /* 1_to_N mode*/
                {
                    pBufferArray[0] = m_pBufferArray[BufCounter].get();
                    BufCounter++;
                    msdk_printf(MSDK_STRING("source 1 to n\n"));
                }
            }
            
        }
        

        /**/
        /* Vector stored linearly in the memory !*/
        m_InputParamsArray[i].pVppCompDstRects = m_VppDstRects.empty() ? NULL : &m_VppDstRects[0];

        // if session has VPP plus ENCODE only (-i::source option)
        // use decode source session as input
        sts = MFX_ERR_MORE_DATA;
        if (Source == m_InputParamsArray[i].eMode)
        {
#if (defined(_WIN32) || defined(_WIN64)) && (MFX_VERSION >= 1031)
            sts = CheckAndFixAdapterDependency(i, pSinkPipeline);
            MSDK_CHECK_STATUS(sts, "CheckAndFixAdapterDependency failed");
            // force implementation type based on iGfx/dGfx parameters
            ForceImplForSession(i);
#endif 
        sts = pThreadPipeline->pPipeline->Init(&m_InputParamsArray[i],
                                               m_pAllocArray[i].get(),
                                               hdls[i],
                                               pParentPipeline,
                                               pBufferArray,
                                               m_pExtBSProcArray.back().get(),
                                               m_pLoader.get());
        }
        else
        {
#if (defined(_WIN32) || defined(_WIN64)) && (MFX_VERSION >= 1031)
            sts = CheckAndFixAdapterDependency(i, pParentPipeline);
            MSDK_CHECK_STATUS(sts, "CheckAndFixAdapterDependency failed");
            // force implementation type based on iGfx/dGfx parameters
            ForceImplForSession(i);
#endif 
        sts = pThreadPipeline->pPipeline->Init(&m_InputParamsArray[i],
                                               m_pAllocArray[i].get(),
                                               hdls[i],
                                               pParentPipeline,
                                               pBufferArray,
                                               m_pExtBSProcArray.back().get(),
                                               m_pLoader.get());
        }
        MSDK_CHECK_STATUS(sts, "pThreadPipeline->pPipeline->Init failed");

        if (!pParentPipeline && m_InputParamsArray[i].bIsJoin)
            pParentPipeline = pThreadPipeline->pPipeline.get();

        // set the session's start status (like it is waiting)
        pThreadPipeline->startStatus = MFX_WRN_DEVICE_BUSY;
        // set other session's parameters
        pThreadPipeline->implType = m_InputParamsArray[i].libType;
        m_pThreadContextArray.push_back(std::move(pThreadPipeline));

        mfxVersion ver = {{0, 0}};
        sts = m_pThreadContextArray[i]->pPipeline->QueryMFXVersion(&ver);
        MSDK_CHECK_STATUS(sts, "m_pThreadContextArray[i]->pPipeline->QueryMFXVersion failed");

        PrintInfo(i, &m_InputParamsArray[i], &ver);
    }

    for (i = 0; i < m_InputParamsArray.size(); i++)
    {
        sts = m_pThreadContextArray[i]->pPipeline->CompleteInit();
        MSDK_CHECK_STATUS(sts, "m_pThreadContextArray[i]->pPipeline->CompleteInit failed");

        if (m_pThreadContextArray[i]->pPipeline->GetJoiningFlag())
            msdk_printf(MSDK_STRING("Session %d was joined with other sessions\n"), i);
        else
            msdk_printf(MSDK_STRING("Session %d was NOT joined with other sessions\n"), i);

        m_pThreadContextArray[i]->pPipeline->SetPipelineID(i);
    }

    msdk_printf(MSDK_STRING("\n"));

    return sts;

} // mfxStatus Launcher::Init()

void Launcher::Run()
{
    if (m_InputParamsArray[0].RtspDumpOnly)
    {
        msdk_printf(MSDK_STRING("RTSP dump only mode is enabled\n"));
        DoRtspDump();
        return;
    }

    msdk_printf(MSDK_STRING("Transcoding started\n"));

    // mark start time
    m_StartTime = GetTick();

    // Robust flag is applied to every seession if enabled in one
    if (m_pThreadContextArray[0]->pPipeline->GetRobustFlag())
    {
        DoRobustTranscoding();
    }
    else
    {
        DoTranscoding();
    }

    msdk_printf(MSDK_STRING("\nTranscoding finished\n"));

} // mfxStatus Launcher::Init()

void Launcher::DoTranscoding()
{
    auto RunTranscodeRoutine = [](ThreadTranscodeContext* context)
    {
        context->handle = std::async(std::launch::async, [context](){
                                context->TranscodeRoutine();
                          });
    };

    bool isOverlayUsed = false;
    for (const auto& context : m_pThreadContextArray)
    {
        MSDK_CHECK_POINTER_NO_RET(context);
        RunTranscodeRoutine(context.get());

        MSDK_CHECK_POINTER_NO_RET(context->pPipeline);
        isOverlayUsed = isOverlayUsed || context->pPipeline->IsOverlayUsed();
    }

    // Transcoding threads waiting cycle
    bool aliveNonOverlaySessions = true;
    while (aliveNonOverlaySessions)
    {
        aliveNonOverlaySessions = false;

        for (size_t i = 0; i < m_pThreadContextArray.size(); ++i)
        {
            if (!m_pThreadContextArray[i]->handle.valid())
                continue;

            //Payslip interval to check the state of working threads:
            //such interval is usually a realtime, i.e. for 30 fps this would be 33ms,
            //66ms typically mean either 1/fps or 2/fps payslip checks.
            auto waitSts = m_pThreadContextArray[i]->handle.wait_for(std::chrono::milliseconds(66));
            if (waitSts == std::future_status::ready)
            {
                // Invoke get() of the handle just to reset the valid state.
                // This allows to skip already processed sessions
                m_pThreadContextArray[i]->handle.get();

                // Session is completed, let's check for its status
                if (m_pThreadContextArray[i]->transcodingSts < MFX_ERR_NONE)
                {
                    // Stop all the sessions if an error happened in one
                    // But do not stop in robust mode when gpu hang's happened
                    if (m_pThreadContextArray[i]->transcodingSts != MFX_ERR_GPU_HANG ||
                        !m_pThreadContextArray[i]->pPipeline->GetRobustFlag())
                    {
                        msdk_stringstream ss;
                        ss << MSDK_STRING("\n\n session ") << i << MSDK_STRING(" [")
                           << m_pThreadContextArray[i]->pPipeline->GetSessionText()
                           << MSDK_STRING("] failed with status ")
                           << StatusToString(m_pThreadContextArray[i]->transcodingSts)
                           << MSDK_STRING(" shutting down the application...")
                           << std::endl << std::endl;
                        msdk_printf(MSDK_STRING("%s"), ss.str().c_str());

                        for (const auto& context : m_pThreadContextArray)
                        {
                            context->pPipeline->StopSession();
                        }
                    }
                }
                else if (m_pThreadContextArray[i]->transcodingSts > MFX_ERR_NONE)
                {
                    msdk_stringstream ss;
                    ss << MSDK_STRING("\n\n session ") << i << MSDK_STRING(" [")
                    << m_pThreadContextArray[i]->pPipeline->GetSessionText()
                    << MSDK_STRING("] returned warning status ")
                    << StatusToString(m_pThreadContextArray[i]->transcodingSts)
                    << std::endl << std::endl;
                    msdk_printf(MSDK_STRING("%s"), ss.str().c_str());
                }
            }
            else
            {
                aliveNonOverlaySessions = aliveNonOverlaySessions || !m_pThreadContextArray[i]->pPipeline->IsOverlayUsed();
            }
        }

        // Stop overlay sessions
        // Note: Overlay sessions never stop themselves so they should be forcibly stopped
        // after stopping of all non-overlay sessions
        if (!aliveNonOverlaySessions && isOverlayUsed)
        {
            // Sending stop message
            for (const auto& context : m_pThreadContextArray)
            {
                if (context->pPipeline->IsOverlayUsed())
                {
                    context->pPipeline->StopSession();
                }
            }

            // Waiting for them to be stopped
            for (const auto& context : m_pThreadContextArray)
            {
                if (!context->handle.valid())
                    continue;

                context->handle.wait();
            }
        }
    }
}

void Launcher::DoRobustTranscoding()
{
    mfxStatus sts = MFX_ERR_NONE;

    // Cycle for handling MFX_ERR_GPU_HANG during transcoding
    // If it's returned, reset all the pipelines and start over from the last point
    bool bGPUHang = false;
    for ( ; ; )
    {
        if (bGPUHang)
        {
            for (size_t i = 0; i < m_pThreadContextArray.size(); i++)
            {
                sts = m_pThreadContextArray[i]->pPipeline->Reset(m_pLoader.get());
                if (sts)
                {
                    msdk_printf(MSDK_STRING("\n[WARNING] GPU Hang recovery wasn't succeed. Exiting...\n"));
                    return;
                }
            }
            bGPUHang = false;
            msdk_printf(MSDK_STRING("\n[WARNING] Successfully recovered. Continue transcoding.\n"));
        }

        DoTranscoding();

        for (size_t i = 0; i < m_pThreadContextArray.size(); i++)
        {
            if (m_pThreadContextArray[i]->transcodingSts == MFX_ERR_GPU_HANG)
            {
                bGPUHang = true;
            }
        }
        if (!bGPUHang)
            break;
        msdk_printf(MSDK_STRING("\n[WARNING] GPU Hang has happened. Trying to recover...\n"));
    }
}

mfxStatus Launcher::ProcessResult()
{
    //No performance data for RTSP dump mode
    if (m_InputParamsArray[0].RtspDumpOnly)
    {
        return MFX_ERR_NONE;
    }

    FILE* pPerfFile = m_parser.GetPerformanceFile();

    msdk_stringstream ssTranscodingTime;
    ssTranscodingTime << std::endl << MSDK_STRING("Common transcoding time is ") << GetTime(m_StartTime) << MSDK_STRING(" sec") << std::endl;

    m_parser.PrintParFileName();

    msdk_printf(MSDK_STRING("%s"),ssTranscodingTime.str().c_str());
    if (pPerfFile)
    {
        msdk_fprintf(pPerfFile, MSDK_STRING("%s"), ssTranscodingTime.str().c_str());
    }

    mfxStatus FinalSts = MFX_ERR_NONE;
    msdk_printf(MSDK_STRING("-------------------------------------------------------------------------------\n"));

    for (mfxU32 i = 0; i < m_pThreadContextArray.size(); i++)
    {
        mfxStatus transcodingSts = m_pThreadContextArray[i]->transcodingSts;
        mfxF64 workTime = m_pThreadContextArray[i]->working_time;
        mfxU32 framesNum = m_pThreadContextArray[i]->numTransFrames;

        if (!FinalSts)
            FinalSts = transcodingSts;

        msdk_string SessionStsStr = transcodingSts ? msdk_string(MSDK_STRING("FAILED"))
            : msdk_string((MSDK_STRING("PASSED")));

        msdk_stringstream ss;
        ss << MSDK_STRING("*** session ") << i
           << MSDK_STRING(" [") << m_pThreadContextArray[i]->pPipeline->GetSessionText()
           << MSDK_STRING("] ") << SessionStsStr <<MSDK_STRING(" (")
           << StatusToString(transcodingSts) << MSDK_STRING(") ")
           << workTime << MSDK_STRING(" sec, ")
           << framesNum << MSDK_STRING(" frames, ")
           << std::fixed << std::setprecision(3) << framesNum / workTime << MSDK_STRING(" fps")
           << std::endl
           << m_parser.GetLine(i) << std::endl << std::endl;

        msdk_printf(MSDK_STRING("%s"),ss.str().c_str());
        if (pPerfFile)
        {
            msdk_fprintf(pPerfFile, MSDK_STRING("%s"), ss.str().c_str());
        }

    }
    msdk_printf(MSDK_STRING("-------------------------------------------------------------------------------\n"));

    msdk_stringstream ssTest;
    ssTest << std::endl << MSDK_STRING("The test ") << (FinalSts ? msdk_string(MSDK_STRING("FAILED")) : msdk_string(MSDK_STRING("PASSED"))) << std::endl;

    msdk_printf(MSDK_STRING("%s"),ssTest.str().c_str());
    if (pPerfFile)
    {
        msdk_fprintf(pPerfFile, MSDK_STRING("%s"), ssTest.str().c_str());
    }
    return FinalSts;
} // mfxStatus Launcher::ProcessResult()

#if (defined(_WIN32) || defined(_WIN64)) && (MFX_VERSION >= 1031)
mfxStatus Launcher::QueryAdapters()
{
    mfxU32 num_adapters_available;

    mfxStatus sts = MFXQueryAdaptersNumber(&num_adapters_available);
    MFX_CHECK_STS(sts);

    m_DisplaysData.resize(num_adapters_available);
    m_Adapters = { m_DisplaysData.data(), mfxU32(m_DisplaysData.size()), 0u };

    sts = MFXQueryAdapters(nullptr, &m_Adapters);
    MFX_CHECK_STS(sts);

    return MFX_ERR_NONE;
}

void Launcher::ForceImplForSession(mfxU32 idxSession)
{
    //change only 8 bit of the implementation. Don't touch type of frames
    mfxIMPL impl = m_InputParamsArray[idxSession].libType & mfxI32(~0xFF);

    mfxU32 idx = GetPreferredAdapterNum(m_Adapters, m_InputParamsArray[idxSession]);
    switch (m_Adapters.Adapters[idx].Number)
    {
    case 0:
        impl |= MFX_IMPL_HARDWARE;
        break;
    case 1:
        impl |= MFX_IMPL_HARDWARE2;
        break;
    case 2:
        impl |= MFX_IMPL_HARDWARE3;
        break;
    case 3:
        impl |= MFX_IMPL_HARDWARE4;
        break;

    default:
        // try searching on all display adapters
        impl |= MFX_IMPL_HARDWARE_ANY;
        break;
    }

    m_InputParamsArray[idxSession].libType = impl;
}

mfxStatus Launcher::CheckAndFixAdapterDependency(mfxU32 idxSession, CTranscodingPipeline * pParentPipeline)
{
    if (!pParentPipeline)
        return MFX_ERR_NONE;

    // Inherited sessions must have the same adapter as parent
    if ((pParentPipeline->IsPrefferiGfx() || pParentPipeline->IsPrefferdGfx()) && !m_InputParamsArray[idxSession].bPrefferiGfx && !m_InputParamsArray[idxSession].bPrefferdGfx)
    {
        m_InputParamsArray[idxSession].bPrefferiGfx = pParentPipeline->IsPrefferiGfx();
        m_InputParamsArray[idxSession].bPrefferdGfx = pParentPipeline->IsPrefferdGfx();
        msdk_stringstream ss;
        ss << MSDK_STRING("\n\n session with index: ") << idxSession
            << MSDK_STRING(" adapter type was forced to ")
            << (pParentPipeline->IsPrefferiGfx() ? MSDK_STRING("integrated") : MSDK_STRING("discrete"))
            << std::endl << std::endl;
        msdk_printf(MSDK_STRING("%s"), ss.str().c_str());

        return MFX_ERR_NONE;
    }

    // App can't change initialization of the previous session (parent session)
    if (!pParentPipeline->IsPrefferiGfx() && !pParentPipeline->IsPrefferdGfx() && (m_InputParamsArray[idxSession].bPrefferiGfx || m_InputParamsArray[idxSession].bPrefferdGfx))
    {
        msdk_stringstream ss;
        ss << MSDK_STRING("\n\n session with index: ") << idxSession
            << MSDK_STRING(" failed because parent session [")
            << pParentPipeline->GetSessionText()
            << MSDK_STRING("] doesn't have explicit adapter setting")
            << std::endl << std::endl;
        msdk_printf(MSDK_STRING("%s"), ss.str().c_str());

        return MFX_ERR_UNSUPPORTED;
    }

    // Inherited sessions must have the same adapter as parent
    if (pParentPipeline->IsPrefferiGfx() && !m_InputParamsArray[idxSession].bPrefferiGfx)
    {
        msdk_stringstream ss;
        ss << MSDK_STRING("\n\n session with index: ") << idxSession
            << MSDK_STRING(" failed because it has different adapter type with parent session [")
            << pParentPipeline->GetSessionText()
            << MSDK_STRING("]")
            << std::endl << std::endl;
        msdk_printf(MSDK_STRING("%s"), ss.str().c_str());

        return MFX_ERR_UNSUPPORTED;
    }

    // Inherited sessions must have the same adapter as parent
    if (pParentPipeline->IsPrefferdGfx() && !m_InputParamsArray[idxSession].bPrefferdGfx)
    {
        msdk_stringstream ss;
        ss << MSDK_STRING("\n\n session with index: ") << idxSession
            << MSDK_STRING(" failed because it has different adapter type with parent session [")
            << pParentPipeline->GetSessionText()
            << MSDK_STRING("]")
            << std::endl << std::endl;
        msdk_printf(MSDK_STRING("%s"), ss.str().c_str());

        return MFX_ERR_UNSUPPORTED;
    }

    return MFX_ERR_NONE;
}
#endif

mfxStatus Launcher::VerifyCrossSessionsOptions()
{
    bool IsSinkPresence = false;
    bool IsSourcePresence = false;
    bool IsHeterSessionJoin = false;
    bool IsFirstInTopology = true;
    bool areAllInterSessionsOpaque = true;

    mfxU16 minAsyncDepth = 0;
    bool bUseExternalAllocator = false;
    bool bSingleTexture = false;

#if (MFX_VERSION >= 1025)
    bool allMFEModesEqual=true;
    bool allMFEFramesEqual=true;
    bool allMFESessionsJoined = true;

    mfxU16 usedMFEMaxFrames = 0;
    mfxU16 usedMFEMode = 0;

    for (mfxU32 i = 0; i < m_InputParamsArray.size(); i++)
    {
        // loop over all sessions and check mfe-specific params
        // for mfe is required to have sessions joined, HW impl
        if(m_InputParamsArray[i].numMFEFrames > 1)
        {
            usedMFEMaxFrames = m_InputParamsArray[i].numMFEFrames;
            for (mfxU32 j = 0; j < m_InputParamsArray.size(); j++)
            {
                if(m_InputParamsArray[j].numMFEFrames &&
                   m_InputParamsArray[j].numMFEFrames != usedMFEMaxFrames)
                {
                    m_InputParamsArray[j].numMFEFrames = usedMFEMaxFrames;
                    allMFEFramesEqual = false;
                    m_InputParamsArray[j].MFMode = m_InputParamsArray[j].MFMode < MFX_MF_AUTO
                      ? MFX_MF_AUTO : m_InputParamsArray[j].MFMode;
                }
                if(m_InputParamsArray[j].bIsJoin == false)
                {
                    allMFESessionsJoined = false;
                    m_InputParamsArray[j].bIsJoin = true;
                }
            }
        }
        if(m_InputParamsArray[i].MFMode >= MFX_MF_AUTO)
        {
            usedMFEMode = m_InputParamsArray[i].MFMode;
            for (mfxU32 j = 0; j < m_InputParamsArray.size(); j++)
            {
                if(m_InputParamsArray[j].MFMode &&
                   m_InputParamsArray[j].MFMode != usedMFEMode)
                {
                    m_InputParamsArray[j].MFMode = usedMFEMode;
                    allMFEModesEqual = false;
                }
                if(m_InputParamsArray[j].bIsJoin == false)
                {
                    allMFESessionsJoined = false;
                    m_InputParamsArray[j].bIsJoin = true;
                }
            }
        }
    }
    if(!allMFEFramesEqual)
        msdk_printf(MSDK_STRING("WARNING: All sessions for MFE should have the same number of MFE frames!\n used ammount of frame for MFE: %d\n"),  (int)usedMFEMaxFrames);
    if(!allMFEModesEqual)
        msdk_printf(MSDK_STRING("WARNING: All sessions for MFE should have the same mode!\n, used mode: %d\n"),  (int)usedMFEMode);
    if(!allMFESessionsJoined)
        msdk_printf(MSDK_STRING("WARNING: Sessions for MFE should be joined! All sessions forced to be joined\n"));
#endif

    for (mfxU32 i = 0; i < m_InputParamsArray.size(); i++)
    {
        if (
            ((m_InputParamsArray[i].eMode == Source) || (m_InputParamsArray[i].eMode == Sink)))
        {
            areAllInterSessionsOpaque = false;
        }

        // Any plugin or static frame alpha blending
        // CPU rotate plugin works with opaq frames in native mode
        if ((m_InputParamsArray[i].nRotationAngle && m_InputParamsArray[i].eMode != Native) ||
            m_InputParamsArray[i].bOpenCL ||
            m_InputParamsArray[i].EncoderFourCC ||
            m_InputParamsArray[i].DecoderFourCC ||
            m_InputParamsArray[i].nVppCompSrcH ||
            m_InputParamsArray[i].nVppCompSrcW ||
            m_InputParamsArray[i].bV4l2RawInput)
        {
            bUseExternalAllocator = true;
        }

        if (m_InputParamsArray[i].bSingleTexture)
        {
            bSingleTexture = true;
        }

        // All sessions have to know about timeout
        if (m_InputParamsArray[i].nTimeout && (m_InputParamsArray[i].eMode == Sink))
        {
            for (mfxU32 j = 0; j < m_InputParamsArray.size(); j++)
            {
                if (m_InputParamsArray[j].MaxFrameNumber != MFX_INFINITE)
                {
                    msdk_printf(MSDK_STRING("\"-timeout\" option isn't compatible with \"-n\". \"-n\" will be ignored.\n"));
                    for (mfxU32 k = 0; k < m_InputParamsArray.size(); k++)
                    {
                        m_InputParamsArray[k].MaxFrameNumber = MFX_INFINITE;
                    }
                    break;
                }
            }
            msdk_printf(MSDK_STRING("Timeout %d seconds has been set to all sessions\n"), m_InputParamsArray[i].nTimeout);
            for (mfxU32 j = 0; j < m_InputParamsArray.size(); j++)
            {
                m_InputParamsArray[j].nTimeout = m_InputParamsArray[i].nTimeout;
            }
        }

        // All sessions have to know if robust mode enabled
        if (m_InputParamsArray[i].bRobustFlag)
        {
            for (mfxU32 j = 0; j < m_InputParamsArray.size(); j++)
            {
                m_InputParamsArray[j].bRobustFlag = m_InputParamsArray[i].bRobustFlag;
            }
        }

        if (Source == m_InputParamsArray[i].eMode)
        {
            if (m_InputParamsArray[i].nAsyncDepth < minAsyncDepth)
            {
                minAsyncDepth = m_InputParamsArray[i].nAsyncDepth;
            }
            // topology definition
            if (!IsSinkPresence)
            {
                PrintError(MSDK_STRING("Error in par file. Decode source session must be declared BEFORE encode sinks \n"));
                return MFX_ERR_UNSUPPORTED;
            }
            IsSourcePresence = true;

            if (IsFirstInTopology)
            {
                if (m_InputParamsArray[i].bIsJoin)
                    IsHeterSessionJoin = true;
                else
                    IsHeterSessionJoin = false;
            }
            else
            {
                if (m_InputParamsArray[i].bIsJoin && !IsHeterSessionJoin)
                {
                    PrintError(MSDK_STRING("Error in par file. All heterogeneous sessions must be joined \n"));
                    return MFX_ERR_UNSUPPORTED;
                }
                if (!m_InputParamsArray[i].bIsJoin && IsHeterSessionJoin)
                {
                    PrintError(MSDK_STRING("Error in par file. All heterogeneous sessions must be NOT joined \n"));
                    return MFX_ERR_UNSUPPORTED;
                }
            }

            if (IsFirstInTopology)
                IsFirstInTopology = false;

        }
        else if (Sink == m_InputParamsArray[i].eMode)
        {
            minAsyncDepth = m_InputParamsArray[i].nAsyncDepth;
            IsSinkPresence = true;

            if (IsFirstInTopology)
            {
                if (m_InputParamsArray[i].bIsJoin)
                    IsHeterSessionJoin = true;
                else
                    IsHeterSessionJoin = false;
            }
            else
            {
                if (m_InputParamsArray[i].bIsJoin && !IsHeterSessionJoin)
                {
                    PrintError(MSDK_STRING("Error in par file. All heterogeneous sessions must be joined \n"));
                    return MFX_ERR_UNSUPPORTED;
                }
                if (!m_InputParamsArray[i].bIsJoin && IsHeterSessionJoin)
                {
                    PrintError(MSDK_STRING("Error in par file. All heterogeneous sessions must be NOT joined \n"));
                    return MFX_ERR_UNSUPPORTED;
                }
            }

            if (IsFirstInTopology)
                IsFirstInTopology = false;
        }
        if (MFX_IMPL_SOFTWARE != m_InputParamsArray[i].libType)
        {
            // TODO: can we avoid ifdef and use MFX_IMPL_VIA_VAAPI?
#if defined(_WIN32) || defined(_WIN64)
            m_eDevType = (MFX_IMPL_VIA_D3D11 == MFX_IMPL_VIA_MASK(m_InputParamsArray[i].libType))?
                MFX_HANDLE_D3D11_DEVICE :
                MFX_HANDLE_D3D9_DEVICE_MANAGER;
#elif defined(LIBVA_SUPPORT)
            m_eDevType = MFX_HANDLE_VA_DISPLAY;
            m_accelerationMode = MFX_ACCEL_MODE_VIA_VAAPI;
#endif
        }
        m_accelerationMode = MFX_ACCEL_MODE_VIA_VAAPI;
    }

    // Async depth between inter-sessions should be equal to the minimum async depth of all these sessions.
    for (mfxU32 i = 0; i < m_InputParamsArray.size(); i++)
    {
        if ((m_InputParamsArray[i].eMode == Source) || (m_InputParamsArray[i].eMode == Sink))
        {
            m_InputParamsArray[i].nAsyncDepth = minAsyncDepth;
        }
    }

    if(!areAllInterSessionsOpaque)
    {
        msdk_printf(MSDK_STRING("Some inter-sessions do not use opaque memory (possibly because of -o::raw).\nOpaque memory in all inter-sessions is disabled.\n"));
    }

    if (IsSinkPresence && !IsSourcePresence)
    {
        PrintError(MSDK_STRING("Error: Sink must be defined"));
        return MFX_ERR_UNSUPPORTED;
    }

    if(bSingleTexture)
    {
        bool showWarning = false;
        for (mfxU32 j = 0; j < m_InputParamsArray.size(); j++)
        {
            if (!m_InputParamsArray[j].bSingleTexture)
            {
                showWarning = true;
            }
            m_InputParamsArray[j].bSingleTexture = true;
        }
        if (showWarning)
        {
            msdk_printf(MSDK_STRING("WARNING: At least one session has -single_texture_d3d11 option, all other sessions are modified to have this setting enabled al well.\n"));
        }
    }

    return MFX_ERR_NONE;

} // mfxStatus Launcher::VerifyCrossSessionsOptions()

mfxStatus Launcher::CreateSafetyBuffers()
{
    SafetySurfaceBuffer* pBuffer     = NULL;
    SafetySurfaceBuffer* pPrevBuffer = NULL;

    if (m_sinkNum >= 2)
    {
        for (uint j = 0; j < m_sinkNum; j++)
        {
            SafetySurfaceBuffer* pBuffer     = NULL;
            SafetySurfaceBuffer* pPrevBuffer = NULL;
            for (mfxU32 i = 0; i < (m_sourceNum + 1); i++)
            {
                /* this is for 1 to N case*/
                if ((Source == m_InputParamsArray[i].eMode) &&
                        (Native == m_InputParamsArray[0].eModeExt))
                {
                    pBuffer = new SafetySurfaceBuffer(pPrevBuffer);
                    pPrevBuffer = pBuffer;
                    m_pBufferArray.push_back((std::unique_ptr<SafetySurfaceBuffer>(pBuffer)));
                }

                /* And N_to_1 case: composition should be enabled!
                 * else it is logic error */
                if ( (Source != m_InputParamsArray[i].eMode) &&
                        ( (VppComp     == m_InputParamsArray[0].eModeExt) ||
                          (VppCompOnly == m_InputParamsArray[0].eModeExt) ) )
                {
                    pBuffer = new SafetySurfaceBuffer(pPrevBuffer);
                    pPrevBuffer = pBuffer;
                    m_pBufferArray.push_back((std::unique_ptr<SafetySurfaceBuffer>(pBuffer)));
                }
            }
        }
    }
    else
    {
        for (mfxU32 i = 0; i < m_InputParamsArray.size(); i++)
        {
            /* this is for 1 to N case*/
            if ((Source == m_InputParamsArray[i].eMode) &&
                    (Native == m_InputParamsArray[0].eModeExt))
            {
                pBuffer = new SafetySurfaceBuffer(pPrevBuffer);
                pPrevBuffer = pBuffer;
                m_pBufferArray.push_back((std::unique_ptr<SafetySurfaceBuffer>(pBuffer)));
            }

            /* And N_to_1 case: composition should be enabled!
             * else it is logic error */
            if ( (Source != m_InputParamsArray[i].eMode) &&
                    ( (VppComp     == m_InputParamsArray[0].eModeExt) ||
                      (VppCompOnly == m_InputParamsArray[0].eModeExt) ) )
            {
                pBuffer = new SafetySurfaceBuffer(pPrevBuffer);
                pPrevBuffer = pBuffer;
                m_pBufferArray.push_back((std::unique_ptr<SafetySurfaceBuffer>(pBuffer)));

            }
        }
    }
    return MFX_ERR_NONE;

} // mfxStatus Launcher::CreateSafetyBuffers

void Launcher::Close()
{
    while (m_pThreadContextArray.size()) {
         m_pThreadContextArray[m_pThreadContextArray.size() - 1].reset();
         m_pThreadContextArray.pop_back();
     }

    m_pAllocArray.clear();
    m_pBufferArray.clear();
    m_pExtBSProcArray.clear();
    m_pAllocParams.clear();
    m_hwdevs.clear();

} // void Launcher::Close()

mfxStatus Launcher::VerifyRtspDumpOptions()
{
    bool RtspDumpOnly = false;
    mfxStatus sts = MFX_ERR_NONE;
    unsigned int totalSessions = m_InputParamsArray.size();
    for(unsigned int i = 0; i < totalSessions; i++)
    {
        if (m_InputParamsArray[i].RtspDumpOnly)
        {
            RtspDumpOnly = true;
        }
    }

    if (RtspDumpOnly)
    {
        for(unsigned int i = 0; i < totalSessions; i++)
        {
            if (!m_InputParamsArray[i].RtspDumpOnly)
            {
                msdk_printf(MSDK_STRING("[ERROR] when RTSP dump only mode is enabled in one session, the other sessions in the same par file must enable this mode as well\n"));
                sts = MFX_ERR_UNSUPPORTED;
            }
        }
    }
    return sts;
}

void Launcher::DoRtspDump()
{
    int totalSessions = m_InputParamsArray.size();
    if (totalSessions == 0)
    {
        return;
    }

    std::thread **rtsp_threads = new std::thread *[totalSessions];

    for(int i = 0; i < totalSessions; i++)
    {
        rtsp_threads[i] = new std::thread(FileAndRTSPBitstreamReader::RtspSaveToLocalFile,
                m_InputParamsArray[i].strSrcFile,
                m_InputParamsArray[i].strRtspSaveFile,
                m_InputParamsArray[i].MaxFrameNumber);
    }

    for(int i = 0; i < totalSessions; i++)
    {
        rtsp_threads[i]->join();
        delete rtsp_threads[i];
        rtsp_threads[i] = nullptr;
    }

    delete [] rtsp_threads;
    return;
}

#if defined(_WIN32) || defined(_WIN64)
int _tmain(int argc, TCHAR *argv[])
#else
mfxStatus start_video_e2e_thread(int argc, char *argv[])
#endif
{
    mfxStatus sts;
    Launcher transcode;
    if (argc < 2)
    {
        msdk_printf(MSDK_STRING("[ERROR] Command line is empty. Use -? for getting help on available options.\n"));
        return MFX_ERR_NONE;
    }

    sts = transcode.Init(argc, argv);
    if(sts == MFX_WRN_OUT_OF_RANGE)
    {
        // There's no error in parameters parsing, but we should not continue further. For instance, in case of -? option
        return MFX_ERR_NONE;
    }

    fflush(stdout);
    fflush(stderr);

    MSDK_CHECK_STATUS(sts, "transcode.Init failed");

    transcode.Run();

    sts = transcode.ProcessResult();
    fflush(stdout);
    fflush(stderr);
    MSDK_CHECK_STATUS(sts, "transcode.ProcessResult failed");

    return MFX_ERR_NONE;
}

int get_par_file_num(int argc, char *argv[])
{
    int par_file_count = 0;
    bool found = false;
    for (int i = 0; i < argc; i++)
    {
        if (0 == msdk_strncmp(MSDK_STRING("-par"), argv[i], msdk_strlen(MSDK_STRING("-par"))))
        {
            //Multiple options "-par " are not allowed
            if (found)
            {
                msdk_printf(MSDK_STRING("[ERROR] Only one -par option is allowd in command line\n"));
                return -1;
            }
            found = true;
        }
        else if (found){
            if (0 != msdk_strncmp(MSDK_STRING("-"), argv[i], msdk_strlen(MSDK_STRING("-"))))
            {
                par_file_count++;
            }
            else
            {
                break; 
            }
        }
    }
    msdk_printf(MSDK_STRING("Found %d par file in command line\n"), par_file_count);
    return par_file_count;
}

void free_thread_argv(int par_file_num, char ***argv_pool)
{
    for (int i = 0; i < par_file_num; i++)
    {
        delete [] argv_pool[i];
    }
    return;
}

int set_up_thread_argv(int argc, char *argv[], int par_file_num, char ***argv_pool)
{
    if (argc - par_file_num < 2)
    {
        msdk_printf(MSDK_STRING("[ERROR] Par file number(%d) is not correct\n"), par_file_num);
        return -1;
    }

    for (int i = 0; i < par_file_num; i++)
    {
        //The last argument must be nullptr
        argv_pool[i] = new char *[argc - par_file_num + 2];
        argv_pool[i][argc - par_file_num + 1] = nullptr;
    }

    bool found = false;
    int thread_argv_idx = 0;
    int par_file_count = 0;
    for (int i = 0; i < argc; i++)
    {
        if (0 == msdk_strncmp(MSDK_STRING("-par"), argv[i], msdk_strlen(MSDK_STRING("-par"))))
        {
            found = true;
        }
        else if (found){
            if (0 != msdk_strncmp(MSDK_STRING("-"), argv[i], msdk_strlen(MSDK_STRING("-"))))
            {
                argv_pool[par_file_count][thread_argv_idx] = argv[i];
                par_file_count++;
                continue;
            }
            else
            {
                //Completed processing par file options
                found = false;
                thread_argv_idx++;
            }
        }

        for (int j = 0; j < par_file_num; j++)
        {
            argv_pool[j][thread_argv_idx] = argv[i];
        }
        thread_argv_idx++;
    }
    return 0;
}

void sig_handler(int sig)
{
    if (sig == SIGINT || sig == SIGHUP)
    {
        GlobalValue::AppStop = true;
        usleep(500000);//500ms
        exit(0);
    }
}

int main(int argc, char *argv[])
{
    char* install_overwrite_env = nullptr;
    if ((install_overwrite_env = getenv("SVET_INSTALL_OVERWRITE")) == nullptr
        || strncmp(install_overwrite_env, "True", 4) != 0)
    {
        string LD_MSDK_PATH = "/opt/intel/svet/onevpl/lib";
        char* ld_path_env = nullptr;
        unsigned int msdk_path_length = LD_MSDK_PATH.length();
        if ((ld_path_env = getenv("LD_LIBRARY_PATH")) != nullptr)
        {
            if (strnlen(ld_path_env, msdk_path_length + 1) < msdk_path_length || strncmp(LD_MSDK_PATH.c_str(), ld_path_env, msdk_path_length) != 0)
            {
                printf("[ERROR] Link to wrong MediaSDK Libraries.\nPlease run 'source ./svet_env_setup.sh' to set up the right environment!\n");
                return 0;
            }
        }

    }

    signal(SIGINT, sig_handler);
    signal(SIGHUP, sig_handler);

    int par_file_num = get_par_file_num(argc, argv);

    if (par_file_num <= 1)
    {
        start_video_e2e_thread(argc, argv);
        return 0;
    }

    if (par_file_num > VIDEO_E2E_MAX_DISPLAY)
    {
        msdk_printf(MSDK_STRING("[ERROR] the par file number(%d) exceed the limit (%d)\n"),
                par_file_num, VIDEO_E2E_MAX_DISPLAY);
        return 0;
    }

    std::thread ** thread_pool = new std::thread *[par_file_num]; 
    char *** argv_pool = new char **[par_file_num];
    int argc_thread = argc - par_file_num + 1;
    int ret = set_up_thread_argv(argc, argv, par_file_num, argv_pool);
    if (ret == 0)
    {
        for (int i = 0; i < par_file_num; i++)
        {
            //Create a thread for every par file
            thread_pool[i] = new std::thread(start_video_e2e_thread, argc_thread, argv_pool[i]);
            //Make sure the 1st par file get the the first display, 2nd par file get second display and so on
            usleep(5);
        }

        for (int i = 0; i < par_file_num; i++)
        {
            thread_pool[i]->join();
            delete thread_pool[i];
            thread_pool[i] = nullptr;
        }

        free_thread_argv(par_file_num, argv_pool);
    }

    delete [] argv_pool;
    argv_pool = nullptr;
    delete [] thread_pool;
    thread_pool = nullptr;
    return 0;
}

