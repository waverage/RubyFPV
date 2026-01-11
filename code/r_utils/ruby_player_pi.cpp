#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <linux/random.h>
#include <inttypes.h>
#include <semaphore.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>

#include "../base/base.h"
#include "../base/config.h"
#include "../base/config_obj_names.h"
#include "../base/hardware.h"
#include "../base/hardware_procs.h"
#include "../base/hdmi.h"
#include "../base/parser_h264.h"
#include "../renderer/drm_core.h"
#include <ctype.h>
#include <sys/ioctl.h>
#include "../base/shared_mem.h"
#include "../r_station/shared_vars.h"

#ifndef HW_PLATFORM_RASPBERRY_PI5
#error "ONLY FOR PI5 PLATFORM!"
#endif


#include "../base/ctrl_settings.h"
#include "../renderer/drm_core.h"
#include "../renderer/render_engine.h"
#include "../renderer/render_engine_cairo.h"

// FFmpeg Headers (The Pi 5 Engine)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}


//bool g_bQuit = false;
bool g_bDebug = false;
bool g_bPlayFile = false;
bool g_bPlayingIntro = false;
bool g_bExitOnEnd = false;
bool g_bPlayStreamPipe = false;
bool g_bPlayStreamUDP = false;
bool g_bPlayStreamSM = false;
bool g_bInitUILayerToo = false;
bool g_bUseH265Decoder = false;

int g_drm_fd = -1;

char g_szPlayFileName[MAX_FILE_PATH_SIZE];
int g_iFileFPS = 30;
int g_iFileTempSlices = 1;
int g_iFileDetectedSlices = 1;
int g_iCustomWidth = 0;
int g_iCustomHeight = 0;
int g_iCustomRefresh = 0;
u32 g_uCPUAffinityMask = 0;
int g_iRawPriority = -1;

#define PIPE_BUFFER_SIZE 200000
u8 g_uPipeBuffer[PIPE_BUFFER_SIZE];
int g_iPipeBufferWritePos = 0;
int g_iPipeBufferReadPos = 0;

sem_t* s_pSemaphoreSMData = NULL;

// FFmpeg Contexts
AVCodecContext *g_pCodecCtx = NULL;
AVPacket *g_pPacket = NULL;
AVFrame *g_pFrame = NULL;
AVCodecParserContext *g_pParser = NULL;
struct SwsContext* g_pSwsCtx = NULL;

shared_mem_process_stats* g_pSMProcessStats = NULL;

// --- PI 5 DECODER INIT ---
int pi5_init_decoder(bool h265) {
   // h264_v4l2m2m is the hardware accelerated path for Pi 5
   const char* codec_name = h265 ? "hevc_v4l2m2m" : "h264";

   log_line("[PLAYER] Pi 5 Initializing hardware decoder (%s)...", codec_name);

   const AVCodec *codec = avcodec_find_decoder(h265 ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);//avcodec_find_decoder_by_name(codec_name);
   if (!codec) {
      // codec = avcodec_find_decoder(h265 ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);

      // if (!codec) {
      log_softerror_and_alarm("Pi 5 HW Decoder %s not found. Check FFmpeg installation.", codec_name);
      return -1;
      // }
   }

   g_pCodecCtx = avcodec_alloc_context3(codec);
   if (h265) {
      AVDictionary *options = NULL;
      av_dict_set(&options, "video_device", "/dev/video19", 0);
      if (avcodec_open2(g_pCodecCtx, codec, &options) < 0) 
      {
         log_softerror_and_alarm("Failed to open Pi 5 HW h265 decoder.");
         return -1;
      }
   } else {
      if (avcodec_open2(g_pCodecCtx, codec, NULL) < 0) {
         log_softerror_and_alarm("Failed to open Pi 5 HW decoder.");
         return -1;
      }
   }   

   g_pPacket = av_packet_alloc();
   g_pFrame = av_frame_alloc();

   g_pParser = av_parser_init(codec->id);
   if (!g_pParser) {
      log_softerror_and_alarm("Failed to initialize parser");
      return -1;
   }

   // Inside pi5_init_decoder:
   // We create a context to convert YUV420P (from decoder) to NV12 (for DRM)
   g_pSwsCtx = sws_getContext(hdmi_get_current_resolution_width(), hdmi_get_current_resolution_height(), AV_PIX_FMT_YUV420P,
                           hdmi_get_current_resolution_width(), hdmi_get_current_resolution_height(), AV_PIX_FMT_NV12,
                           SWS_POINT, NULL, NULL, NULL);

   log_line("[PLAYER] Pi 5 Hardware Decoder (%s) Initialized.", codec_name);
   return 0;
}

// void ruby_drm_render_nv12_from_avframe(AVFrame* frame) {
//     if (!frame || !g_pSwsCtx) return;

//     static int last_w = 0;
//     static int last_h = 0;

//     // 1. Get the current active Ruby DRM back-buffer
//     type_drm_buffer* back_buffer = ruby_drm_core_get_back_draw_buffer();

//     int dst_stride = (int)back_buffer->uStride; // This is 1920
//     int screen_h = hdmi_get_current_resolution_height(); // This is 1080
    
//     // 2. Point to our destination memory
//     // dst_data[0] = Y plane (starts at beginning)
//     // dst_data[1] = UV plane (starts exactly after Y plane)
//     uint8_t* dst_data[2] = { 
//         back_buffer->pData, 
//         back_buffer->pData + (dst_stride * screen_h) 
//     };
    
//     // CRITICAL: We MUST use the hardware stride (e.g., 1920) for the destination
//     int dst_linesize[2] = { dst_stride, dst_stride };

//     // 3. Convert YUV420P (3 planes) -> NV12 (2 planes)
//     // This handles the colors and the row-alignment simultaneously
//     sws_scale(g_pSwsCtx, 
//               (const uint8_t* const*)frame->data, frame->linesize, 
//               0, frame->height, 
//               dst_data, dst_linesize);

//     // 4. Handle First-Frame or Resolution Change
//     if (frame->width != last_w || frame->height != last_h) {
//         log_line("[PLAYER] Resolution changed to %dx%d. Updating DRM Plane.", frame->width, frame->height);

//         log_line("=====================================================");
//         log_line("[DEBUG] FRAME RESOLUTION: %d x %d", frame->width, frame->height);
//         log_line("[DEBUG] FRAME FORMAT:     %d (NV12 is 23, YUV420P is 0)", frame->format);
//         log_line("[DEBUG] FRAME STRIDES:    Y: %d, U: %d, V: %d", frame->linesize[0], frame->linesize[1], frame->linesize[2]);
//         log_line("[DEBUG] DRM BUFFER SIZE:  %u bytes", back_buffer->uSize);
//         log_line("[DEBUG] DRM STRIDE:       %u bytes", back_buffer->uStride);
//         log_line("[DEBUG] DST STRIDE:       %d, %d", dst_linesize[0], dst_linesize[1]);
//         log_line("[DEBUG] DRM FB ID:        %u", back_buffer->uBufferId);
//         log_line("=====================================================");
        
//         // Tell the Pi 5 HVS hardware to refresh its scaling math
//         ruby_drm_set_video_source_size(frame->width, frame->height);
//         ruby_drm_core_set_plane_properties_and_buffer(back_buffer->uBufferId);
        
//         last_w = frame->width;
//         last_h = frame->height;
//     }

//     // 5. Flip the buffers
//     ruby_drm_swap_mainback_buffers();
// }

void ruby_drm_render_nv12_from_avframe(AVFrame* frame) {
    if (!frame) return;

    type_drm_buffer* back_buffer = ruby_drm_core_get_back_draw_buffer();
    uint8_t* fb_map = back_buffer->pData;
    
    // --- THE HARDWARE PARAMETERS ---
    int dst_stride = back_buffer->uStride;        // 1920 (The memory "width")
    int screen_h = hdmi_get_current_resolution_height(); // 1080 (The memory "height")

    // --- THE VIDEO PARAMETERS ---
    int width = frame->width;   // 1280
    int height = frame->height; // 720

    // 1. Copy Y-Plane (Luminance)
    // We copy row-by-row to ensure 1280 pixels are centered in the 1920 stride.
    uint8_t* src_y = frame->data[0];
    for (int i = 0; i < height; i++) {
        memcpy(fb_map + (i * dst_stride), 
               src_y + (i * frame->linesize[0]), 
               width);
    }

    // 2. Interleave and Copy UV-Plane (Chrominance)
    // FIX: The UV plane MUST start after the full 1080p Y-buffer (dst_stride * 1080).
    // If you use height (720), the hardware won't find the color data.
    uint8_t* dst_uv = fb_map + (dst_stride * screen_h); 
    
    uint8_t* src_u = frame->data[1];
    uint8_t* src_v = frame->data[2];

    for (int i = 0; i < height / 2; i++) {
        uint8_t* row_u = src_u + (i * frame->linesize[1]);
        uint8_t* row_v = src_v + (i * frame->linesize[2]);
        uint8_t* row_dst = dst_uv + (i * dst_stride);

        // We manually "weave" U and V bytes to create the NV12 format.
        // This fixes the green color bug without using swscale.
        for (int j = 0; j < width / 2; j++) {
            row_dst[j*2] = row_u[j];     // U byte
            row_dst[j*2 + 1] = row_v[j]; // V byte
        }
    }

    // Update Ruby state for scaling
    static int s_last_w = 0, s_last_h = 0;
    if (width != s_last_w || height != s_last_h) {
        ruby_drm_set_video_source_size(width, height);
        ruby_drm_core_set_plane_properties_and_buffer(back_buffer->uBufferId);
        s_last_w = width; s_last_h = height;
    }

    ruby_drm_swap_mainback_buffers();
}

// --- FEED DATA & RENDER ---
void pi5_decode_and_display(u8* pData, int iLen) {
    g_pPacket->data = pData;
    g_pPacket->size = iLen;

    if (avcodec_send_packet(g_pCodecCtx, g_pPacket) < 0) return;

   static int f_count = 0;

   while (avcodec_receive_frame(g_pCodecCtx, g_pFrame) >= 0) {
        // The Pi 5 HW decoder outputs NV12 format.
        // We push this directly to the DRM plane we initialized in _do_mode
        // Note: You must ensure ruby_drm_core_init was called with DRM_FORMAT_NV12
        
        // This is where we bridge to the DRM logic we fixed earlier:
        // ruby_drm_core_render_nv12_frame(g_pFrame->data[0], g_pFrame->data[1], width, height);
      //log_line("[PLAYER] Decoder format: %d (NV12 is %d, YUV420P is %d)", 
      //    g_pFrame->format, AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P);
      ruby_drm_render_nv12_from_avframe(g_pFrame);
      f_count++;

      if (f_count % 100 == 0) log_line("[PLAYER] Rendered 100 frames to DRM.");
   }
}

// --- REWRITTEN PIPE MODE ---
void _do_stream_mode_pipe() {
   log_line("[PLAYER] _do_stream_mode_pipe");

   int readfd = open(FIFO_RUBY_STATION_VIDEO_STREAM_DISPLAY, O_RDONLY);
   if (readfd < 0) {
      log_error_and_alarm("[PLAYER] Failed to open video FIFO.");
      return;
   }

   log_line("[PLAYER] wait for display to be connected");

   // 1. Initialize HDMI and DRM Layers (following your file player logic)
   ruby_drm_core_wait_for_display_connected(); 
   if (hdmi_enum_modes() < 0) {
      log_error_and_alarm("[PLAYER] Failed to enumerate HDMI modes.");
      close(readfd);
      return;
   }

   int iHDMIIndex = hdmi_load_current_mode();
   if ( iHDMIIndex < 0 )
      iHDMIIndex = hdmi_get_best_resolution_index_for(DEFAULT_RADXA_DISPLAY_WIDTH, DEFAULT_RADXA_DISPLAY_HEIGHT, DEFAULT_RADXA_DISPLAY_REFRESH);
   
   int w = hdmi_get_current_resolution_width();
   int h = hdmi_get_current_resolution_height();
   int r = hdmi_get_current_resolution_refresh();
   log_line("[PLAYER] HDMI mode to use: %d (%d x %d @ %d)", iHDMIIndex,  w, h, r);

   if (g_bInitUILayerToo) {
      log_line("[PLAYER] Init display UI layer too...");
      ruby_drm_core_init(0, DRM_FORMAT_ARGB8888, w, h, r);
      ruby_drm_core_set_plane_properties_and_buffer(ruby_drm_core_get_main_draw_buffer_id());
   }

   log_line("[PLAYER] Init display video layer...");
   ruby_drm_core_init(1, DRM_FORMAT_NV12, w, h, r);
   log_line("[PLAYER] Done init display video layer.");

   // 2. Init Pi 5 Hardware Decoder
   if (pi5_init_decoder(g_bUseH265Decoder) != 0) {
      log_error_and_alarm("[PLAYER] pi5_init_decoder failed");
      close(readfd);
      ruby_drm_core_uninit();
      return;
   }

   log_line("[PLAYER] Starting Pi 5 Pipe Player Loop (W:%d H:%d @%d)...", w, h, r);

   int nRead = 0;
   bool bAnyInputEver = false;
   u32 uTimeLastCheck = get_current_timestamp_ms();
   int iTotalRead = 0;

   // Use the same buffer size as the file player for consistency
   int iMaxReadSize = 1024 * 64;
   u8* pipe_buffer = (u8*)malloc(iMaxReadSize);

   while (!g_bQuit) {
      if (g_pSMProcessStats) {
         g_pSMProcessStats->lastActiveTime = get_current_timestamp_ms();
      }

      nRead = read(readfd, pipe_buffer, iMaxReadSize);

      if (nRead < 0) {
         if (errno == EAGAIN || errno == EINTR) continue;
         log_line("[PLAYER] Error reading pipe: %s", strerror(errno));
         break;
      }

      if (nRead == 0) {
         hardware_sleep_micros(1000);
         continue;
      }

      if (!bAnyInputEver) {
         log_line("[PLAYER] First stream data received (%d bytes)", nRead);
         bAnyInputEver = true;
      }

      // --- THE PARSER BRIDGE ---
      // This takes the raw pipe chunk and feeds the decoder only full NAL units
      u8* data = pipe_buffer;
      int size = nRead;

      while (size > 0 && !g_bQuit) {
         int len = av_parser_parse2(g_pParser, g_pCodecCtx, 
                                    &g_pPacket->data, &g_pPacket->size,
                                    data, size, 
                                    AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
         data += len;
         size -= len;

         if (g_pPacket->size > 0) {
               // Now we have a guaranteed complete frame/NAL unit
               pi5_decode_and_display(g_pPacket->data, g_pPacket->size);
         }
      }

      // --- STATS LOGGING ---
      iTotalRead += nRead;
      u32 uTime = get_current_timestamp_ms();
      if (uTime > uTimeLastCheck + 4000) {
         log_line("[PLAYER] Pipe player alive, bitrate: %d kbps", (iTotalRead * 8) / (uTime - uTimeLastCheck));
         iTotalRead = 0;
         uTimeLastCheck = uTime;
      }
   }

   log_line("[PLAYER] Cleaning up pipe player...");
   free(pipe_buffer);
   close(readfd);

   // Standard FFmpeg cleanup
   if (g_pFrame) av_frame_free(&g_pFrame);
   if (g_pPacket) av_packet_free(&g_pPacket);
   if (g_pCodecCtx) avcodec_free_context(&g_pCodecCtx);
   if (g_pParser) av_parser_close(g_pParser);

   ruby_drm_core_uninit();
}

void _signal_play_file_will_finish()
{
   if ( ! g_bExitOnEnd )
      return;

   sem_t* ps = sem_open(SEMAPHORE_VIDEO_FILE_PLAYBACK_WILL_FINISH, O_CREAT, S_IWUSR | S_IRUSR, 0);
   if ( (NULL != ps) && (SEM_FAILED != ps) )
   {
      log_line("Signaling semaphore that playback will finish (%s)", SEMAPHORE_VIDEO_FILE_PLAYBACK_WILL_FINISH);
      sem_post(ps);
      sem_close(ps);
   }
   else
      log_softerror_and_alarm("Failed to open and signal semaphore %s", SEMAPHORE_VIDEO_FILE_PLAYBACK_WILL_FINISH);
   hardware_sleep_ms(100);
}

void _signal_play_file_finished()
{
   if ( ! g_bExitOnEnd )
      return;

   sem_t* ps = sem_open(SEMAPHORE_VIDEO_FILE_PLAYBACK_FINISHED, O_CREAT, S_IWUSR | S_IRUSR, 0);
   if ( (NULL != ps) && (SEM_FAILED != ps) )
   {
      log_line("Signaling semaphore that playback finished (%s)", SEMAPHORE_VIDEO_FILE_PLAYBACK_FINISHED);
      sem_post(ps);
      sem_close(ps);
   }
   else
      log_softerror_and_alarm("Failed to open and signal semaphore %s", SEMAPHORE_VIDEO_FILE_PLAYBACK_FINISHED);
}

void _do_player_mode()
{
   ruby_drm_core_wait_for_display_connected(); 
   if ( hdmi_enum_modes() < 0 )
   {
      log_error_and_alarm("[PLAYER] Failed to enumerate HDMI modes. Exit player.");
      _signal_play_file_will_finish();
      _signal_play_file_finished();
      return;
   }

   int iHDMIIndex = hdmi_load_current_mode();
   if ( iHDMIIndex < 0 )
      iHDMIIndex = hdmi_get_best_resolution_index_for(DEFAULT_RADXA_DISPLAY_WIDTH, DEFAULT_RADXA_DISPLAY_HEIGHT, DEFAULT_RADXA_DISPLAY_REFRESH);
   
   int w = hdmi_get_current_resolution_width();
   int h = hdmi_get_current_resolution_height();
   int r = hdmi_get_current_resolution_refresh();
   log_line("[PLAYER] HDMI mode to use: %d (%d x %d @ %d)", iHDMIIndex,  w, h, r);

   if ( g_bInitUILayerToo )
   {
      log_line("[PLAYER] Init display UI layer too...");
      ruby_drm_core_init(0, DRM_FORMAT_ARGB8888,  w, h, r);
      //ruby_drm_swap_mainback_buffers();
      ruby_drm_core_set_plane_properties_and_buffer(ruby_drm_core_get_main_draw_buffer_id());

      log_line("[PLAYER] Done init display UI layer too.");
   }

   log_line("[PLAYER] Init display video layer...");
   // Init on a "overlay" plane here
   ruby_drm_core_init(1, DRM_FORMAT_NV12, w, h, r);
   log_line("[PLAYER] Done init display video layer.");

   // 2. Init Pi 5 Decoder
   if (pi5_init_decoder(g_bUseH265Decoder) != 0) {
      log_error_and_alarm("[PLAYER] pi5_init_decoder failed");
      _signal_play_file_will_finish();
      ruby_drm_core_uninit();
      _signal_play_file_finished();
      return;
   }

   FILE* fp = fopen(g_szPlayFileName,"rb");
   if ( NULL == fp )
   {
      log_error_and_alarm("[PLAYER] Failed to open input file [%s]. Exit.", g_szPlayFileName);
      _signal_play_file_will_finish();
      av_frame_free(&g_pFrame);
      av_packet_free(&g_pPacket);
      avcodec_free_context(&g_pCodecCtx);
      ruby_drm_core_uninit();
      _signal_play_file_finished();
      return;
   }

   log_line("[PLAYER] Opened input video file (%s), has %d FPS", g_szPlayFileName, g_iFileFPS);

   log_line("[PLAYER] Starting Pi 5 Pipe Player Loop...");

   int nRead = 1;
   int iCount = 0;
   u32 uTimeLastCheck = get_current_timestamp_ms();
   unsigned char uBuffer[4096];

   u32 uCurrentParseToken = 0x11111111;
   u32 uNALType = 0;
   u32 uPrevNALType = 0;
   u32 uTimeLastFrame = 0;
   int iTotalRead = 0;
   u8* file_buffer = (u8*)malloc(1024 * 64);

   while ( (nRead > 0) && (!g_bQuit) )
   {
      nRead = fread(file_buffer, 1, 1024 * 64, fp);
      if ( nRead <= 0 )
         break;
      
      u8* data = file_buffer;
      int size = nRead;

      while (size > 0) {
         // This function takes your raw 64KB chunk and finds the next full frame
         int len = av_parser_parse2(g_pParser, g_pCodecCtx, 
                                    &g_pPacket->data, &g_pPacket->size,
                                    data, size, 
                                    AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
         data += len;
         size -= len;

         if (g_pPacket->size > 0) {
            if ( g_bQuit )
               break;

            // Now we are sending a GUARANTEED complete NAL unit to the decoder
            pi5_decode_and_display(g_pPacket->data, g_pPacket->size);
         }
      }

      while ( (access("/tmp/pausedvr", R_OK) != -1) && (!g_bQuit) )
      {
         struct timespec to_sleep = { 0, (long int)(50*1000*1000) };
         clock_nanosleep(RUBY_HW_CLOCK_ID, 0, &to_sleep, NULL);
      }

      if ( g_bQuit )
         break;
      
      //here
      // pi5_decode_and_display(uBuffer, nRead);
      
      iTotalRead += nRead;
      if ( (iCount % 10) == 0 )
      {
         u32 uTime = get_current_timestamp_ms();
         if ( uTime > uTimeLastCheck + 4000 )
         {
            uTimeLastCheck = uTime;
            log_line("[PLAYER] Video player alive, reading %d bits/sec", iTotalRead*8/4);
            iTotalRead = 0;
         }
      }
   }

   _signal_play_file_will_finish();
   av_frame_free(&g_pFrame);
   av_packet_free(&g_pPacket);
   avcodec_free_context(&g_pCodecCtx);
   ruby_drm_core_uninit();
   _signal_play_file_finished();
   free(file_buffer);
}

void _do_stream_mode_sm()
{
}

void _do_stream_mode_udp()
{
}

void handle_sigint(int sig) 
{ 
   log_line("Caught signal to stop: %d", sig);
   g_bQuit = true;
}

int main(int argc, char *argv[])
{
   if ( strcmp(argv[argc-1], "-ver") == 0 )
   {
      printf("%d.%d (b-%d)", SYSTEM_SW_VERSION_MAJOR, SYSTEM_SW_VERSION_MINOR, SYSTEM_SW_BUILD_NUMBER);
      return 0;
   }

   if ( argc < 2 )
   {
      printf("\nUsage: ruby_player_pi [params]\nParams:\n\n");
      printf("-p Play the live video stream from pipe\n");
      printf("-u Play the live video stream from UDP socket\n");
      printf("-sm Play the live video stream from sharedmem\n");
      printf("-h265 use H265 decoder\n");
      printf("-af [aff] cpu affinity\n");
      printf("-rawp [prio] raw priority\n");
      printf("-file [filename] play a file\n");
      printf("-fps [fps]\n");
      printf("-endexit exit on end\n");
      printf("-m [wxh@r] sets a custom video mode\n");
      printf("-b playing intro\n");
      printf("-i init UI layer too when playing stream or files\n");
      printf("-drmfd Pass DRM file descriptor\n");
      printf("-d debug output to stdout\n\n");
      return 0;
   }

   g_szPlayFileName[0] = 0;
   int iParam = 0;

   do
   {
      if ( 0 == strcmp(argv[iParam], "-p") )
         g_bPlayStreamPipe = true;
      if ( 0 == strcmp(argv[iParam], "-u") )
         g_bPlayStreamUDP = true;
      if ( 0 == strcmp(argv[iParam], "-sm") )
         g_bPlayStreamSM = true;
      if ( 0 == strcmp(argv[iParam], "-i") )
         g_bInitUILayerToo = true;
      if ( 0 == strcmp(argv[iParam], "-b") )
         g_bPlayingIntro = true;
      if ( 0 == strcmp(argv[iParam], "-h265") )
         g_bUseH265Decoder = true;
      if ( 0 == strcmp(argv[iParam], "-endexit") )
         g_bExitOnEnd = true;

      if ( 0 == strcmp(argv[iParam], "-rawp") )
      {
         g_iRawPriority = atoi(argv[iParam+1]);
         iParam++;
      }

      if ( 0 == strcmp(argv[iParam], "-af") )
      {
         g_uCPUAffinityMask = (uint32_t) atoi(argv[iParam+1]);
         iParam++;
      }

      if ( 0 == strcmp(argv[iParam], "-d") )
      {
         g_bDebug = true;
         log_enable_stdout();
      }

      if ( 0 == strcmp(argv[iParam], "-fps") )
      {
         g_iFileFPS = atoi(argv[iParam+1]);
         if ( (g_iFileFPS < 10) || (g_iFileFPS > 240) )
            g_iFileFPS = 30;
         iParam++;
      }

      if ( 0 == strcmp(argv[iParam], "-file") )
      {
         g_bPlayFile = true;
         iParam++;
         strncpy(g_szPlayFileName, argv[iParam], MAX_FILE_PATH_SIZE);
         if ( NULL != strstr(g_szPlayFileName, ".h265") )
            g_bUseH265Decoder = true;
         iParam++;
         continue;
      }
      if ( 0 == strcmp(argv[iParam], "-m") )
      {
         iParam++;
         char szTmp[256];
         strncpy(szTmp, argv[iParam], 255);
         szTmp[255] = 0;
         for( int i=0; i<(int)strlen(szTmp); i++ )
         {
            if ( ! isdigit(szTmp[i]) )
               szTmp[i] = ' ';
         }

         if ( 3 != sscanf(szTmp, "%d %d %d", &g_iCustomWidth, &g_iCustomHeight, &g_iCustomRefresh) )
         {
            g_iCustomWidth = 0;
            g_iCustomHeight = 0;
            g_iCustomRefresh = 0;
         }
         continue;
      }

      if ( 0 == strcmp(argv[iParam], "-drmfd") )
      {
         g_drm_fd = atoi(argv[iParam+1]);
         iParam++;
         log_line("[PLAYER] Got -drmfd %d", g_drm_fd);
         ruby_drm_core_set_fd(g_drm_fd);
         continue;
      }

      iParam++;
   }
   while (iParam < argc);

   if ( g_bPlayFile )
      log_init("RubyPlayerF");
   else if ( g_bPlayStreamPipe )
      log_init("RubyPlayerP");
   else if ( g_bPlayStreamUDP )
      log_init("RubyPlayerU");
   else if ( g_bPlayStreamSM )
      log_init("RubyPlayerS");
   else
      log_init("RubyPlayer");

   load_ControllerSettings();

   log_line("Raw priority param: %d, CPU affinity mask param: %d", g_iRawPriority, g_uCPUAffinityMask);

   pthread_t this_thread = pthread_self();
   struct sched_param params;
   int policy = 0;
   int ret = 0;
   ret = pthread_getschedparam(this_thread, &policy, &params);
   if ( ret != 0 )
      log_softerror_and_alarm("Failed to get initial schedule param");
   else
      log_line("Initial thread policy/priority: %d/%d", policy, params.sched_priority);


   if ( (g_iRawPriority > 1) && (g_iRawPriority < 100) )
   {
      params.sched_priority = 100 - g_iRawPriority;
      ret = pthread_setschedparam(this_thread, SCHED_FIFO, &params);
      if ( ret != 0 )
         log_softerror_and_alarm("Failed to set thread schedule class, error: %d, %s", errno, strerror(errno));
      else
         log_line("Did set new thread priority.");
   }

   if ( g_uCPUAffinityMask > 0 )
   {
      cpu_set_t cpuSet;
      CPU_ZERO(&cpuSet);
      for( int i=0; i<8; i++ )
      {
         if ( g_uCPUAffinityMask & (0x01<<i) )
            CPU_SET(i, &cpuSet);
      }
      pid_t pid = getpid();
      if ( 0 != sched_setaffinity(pid, sizeof(cpuSet), &cpuSet) )
         log_line("Failed to set affinities for the entire process, error: %d (%s)", errno, strerror(errno)); 
      else
         log_line("Did set affinities for the entire process, to mask: %d", g_uCPUAffinityMask);

      if ( 0 != pthread_setaffinity_np(this_thread, sizeof(cpuSet), &cpuSet) )
         log_line("Failed to set cpu affinity for main thread, error: %d, (%s)", errno, strerror(errno));
      else
         log_line("Did set cpu affinity for main thread to mask: %u", g_uCPUAffinityMask);
   }

   signal(SIGINT, handle_sigint);
   signal(SIGTERM, handle_sigint);
   signal(SIGQUIT, handle_sigint);

   ret = pthread_getschedparam(this_thread, &policy, &params);
   if ( ret != 0 )
      log_softerror_and_alarm("Failed to get new schedule param");
   else
      log_line("New thread policy/priority: %d/%d", policy, params.sched_priority);

   g_pSMProcessStats = shared_mem_process_stats_open_write(SHARED_MEM_WATCHDOG_MPP_PLAYER);
   if ( NULL == g_pSMProcessStats )
      log_softerror_and_alarm("Failed to open shared mem for process watchdog for writing: %s", SHARED_MEM_WATCHDOG_MPP_PLAYER);
   else
      log_line("Opened shared mem for process watchdog for writing (%s).", SHARED_MEM_WATCHDOG_MPP_PLAYER);
 
   g_pProcessStatsCentral = shared_mem_process_stats_open_read(SHARED_MEM_WATCHDOG_CENTRAL);
   if ( NULL == g_pProcessStatsCentral )
      log_softerror_and_alarm("Failed to open shared mem for ruby_central process watchdog for writing: %s", SHARED_MEM_WATCHDOG_CENTRAL);
   else
      log_line("Opened shared mem for ruby_centrall process watchdog for writing.");

   if ( g_bPlayFile )
      log_line("Running mode: play file: [%s] [%d FPS] [exit on end: %s] [playing intro: %s]", g_szPlayFileName, g_iFileFPS, g_bExitOnEnd?"yes":"no", g_bPlayingIntro?"yes":"no");
   if ( g_bPlayStreamPipe )
      log_line("Running mode: stream from pipe");
   if ( g_bPlayStreamUDP )
      log_line("Running mode: stream from UDP");
   if ( g_bPlayStreamSM )
      log_line("Running mode: stream from sharedmem");
   if ( 0 != g_iCustomWidth )
      log_line("Set custom video mode: %dx%d@%d", g_iCustomWidth, g_iCustomHeight, g_iCustomRefresh);

   if (g_pProcessStatsCentral->drmFd > 0) {
      log_line("[PLAYER] Got -drmfd %d from shared mem", g_pProcessStatsCentral->drmFd);
      ruby_drm_core_set_fd(g_pProcessStatsCentral->drmFd);
   }

   if ( (!g_bPlayFile) && (!g_bPlayStreamPipe) && (!g_bPlayStreamUDP) && (!g_bPlayStreamSM) )
   {
      log_softerror_and_alarm("Invalid params, no mode specified. Exit.");
      shared_mem_process_stats_close(SHARED_MEM_WATCHDOG_MPP_PLAYER, g_pSMProcessStats);
      shared_mem_process_stats_close(SHARED_MEM_WATCHDOG_CENTRAL, g_pProcessStatsCentral);
      return 0;
   }
   else if ( g_bPlayFile )
      _do_player_mode();
   else if ( g_bPlayStreamPipe )
      _do_stream_mode_pipe();
   else if ( g_bPlayStreamUDP )
      _do_stream_mode_udp();
   else if ( g_bPlayStreamSM )
      _do_stream_mode_sm();

   log_line("Cleaning up on exit...");
   shared_mem_process_stats_close(SHARED_MEM_WATCHDOG_MPP_PLAYER, g_pSMProcessStats);
   shared_mem_process_stats_close(SHARED_MEM_WATCHDOG_CENTRAL, g_pProcessStatsCentral);
   log_line("Will exit now");
   return 0;
}

