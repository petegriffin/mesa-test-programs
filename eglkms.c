/*
 * Copyright © 2011 Kristian Høgsberg
 * Copyright © 2011 Benjamin Franzke
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/*
 * Reference: https://cgit.freedesktop.org/mesa/demos/tree/src/egl/opengl/eglkms.c?id=mesa-demos-8.3.0
 */

#include <stdio.h>
#include <stdlib.h>

#include <assert.h>
#include <errno.h>

#include <sys/ioctl.h>

#include <gbm.h>
#include <epoxy/gl.h>
#include <epoxy/egl.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

EGLDisplay display;
EGLContext context;
EGLSurface surface;

struct kms {
   drmModeConnector *connector;
   drmModeEncoder *encoder;
   drmModeModeInfo mode;
   uint32_t fb_id;
};

static EGLBoolean
setup_kms(int fd, struct kms *kms)
{
   drmModeRes *resources = NULL;
   drmModeConnector *connector = NULL;
   drmModeEncoder *encoder = NULL;
   int i;

   resources = drmModeGetResources(fd);
   if (!resources) {
      fprintf(stderr, "drmModeGetResources failed\n");
      return EGL_FALSE;
   }

   for (i = 0; i < resources->count_connectors; i++) {
      connector = drmModeGetConnector(fd, resources->connectors[i]);
      if (connector == NULL)
         continue;

      if (connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0)
         break;

      drmModeFreeConnector(connector);
   }

   if (i == resources->count_connectors) {
      fprintf(stderr, "No currently active connector found.\n");
      return EGL_FALSE;
   }

   for (i = 0; i < resources->count_encoders; i++) {
      encoder = drmModeGetEncoder(fd, resources->encoders[i]);

      if (encoder == NULL)
         continue;

      if (encoder->encoder_id == connector->encoder_id)
         break;

      drmModeFreeEncoder(encoder);
   }

   kms->connector = connector;
   kms->encoder = encoder;
   kms->mode = connector->modes[0];

   return EGL_TRUE;
}

GLuint program;

GLuint LoadShader(const char *name, GLenum type)
{
   FILE *f;
   size_t size;
   char *buff;
   GLuint shader;
   GLint compiled;
   const GLchar *source[1];

   assert((f = fopen(name, "r")) != NULL);

   // get file size
   fseek(f, 0, SEEK_END);
   size = ftell(f);
   fseek(f, 0, SEEK_SET);

   assert((buff = malloc(size)) != NULL);
   assert(fread(buff, 1, size, f) == size);
   source[0] = buff;
   fclose(f);
   shader = glCreateShader(type);
   glShaderSource(shader, 1, source, (GLint *)&size);
   glCompileShader(shader);
   free(buff);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
   if (!compiled) {
      GLint infoLen = 0;
      glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
      if (infoLen > 1) {
         char *infoLog = malloc(infoLen);
         glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
         fprintf(stderr, "Error compiling shader %s:\n%s\n", name, infoLog);
         free(infoLog);
      }
      glDeleteShader(shader);
      return 0;
   }

   return shader;
}

static void InitGLES(int width, int height)
{
	GLint linked;
	GLuint vertexShader;
	GLuint fragmentShader;
	assert((vertexShader = LoadShader("egl-color.vert", GL_VERTEX_SHADER)) != 0);
	assert((fragmentShader = LoadShader("egl-color.frag", GL_FRAGMENT_SHADER)) != 0);
	assert((program = glCreateProgram()) != 0);
	glAttachShader(program, vertexShader);
	glAttachShader(program, fragmentShader);
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (!linked) {
		GLint infoLen = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
		if (infoLen > 1) {
			char *infoLog = malloc(infoLen);
			glGetProgramInfoLog(program, infoLen, NULL, infoLog);
			fprintf(stderr, "Error linking program:\n%s\n", infoLog);
			free(infoLog);
		}
		glDeleteProgram(program);
		exit(1);
	}

	glClearColor(0, 0, 0, 0);
	glViewport(0, 0, width/2, height/2);
	//glEnable(GL_DEPTH_TEST);

	glUseProgram(program);
}


void Render(void)
{
	GLfloat vertex[] = {
		-1, -1, 0,
		-1, 1, 0,
		1, 1, 0,
		1, -1, 0
	};
	GLfloat color[] = {
		1, 0, 0, 1,
		0, 1, 0, 1,
		0, 0, 1, 1,
	};
	//GLuint index[] = {
	//	0, 1, 2
	//};

	GLint position = glGetAttribLocation(program, "positionIn");
	glEnableVertexAttribArray(position);
	glVertexAttribPointer(position, 3, GL_FLOAT, 0, 0, vertex);

	GLint colorIn = glGetAttribLocation(program, "colorIn");
	glEnableVertexAttribArray(colorIn);
	glVertexAttribPointer(colorIn, 4, GL_FLOAT, 0, 0, color);

	assert(glGetError() == GL_NO_ERROR);

	glClear(GL_COLOR_BUFFER_BIT
			//| GL_DEPTH_BUFFER_BIT
	       );
	printf("%x\n", glGetError());
	assert(glGetError() == GL_NO_ERROR);

	//glDrawElements(GL_TRIANGLES, sizeof(index)/sizeof(GLuint), GL_UNSIGNED_INT, index);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	assert(glGetError() == GL_NO_ERROR);

	eglSwapBuffers(display, surface);

}

static const char device_name[] = "/dev/dri/card0";

static const EGLint attribs[] = {
   EGL_BUFFER_SIZE, 32,
   EGL_DEPTH_SIZE, EGL_DONT_CARE,
   EGL_STENCIL_SIZE, EGL_DONT_CARE,
   EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
   EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
   EGL_NONE
};

int main(int argc, char *argv[])
{
   EGLConfig config;
   EGLint major, minor, n;
   const char *ver;
   uint32_t handle, stride;
   struct kms kms;
   int ret, fd;
   struct gbm_device *gbm;
   struct gbm_bo *bo;
   drmModeCrtcPtr saved_crtc;
   struct gbm_surface *gs;

   fd = open(device_name, O_RDWR);
   if (fd < 0) {
      /* Probably permissions error */
      fprintf(stderr, "couldn't open %s, skipping\n", device_name);
      return -1;
   }

   gbm = gbm_create_device(fd);
   if (gbm == NULL) {
      fprintf(stderr, "couldn't create gbm device\n");
      ret = -1;
      goto close_fd;
   }

   display = eglGetDisplay(gbm);
   if (display == EGL_NO_DISPLAY) {
      fprintf(stderr, "eglGetDisplay() failed\n");
      ret = -1;
      goto destroy_gbm_device;
   }

   if (!eglInitialize(display, &major, &minor)) {
      printf("eglInitialize() failed\n");
      ret = -1;
      goto egl_terminate;
   }

   ver = eglQueryString(display, EGL_VERSION);
   printf("EGL_VERSION = %s\n", ver);

   if (!setup_kms(fd, &kms)) {
      ret = -1;
      goto egl_terminate;
   }

   eglBindAPI(EGL_OPENGL_ES_API);

   if (!eglChooseConfig(display, attribs, &config, 1, &n) || n != 1) {
      fprintf(stderr, "failed to choose argb config\n");
      ret = -1;
      goto egl_terminate;
   }

   static const EGLint context_attribs[] = {
      EGL_CONTEXT_CLIENT_VERSION, 2,
      EGL_NONE
   };
   context = eglCreateContext(display, &config[0], EGL_NO_CONTEXT, context_attribs);
   if (context == NULL) {
      fprintf(stderr, "failed to create context\n");
      ret = -1;
      goto egl_terminate;
   }

   gs = gbm_surface_create(gbm, kms.mode.hdisplay, kms.mode.vdisplay,
            GBM_BO_FORMAT_XRGB8888,
            GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
   surface = eglCreateWindowSurface(display, config, gs, NULL);

   if (!eglMakeCurrent(display, surface, surface, context)) {
      fprintf(stderr, "failed to make context current\n");
      ret = -1;
      goto destroy_context;
   }

   InitGLES(kms.mode.hdisplay, kms.mode.vdisplay);
   Render();

   bo = gbm_surface_lock_front_buffer(gs);
   handle = gbm_bo_get_handle(bo).u32;
   stride = gbm_bo_get_stride(bo);

   printf("handle=%d, stride=%d\n", handle, stride);

   ret = drmModeAddFB(fd,
            kms.mode.hdisplay, kms.mode.vdisplay,
            24, 32, stride, handle, &kms.fb_id);
   if (ret) {
      fprintf(stderr, "failed to create fb\n");
      goto rm_fb;
   }

   saved_crtc = drmModeGetCrtc(fd, kms.encoder->crtc_id);
   if (saved_crtc == NULL)
      goto rm_fb;

   ret = drmModeSetCrtc(fd, kms.encoder->crtc_id, kms.fb_id, 0, 0,
         &kms.connector->connector_id, 1, &kms.mode);
   if (ret) {
      fprintf(stderr, "failed to set mode: %m\n");
      goto free_saved_crtc;
   }

   getchar();

   ret = drmModeSetCrtc(fd, saved_crtc->crtc_id, saved_crtc->buffer_id,
                        saved_crtc->x, saved_crtc->y,
                        &kms.connector->connector_id, 1, &saved_crtc->mode);
   if (ret) {
      fprintf(stderr, "failed to restore crtc: %m\n");
   }

free_saved_crtc:
   drmModeFreeCrtc(saved_crtc);
rm_fb:
   drmModeRmFB(fd, kms.fb_id);
   eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
destroy_context:
   eglDestroyContext(display, context);
egl_terminate:
   eglTerminate(display);
destroy_gbm_device:
   gbm_device_destroy(gbm);
close_fd:
   close(fd);

   return ret;
}
