#include <bc_cat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "streamhlp.h"

//#define DEBUG
#ifdef DEBUG
#define LOGD(a) a
#else
#define LOGD(a)
#endif

typedef int GLint;
typedef unsigned int GLuint;
typedef unsigned char GLubyte;
typedef unsigned int GLenum;

#ifndef GL_APIENTRYP
#define GL_APIENTRYP
#endif
#ifndef GL_IMG_texture_stream
//#define GL_TEXTURE_STREAM_IMG                                   0x8C0D     
#define GL_TEXTURE_NUM_STREAM_DEVICES_IMG                       0x8C0E     
#define GL_TEXTURE_STREAM_DEVICE_WIDTH_IMG                      0x8C0F
#define GL_TEXTURE_STREAM_DEVICE_HEIGHT_IMG                     0x8EA0     
#define GL_TEXTURE_STREAM_DEVICE_FORMAT_IMG                     0x8EA1      
#define GL_TEXTURE_STREAM_DEVICE_NUM_BUFFERS_IMG                0x8EA2     
typedef void (GL_APIENTRYP PFNGLTEXBINDSTREAMIMGPROC) (GLint device, GLint deviceoffset);
typedef const GLubyte *(GL_APIENTRYP PFNGLGETTEXSTREAMDEVICENAMEIMGPROC) (GLenum target);
typedef void (GL_APIENTRYP PFNGLGETTEXSTREAMDEVICEATTRIBUTEIVIMGPROC) (GLenum target, GLenum pname, GLint *params);
#define GL_IMG_texture_stream 1
#endif

#ifndef BC_PIX_FMT_ARGB
#define BC_PIX_FMT_ARGB     BC_FOURCC('A', 'R', 'G', 'B')
#endif

static PFNGLTEXBINDSTREAMIMGPROC *glTexBindStreamIMG = NULL;
static PFNGLGETTEXSTREAMDEVICEATTRIBUTEIVIMGPROC *glGetTexAttrIMG = NULL;
static PFNGLGETTEXSTREAMDEVICENAMEIMGPROC *glGetTexDeviceIMG = NULL;

static int gl_streaming = 0;
static int gl_streaming_initialized = 0;
static int bc_cat[10];
static int tex_free[10];
static const GLubyte * bcdev[10];
static int bcdev_w, bcdev_h, bcdev_n;
static int bcdev_fmt;
static struct {
	unsigned long *buf_paddr;
	char		  **buf_vaddr;
	unsigned int 	n_buff;
} buffers[10];
//static unsigned long buf_paddr[10];    // physical address
//static char *buf_vaddr[10];            // virtual adress

static void Streaming_Initialize(eglGetProcAddress_PTR GetProcAddress) {
	if (gl_streaming_initialized)
		return;
	// get the extension functions
	gl_streaming_initialized = 1;
    glTexBindStreamIMG =(PFNGLTEXBINDSTREAMIMGPROC*)GetProcAddress("glTexBindStreamIMG");
    glGetTexAttrIMG = (PFNGLGETTEXSTREAMDEVICEATTRIBUTEIVIMGPROC*)GetProcAddress("glGetTexStreamDeviceAttributeivIMG");
    glGetTexDeviceIMG = (PFNGLGETTEXSTREAMDEVICENAMEIMGPROC*)GetProcAddress("glGetTexStreamDeviceNameIMG");

	if (!glTexBindStreamIMG || !glGetTexAttrIMG || !glGetTexDeviceIMG) {
		gl_streaming = 0;
		return;
	}
	gl_streaming = 1;
	// initialise the bc_cat ids
	for (int i=0; i<10; i++) {
		bc_cat[i] = -1;
		tex_free[i] = 0;
	}
}

static int open_bccat(int i) {
LOGD(printf("open_bccat(%d)\n", i);)
	if (bc_cat[i]>-1)
		return bc_cat[i];
	char buff[]="/dev/bccat0";
	buff[strlen(buff)-1]='0'+i;
	bc_cat[i] = open(buff, O_RDWR|O_NDELAY);
    return bc_cat[i];
}
static void close_bccat(int i) {
LOGD(printf("close_bccat(%d)\n", i);)
    if (bc_cat[i]==-1)
        return;
    close(bc_cat[i]);
    bc_cat[i]=-1;
    return;
}

static int alloc_buff(int buff, int width, int height, unsigned long fourcc, int mem_mul, int mem_div, int buffs) {
LOGD(printf("alloc_buff(%d, %d, %d, %c%c%c%c, %d, %d, %d)\n", buff, width, height, fourcc&0xff, (fourcc>>8)&0xff, (fourcc>>16)&0xff, (fourcc>>24)&0xff, mem_mul, mem_div, buffs);)
	if (!gl_streaming_initialized)
		Streaming_Initialize;
	if (!gl_streaming)
		return 0;
	if ((buff<0) || (buff>9))
		return 0;
	if (tex_free[buff])
		return 0;
	if (open_bccat(buff)<0)
		return 0;
    BCIO_package ioctl_var;
    bc_buf_params_t buf_param;
	buf_param.count = buffs;
	buf_param.width = width;
	buf_param.height = height;
	buf_param.fourcc = fourcc;
	buf_param.type = BC_MEMORY_MMAP;
	if (ioctl(bc_cat[buff], BCIOREQ_BUFFERS, &buf_param) != 0) {
		fprintf(stderr, "StreamHlp: BCIOREQ_BUFFERS failed for buffer %d\n", buff);
		close_bccat(buff);
		return 0;
	}
	if (ioctl(bc_cat[buff], BCIOGET_BUFFERCOUNT, &ioctl_var) != 0) {
		fprintf(stderr, "StreamHlp: BCIOREQ_BUFFERCOUNT failed for buffer %d\n", buff);
		close_bccat(buff);
		return 0;
	}
	if (ioctl_var.output == 0) {
		fprintf(stderr, "StreamHlp: Streaming, no texture buffer available for buffer %d\n", buff);
		close_bccat(buff);
		return 0;
	}
	const char *bcdev = glGetTexDeviceIMG(buff);
	if (!bcdev) {
		fprintf(stderr, "StreamHlp: problem with getting the GL_IMG_texture_stream device for buffer %d\n", buff);
		close_bccat(buff);
		return 0;
	} else {
		bcdev_w = width;
		bcdev_h = height;
		bcdev_n = 1;
		glGetTexAttrIMG(buff, GL_TEXTURE_STREAM_DEVICE_NUM_BUFFERS_IMG, &bcdev_n);
		glGetTexAttrIMG(buff, GL_TEXTURE_STREAM_DEVICE_WIDTH_IMG, &bcdev_w);
		glGetTexAttrIMG(buff, GL_TEXTURE_STREAM_DEVICE_HEIGHT_IMG, &bcdev_h);
		glGetTexAttrIMG(buff, GL_TEXTURE_STREAM_DEVICE_FORMAT_IMG, &bcdev_fmt);
		fprintf(stderr, "StreamHlp: Streaming device = %s num: %d, width: %d, height: %d, format: 0x%x for buffer %d\n",
			bcdev, bcdev_n, bcdev_w, bcdev_h, bcdev_fmt, buff);
		if (bcdev_w!=width) {
			printf("StreamHlp: Streaming not activate, buffer width != asked width for buffer %d\n", buff);
			close_bccat(buff);
			return 0;
		}
	}
/*	LOAD_GLES(glTexParameterf);
    gles_glTexParameterf(GL_TEXTURE_STREAM_IMG, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gles_glTexParameterf(GL_TEXTURE_STREAM_IMG, GL_TEXTURE_MAG_FILTER, GL_LINEAR);*/
	
	buffers[buff].buf_paddr = (unsigned long*)malloc(sizeof(unsigned long)*buffs);
	buffers[buff].buf_vaddr = (char**)malloc(sizeof(char*)*buffs);
	buffers[buff].n_buff = buffs;
	for (int idx=0; idx<buffs; idx++) {
		ioctl_var.input = idx;
		if (ioctl(bc_cat[buff], BCIOGET_BUFFERPHYADDR, &ioctl_var) != 0) {
			fprintf(stderr, "StreamHlp: BCIOGET_BUFFERADDR failed for buffer %d.%d\n", buff, idx);
			close_bccat(buff);
			return 0;
		} else {
			buffers[buff].buf_paddr[idx] = ioctl_var.output;
			buffers[buff].buf_vaddr[idx] = (char *)mmap(NULL, (width*height*mem_mul)/mem_div,
							  PROT_READ | PROT_WRITE, MAP_SHARED,
							  bc_cat[buff], buffers[buff].buf_paddr[idx]);

			if (buffers[buff].buf_vaddr[idx] == MAP_FAILED) {
				fprintf(stderr, "StreamHlp: mmap failed for buffer %d.%d\n", buff, idx);
				close_bccat(buff);
				return 0;
			}
		}
	}
	
	fprintf(stderr, "StreamHlp: Streaming Texture initialized successfully on buffer %d\n", buff);
	// All done!
	tex_free[buff] = (width*height*mem_mul)/mem_div;
	return 1;
}

static int free_buff(int buff) {
LOGD(printf("free_buff(%d)\n", buff);)
	if (!gl_streaming)
		return 0;
	if ((buff<0) || (buff>9))
		return 0;
	for (int idx = 0; idx < buffers[buff].n_buff; idx++)
		munmap(buffers[buff].buf_vaddr[idx], tex_free[buff]);
	free(buffers[buff].buf_paddr);
	free(buffers[buff].buf_vaddr);
	buffers[buff].n_buff = 0;
    close_bccat(buff);
	tex_free[buff] = 0;
	return 1;
}

static int streaming_inited = 0;

typedef struct {
	int	active;
	unsigned int texID;	// ID of texture
} glstreaming_t;

static glstreaming_t stream_cache[10];
static unsigned int frame_number;

// Function to start the Streaming texture Cache
int InitStreamingHlp(eglGetProcAddress_PTR GetProcAddress) {
LOGD(printf("InitStreamingCache\n");)
	if (streaming_inited)
		return gl_streaming;
	Streaming_Initialize(GetProcAddress);
	for (int i=0; i<10; i++) {
		stream_cache[i].active = 0;
		stream_cache[i].texID = i;
	}
	frame_number = 0;
	streaming_inited = 1;
	return gl_streaming;
}

int StreamingHlpAvailable() {
	if (!streaming_inited)
		return 0;

	return gl_streaming;
}

// Function to get a Streaming buffer address
void* GetStreamingBuffer(int ID, unsigned int buff) {
//LOGD(printf("GetStreamingBuffer(%i)\n", buff);)
	if (!gl_streaming)
		return NULL;
	if ((ID<0) || (ID>9))
		return NULL;
	if (!tex_free[ID])
		return NULL;
	return buffers[ID].buf_vaddr[buff];
}

// Function to free a streamed texture ID
void DeleteStreamedTexture(int ID) {
LOGD(printf("FreeStreamed(%i)\n", ID);)
	if (!gl_streaming)
		return;
	if ((ID<0) || (ID>9))
		return;
	if (!tex_free[ID])
		return;
	if (!stream_cache[ID].active)
		return;
		
	free_buff(ID);
	stream_cache[ID].active = 0;
	stream_cache[ID].texID = 0;
}


int CreateStreamTexture(int width, int height, STREAMHLP_FORMAT type, int nbuff) {
LOGD(printf("CreateStreamTexture(%d, %d, %d, %d)\n", width, height, type, nbuff);)
	unsigned long fourcc;
	int mem_mul;
	int mem_div;
	switch(type) {
		case NV12: fourcc=BC_PIX_FMT_NV12; mem_mul=3; mem_div=2; break;
		case UYVY: fourcc=BC_PIX_FMT_UYVY; mem_mul=2; mem_div=1; break;
		case YUYV: fourcc=BC_PIX_FMT_YUYV; mem_mul=2; mem_div=1; break;
		case RGB565: fourcc=BC_PIX_FMT_RGB565; mem_mul=2; mem_div=1; break;
		case ARGB: fourcc=BC_PIX_FMT_ARGB; mem_mul=4; mem_div=1; break;
		default: return -1;
	}

	static int i = 0;
	int j=0;
	while (j<10) {
		int k=(i+j)%10;
		if (!tex_free[k]) {
			if (alloc_buff(k, width, height, fourcc, mem_mul, mem_div,nbuff)) {
				stream_cache[k].active = 1;
				stream_cache[k].texID = k;
                i = (i+j+1)%10;
				return k;
			} else {                
                return -1;	// Probably useless to try again and again
            }
		}
        j++;
	}
	return -1;
}

void BindStreamedTexture(int ID, unsigned int buffer) {
//LOGD(printf("BindStreamedTexture(%d, %u)\n", ID, buffer);)
	if (!gl_streaming)
		return;
	if (ID==-1) {
		glTexBindStreamIMG(-1, 0);	//unbind?
		return;
	}
	if ((ID<0) || (ID>9))
		return;
	if (!tex_free[ID])
		return;
	if (!stream_cache[ID].active)
		return;

	glTexBindStreamIMG(ID, buffer);
}