/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup freestyle
 */

#pragma once

#include "../BPy_StrokeShader.h"

///////////////////////////////////////////////////////////////////////////////////////////

extern PyTypeObject IncreasingThicknessShader_Type;

#define BPy_IncreasingThicknessShader_Check(v) \
  (PyObject_IsInstance((PyObject *)v, (PyObject *)&IncreasingThicknessShader_Type))

/*---------------------------Python BPy_IncreasingThicknessShader structure definition----------*/
typedef struct {
  BPy_StrokeShader py_ss;
} BPy_IncreasingThicknessShader;

///////////////////////////////////////////////////////////////////////////////////////////
