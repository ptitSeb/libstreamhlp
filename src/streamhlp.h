#ifndef _STREAMING_H_
#define _STREAMING_H_

#ifndef GL_IMG_texture_stream
#define GL_TEXTURE_STREAM_IMG                                   0x8C0D     
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*__eglMustCastToProperFunctionPointerType)(void);
typedef __eglMustCastToProperFunctionPointerType (*eglGetProcAddress_PTR)(const char * procname);

typedef enum {
	NV12,		//YUV 4:2:0
	UYVY,		//YUV 4:2:2
	YUYV,		//YUV 4:2:2
	RGB565,		//RGB 5:6:5
	ARGB		//ARGB 8:8:8:8
} STREAMHLP_FORMAT;

// Function to start the Streaming texture Cache. Return 0 if failed, non-0 if OK.
int InitStreamingHlp(eglGetProcAddress_PTR GetProcAddress);

// 0 if no Streaming, non 0 if available
int StreamingHlpAvailable();

// create a new streaming texture. Return ID of the texture, or -1 if failed
int CreateStreamTexture(int width, int height, STREAMHLP_FORMAT type, int nbuff);

// delete a previously created texture
void DeleteStreamedTexture(int ID);

// To bind a streaming texture in current texture unit (don't forget to glEnabled(GL_TEXTURE_STREAM_IMG))
void BindStreamedTexture(int ID, unsigned int buffer);

// Function to get a Streaming buffer address
void* GetStreamingBuffer(int ID, unsigned int buffer);

#ifdef __cplusplus
}
#endif

#endif //_STREAMING_H_