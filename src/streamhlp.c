#include <bc_cat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>

#include "streamhlp.h"

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
#define BC_PIX_FMT_ARGB     BC_FOURCC('A', 'R', 'G', 'B') /*ARGB 8:8:8:8*/
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
static unsigned long buf_paddr[10];    // physical address
static char *buf_vaddr[10];            // virtual adress

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
		tex_free[i] = 1;
	}
}

static int open_bccat(int i) {
	if (bc_cat[i]>-1)
		return bc_cat[i];
	char buff[]="/dev/bccat0";
	buff[strlen(buff)-1]='0'+i;
	bc_cat[i] = open(buff, O_RDWR|O_NDELAY);
    return bc_cat[i];
}
static void close_bccat(int i) {
    if (bc_cat[i]==-1)
        return;
    close(bc_cat[i]);
    bc_cat[i]=-1;
    return;
}

static int alloc_buff(int buff, int width, int height, unsigned long fourcc) {
	if (!gl_streaming_initialized)
		Streaming_Initialize;
	if (!gl_streaming)
		return 0;
	if ((buff<0) || (buff>9))
		return 0;
	if (!tex_free[buff])
		return 0;
	if (open_bccat(buff)<0)
		return 0;
    BCIO_package ioctl_var;
    bc_buf_params_t buf_param;
	buf_param.count = 1;	// only 1 buffer?
	buf_param.width = width;
	buf_param.height = height;
	buf_param.fourcc = fourcc;
	buf_param.type = BC_MEMORY_MMAP;
	if (ioctl(bc_cat[buff], BCIOREQ_BUFFERS, &buf_param) != 0) {
		printf("StreamHlp: BCIOREQ_BUFFERS failed\n");
		return 0;
	}
	if (ioctl(bc_cat[buff], BCIOGET_BUFFERCOUNT, &ioctl_var) != 0) {
		printf("StreamHlp: BCIOREQ_BUFFERCOUNT failed\n");
		return 0;
	}
	if (ioctl_var.output == 0) {
		printf("StreamHlp: Streaming, no texture buffer available\n");
		return 0;
	}
	const char *bcdev = glGetTexDeviceIMG(buff);
	if (!bcdev) {
		printf("StreamHlp: problem with getting the GL_IMG_texture_stream device\n");
		return 0;
	} else {
		bcdev_w = width;
		bcdev_h = height;
		bcdev_n = 1;
		glGetTexAttrIMG(buff, GL_TEXTURE_STREAM_DEVICE_NUM_BUFFERS_IMG, &bcdev_n);
		glGetTexAttrIMG(buff, GL_TEXTURE_STREAM_DEVICE_WIDTH_IMG, &bcdev_w);
		glGetTexAttrIMG(buff, GL_TEXTURE_STREAM_DEVICE_HEIGHT_IMG, &bcdev_h);
		glGetTexAttrIMG(buff, GL_TEXTURE_STREAM_DEVICE_FORMAT_IMG, &bcdev_fmt);
		printf("StreamHlp: Streaming device = %s num: %d, width: %d, height: %d, format: 0x%x\n",
			bcdev, bcdev_n, bcdev_w, bcdev_h, bcdev_fmt);
		if (bcdev_w!=width) {
			printf("StreamHlp: Streaming not activate, buffer width != asked width\n");
			return 0;
		}
	}
/*	LOAD_GLES(glTexParameterf);
    gles_glTexParameterf(GL_TEXTURE_STREAM_IMG, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gles_glTexParameterf(GL_TEXTURE_STREAM_IMG, GL_TEXTURE_MAG_FILTER, GL_LINEAR);*/
	
	ioctl_var.input = 0;
	if (ioctl(bc_cat[buff], BCIOGET_BUFFERPHYADDR, &ioctl_var) != 0) {
		printf("StreamHlp: BCIOGET_BUFFERADDR failed\n");
		return 0;
	} else {
		buf_paddr[buff] = ioctl_var.output;
		buf_vaddr[buff] = (char *)mmap(NULL, width*height*2,
						  PROT_READ | PROT_WRITE, MAP_SHARED,
						  bc_cat[buff], buf_paddr[buff]);

		if (buf_vaddr[buff] == MAP_FAILED) {
			printf("StreamHlp: mmap failed\n");
			return 0;
		}
	}
	
	printf("StreamHlp: Streaming Texture initialized successfully\n");
	// All done!
	tex_free[buff] = 0;
	return 1;
}

static int free_buff(int buff) {
	if (!gl_streaming)
		return 0;
	if ((buff<0) || (buff>9))
		return 0;
    close_bccat(buff);
	tex_free[buff] = 1;
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
//printf("InitStreamingCache\n");
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
void* GetStreamingBuffer(int ID) {
//printf("GetStreamingBuffer(%i)\n", buff);
	if (!gl_streaming)
		return NULL;
	if ((ID<0) || (ID>9))
		return NULL;
	if (tex_free[ID])
		return NULL;
	return buf_vaddr[ID];
}

// Function to free a streamed texture ID
void DeleteStreamedTexture(int ID) {
//printf("FreeStreamed(%i)", ID);
	if (!gl_streaming)
		return;
	if ((ID<0) || (ID>9))
		return;
	if (tex_free[ID])
		return;
	if (!stream_cache[ID].active)
		return;
		
	free_buff(ID);
	stream_cache[ID].active = 0;
	stream_cache[ID].texID = 0;
}


int CreateStreamTexture(int width, int height, STREAMHLP_FORMAT type) {
	unsigned long fourcc;
	switch(type) {
		case NV12: fourcc=BC_PIX_FMT_NV12; break;
		case UYVY: fourcc=BC_PIX_FMT_UYVY; break;
		case YUYV: fourcc=BC_PIX_FMT_YUYV; break;
		case RGB565: fourcc=BC_PIX_FMT_RGB565; break;
		case ARGB: fourcc=BC_PIX_FMT_ARGB; break;
		default: return -1;
	}

	static int i = 0;
	int j=0;
	while (j<0) {
		int k=(i+j)%10;
		if (tex_free[k]) {
			if (alloc_buff(k, width, height, fourcc)) {
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

void BindStreamedTexture(int ID, unsigned int offset) {
	if (!gl_streaming)
		return;
	if ((ID<0) || (ID>9))
		return;
	if (tex_free[ID])
		return;
	if (!stream_cache[ID].active)
		return;

	glTexBindStreamIMG(ID, offset);
}