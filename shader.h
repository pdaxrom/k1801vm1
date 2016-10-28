/*
 *
 * Portable Pyldin-601 emulator
 * Copyright (c) Sasha Chukov, 2016
 *
 */

#ifndef SHADER_H
#define SHADER_H

#ifdef USE_GLES2
#include "GLES2/gl2.h"
#else
#define GL_GLEXT_PROTOTYPES
#include <SDL_opengl.h>
#endif

char* load_shader(char *sFilename);
int process_shader(GLuint *shader, char *fileName, GLint shaderType);

#endif
