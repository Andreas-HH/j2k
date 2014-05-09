#include <stdio.h>
#include <stdlib.h>
#include <libavformat/avformat.h>


int open_codec_context(int *stream_idx, AVFormatContext *fmt_ctx, enum AVMediaType type, const char *src_filename) {
  int ret;
  AVStream *st;
  AVCodecContext *dec_ctx = NULL;
  AVCodec *dec = NULL;
  AVDictionary *opts = NULL;

  ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
  if (ret < 0) {
    fprintf(stderr, "Could not find %s stream in input file '%s'\n", av_get_media_type_string(type), src_filename);
    return ret;
  } else {
    *stream_idx = ret;
    st = fmt_ctx->streams[*stream_idx];

    /* find decoder for the stream */
    dec_ctx = st->codec;
    dec = avcodec_find_decoder(dec_ctx->codec_id);
    if (!dec) {
      fprintf(stderr, "Failed to find %s codec\n", av_get_media_type_string(type));
      return ret;
    }

    /* Init the decoders, with or without reference counting */
//     if (api_mode == API_MODE_NEW_API_REF_COUNT)
//       av_dict_set(&opts, "refcounted_frames", "1", 0);
    if ((ret = avcodec_open2(dec_ctx, dec, &opts)) < 0) {
      fprintf(stderr, "Failed to open %s codec\n", av_get_media_type_string(type));
      return ret;
    }
  }

  return 0;
}

int decode_packet(int *got_frame, int cached, AVPacket *pkt, AVCodecContext *video_dec_ctx, AVFrame *frame) {
  int ret = 0;
  int decoded = pkt->size;

  *got_frame = 0;

  /* decode video frame */
  ret = avcodec_decode_video2(video_dec_ctx, frame, got_frame, pkt);
  if (ret < 0) {
    fprintf(stderr, "Error decoding video frame\n");
    return ret;
  }

  return decoded;
}
 
int main(int argc, const char *argv[]) {
  int ret = 0, got_frame, got_output;
  int video_stream_idx = -1;
  int video_dst_bufsize;
  const char *src_filename;
  const char *dst_filename;
  FILE *dst_file                  = NULL;
  AVCodec *codec_enc              = NULL;
  AVFormatContext *fmt_ctx        = NULL;
  AVStream *video_stream          = NULL;
  AVCodecContext *video_dec_ctx   = NULL;
  AVCodecContext *video_enc_ctx   = NULL;
  AVFrame *frame                  = NULL;
  AVPacket pkt_dec, pkt_enc;
  uint8_t *video_dst_data[4]      = {NULL};
  int video_dst_linesize[4];
  
  if (argc != 3) {
    printf("Usage: %s <in_file> <out_file>\n", argv[0]);
    exit(1);
  }
  
  av_register_all();
  
  src_filename = argv[1];
  dst_filename = argv[2];
  
  codec_enc = avcodec_find_encoder(AV_CODEC_ID_JPEG2000);
  if (!codec_enc) {
      fprintf(stderr, "Codec not found\n");
      exit(1);
  }
  
  video_enc_ctx = avcodec_alloc_context3(codec_enc);
  if (!video_enc_ctx) {
      fprintf(stderr, "Could not allocate video codec context\n");
      exit(1);
  }
//   j2kenc_init(video_enc_ctx);
  
  /* open input file, and allocate format context */
  if (avformat_open_input(&fmt_ctx, src_filename, NULL, NULL) < 0) {
    fprintf(stderr, "Could not open source file %s\n", src_filename);
    exit(1);
  }
  
  /* retrieve stream information */
  if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
    fprintf(stderr, "Could not find stream information\n");
    exit(1);
  }
  
  if (open_codec_context(&video_stream_idx, fmt_ctx, AVMEDIA_TYPE_VIDEO, src_filename) >= 0) {
    video_stream = fmt_ctx->streams[video_stream_idx];
    video_dec_ctx = video_stream->codec;
    
    video_enc_ctx->width = video_dec_ctx->width;
    video_enc_ctx->height = video_dec_ctx->height;
    video_enc_ctx->pix_fmt = video_dec_ctx->pix_fmt;
    
    // make ffmpeg not complain about j2k being experiemntal
    video_enc_ctx->strict_std_compliance = -2;
    
    if (avcodec_open2(video_enc_ctx, codec_enc, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }

    dst_file = fopen(dst_filename, "wb");
    if (!dst_file) {
      fprintf(stderr, "Could not open destination file %s\n", dst_filename);
      ret = 1;
      goto end;
    }

    /* allocate image where the decoded image will be put */
    ret = av_image_alloc(video_dst_data, video_dst_linesize,
              video_dec_ctx->width, video_dec_ctx->height,
              video_dec_ctx->pix_fmt, 1);
    if (ret < 0) {
      fprintf(stderr, "Could not allocate raw video buffer\n");
      goto end;
    }
    video_dst_bufsize = ret;
  }
  
  /* dump input information to stderr */
  av_dump_format(fmt_ctx, 0, src_filename, 0);
  
  frame = av_frame_alloc();
  if (!frame) {
    fprintf(stderr, "Could not allocate frame\n");
    ret = AVERROR(ENOMEM);
    goto end;
  }
  
  /* initialize packet, set data to NULL, let the demuxer fill it */
  av_init_packet(&pkt_dec);
  pkt_dec.data = NULL;
  pkt_dec.size = 0;

  if (video_stream)
    printf("Demuxing video from file '%s' into '%s'\n", src_filename, dst_filename);
  
  /* read frames from the file */
  while (av_read_frame(fmt_ctx, &pkt_dec) >= 0) {
//     AVPacket orig_pkt = pkt;
    do {
      ret = decode_packet(&got_frame, 0, &pkt_dec, video_dec_ctx, frame);
      if (ret < 0)
        break;
      pkt_dec.data += ret;
      pkt_dec.size -= ret;
    } while (pkt_dec.size > 0);
//     av_free_packet(&orig_pkt);
  }
  /* flush cached frames */
  pkt_dec.data = NULL;
  pkt_dec.size = 0;
  do {
    decode_packet(&got_frame, 1, &pkt_dec, video_dec_ctx, frame);
    if (got_frame) {
      // DO SOME ENCODING HERE
      av_init_packet(&pkt_enc);
      pkt_enc.data = NULL;
      pkt_enc.size = 0;
      
      ret = avcodec_encode_video2(video_enc_ctx, &pkt_enc, frame, &got_output);
      if (ret < 0) {
	fprintf(stderr, "Error encoding frame\n");
	goto end;
      }

      if (got_output) {
	printf("Write frame (size=%5d)\n", pkt_enc.size);
	fwrite(pkt_enc.data, 1, pkt_enc.size, dst_file);
	
      }
    }
  } while (got_frame);
  
  printf("Demuxing succeeded.\n");
  
end:
  av_free_packet(&pkt_enc);
  av_free_packet(&pkt_dec);
  if (video_dec_ctx)
    avcodec_close(video_dec_ctx);
  if (video_enc_ctx)
    avcodec_close(video_enc_ctx);
//   if (codec_enc)
//     av_free(codec_enc);
  avformat_close_input(&fmt_ctx);
  if (dst_file)
    fclose(dst_file);
  else
    av_frame_free(&frame);
  av_free(video_dst_data[0]);

  return ret < 0;
}
