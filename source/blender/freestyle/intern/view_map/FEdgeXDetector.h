/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup freestyle
 * \brief Detects/flags/builds extended features edges on the WXEdge structure
 */

#include <vector>

#include "../geometry/Geom.h"

#include "../system/FreestyleConfig.h"
#include "../system/ProgressBar.h"
#include "../system/RenderMonitor.h"

#include "../winged_edge/Curvature.h"
#include "../winged_edge/WXEdge.h"

#include "MEM_guardedalloc.h"

namespace Freestyle {

using namespace Geometry;

/** This class takes as input a WXEdge structure and fills it */
class FEdgeXDetector {
 public:
  FEdgeXDetector()
  {
    _pProgressBar = nullptr;
    _pRenderMonitor = nullptr;
    _computeViewIndependent = true;
#if 0
    _bbox_diagonal = 1.0;
#endif
    _meanEdgeSize = 0;
    _computeRidgesAndValleys = true;
    _computeSuggestiveContours = true;
    _computeMaterialBoundaries = true;
    _sphereRadius = 1.0;
    _orthographicProjection = false;
    _faceSmoothness = false;
    _changes = false;
    _kr_derivative_epsilon = 0.0;
    _creaseAngle = 0.7;  // angle of 134.43 degrees
  }

  virtual ~FEdgeXDetector() {}

  /** Process shapes from a WingedEdge containing a list of WShapes */
  virtual void processShapes(WingedEdge &);

  // GENERAL STUFF
  virtual void preProcessShape(WXShape *iWShape);
  virtual void preProcessFace(WXFace *iFace);
  virtual void computeCurvatures(WXVertex *iVertex);

  // SILHOUETTE
  virtual void processSilhouetteShape(WXShape *iWShape);
  virtual void ProcessSilhouetteFace(WXFace *iFace);
  virtual void ProcessSilhouetteEdge(WXEdge *iEdge);

  // CREASE
  virtual void processCreaseShape(WXShape *iWShape);
  virtual void ProcessCreaseEdge(WXEdge *iEdge);

  /** Sets the minimum angle for detecting crease edges
   *  \param angle:
   *    The angular threshold in degrees (between 0 and 180) for detecting crease edges. An edge is
   * considered a crease edge if the angle between two faces sharing the edge is smaller than the
   * given threshold.
   */
  // XXX angle should be in radian...
  inline void setCreaseAngle(float angle)
  {
    if (angle < 0.0) {
      angle = 0.0;
    }
    else if (angle > 180.0) {
      angle = 180.0;
    }
    angle = cos(M_PI * (180.0 - angle) / 180.0);
    if (angle != _creaseAngle) {
      _creaseAngle = angle;
      _changes = true;
    }
  }

  // BORDER
  virtual void processBorderShape(WXShape *iWShape);
  virtual void ProcessBorderEdge(WXEdge *iEdge);

  // RIDGES AND VALLEYS
  virtual void processRidgesAndValleysShape(WXShape *iWShape);
  virtual void ProcessRidgeFace(WXFace *iFace);

  // SUGGESTIVE CONTOURS
  virtual void processSuggestiveContourShape(WXShape *iWShape);
  virtual void ProcessSuggestiveContourFace(WXFace *iFace);
  virtual void postProcessSuggestiveContourShape(WXShape *iShape);
  virtual void postProcessSuggestiveContourFace(WXFace *iFace);
  /** Sets the minimal derivative of the radial curvature for suggestive contours
   *  \param dkr:
   *    The minimal derivative of the radial curvature
   */
  inline void setSuggestiveContourKrDerivativeEpsilon(float dkr)
  {
    if (dkr != _kr_derivative_epsilon) {
      _kr_derivative_epsilon = dkr;
      _changes = true;
    }
  }

  // MATERIAL BOUNDARY
  virtual void processMaterialBoundaryShape(WXShape *iWShape);
  virtual void ProcessMaterialBoundaryEdge(WXEdge *iEdge);

  // EDGE MARKS
  virtual void processEdgeMarksShape(WXShape *iShape);
  virtual void ProcessEdgeMarks(WXEdge *iEdge);

  // EVERYBODY
  virtual void buildSmoothEdges(WXShape *iShape);

  /** Sets the current viewpoint */
  inline void setViewpoint(const Vec3f &ivp)
  {
    _Viewpoint = ivp;
  }

  inline void enableOrthographicProjection(bool b)
  {
    _orthographicProjection = b;
  }

  inline void enableRidgesAndValleysFlag(bool b)
  {
    _computeRidgesAndValleys = b;
  }

  inline void enableSuggestiveContours(bool b)
  {
    _computeSuggestiveContours = b;
  }

  inline void enableMaterialBoundaries(bool b)
  {
    _computeMaterialBoundaries = b;
  }

  inline void enableFaceSmoothness(bool b)
  {
    if (b != _faceSmoothness) {
      _faceSmoothness = b;
      _changes = true;
    }
  }

  inline void enableFaceMarks(bool b)
  {
    if (b != _faceMarks) {
      _faceMarks = b;
      _changes = true;
    }
  }

  /** Sets the radius of the geodesic sphere around each vertex (for the curvature computation)
   *  \param r:
   *    The radius of the sphere expressed as a ratio of the mean edge size
   */
  inline void setSphereRadius(float r)
  {
    if (r != _sphereRadius) {
      _sphereRadius = r;
      _changes = true;
    }
  }

  inline void setProgressBar(ProgressBar *iProgressBar)
  {
    _pProgressBar = iProgressBar;
  }

  inline void setRenderMonitor(RenderMonitor *iRenderMonitor)
  {
    _pRenderMonitor = iRenderMonitor;
  }

 protected:
  Vec3f _Viewpoint;
#if 0
  real _bbox_diagonal;  // diagonal of the current processed shape bbox
#endif
  // oldtmp values
  bool _computeViewIndependent;
  real _meanK1;
  real _meanKr;
  real _minK1;
  real _minKr;
  real _maxK1;
  real _maxKr;
  uint _nPoints;
  real _meanEdgeSize;
  bool _orthographicProjection;

  bool _computeRidgesAndValleys;
  bool _computeSuggestiveContours;
  bool _computeMaterialBoundaries;
  bool _faceSmoothness;
  bool _faceMarks;
  float _sphereRadius;  // expressed as a ratio of the mean edge size
  float _creaseAngle;   // [-1, 1] compared with the inner product of face normals
  bool _changes;

  float _kr_derivative_epsilon;

  ProgressBar *_pProgressBar;
  RenderMonitor *_pRenderMonitor;

  MEM_CXX_CLASS_ALLOC_FUNCS("Freestyle:FEdgeXDetector")
};

} /* namespace Freestyle */
