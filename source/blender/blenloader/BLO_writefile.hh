/* SPDX-FileCopyrightText: 2001-2002 NaN Holding BV. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup blenloader
 * \brief external `writefile.cc` function prototypes.
 */

#include "BLI_sys_types.h"

struct BlendThumbnail;
struct Main;
struct MemFile;
struct ReportList;

/* -------------------------------------------------------------------- */
/** \name BLO Write File API
 *
 * \see #BLO_read_from_file for file reading.
 * \{ */

/**
 * Adjust paths when saving (kept unless #BlendFileWriteParams.use_save_as_copy is set).
 */
enum eBLO_WritePathRemap {
  /** No path manipulation. */
  BLO_WRITE_PATH_REMAP_NONE = 0,
  /** Remap existing relative paths (default). */
  BLO_WRITE_PATH_REMAP_RELATIVE = 1,
  /** Remap paths making all paths relative to the new location. */
  BLO_WRITE_PATH_REMAP_RELATIVE_ALL = 2,
  /** Make all paths absolute. */
  BLO_WRITE_PATH_REMAP_ABSOLUTE = 3,
};

/** Similar to #BlendFileReadParams. */
struct BlendFileWriteParams {
  eBLO_WritePathRemap remap_mode;
  /** Save `.blend1`, `.blend2`... etc. */
  uint use_save_versions : 1;
  /** On write, restore paths after editing them (see #BLO_WRITE_PATH_REMAP_RELATIVE). */
  uint use_save_as_copy : 1;
  uint use_userdef : 1;
  const BlendThumbnail *thumb;
};

/**
 * \return Success.
 */
extern bool BLO_write_file(Main *mainvar,
                           const char *filepath,
                           int write_flags,
                           const BlendFileWriteParams *params,
                           ReportList *reports);

/**
 * \return Success.
 */
extern bool BLO_write_file_mem(Main *mainvar, MemFile *compare, MemFile *current, int write_flags);

/** \} */
