/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup freestyle
 */

#pragma once

#include "../BPy_UnaryFunction1D.h"

///////////////////////////////////////////////////////////////////////////////////////////

extern PyTypeObject UnaryFunction1DUnsigned_Type;

#define BPy_UnaryFunction1DUnsigned_Check(v) \
  (PyObject_IsInstance((PyObject *)v, (PyObject *)&UnaryFunction1DUnsigned_Type))

/*---------------------------Python BPy_UnaryFunction1DUnsigned structure definition----------*/
typedef struct {
  BPy_UnaryFunction1D py_uf1D;
  Freestyle::UnaryFunction1D<uint> *uf1D_unsigned;
} BPy_UnaryFunction1DUnsigned;

/*---------------------------Python BPy_UnaryFunction1DUnsigned visible prototypes-----------*/
int UnaryFunction1DUnsigned_Init(PyObject *module);

///////////////////////////////////////////////////////////////////////////////////////////
