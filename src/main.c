#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>
#include <vorbis/vorbisfile.h>

#define READ 1024
#define MAX_CHANNELS 20
#define QUALITY -0.1
#define HEADER_SIZE 44
#define OUTPUT_NAME "Encoded.ogg"
char readbuffer[READ*2*MAX_CHANNELS + HEADER_SIZE]; /* out of the data segment, not the stack */
//static char output_filename[300] = "Encoded";

static long long ogg_file_size;

typedef struct{
  FILE *			input_file;
  FILE *			output_file;
  
  ogg_stream_state 	os; /* take physical pages, weld into a logical
                          stream of packets */
  ogg_page         	og; /* one Ogg bitstream page.  Vorbis packets are inside */
  ogg_packet       	op; /* one raw packet of data for decode */

  vorbis_info      	vi; /* struct that stores all the static vorbis bitstream
                          settings */
  vorbis_comment   	vc; /* struct that stores all the user comments */

  vorbis_dsp_state 	vd; /* central working state for the packet->PCM decoder */
  vorbis_block     	vb; /* local working space for packet->PCM decode */
  
  int 				eos;
  int				channels;
  int				sample_rate;
  
  time_t			time_start;
  time_t 			time_elapsed;
}codec_setup;

codec_setup codec;

FILE *read_input_file(const char *name){
	FILE *input_file = fopen(name,"r");
	if(!input_file){
		printf("Error, cannot find file or filepath %s",name);
		return 0;
	}
	return input_file;
}

FILE *create_output_file(const char *name) {
    char filename[300];
    char base[256];
    int i = 0;

    // Strip the directory path and extensions (.ogg, .wav)
    const char *file_name = strrchr(name, '/');
    if (file_name) {
        // Skip the path
        file_name++;
    } else {
        file_name = name;
    }

    // Copy the base name (without extensions)
    strncpy(base, file_name, sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0'; // Ensure null-termination

    // Remove .ogg or .wav extension
    char *ext = strstr(base, ".ogg");
    if (ext) *ext = '\0'; // Remove .ogg extension
    else {
        ext = strstr(base, ".wav");
        if (ext) *ext = '\0'; // Remove .wav extension
    }
    while (1) {
        snprintf(filename, sizeof(filename), "data/%s%d.ogg", base, i);

        FILE *check = fopen(filename, "r");
        if (!check) {
            break;
        }
        fclose(check);
        i++; 
    }

    FILE *output_file = fopen(filename, "w");
    if (!output_file) {
        printf("Error: Cannot create output file: %s\n", filename);
        return NULL;
    }

    printf("Output file created: %s\n", filename);
    return output_file;
}



int codec_init(char * output_filename_v2) {
		/* Reset end_of_stream flag */
    codec.eos = 0;
    ogg_file_size = 0;
    
        /* Output file */
    codec.output_file = create_output_file(output_filename_v2);
    if (!codec.output_file) return 0;

		/* Init Vorbis */
    vorbis_info_init(&codec.vi);
    if (vorbis_encode_init_vbr(&codec.vi, codec.channels, codec.sample_rate, QUALITY)) {
        fprintf(stderr, "Error initializing the encoder\n\n");
        return 0;
    }
    
		/*Add Comment*/
    vorbis_comment_init(&codec.vc);
    vorbis_comment_add_tag(&codec.vc, "ENCODER", "encoder_example.c");
    vorbis_analysis_init(&codec.vd, &codec.vi);
    vorbis_block_init(&codec.vd, &codec.vb);

		/*Init Ogg Stream*/
    srand(time(NULL));
    ogg_stream_init(&codec.os, rand());

		/* Write header packets */
    ogg_packet header_pkt, header_comm, header_code;
    vorbis_analysis_headerout(&codec.vd, &codec.vc, &header_pkt, &header_comm, &header_code);
    ogg_stream_packetin(&codec.os, &header_pkt);
    ogg_stream_packetin(&codec.os, &header_comm);
    ogg_stream_packetin(&codec.os, &header_code);

    while (!codec.eos) {
        int result = ogg_stream_flush(&codec.os, &codec.og);
        if (result == 0) break;
        fwrite(codec.og.header, 1, codec.og.header_len, codec.output_file);
        fwrite(codec.og.body, 1, codec.og.body_len, codec.output_file);
    }
	codec.time_start = time(NULL);
    return 1;
}

void codec_stop(){
	ogg_stream_clear(&codec.os);
	vorbis_block_clear(&codec.vb);
	vorbis_dsp_clear(&codec.vd);
	vorbis_comment_clear(&codec.vc);
	vorbis_info_clear(&codec.vi);
	fclose(codec.output_file);
}

int codec_append(size_t frames_read){		
		codec.time_elapsed = time(NULL) - codec.time_start;
	    long i = 0;
		if(frames_read==0 || codec.time_elapsed >= 1){

		  vorbis_analysis_wrote(&codec.vd,0);

		}else{

		  float **buffer=vorbis_analysis_buffer(&codec.vd,frames_read);

			for (i = 0; i < frames_read / (2 * codec.channels); i++) {
				for (int ch = 0; ch < codec.channels; ch++) {
					int sample_index = i * 2 * codec.channels + 2 * ch; // audio_sample * Byte_Size * Max_Channels + 2 * channel;
					int16_t sample = (readbuffer[sample_index + 1] << 8) | (0x00ff & readbuffer[sample_index]);
					buffer[ch][i] = sample / 32768.f;
				}
			}

		  vorbis_analysis_wrote(&codec.vd,i);
		}

		while(vorbis_analysis_blockout(&codec.vd,&codec.vb)==1){

		  vorbis_analysis(&codec.vb,NULL);
		  vorbis_bitrate_addblock(&codec.vb);

		  while(vorbis_bitrate_flushpacket(&codec.vd,&codec.op)){
			  
			ogg_file_size += codec.op.bytes; 
			ogg_stream_packetin(&codec.os,&codec.op);
			
			 while(!codec.eos){
			  int result=ogg_stream_pageout(&codec.os,&codec.og);
			  if(result==0)break;
			  fwrite(codec.og.header,1,codec.og.header_len,codec.output_file);
			  fwrite(codec.og.body,1,codec.og.body_len,codec.output_file);

			  if(ogg_page_eos(&codec.og))codec.eos=1;
			 }

		  }
		}
	return 0;
}

void help(){
    printf("USAGE: extract_portion *filename.ogg *time_start *duration\n");
}

int main(int argc, char** argv){
    if(argc != 4){
        help();
        return 1;
    }

    const char* filename = argv[1];
    double start_time = atof(argv[2]);
    double duration = atof(argv[3]);
    double end_time = start_time + duration;

    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "ERROR: Could not open file %s\n", filename);
        return 1;
    }

    OggVorbis_File ov;
    if (ov_open(fp, &ov, NULL, 0) < 0) {
        fprintf(stderr, "ERROR: Input is not a valid Ogg bitstream.\n");
        fclose(fp);
        return 1;
    }

    printf("File Opened Successfully.\n");

    double total_time = ov_time_total(&ov, -1);
    printf("Total time = %.2fs\n", total_time);

    if (start_time > total_time || end_time > total_time) {
        fprintf(stderr, "ERROR: Start or end time exceeds file duration: %.2fs\n",total_time);
        ov_clear(&ov);
        return 1;
    }

    if (ov_time_seek(&ov, start_time) != 0) {
        fprintf(stderr, "ERROR: Failed to seek the starting time.\n");
        ov_clear(&ov);
        return 1;
    }

    // Set channels and sample rate
    // Set channels and sample rate
	vorbis_info *vi = ov_info(&ov, -1);
	codec.channels = vi->channels;
	codec.sample_rate = vi->rate;

	// Initialize encoder
	if (!codec_init("extracted_segment.ogg")) {
		fprintf(stderr, "ERROR: Failed to initialize codec.\n");
		ov_clear(&ov);
		fclose(fp);
		return 1;
	}

	printf("Extracting from %.2fs to %.2fs\n", start_time, end_time);
	int current_section;
	long ret;

	while (ov_time_tell(&ov) < end_time) {
		ret = ov_read(&ov, readbuffer, sizeof(readbuffer), 0, 2, 1, &current_section);
		if (ret == 0) break; // EOF
		if (ret < 0) {
			fprintf(stderr, "WARNING: Error in stream\n");
			continue;
		}
		codec_append(ret);
	}

	codec_stop();
	printf("Extraction complete.\n");

	ov_clear(&ov);
	return 0;
}
