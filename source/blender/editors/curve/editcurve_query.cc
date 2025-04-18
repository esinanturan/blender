/* SPDX-FileCopyrightText: 2001-2002 NaN Holding BV. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup edcurve
 */

#include "DNA_curve_types.h"
#include "DNA_object_types.h"

#include "BLI_listbase.h"
#include "BLI_math_vector.h"

#include "BKE_curve.hh"
#include "BKE_layer.hh"

#include "ED_curve.hh"
#include "ED_view3d.hh"

#include "curve_intern.hh"

using blender::Vector;

/* -------------------------------------------------------------------- */
/** \name Cursor Picking API
 * \{ */

struct PickUserData {
  BPoint *bp;
  BezTriple *bezt;
  Nurb *nurb;
  float dist;
  int hpoint;
  uint8_t select;
  float mval_fl[2];
  bool is_changed;
};

static void curve_pick_vert__do_closest(void *user_data,
                                        Nurb *nu,
                                        BPoint *bp,
                                        BezTriple *bezt,
                                        int beztindex,
                                        bool handles_visible,
                                        const float screen_co[2])
{
  PickUserData *data = static_cast<PickUserData *>(user_data);

  uint8_t flag;
  float dist_test;

  if (bp) {
    flag = bp->f1;
  }
  else {
    BLI_assert(handles_visible || beztindex == 1);

    if (beztindex == 0) {
      flag = bezt->f1;
    }
    else if (beztindex == 1) {
      flag = bezt->f2;
    }
    else {
      flag = bezt->f3;
    }
  }

  dist_test = len_manhattan_v2v2(data->mval_fl, screen_co);
  if ((flag & SELECT) == data->select) {
    dist_test += 5.0f;
  }
  if (bezt && beztindex == 1) {
    dist_test += 3.0f; /* middle points get a small disadvantage */
  }

  if (dist_test < data->dist) {
    data->dist = dist_test;

    data->bp = bp;
    data->bezt = bezt;
    data->nurb = nu;
    data->hpoint = bezt ? beztindex : 0;
    data->is_changed = true;
  }

  UNUSED_VARS_NDEBUG(handles_visible);
}

bool ED_curve_pick_vert_ex(ViewContext *vc,
                           const bool select,
                           const int dist_px,
                           Nurb **r_nurb,
                           BezTriple **r_bezt,
                           BPoint **r_bp,
                           short *r_handle,
                           Base **r_base)
{
  PickUserData data{};

  data.dist = dist_px;
  data.hpoint = 0;
  data.select = select ? SELECT : 0;
  data.mval_fl[0] = vc->mval[0];
  data.mval_fl[1] = vc->mval[1];

  Vector<Base *> bases = BKE_view_layer_array_from_bases_in_edit_mode_unique_data(
      vc->scene, vc->view_layer, vc->v3d);
  for (Base *base : bases) {
    data.is_changed = false;

    ED_view3d_viewcontext_init_object(vc, base->object);
    ED_view3d_init_mats_rv3d(vc->obedit, vc->rv3d);
    nurbs_foreachScreenVert(vc, curve_pick_vert__do_closest, &data, V3D_PROJ_TEST_CLIP_DEFAULT);

    if (r_base && data.is_changed) {
      *r_base = base;
    }
  }

  *r_nurb = data.nurb;
  *r_bezt = data.bezt;
  *r_bp = data.bp;

  if (r_handle) {
    *r_handle = data.hpoint;
  }

  return (data.bezt || data.bp);
}

bool ED_curve_pick_vert(ViewContext *vc,
                        short sel,
                        Nurb **r_nurb,
                        BezTriple **r_bezt,
                        BPoint **r_bp,
                        short *r_handle,
                        Base **r_base)
{
  return ED_curve_pick_vert_ex(
      vc, sel, ED_view3d_select_dist_px(), r_nurb, r_bezt, r_bp, r_handle, r_base);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Selection Queries
 * \{ */

void ED_curve_nurb_vert_selected_find(
    Curve *cu, View3D *v3d, Nurb **r_nu, BezTriple **r_bezt, BPoint **r_bp)
{
  /* In nu and (bezt or bp) selected are written if there's 1 sel. */
  /* If more points selected in 1 spline: return only nu, bezt and bp are 0. */
  ListBase *editnurb = &cu->editnurb->nurbs;
  BezTriple *bezt1;
  BPoint *bp1;
  int a;

  *r_nu = nullptr;
  *r_bezt = nullptr;
  *r_bp = nullptr;

  LISTBASE_FOREACH (Nurb *, nu1, editnurb) {
    if (nu1->type == CU_BEZIER) {
      bezt1 = nu1->bezt;
      a = nu1->pntsu;
      while (a--) {
        if (BEZT_ISSEL_ANY_HIDDENHANDLES(v3d, bezt1)) {
          if (!ELEM(*r_nu, nullptr, nu1)) {
            *r_nu = nullptr;
            *r_bp = nullptr;
            *r_bezt = nullptr;
            return;
          }

          if (*r_bezt || *r_bp) {
            *r_bp = nullptr;
            *r_bezt = nullptr;
          }
          else {
            *r_bezt = bezt1;
            *r_nu = nu1;
          }
        }
        bezt1++;
      }
    }
    else {
      bp1 = nu1->bp;
      a = nu1->pntsu * nu1->pntsv;
      while (a--) {
        if (bp1->f1 & SELECT) {
          if (!ELEM(*r_nu, nullptr, nu1)) {
            *r_bp = nullptr;
            *r_bezt = nullptr;
            *r_nu = nullptr;
            return;
          }

          if (*r_bezt || *r_bp) {
            *r_bp = nullptr;
            *r_bezt = nullptr;
          }
          else {
            *r_bp = bp1;
            *r_nu = nu1;
          }
        }
        bp1++;
      }
    }
  }
}

bool ED_curve_active_center(Curve *cu, float center[3])
{
  Nurb *nu = nullptr;
  void *vert = nullptr;

  if (!BKE_curve_nurb_vert_active_get(cu, &nu, &vert)) {
    return false;
  }

  if (nu->type == CU_BEZIER) {
    BezTriple *bezt = (BezTriple *)vert;
    copy_v3_v3(center, bezt->vec[1]);
  }
  else {
    BPoint *bp = (BPoint *)vert;
    copy_v3_v3(center, bp->vec);
  }

  return true;
}

/** \} */
