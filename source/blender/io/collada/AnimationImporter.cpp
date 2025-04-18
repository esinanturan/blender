/* SPDX-FileCopyrightText: 2010-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup collada
 */

#include <cstddef>

#include "COLLADAFWAnimation.h"
#include "COLLADAFWAnimationCurve.h"
#include "COLLADAFWAnimationList.h"
#include "COLLADAFWCamera.h"
#include "COLLADAFWEffect.h"
#include "COLLADAFWLight.h"
#include "COLLADAFWNode.h"
#include "COLLADAFWRotate.h"
#include "COLLADAFWUniqueId.h"

#include "DNA_armature_types.h"

#include "ED_keyframing.hh"

#include "ANIM_action.hh"
#include "ANIM_action_legacy.hh"
#include "ANIM_animdata.hh"
#include "ANIM_fcurve.hh"

#include "BLI_math_matrix.h"
#include "BLI_string.h"

#include "BKE_action.hh"
#include "BKE_armature.hh"
#include "BKE_fcurve.hh"
#include "BKE_object.hh"

#include "AnimationImporter.h"
#include "ArmatureImporter.h"
#include "collada_utils.h"

#include <algorithm>

/* first try node name, if not available (since is optional), fall back to original id */
template<class T> static const char *bc_get_joint_name(T *node)
{
  const std::string &id = node->getName();
  return id.empty() ? node->getOriginalId().c_str() : id.c_str();
}

/**
 * Ensures that the given ID has an action assigned to it and, for layered
 * actions, an assigned slot.
 */
static void ensure_action_and_slot_for_id(Main *bmain, ID &id)
{
  bAction *dna_action = blender::animrig::id_action_ensure(bmain, &id);
  BLI_assert(dna_action != nullptr);

  if (blender::animrig::legacy::action_treat_as_legacy(*dna_action)) {
    /* We don't ensure a slot for legacy actions, since they don't have slots. */
    return;
  }

  blender::animrig::Action &action = dna_action->wrap();
  blender::animrig::Slot *slot = blender::animrig::assign_action_ensure_slot_for_keying(action,
                                                                                        id);
  BLI_assert(slot != nullptr);
  UNUSED_VARS_NDEBUG(slot);
}

FCurve *AnimationImporter::create_fcurve(int array_index, const char *rna_path)
{
  FCurve *fcu = BKE_fcurve_create();
  fcu->flag = (FCURVE_VISIBLE | FCURVE_SELECTED);
  fcu->rna_path = BLI_strdupn(rna_path, strlen(rna_path));
  fcu->array_index = array_index;
  return fcu;
}

void AnimationImporter::add_bezt(FCurve *fcu,
                                 float frame,
                                 float value,
                                 eBezTriple_Interpolation ipo)
{
  // float fps = float(FPS);
  BezTriple bez;
  memset(&bez, 0, sizeof(BezTriple));
  bez.vec[1][0] = frame;
  bez.vec[1][1] = value;
  bez.ipo = ipo; /* use default interpolation mode here... */
  bez.f1 = bez.f2 = bez.f3 = SELECT;
  bez.h1 = bez.h2 = HD_AUTO;
  blender::animrig::insert_bezt_fcurve(fcu, &bez, INSERTKEY_NOFLAGS);
  BKE_fcurve_handles_recalc(fcu);
}

void AnimationImporter::animation_to_fcurves(COLLADAFW::AnimationCurve *curve)
{
  COLLADAFW::FloatOrDoubleArray &input = curve->getInputValues();
  COLLADAFW::FloatOrDoubleArray &output = curve->getOutputValues();

  float fps = float(FPS);
  size_t dim = curve->getOutDimension();
  uint i;

  std::vector<FCurve *> &fcurves = curve_map[curve->getUniqueId()];

  switch (dim) {
    case 1: /* X, Y, Z or angle */
    case 3: /* XYZ */
    case 4:
    case 16: /* matrix */
    {
      for (i = 0; i < dim; i++) {
        FCurve *fcu = BKE_fcurve_create();

        fcu->flag = (FCURVE_VISIBLE | FCURVE_SELECTED);
        fcu->array_index = 0;
        fcu->auto_smoothing = U.auto_smoothing_new;

        for (uint j = 0; j < curve->getKeyCount(); j++) {
          BezTriple bez;
          memset(&bez, 0, sizeof(BezTriple));

          /* input, output */
          bez.vec[1][0] = bc_get_float_value(input, j) * fps;
          bez.vec[1][1] = bc_get_float_value(output, j * dim + i);
          bez.h1 = bez.h2 = HD_AUTO;

          if (curve->getInterpolationType() == COLLADAFW::AnimationCurve::INTERPOLATION_BEZIER ||
              curve->getInterpolationType() == COLLADAFW::AnimationCurve::INTERPOLATION_STEP)
          {
            COLLADAFW::FloatOrDoubleArray &intan = curve->getInTangentValues();
            COLLADAFW::FloatOrDoubleArray &outtan = curve->getOutTangentValues();

            /* In-tangent. */
            uint index = 2 * (j * dim + i);
            bez.vec[0][0] = bc_get_float_value(intan, index) * fps;
            bez.vec[0][1] = bc_get_float_value(intan, index + 1);

            /* Out-tangent. */
            bez.vec[2][0] = bc_get_float_value(outtan, index) * fps;
            bez.vec[2][1] = bc_get_float_value(outtan, index + 1);
            if (curve->getInterpolationType() == COLLADAFW::AnimationCurve::INTERPOLATION_BEZIER) {
              bez.ipo = BEZT_IPO_BEZ;
              bez.h1 = bez.h2 = HD_AUTO_ANIM;
            }
            else {
              bez.ipo = BEZT_IPO_CONST;
            }
          }
          else {
            bez.ipo = BEZT_IPO_LIN;
          }
#if 0
          bez.ipo = U.ipo_new; /* use default interpolation mode here... */
#endif
          bez.f1 = bez.f2 = bez.f3 = SELECT;

          blender::animrig::insert_bezt_fcurve(fcu, &bez, INSERTKEY_NOFLAGS);
        }

        BKE_fcurve_handles_recalc(fcu);

        fcurves.push_back(fcu);
        unused_curves.push_back(fcu);
      }
      break;
    }
    default:
      fprintf(stderr,
              "Output dimension of %d is not yet supported (animation id = %s)\n",
              int(dim),
              curve->getOriginalId().c_str());
  }
}

void AnimationImporter::fcurve_deg_to_rad(FCurve *cu)
{
  for (uint i = 0; i < cu->totvert; i++) {
    /* TODO: convert handles too. */
    cu->bezt[i].vec[1][1] *= DEG2RADF(1.0f);
    cu->bezt[i].vec[0][1] *= DEG2RADF(1.0f);
    cu->bezt[i].vec[2][1] *= DEG2RADF(1.0f);
  }
}

void AnimationImporter::fcurve_scale(FCurve *cu, int scale)
{
  for (uint i = 0; i < cu->totvert; i++) {
    /* TODO: convert handles too. */
    cu->bezt[i].vec[1][1] *= scale;
    cu->bezt[i].vec[0][1] *= scale;
    cu->bezt[i].vec[2][1] *= scale;
  }
}

void AnimationImporter::fcurve_is_used(FCurve *fcu)
{
  unused_curves.erase(std::remove(unused_curves.begin(), unused_curves.end(), fcu),
                      unused_curves.end());
}

AnimationImporter::~AnimationImporter()
{
  /* free unused FCurves */
  for (FCurve *unused_curve : unused_curves) {
    BKE_fcurve_free(unused_curve);
  }

  if (!unused_curves.empty()) {
    fprintf(stderr, "removed %d unused curves\n", int(unused_curves.size()));
  }
}

bool AnimationImporter::write_animation(const COLLADAFW::Animation *anim)
{
  if (anim->getAnimationType() == COLLADAFW::Animation::ANIMATION_CURVE) {
    COLLADAFW::AnimationCurve *curve = (COLLADAFW::AnimationCurve *)anim;

    /* XXX Don't know if it's necessary
     * Should we check outPhysicalDimension? */
    if (curve->getInPhysicalDimension() != COLLADAFW::PHYSICAL_DIMENSION_TIME) {
      fprintf(stderr, "Inputs physical dimension is not time.\n");
      return true;
    }

    /* a curve can have mixed interpolation type,
     * in this case curve->getInterpolationTypes returns a list of interpolation types per key */
    COLLADAFW::AnimationCurve::InterpolationType interp = curve->getInterpolationType();

    if (interp != COLLADAFW::AnimationCurve::INTERPOLATION_MIXED) {
      switch (interp) {
        case COLLADAFW::AnimationCurve::INTERPOLATION_LINEAR:
        case COLLADAFW::AnimationCurve::INTERPOLATION_BEZIER:
        case COLLADAFW::AnimationCurve::INTERPOLATION_STEP:
          animation_to_fcurves(curve);
          break;
        default:
          /* TODO: there are also CARDINAL, HERMITE, BSPLINE and STEP types. */
          fprintf(stderr,
                  "CARDINAL, HERMITE and BSPLINE anim interpolation types not supported yet.\n");
          break;
      }
    }
    else {
      /* not supported yet */
      fprintf(stderr, "MIXED anim interpolation type is not supported yet.\n");
    }
  }
  else {
    fprintf(stderr, "FORMULA animation type is not supported yet.\n");
  }

  return true;
}

bool AnimationImporter::write_animation_list(const COLLADAFW::AnimationList *animlist)
{
  const COLLADAFW::UniqueId &animlist_id = animlist->getUniqueId();
  animlist_map[animlist_id] = animlist;

#if 0

  /* should not happen */
  if (uid_animated_map.find(animlist_id) == uid_animated_map.end()) {
    return true;
  }

  /* for bones rna_path is like: pose.bones["bone-name"].rotation */

#endif

  return true;
}

void AnimationImporter::read_node_transform(COLLADAFW::Node *node, Object *ob)
{
  float mat[4][4];
  TransformReader::get_node_mat(mat, node, &uid_animated_map, ob);
  if (ob) {
    copy_m4_m4(ob->runtime->object_to_world.ptr(), mat);
    BKE_object_apply_mat4(ob, ob->object_to_world().ptr(), false, false);
  }
}

void AnimationImporter::modify_fcurve(std::vector<FCurve *> *curves,
                                      const char *rna_path,
                                      int array_index,
                                      int scale)
{
  std::vector<FCurve *>::iterator it;
  int i;
  for (it = curves->begin(), i = 0; it != curves->end(); it++, i++) {
    FCurve *fcu = *it;
    fcu->rna_path = BLI_strdup(rna_path);

    if (array_index == -1) {
      fcu->array_index = i;
    }
    else {
      fcu->array_index = array_index;
    }

    if (scale != 1) {
      fcurve_scale(fcu, scale);
    }

    fcurve_is_used(fcu);
  }
}

void AnimationImporter::unused_fcurve(std::vector<FCurve *> *curves)
{
  /* when an error happens and we can't actually use curve remove it from unused_curves */
  std::vector<FCurve *>::iterator it;
  for (it = curves->begin(); it != curves->end(); it++) {
    FCurve *fcu = *it;
    fcurve_is_used(fcu);
  }
}

void AnimationImporter::find_frames(std::vector<float> *frames, std::vector<FCurve *> *curves)
{
  std::vector<FCurve *>::iterator iter;
  for (iter = curves->begin(); iter != curves->end(); iter++) {
    FCurve *fcu = *iter;

    for (uint k = 0; k < fcu->totvert; k++) {
      /* get frame value from bezTriple */
      float fra = fcu->bezt[k].vec[1][0];
      /* if frame already not added add frame to frames */
      if (std::find(frames->begin(), frames->end(), fra) == frames->end()) {
        frames->push_back(fra);
      }
    }
  }
}

static int get_animation_axis_index(const COLLADABU::Math::Vector3 &axis)
{
  int index;
  if (COLLADABU::Math::Vector3::UNIT_X == axis) {
    index = 0;
  }
  else if (COLLADABU::Math::Vector3::UNIT_Y == axis) {
    index = 1;
  }
  else if (COLLADABU::Math::Vector3::UNIT_Z == axis) {
    index = 2;
  }
  else {
    index = -1;
  }
  return index;
}

void AnimationImporter::Assign_transform_animations(
    COLLADAFW::Transformation *transform,
    const COLLADAFW::AnimationList::AnimationBinding *binding,
    std::vector<FCurve *> *curves,
    bool is_joint,
    char *joint_path)
{
  COLLADAFW::Transformation::TransformationType tm_type = transform->getTransformationType();
  bool is_matrix = tm_type == COLLADAFW::Transformation::MATRIX;
  bool is_rotation = tm_type == COLLADAFW::Transformation::ROTATE;

  /* to check if the no of curves are valid */
  bool xyz =
      (ELEM(tm_type, COLLADAFW::Transformation::TRANSLATE, COLLADAFW::Transformation::SCALE) &&
       binding->animationClass == COLLADAFW::AnimationList::POSITION_XYZ);

  if (!((!xyz && curves->size() == 1) || (xyz && curves->size() == 3) || is_matrix)) {
    fprintf(stderr, "expected %d curves, got %d\n", xyz ? 3 : 1, int(curves->size()));
    return;
  }

  char rna_path[100];

  switch (tm_type) {
    case COLLADAFW::Transformation::TRANSLATE:
    case COLLADAFW::Transformation::SCALE: {
      bool loc = tm_type == COLLADAFW::Transformation::TRANSLATE;
      if (is_joint) {
        SNPRINTF(rna_path, "%s.%s", joint_path, loc ? "location" : "scale");
      }
      else {
        STRNCPY(rna_path, loc ? "location" : "scale");
      }

      switch (binding->animationClass) {
        case COLLADAFW::AnimationList::POSITION_X:
          modify_fcurve(curves, rna_path, 0);
          break;
        case COLLADAFW::AnimationList::POSITION_Y:
          modify_fcurve(curves, rna_path, 1);
          break;
        case COLLADAFW::AnimationList::POSITION_Z:
          modify_fcurve(curves, rna_path, 2);
          break;
        case COLLADAFW::AnimationList::POSITION_XYZ:
          modify_fcurve(curves, rna_path, -1);
          break;
        default:
          unused_fcurve(curves);
          fprintf(stderr,
                  "AnimationClass %d is not supported for %s.\n",
                  binding->animationClass,
                  loc ? "TRANSLATE" : "SCALE");
      }
      break;
    }

    case COLLADAFW::Transformation::ROTATE: {
      if (is_joint) {
        SNPRINTF(rna_path, "%s.rotation_euler", joint_path);
      }
      else {
        STRNCPY(rna_path, "rotation_euler");
      }
      std::vector<FCurve *>::iterator iter;
      for (iter = curves->begin(); iter != curves->end(); iter++) {
        FCurve *fcu = *iter;

        /* if transform is rotation the fcurves values must be turned in to radian. */
        if (is_rotation) {
          fcurve_deg_to_rad(fcu);
        }
      }
      const COLLADAFW::Rotate *rot = (COLLADAFW::Rotate *)transform;
      const COLLADABU::Math::Vector3 &axis = rot->getRotationAxis();

      switch (binding->animationClass) {
        case COLLADAFW::AnimationList::ANGLE: {
          int axis_index = get_animation_axis_index(axis);
          if (axis_index >= 0) {
            modify_fcurve(curves, rna_path, axis_index);
          }
          else {
            unused_fcurve(curves);
          }
          break;
        }
        case COLLADAFW::AnimationList::AXISANGLE:
        /* TODO: convert axis-angle to quaternion? or XYZ? */
        default:
          unused_fcurve(curves);
          fprintf(stderr,
                  "AnimationClass %d is not supported for ROTATE transformation.\n",
                  binding->animationClass);
      }
      break;
    }

    case COLLADAFW::Transformation::MATRIX:
#if 0
    {
      COLLADAFW::Matrix *mat = (COLLADAFW::Matrix *)transform;
      COLLADABU::Math::Matrix4 mat4 = mat->getMatrix();
      switch (binding->animationClass) {
        case COLLADAFW::AnimationList::TRANSFORM:
      }
    }
#endif
      unused_fcurve(curves);
      break;
    case COLLADAFW::Transformation::SKEW:
    case COLLADAFW::Transformation::LOOKAT:
      unused_fcurve(curves);
      fprintf(stderr, "Animation of SKEW and LOOKAT transformations is not supported yet.\n");
      break;
  }
}

void AnimationImporter::Assign_color_animations(const COLLADAFW::UniqueId &listid,
                                                AnimData &adt,
                                                const char *anim_type)
{
  BLI_assert(adt.action != nullptr);

  char rna_path[100];
  STRNCPY(rna_path, anim_type);

  const COLLADAFW::AnimationList *animlist = animlist_map[listid];
  if (animlist == nullptr) {
    fprintf(stderr,
            "Collada: No animlist found for ID: %s of type %s\n",
            listid.toAscii().c_str(),
            anim_type);
    return;
  }

  const COLLADAFW::AnimationList::AnimationBindings &bindings = animlist->getAnimationBindings();
  /* all the curves belonging to the current binding */
  std::vector<FCurve *> animcurves;
  for (uint j = 0; j < bindings.getCount(); j++) {
    animcurves = curve_map[bindings[j].animation];

    switch (bindings[j].animationClass) {
      case COLLADAFW::AnimationList::COLOR_R:
        modify_fcurve(&animcurves, rna_path, 0);
        break;
      case COLLADAFW::AnimationList::COLOR_G:
        modify_fcurve(&animcurves, rna_path, 1);
        break;
      case COLLADAFW::AnimationList::COLOR_B:
        modify_fcurve(&animcurves, rna_path, 2);
        break;
      case COLLADAFW::AnimationList::COLOR_RGB:
      case COLLADAFW::AnimationList::COLOR_RGBA: /* to do-> set intensity */
        modify_fcurve(&animcurves, rna_path, -1);
        break;

      default:
        unused_fcurve(&animcurves);
        fprintf(stderr,
                "AnimationClass %d is not supported for %s.\n",
                bindings[j].animationClass,
                "COLOR");
    }

    std::vector<FCurve *>::iterator iter;
    /* Add the curves of the current animation to the object */
    for (iter = animcurves.begin(); iter != animcurves.end(); iter++) {
      FCurve *fcu = *iter;
      blender::animrig::action_fcurve_attach(
          adt.action->wrap(), adt.slot_handle, *fcu, std::nullopt);
      fcurve_is_used(fcu);
    }
  }
}

void AnimationImporter::Assign_float_animations(const COLLADAFW::UniqueId &listid,
                                                AnimData &adt,
                                                const char *anim_type)
{
  BLI_assert(adt.action != nullptr);

  char rna_path[100];
  if (animlist_map.find(listid) == animlist_map.end()) {
    return;
  }

  /* anim_type has animations */
  const COLLADAFW::AnimationList *animlist = animlist_map[listid];
  const COLLADAFW::AnimationList::AnimationBindings &bindings = animlist->getAnimationBindings();
  /* all the curves belonging to the current binding */
  std::vector<FCurve *> animcurves;
  for (uint j = 0; j < bindings.getCount(); j++) {
    animcurves = curve_map[bindings[j].animation];

    STRNCPY(rna_path, anim_type);
    modify_fcurve(&animcurves, rna_path, 0);
    std::vector<FCurve *>::iterator iter;
    /* Add the curves of the current animation to the object */
    for (iter = animcurves.begin(); iter != animcurves.end(); iter++) {
      FCurve *fcu = *iter;
      /* All anim_types whose values are to be converted from Degree to Radians can be ORed here
       */
      if (STREQ("spot_size", anim_type)) {
        /* NOTE: Do NOT convert if imported file was made by blender <= 2.69.10
         * Reason: old blender versions stored spot_size in radians (was a bug)
         */
        if (this->import_from_version.empty() ||
            BLI_strcasecmp_natural(this->import_from_version.c_str(), "2.69.10") != -1)
        {
          fcurve_deg_to_rad(fcu);
        }
      }
      /** XXX What About animation-type "rotation" ? */

      blender::animrig::action_fcurve_attach(
          adt.action->wrap(), adt.slot_handle, *fcu, std::nullopt);
      fcurve_is_used(fcu);
    }
  }
}

float AnimationImporter::convert_to_focal_length(float in_xfov,
                                                 int fov_type,
                                                 float aspect,
                                                 float sensorx)
{
  /* NOTE: Needs more testing (As we currently have no official test data for this) */
  float xfov = (fov_type == CAMERA_YFOV) ?
                   (2.0f * atanf(aspect * tanf(DEG2RADF(in_xfov) * 0.5f))) :
                   DEG2RADF(in_xfov);
  return fov_to_focallength(xfov, sensorx);
}

void AnimationImporter::Assign_lens_animations(const COLLADAFW::UniqueId &listid,
                                               AnimData &adt,
                                               const double aspect,
                                               const Camera *cam,
                                               const char *anim_type,
                                               int fov_type)
{
  BLI_assert(adt.action != nullptr);

  char rna_path[100];
  if (animlist_map.find(listid) == animlist_map.end()) {
    return;
  }

  /* anim_type has animations */
  const COLLADAFW::AnimationList *animlist = animlist_map[listid];
  const COLLADAFW::AnimationList::AnimationBindings &bindings = animlist->getAnimationBindings();
  /* all the curves belonging to the current binding */
  std::vector<FCurve *> animcurves;
  for (uint j = 0; j < bindings.getCount(); j++) {
    animcurves = curve_map[bindings[j].animation];

    STRNCPY(rna_path, anim_type);

    modify_fcurve(&animcurves, rna_path, 0);
    std::vector<FCurve *>::iterator iter;
    /* Add the curves of the current animation to the object */
    for (iter = animcurves.begin(); iter != animcurves.end(); iter++) {
      FCurve *fcu = *iter;

      for (uint i = 0; i < fcu->totvert; i++) {
        fcu->bezt[i].vec[0][1] = convert_to_focal_length(
            fcu->bezt[i].vec[0][1], fov_type, aspect, cam->sensor_x);
        fcu->bezt[i].vec[1][1] = convert_to_focal_length(
            fcu->bezt[i].vec[1][1], fov_type, aspect, cam->sensor_x);
        fcu->bezt[i].vec[2][1] = convert_to_focal_length(
            fcu->bezt[i].vec[2][1], fov_type, aspect, cam->sensor_x);
      }

      blender::animrig::action_fcurve_attach(
          adt.action->wrap(), adt.slot_handle, *fcu, std::nullopt);
      fcurve_is_used(fcu);
    }
  }
}

void AnimationImporter::apply_matrix_curves(Object *ob,
                                            std::vector<FCurve *> &animcurves,
                                            COLLADAFW::Node *root,
                                            COLLADAFW::Node *node,
                                            COLLADAFW::Transformation *tm)
{
  bool is_joint = node->getType() == COLLADAFW::Node::JOINT;
  const char *bone_name = is_joint ? bc_get_joint_name(node) : nullptr;
  char joint_path[200];
  if (is_joint) {
    armature_importer->get_rna_path_for_joint(node, joint_path, sizeof(joint_path));
  }

  std::vector<float> frames;
  find_frames(&frames, &animcurves);

  float irest_dae[4][4];
  float rest[4][4], irest[4][4];

  if (is_joint) {
    get_joint_rest_mat(irest_dae, root, node);
    invert_m4(irest_dae);

    Bone *bone = BKE_armature_find_bone_name((bArmature *)ob->data, bone_name);
    if (!bone) {
      fprintf(stderr, "cannot find bone \"%s\"\n", bone_name);
      return;
    }

    unit_m4(rest);
    copy_m4_m4(rest, bone->arm_mat);
    invert_m4_m4(irest, rest);
  }
  /* new curves to assign matrix transform animation */
  FCurve *newcu[10]; /* if tm_type is matrix, then create 10 curves: 4 rot, 3 loc, 3 scale */
  uint totcu = 10;
  const char *tm_str = nullptr;
  char rna_path[200];
  for (int i = 0; i < totcu; i++) {

    int axis = i;

    if (i < 4) {
      tm_str = "rotation_quaternion";
      axis = i;
    }
    else if (i < 7) {
      tm_str = "location";
      axis = i - 4;
    }
    else {
      tm_str = "scale";
      axis = i - 7;
    }

    if (is_joint) {
      SNPRINTF(rna_path, "%s.%s", joint_path, tm_str);
    }
    else {
      STRNCPY(rna_path, tm_str);
    }
    newcu[i] = create_fcurve(axis, rna_path);
    newcu[i]->totvert = frames.size();
  }

  if (frames.empty()) {
    return;
  }

  std::sort(frames.begin(), frames.end());

  std::vector<float>::iterator it;

  /* sample values at each frame */
  for (it = frames.begin(); it != frames.end(); it++) {
    float fra = *it;

    float mat[4][4];
    float matfra[4][4];

    unit_m4(matfra);

    /* calc object-space mat */
    evaluate_transform_at_frame(matfra, node, fra);

    /* for joints, we need a special matrix */
    if (is_joint) {
      /* special matrix: iR * M * iR_dae * R
       * where R, iR are bone rest and inverse rest mats in world space (Blender bones),
       * iR_dae is joint inverse rest matrix (DAE)
       * and M is an evaluated joint world-space matrix (DAE) */
      float temp[4][4], par[4][4];

      /* calc M */
      calc_joint_parent_mat_rest(par, nullptr, root, node);
      mul_m4_m4m4(temp, par, matfra);

      /* calc special matrix */
      mul_m4_series(mat, irest, temp, irest_dae, rest);
    }
    else {
      copy_m4_m4(mat, matfra);
    }

    float rot[4], loc[3], scale[3];
    mat4_decompose(loc, rot, scale, mat);

    /* add keys */
    for (int i = 0; i < totcu; i++) {
      if (i < 4) {
        add_bezt(newcu[i], fra, rot[i]);
      }
      else if (i < 7) {
        add_bezt(newcu[i], fra, loc[i - 4]);
      }
      else {
        add_bezt(newcu[i], fra, scale[i - 7]);
      }
    }
  }
  Main *bmain = CTX_data_main(mContext);

  ensure_action_and_slot_for_id(bmain, ob->id);

  /* add curves */
  for (int i = 0; i < totcu; i++) {
    if (is_joint) {
      add_bone_fcurve(ob, node, newcu[i]);
    }
    else {
      blender::animrig::action_fcurve_attach(
          ob->adt->action->wrap(), ob->adt->slot_handle, *newcu[i], std::nullopt);
    }
#if 0
    fcurve_is_used(newcu[i]); /* never added to unused */
#endif
  }

  if (is_joint) {
    bPoseChannel *chan = BKE_pose_channel_find_name(ob->pose, bone_name);
    chan->rotmode = ROT_MODE_QUAT;
  }
  else {
    ob->rotmode = ROT_MODE_QUAT;
  }
}

/*
 * This function returns the aspect ration from the Collada camera.
 *
 * NOTE:COLLADA allows to specify either XFov, or YFov alone.
 * In that case the aspect ratio can be determined from
 * the viewport aspect ratio (which is 1:1 ?)
 * XXX: check this: its probably wrong!
 * If both values are specified, then the aspect ration is simply xfov/yfov
 * and if aspect ratio is defined, then .. well then its that one.
 */
static double get_aspect_ratio(const COLLADAFW::Camera *camera)
{
  double aspect = camera->getAspectRatio().getValue();

  if (aspect == 0) {
    const double yfov = camera->getYFov().getValue();

    if (yfov == 0) {
      aspect = 1; /* assume yfov and xfov are equal */
    }
    else {
      const double xfov = camera->getXFov().getValue();
      if (xfov == 0) {
        aspect = 1;
      }
      else {
        aspect = xfov / yfov;
      }
    }
  }
  return aspect;
}

void AnimationImporter::translate_Animations(
    COLLADAFW::Node *node,
    std::map<COLLADAFW::UniqueId, COLLADAFW::Node *> &root_map,
    std::multimap<COLLADAFW::UniqueId, Object *> &object_map,
    std::map<COLLADAFW::UniqueId, const COLLADAFW::Object *> FW_object_map,
    std::map<COLLADAFW::UniqueId, Material *> uid_material_map)
{
  bool is_joint = node->getType() == COLLADAFW::Node::JOINT;
  COLLADAFW::UniqueId uid = node->getUniqueId();
  COLLADAFW::Node *root = root_map.find(uid) == root_map.end() ? node : root_map[uid];

  Object *ob;
  if (is_joint) {
    ob = armature_importer->get_armature_for_joint(root);
  }
  else {
    ob = object_map.find(uid) == object_map.end() ? nullptr : object_map.find(uid)->second;
  }

  if (!ob) {
    fprintf(stderr, "cannot find Object for Node with id=\"%s\"\n", node->getOriginalId().c_str());
    return;
  }

  AnimationImporter::AnimMix *animType = get_animation_type(node, FW_object_map);
  Main *bmain = CTX_data_main(mContext);

  if ((animType->transform) != 0) {
    // const char *bone_name = is_joint ? bc_get_joint_name(node) : nullptr; /* UNUSED */
    char joint_path[200];

    if (is_joint) {
      armature_importer->get_rna_path_for_joint(node, joint_path, sizeof(joint_path));
    }

    ensure_action_and_slot_for_id(bmain, ob->id);

    const COLLADAFW::TransformationPointerArray &nodeTransforms = node->getTransformations();

    /* for each transformation in node */
    for (uint i = 0; i < nodeTransforms.getCount(); i++) {
      COLLADAFW::Transformation *transform = nodeTransforms[i];
      COLLADAFW::Transformation::TransformationType tm_type = transform->getTransformationType();

      bool is_rotation = tm_type == COLLADAFW::Transformation::ROTATE;
      bool is_matrix = tm_type == COLLADAFW::Transformation::MATRIX;

      const COLLADAFW::UniqueId &listid = transform->getAnimationList();

      /* check if transformation has animations */
      if (animlist_map.find(listid) == animlist_map.end()) {
        continue;
      }

      /* transformation has animations */
      const COLLADAFW::AnimationList *animlist = animlist_map[listid];
      const COLLADAFW::AnimationList::AnimationBindings &bindings =
          animlist->getAnimationBindings();
      /* all the curves belonging to the current binding */
      std::vector<FCurve *> animcurves;
      for (uint j = 0; j < bindings.getCount(); j++) {
        animcurves = curve_map[bindings[j].animation];
        if (is_matrix) {
          apply_matrix_curves(ob, animcurves, root, node, transform);
        }
        else {
          /* Calculate RNA-paths and array index of F-Curves according to transformation and
           * animation class */
          Assign_transform_animations(transform, &bindings[j], &animcurves, is_joint, joint_path);

          std::vector<FCurve *>::iterator iter;
          /* Add the curves of the current animation to the object */
          for (iter = animcurves.begin(); iter != animcurves.end(); iter++) {
            FCurve *fcu = *iter;
            blender::animrig::action_fcurve_attach(
                ob->adt->action->wrap(), ob->adt->slot_handle, *fcu, std::nullopt);
            fcurve_is_used(fcu);
          }
        }
      }

      if (is_rotation && !(is_joint || is_matrix)) {
        ob->rotmode = ROT_MODE_EUL;
      }
    }
  }

  if ((animType->light) != 0) {
    Light *lamp = (Light *)ob->data;
    ensure_action_and_slot_for_id(bmain, lamp->id);

    const COLLADAFW::InstanceLightPointerArray &nodeLights = node->getInstanceLights();

    for (uint i = 0; i < nodeLights.getCount(); i++) {
      const COLLADAFW::Light *light = (COLLADAFW::Light *)
          FW_object_map[nodeLights[i]->getInstanciatedObjectId()];

      if ((animType->light & LIGHT_COLOR) != 0) {
        const COLLADAFW::Color *col = &light->getColor();
        const COLLADAFW::UniqueId &listid = col->getAnimationList();

        Assign_color_animations(listid, *lamp->adt, "color");
      }
      if ((animType->light & LIGHT_FOA) != 0) {
        const COLLADAFW::AnimatableFloat *foa = &light->getFallOffAngle();
        const COLLADAFW::UniqueId &listid = foa->getAnimationList();

        Assign_float_animations(listid, *lamp->adt, "spot_size");
      }
      if ((animType->light & LIGHT_FOE) != 0) {
        const COLLADAFW::AnimatableFloat *foe = &light->getFallOffExponent();
        const COLLADAFW::UniqueId &listid = foe->getAnimationList();

        Assign_float_animations(listid, *lamp->adt, "spot_blend");
      }
    }
  }

  if (animType->camera != 0) {

    Camera *cam = (Camera *)ob->data;
    ensure_action_and_slot_for_id(bmain, cam->id);

    const COLLADAFW::InstanceCameraPointerArray &nodeCameras = node->getInstanceCameras();

    for (uint i = 0; i < nodeCameras.getCount(); i++) {
      const COLLADAFW::Camera *camera = (COLLADAFW::Camera *)
          FW_object_map[nodeCameras[i]->getInstanciatedObjectId()];

      if ((animType->camera & CAMERA_XFOV) != 0) {
        const COLLADAFW::AnimatableFloat *xfov = &camera->getXFov();
        const COLLADAFW::UniqueId &listid = xfov->getAnimationList();
        double aspect = get_aspect_ratio(camera);
        Assign_lens_animations(listid, *cam->adt, aspect, cam, "lens", CAMERA_XFOV);
      }

      else if ((animType->camera & CAMERA_YFOV) != 0) {
        const COLLADAFW::AnimatableFloat *yfov = &camera->getYFov();
        const COLLADAFW::UniqueId &listid = yfov->getAnimationList();
        double aspect = get_aspect_ratio(camera);
        Assign_lens_animations(listid, *cam->adt, aspect, cam, "lens", CAMERA_YFOV);
      }

      else if ((animType->camera & CAMERA_XMAG) != 0) {
        const COLLADAFW::AnimatableFloat *xmag = &camera->getXMag();
        const COLLADAFW::UniqueId &listid = xmag->getAnimationList();
        Assign_float_animations(listid, *cam->adt, "ortho_scale");
      }

      else if ((animType->camera & CAMERA_YMAG) != 0) {
        const COLLADAFW::AnimatableFloat *ymag = &camera->getYMag();
        const COLLADAFW::UniqueId &listid = ymag->getAnimationList();
        Assign_float_animations(listid, *cam->adt, "ortho_scale");
      }

      if ((animType->camera & CAMERA_ZFAR) != 0) {
        const COLLADAFW::AnimatableFloat *zfar = &camera->getFarClippingPlane();
        const COLLADAFW::UniqueId &listid = zfar->getAnimationList();
        Assign_float_animations(listid, *cam->adt, "clip_end");
      }

      if ((animType->camera & CAMERA_ZNEAR) != 0) {
        const COLLADAFW::AnimatableFloat *znear = &camera->getNearClippingPlane();
        const COLLADAFW::UniqueId &listid = znear->getAnimationList();
        Assign_float_animations(listid, *cam->adt, "clip_start");
      }
    }
  }
  if (animType->material != 0) {
    const COLLADAFW::InstanceGeometryPointerArray &nodeGeoms = node->getInstanceGeometries();
    for (uint i = 0; i < nodeGeoms.getCount(); i++) {
      const COLLADAFW::MaterialBindingArray &matBinds = nodeGeoms[i]->getMaterialBindings();
      for (uint j = 0; j < matBinds.getCount(); j++) {
        const COLLADAFW::UniqueId &matuid = matBinds[j].getReferencedMaterial();
        const COLLADAFW::Effect *ef = (COLLADAFW::Effect *)(FW_object_map[matuid]);
        if (ef != nullptr) { /* can be nullptr #28909. */
          Material *ma = uid_material_map[matuid];
          if (!ma) {
            fprintf(stderr,
                    "Collada: Node %s refers to undefined material\n",
                    node->getName().c_str());
            continue;
          }
          ensure_action_and_slot_for_id(bmain, ma->id);

          const COLLADAFW::CommonEffectPointerArray &commonEffects = ef->getCommonEffects();
          COLLADAFW::EffectCommon *efc = commonEffects[0];
          if ((animType->material & MATERIAL_SHININESS) != 0) {
            const COLLADAFW::FloatOrParam *shin = &efc->getShininess();
            const COLLADAFW::UniqueId &listid = shin->getAnimationList();
            Assign_float_animations(listid, *ma->adt, "specular_hardness");
          }

          if ((animType->material & MATERIAL_IOR) != 0) {
            const COLLADAFW::FloatOrParam *ior = &efc->getIndexOfRefraction();
            const COLLADAFW::UniqueId &listid = ior->getAnimationList();
            Assign_float_animations(listid, *ma->adt, "raytrace_transparency.ior");
          }

          if ((animType->material & MATERIAL_SPEC_COLOR) != 0) {
            const COLLADAFW::ColorOrTexture *cot = &efc->getSpecular();
            const COLLADAFW::UniqueId &listid = cot->getColor().getAnimationList();
            Assign_color_animations(listid, *ma->adt, "specular_color");
          }

          if ((animType->material & MATERIAL_DIFF_COLOR) != 0) {
            const COLLADAFW::ColorOrTexture *cot = &efc->getDiffuse();
            const COLLADAFW::UniqueId &listid = cot->getColor().getAnimationList();
            Assign_color_animations(listid, *ma->adt, "diffuse_color");
          }
        }
      }
    }
  }

  delete animType;
}

AnimationImporter::AnimMix *AnimationImporter::get_animation_type(
    const COLLADAFW::Node *node,
    std::map<COLLADAFW::UniqueId, const COLLADAFW::Object *> FW_object_map)
{
  AnimMix *types = new AnimMix();

  const COLLADAFW::TransformationPointerArray &nodeTransforms = node->getTransformations();

  /* for each transformation in node */
  for (uint i = 0; i < nodeTransforms.getCount(); i++) {
    COLLADAFW::Transformation *transform = nodeTransforms[i];
    const COLLADAFW::UniqueId &listid = transform->getAnimationList();

    /* check if transformation has animations */
    if (animlist_map.find(listid) == animlist_map.end()) {
      continue;
    }

    types->transform = types->transform | BC_NODE_TRANSFORM;
    break;
  }
  const COLLADAFW::InstanceLightPointerArray &nodeLights = node->getInstanceLights();

  for (uint i = 0; i < nodeLights.getCount(); i++) {
    const COLLADAFW::Light *light = (COLLADAFW::Light *)
        FW_object_map[nodeLights[i]->getInstanciatedObjectId()];
    types->light = setAnimType(&light->getColor(), (types->light), LIGHT_COLOR);
    types->light = setAnimType(&light->getFallOffAngle(), (types->light), LIGHT_FOA);
    types->light = setAnimType(&light->getFallOffExponent(), (types->light), LIGHT_FOE);

    if (types->light != 0) {
      break;
    }
  }

  const COLLADAFW::InstanceCameraPointerArray &nodeCameras = node->getInstanceCameras();
  for (uint i = 0; i < nodeCameras.getCount(); i++) {
    const COLLADAFW::Camera *camera = (COLLADAFW::Camera *)
        FW_object_map[nodeCameras[i]->getInstanciatedObjectId()];
    if (camera == nullptr) {
      /* Can happen if the node refers to an unknown camera. */
      continue;
    }

    const bool is_perspective_type = camera->getCameraType() == COLLADAFW::Camera::PERSPECTIVE;

    int addition;
    const COLLADAFW::Animatable *mag;
    const COLLADAFW::UniqueId listid = camera->getYMag().getAnimationList();
    if (animlist_map.find(listid) != animlist_map.end()) {
      mag = &camera->getYMag();
      addition = (is_perspective_type) ? CAMERA_YFOV : CAMERA_YMAG;
    }
    else {
      mag = &camera->getXMag();
      addition = (is_perspective_type) ? CAMERA_XFOV : CAMERA_XMAG;
    }
    types->camera = setAnimType(mag, (types->camera), addition);

    types->camera = setAnimType(&camera->getFarClippingPlane(), (types->camera), CAMERA_ZFAR);
    types->camera = setAnimType(&camera->getNearClippingPlane(), (types->camera), CAMERA_ZNEAR);

    if (types->camera != 0) {
      break;
    }
  }

  const COLLADAFW::InstanceGeometryPointerArray &nodeGeoms = node->getInstanceGeometries();
  for (uint i = 0; i < nodeGeoms.getCount(); i++) {
    const COLLADAFW::MaterialBindingArray &matBinds = nodeGeoms[i]->getMaterialBindings();
    for (uint j = 0; j < matBinds.getCount(); j++) {
      const COLLADAFW::UniqueId &matuid = matBinds[j].getReferencedMaterial();
      const COLLADAFW::Effect *ef = (COLLADAFW::Effect *)(FW_object_map[matuid]);
      if (ef != nullptr) { /* can be nullptr #28909. */
        const COLLADAFW::CommonEffectPointerArray &commonEffects = ef->getCommonEffects();
        if (!commonEffects.empty()) {
          COLLADAFW::EffectCommon *efc = commonEffects[0];
          types->material = setAnimType(
              &efc->getShininess(), (types->material), MATERIAL_SHININESS);
          types->material = setAnimType(
              &efc->getSpecular().getColor(), (types->material), MATERIAL_SPEC_COLOR);
          types->material = setAnimType(
              &efc->getDiffuse().getColor(), (types->material), MATERIAL_DIFF_COLOR);
#if 0
          types->material = setAnimType(&(efc->get()), (types->material), MATERIAL_TRANSPARENCY);
#endif
          types->material = setAnimType(
              &efc->getIndexOfRefraction(), (types->material), MATERIAL_IOR);
        }
      }
    }
  }
  return types;
}

int AnimationImporter::setAnimType(const COLLADAFW::Animatable *prop, int types, int addition)
{
  int anim_type;
  const COLLADAFW::UniqueId &listid = prop->getAnimationList();
  if (animlist_map.find(listid) != animlist_map.end()) {
    anim_type = types | addition;
  }
  else {
    anim_type = types;
  }

  return anim_type;
}

void AnimationImporter::evaluate_transform_at_frame(float mat[4][4],
                                                    COLLADAFW::Node *node,
                                                    float fra)
{
  const COLLADAFW::TransformationPointerArray &tms = node->getTransformations();

  unit_m4(mat);

  for (uint i = 0; i < tms.getCount(); i++) {
    COLLADAFW::Transformation *tm = tms[i];
    COLLADAFW::Transformation::TransformationType type = tm->getTransformationType();
    float m[4][4];

    unit_m4(m);

    std::string nodename = node->getName().empty() ? node->getOriginalId() : node->getName();
    if (!evaluate_animation(tm, m, fra, nodename.c_str())) {
      switch (type) {
        case COLLADAFW::Transformation::ROTATE:
          dae_rotate_to_mat4(tm, m);
          break;
        case COLLADAFW::Transformation::TRANSLATE:
          dae_translate_to_mat4(tm, m);
          break;
        case COLLADAFW::Transformation::SCALE:
          dae_scale_to_mat4(tm, m);
          break;
        case COLLADAFW::Transformation::MATRIX:
          dae_matrix_to_mat4(tm, m);
          break;
        default:
          fprintf(stderr, "unsupported transformation type %d\n", type);
      }
    }

    float temp[4][4];
    copy_m4_m4(temp, mat);

    mul_m4_m4m4(mat, temp, m);
  }
}

static void report_class_type_unsupported(const char *path,
                                          const COLLADAFW::AnimationList::AnimationClass animclass,
                                          const COLLADAFW::Transformation::TransformationType type)
{
  if (animclass == COLLADAFW::AnimationList::UNKNOWN_CLASS) {
    fprintf(stderr, "%s: UNKNOWN animation class\n", path);
  }
  else {
    fprintf(stderr,
            "%s: animation class %d is not supported yet for transformation type %d\n",
            path,
            animclass,
            type);
  }
}

bool AnimationImporter::evaluate_animation(COLLADAFW::Transformation *tm,
                                           float mat[4][4],
                                           float fra,
                                           const char *node_id)
{
  const COLLADAFW::UniqueId &listid = tm->getAnimationList();
  COLLADAFW::Transformation::TransformationType type = tm->getTransformationType();

  if (!ELEM(type,
            COLLADAFW::Transformation::ROTATE,
            COLLADAFW::Transformation::SCALE,
            COLLADAFW::Transformation::TRANSLATE,
            COLLADAFW::Transformation::MATRIX))
  {
    fprintf(stderr, "animation of transformation %d is not supported yet\n", type);
    return false;
  }

  if (animlist_map.find(listid) == animlist_map.end()) {
    return false;
  }

  const COLLADAFW::AnimationList *animlist = animlist_map[listid];
  const COLLADAFW::AnimationList::AnimationBindings &bindings = animlist->getAnimationBindings();

  if (bindings.getCount()) {
    float vec[3];

    bool is_scale = (type == COLLADAFW::Transformation::SCALE);
    bool is_translate = (type == COLLADAFW::Transformation::TRANSLATE);

    if (is_scale) {
      dae_scale_to_v3(tm, vec);
    }
    else if (is_translate) {
      dae_translate_to_v3(tm, vec);
    }

    for (uint index = 0; index < bindings.getCount(); index++) {
      const COLLADAFW::AnimationList::AnimationBinding &binding = bindings[index];
      std::vector<FCurve *> &curves = curve_map[binding.animation];
      COLLADAFW::AnimationList::AnimationClass animclass = binding.animationClass;
      char path[100];

      switch (type) {
        case COLLADAFW::Transformation::ROTATE:
          SNPRINTF(path, "%s.rotate (binding %u)", node_id, index);
          break;
        case COLLADAFW::Transformation::SCALE:
          SNPRINTF(path, "%s.scale (binding %u)", node_id, index);
          break;
        case COLLADAFW::Transformation::TRANSLATE:
          SNPRINTF(path, "%s.translate (binding %u)", node_id, index);
          break;
        case COLLADAFW::Transformation::MATRIX:
          SNPRINTF(path, "%s.matrix (binding %u)", node_id, index);
          break;
        default:
          break;
      }

      if (type == COLLADAFW::Transformation::ROTATE) {
        if (curves.size() != 1) {
          fprintf(stderr, "expected 1 curve, got %d\n", int(curves.size()));
          return false;
        }

        /* TODO: support other animation-classes. */
        if (animclass != COLLADAFW::AnimationList::ANGLE) {
          report_class_type_unsupported(path, animclass, type);
          return false;
        }

        COLLADABU::Math::Vector3 &axis = ((COLLADAFW::Rotate *)tm)->getRotationAxis();

        float ax[3] = {float(axis[0]), float(axis[1]), float(axis[2])};
        float angle = evaluate_fcurve(curves[0], fra);
        axis_angle_to_mat4(mat, ax, angle);

        return true;
      }
      if (is_scale || is_translate) {
        bool is_xyz = animclass == COLLADAFW::AnimationList::POSITION_XYZ;

        if ((!is_xyz && curves.size() != 1) || (is_xyz && curves.size() != 3)) {
          if (is_xyz) {
            fprintf(stderr, "%s: expected 3 curves, got %d\n", path, int(curves.size()));
          }
          else {
            fprintf(stderr, "%s: expected 1 curve, got %d\n", path, int(curves.size()));
          }
          return false;
        }

        switch (animclass) {
          case COLLADAFW::AnimationList::POSITION_X:
            vec[0] = evaluate_fcurve(curves[0], fra);
            break;
          case COLLADAFW::AnimationList::POSITION_Y:
            vec[1] = evaluate_fcurve(curves[0], fra);
            break;
          case COLLADAFW::AnimationList::POSITION_Z:
            vec[2] = evaluate_fcurve(curves[0], fra);
            break;
          case COLLADAFW::AnimationList::POSITION_XYZ:
            vec[0] = evaluate_fcurve(curves[0], fra);
            vec[1] = evaluate_fcurve(curves[1], fra);
            vec[2] = evaluate_fcurve(curves[2], fra);
            break;
          default:
            report_class_type_unsupported(path, animclass, type);
            break;
        }
      }
      else if (type == COLLADAFW::Transformation::MATRIX) {
        /* for now, of matrix animation,
         * support only the case when all values are packed into one animation */
        if (curves.size() != 16) {
          fprintf(stderr, "%s: expected 16 curves, got %d\n", path, int(curves.size()));
          return false;
        }

        COLLADABU::Math::Matrix4 matrix;
        int mi = 0, mj = 0;

        for (const FCurve *curve : curves) {
          matrix.setElement(mi, mj, evaluate_fcurve(curve, fra));
          mj++;
          if (mj == 4) {
            mi++;
            mj = 0;
          }
        }
        UnitConverter::dae_matrix_to_mat4_(mat, matrix);
        return true;
      }
    }

    if (is_scale) {
      size_to_mat4(mat, vec);
    }
    else {
      copy_v3_v3(mat[3], vec);
    }

    return is_scale || is_translate;
  }

  return false;
}

void AnimationImporter::get_joint_rest_mat(float mat[4][4],
                                           COLLADAFW::Node *root,
                                           COLLADAFW::Node *node)
{
  /* if bind mat is not available,
   * use "current" node transform, i.e. all those tms listed inside <node> */
  if (!armature_importer->get_joint_bind_mat(mat, node)) {
    float par[4][4], m[4][4];

    calc_joint_parent_mat_rest(par, nullptr, root, node);
    get_node_mat(m, node, nullptr, nullptr);
    mul_m4_m4m4(mat, par, m);
  }
}

bool AnimationImporter::calc_joint_parent_mat_rest(float mat[4][4],
                                                   float par[4][4],
                                                   COLLADAFW::Node *node,
                                                   COLLADAFW::Node *end)
{
  float m[4][4];

  if (node == end) {
    par ? copy_m4_m4(mat, par) : unit_m4(mat);
    return true;
  }

  /* use bind matrix if available or calc "current" world mat */
  if (!armature_importer->get_joint_bind_mat(m, node)) {
    if (par) {
      float temp[4][4];
      get_node_mat(temp, node, nullptr, nullptr);
      mul_m4_m4m4(m, par, temp);
    }
    else {
      get_node_mat(m, node, nullptr, nullptr);
    }
  }

  COLLADAFW::NodePointerArray &children = node->getChildNodes();
  for (uint i = 0; i < children.getCount(); i++) {
    if (calc_joint_parent_mat_rest(mat, m, children[i], end)) {
      return true;
    }
  }

  return false;
}

void AnimationImporter::add_bone_fcurve(Object *ob, COLLADAFW::Node *node, FCurve *fcu)
{
  BLI_assert(ob->adt != nullptr && ob->adt->action != nullptr);

  const char *bone_name = bc_get_joint_name(node);

  blender::animrig::action_fcurve_attach(
      ob->adt->action->wrap(), ob->adt->slot_handle, *fcu, bone_name);
}

void AnimationImporter::set_import_from_version(std::string import_from_version)
{
  this->import_from_version = import_from_version;
}
